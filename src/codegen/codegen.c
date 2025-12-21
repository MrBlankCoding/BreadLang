#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen/codegen.h"
#include "core/value.h"

LLVMValueRef cg_build_expr(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTExpr* expr);
int cg_build_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt);
static LLVMValueRef cg_value_to_i8_ptr(Cg* cg, LLVMValueRef value_ptr);

static LLVMValueRef cg_alloc_value(Cg* cg, const char* name) {
    LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, cg->value_type, name ? name : "");
    LLVMSetAlignment(alloca, 16);
    // Initialize to NIL to avoid garbage pointers being freed during copy/assignment
    LLVMValueRef args[] = { cg_value_to_i8_ptr(cg, alloca) };
    LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, args, 1, "");
    return alloca;
}

static LLVMValueRef cg_value_to_i8_ptr(Cg* cg, LLVMValueRef value_ptr) {
    return LLVMBuildBitCast(cg->builder, value_ptr, cg->i8_ptr, "");
}

static void cg_copy_value_into(Cg* cg, LLVMValueRef dst, LLVMValueRef src) {
    LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, src), cg_value_to_i8_ptr(cg, dst)};
    (void)LLVMBuildCall2(cg->builder, cg->ty_value_copy, cg->fn_value_copy, args, 2, "");
}

static LLVMValueRef cg_clone_value(Cg* cg, LLVMValueRef src, const char* name) {
    LLVMValueRef dst = cg_alloc_value(cg, name);
    cg_copy_value_into(cg, dst, src);
    return dst;
}

static CgScope* cg_scope_new(CgScope* parent) {
    CgScope* scope = (CgScope*)malloc(sizeof(CgScope));
    scope->parent = parent;
    scope->vars = NULL;
    return scope;
}

CgVar* cg_scope_find_var(CgScope* scope, const char* name) {
    for (CgScope* s = scope; s; s = s->parent) {
        for (CgVar* v = s->vars; v; v = v->next) {
            if (strcmp(v->name, name) == 0) {
                return v;
            }
        }
    }
    return NULL;
}

CgVar* cg_scope_add_var(CgScope* scope, const char* name, LLVMValueRef alloca) {
    CgVar* v = (CgVar*)malloc(sizeof(CgVar));
    v->name = strdup(name);
    v->alloca = alloca;
    v->next = scope->vars;
    scope->vars = v;
    return v;
}


LLVMValueRef cg_declare_fn(Cg* cg, const char* name, LLVMTypeRef fn_type) {
    LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, name);
    if (fn) {
        return fn;
    }
    return LLVMAddFunction(cg->mod, name, fn_type);
}

int cg_define_functions(Cg* cg) {
    (void)cg;
    return 1;
}

LLVMValueRef cg_value_size(Cg* cg) {
    return LLVMBuildCall2(cg->builder, cg->ty_bread_value_size, cg->fn_bread_value_size, NULL, 0, "");
}

static LLVMValueRef cg_get_string_global(Cg* cg, const char* s) {
    LLVMValueRef existing = LLVMGetNamedGlobal(cg->mod, s);
    if (existing) {
        return existing;
    }

    LLVMValueRef val = LLVMConstString(s, (unsigned)strlen(s), 0);
    LLVMValueRef glob = LLVMAddGlobal(cg->mod, LLVMTypeOf(val), "");
    LLVMSetInitializer(glob, val);
    LLVMSetLinkage(glob, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(glob, 1);
    LLVMSetUnnamedAddress(glob, LLVMGlobalUnnamedAddr);
    return glob;
}

LLVMValueRef cg_build_expr(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTExpr* expr) {
    LLVMValueRef tmp;

    if (!cg || !expr) return NULL;
    
    switch (expr->kind) {
        case AST_EXPR_NIL:
            tmp = cg_alloc_value(cg, "niltmp");
            LLVMValueRef nil_args[] = {cg_value_to_i8_ptr(cg, tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, nil_args, 1, "");
            return tmp;
        case AST_EXPR_BOOL:
            tmp = cg_alloc_value(cg, "booltmp");
            LLVMValueRef b = LLVMConstInt(cg->i32, expr->as.bool_val, 0);
            LLVMValueRef bool_args[] = {cg_value_to_i8_ptr(cg, tmp), b};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_bool, cg->fn_value_set_bool, bool_args, 2, "");
            return tmp;
        case AST_EXPR_INT:
            tmp = cg_alloc_value(cg, "inttmp");
            LLVMValueRef i = LLVMConstInt(cg->i32, expr->as.int_val, 0);
            LLVMValueRef int_args[] = {cg_value_to_i8_ptr(cg, tmp), i};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, int_args, 2, "");
            return tmp;
        case AST_EXPR_DOUBLE:
            tmp = cg_alloc_value(cg, "doubletmp");
            LLVMValueRef d = LLVMConstReal(cg->f64, expr->as.double_val);
            LLVMValueRef dbl_args[] = {cg_value_to_i8_ptr(cg, tmp), d};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_double, cg->fn_value_set_double, dbl_args, 2, "");
            return tmp;
        case AST_EXPR_STRING: {
            tmp = cg_alloc_value(cg, "strtmp");
            const char* s = expr->as.string_val ? expr->as.string_val : "";
            LLVMValueRef str_glob = cg_get_string_global(cg, s);
            LLVMValueRef ptr = LLVMBuildBitCast(cg->builder, str_glob, cg->i8_ptr, "");
            LLVMValueRef str_args[] = {cg_value_to_i8_ptr(cg, tmp), ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_string, cg->fn_value_set_string, str_args, 2, "");
            return tmp;
        }
        case AST_EXPR_STRING_LITERAL: {
            tmp = cg_alloc_value(cg, "strlittmp");
            const char* s = expr->as.string_literal.value ? expr->as.string_literal.value : "";
            
            // Create LLVM string constant
            LLVMValueRef str_glob = cg_get_string_global(cg, s);
            LLVMValueRef ptr = LLVMBuildBitCast(cg->builder, str_glob, cg->i8_ptr, "");
            
            // Set the BreadValue to contain this string (using existing runtime function)
            LLVMValueRef str_args[] = {cg_value_to_i8_ptr(cg, tmp), ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_string, cg->fn_value_set_string, str_args, 2, "");
            return tmp;
        }
        case AST_EXPR_VAR: {
            CgVar* var = NULL;
            if (cg_fn) {
                var = cg_scope_find_var(cg_fn->scope, expr->as.var_name);
            }

            if (var) {
                return cg_clone_value(cg, var->alloca, expr->as.var_name);
            }

            tmp = cg_alloc_value(cg, expr->as.var_name);
            LLVMValueRef name_str = cg_get_string_global(cg, expr->as.var_name);
            LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
            LLVMValueRef args[] = {name_ptr, cg_value_to_i8_ptr(cg, tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_load, cg->fn_var_load, args, 2, "");
            return tmp;
        }
        case AST_EXPR_BINARY: {
            LLVMValueRef left = cg_build_expr(cg, cg_fn, val_size, expr->as.binary.left);
            if (!left) return NULL;
            LLVMValueRef right = cg_build_expr(cg, cg_fn, val_size, expr->as.binary.right);
            if (!right) return NULL;
            tmp = cg_alloc_value(cg, "bintmp");
            LLVMValueRef op = LLVMConstInt(cg->i8, expr->as.binary.op, 0);
            LLVMValueRef args[] = {
                op,
                cg_value_to_i8_ptr(cg, left),
                cg_value_to_i8_ptr(cg, right),
                cg_value_to_i8_ptr(cg, tmp)
            };
            (void)LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
            return tmp;
        }
        case AST_EXPR_CALL: {
            // Handle built-in range function
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                if (expr->as.call.arg_count != 1) {
                    fprintf(stderr, "Error: range() expects 1 argument\n");
                    return NULL;
                }
                
                LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[0]);
                if (!arg_val) return NULL;
                
                tmp = cg_alloc_value(cg, "rangetmp");
                
                // Call bread_range function to create the array
                // We need to extract the integer value from the BreadValue
                LLVMValueRef int_val = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int, 
                                                     (LLVMValueRef[]){cg_value_to_i8_ptr(cg, arg_val)}, 1, "range_n");
                
                // Call bread_range(n) to create the array
                LLVMValueRef range_array = LLVMBuildCall2(cg->builder, cg->ty_range_simple, cg->fn_range_simple,
                                                         (LLVMValueRef[]){int_val}, 1, "range_array");
                
                // Set the result value to the array
                LLVMValueRef array_args[] = {cg_value_to_i8_ptr(cg, tmp), range_array};
                (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_array, cg->fn_value_set_array, array_args, 2, "");
                
                return tmp;
            }
            
            CgFunction* callee_fn = NULL;
            for (CgFunction* f = cg->functions; f; f = f->next) {
                if (strcmp(f->name, expr->as.call.name) == 0) {
                    callee_fn = f;
                    break;
                }
            }

            if (!callee_fn) {
                fprintf(stderr, "Error: Unknown function '%s'\n", expr->as.call.name);
                return NULL;
            }

            if (LLVMCountBasicBlocks(callee_fn->fn) == 0) {
                LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
                LLVMBasicBlockRef entry = LLVMAppendBasicBlock(callee_fn->fn, "entry");
                LLVMPositionBuilderAtEnd(cg->builder, entry);

                callee_fn->ret_slot = LLVMGetParam(callee_fn->fn, 0);

                for (int i = 0; i < callee_fn->param_count; i++) {
                    LLVMValueRef param_val = LLVMGetParam(callee_fn->fn, (unsigned)(i + 1));
                    LLVMValueRef alloca = cg_alloc_value(cg, callee_fn->param_names[i]);
                    cg_scope_add_var(callee_fn->scope, callee_fn->param_names[i], alloca);
                    cg_copy_value_into(cg, alloca, param_val);
                }

                if (!cg_build_stmt_list(cg, callee_fn, val_size, callee_fn->body)) return NULL;
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                    LLVMBuildRetVoid(cg->builder);
                }
                LLVMPositionBuilderAtEnd(cg->builder, current_block);
            }

            int total_args = expr->as.call.arg_count + 1;
            LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * (size_t)total_args);
            if (!args) return NULL;
            tmp = cg_alloc_value(cg, "calltmp");
            args[0] = tmp;

            for (int i = 0; i < expr->as.call.arg_count; i++) {
                LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[i]);
                if (!arg_val) {
                    free(args);
                    return NULL;
                }
                args[i + 1] = arg_val;
            }

            (void)LLVMBuildCall2(cg->builder, callee_fn->type, callee_fn->fn, args, (unsigned)total_args, "");
            free(args);
            return tmp;
        }
        case AST_EXPR_ARRAY_LITERAL: {
            tmp = cg_alloc_value(cg, "arraylittmp");
            
            // Create empty array using existing runtime function
            LLVMValueRef array_ptr = LLVMBuildCall2(cg->builder, cg->ty_array_new, cg->fn_array_new, NULL, 0, "");
            
            // For now, just create empty arrays - element addition will be implemented later
            // TODO: Add elements to array when bread_array_append_value is working
            
            LLVMValueRef array_args[] = {cg_value_to_i8_ptr(cg, tmp), array_ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_array, cg->fn_value_set_array, array_args, 2, "");
            return tmp;
        }
        case AST_EXPR_RANGE: {
            tmp = cg_alloc_value(cg, "rangetmp");
            
            // Evaluate start and end expressions
            LLVMValueRef start_val = cg_build_expr(cg, cg_fn, val_size, expr->as.range.start);
            LLVMValueRef end_val = cg_build_expr(cg, cg_fn, val_size, expr->as.range.end);
            if (!start_val || !end_val) return NULL;
            
            // For now, assume integer ranges - extract integer values
            // TODO: Add proper type checking and conversion
            LLVMValueRef start_int = LLVMConstInt(cg->i32, 0, 0); // Placeholder
            LLVMValueRef end_int = LLVMConstInt(cg->i32, 10, 0);  // Placeholder
            LLVMValueRef inclusive = LLVMConstInt(cg->i32, expr->as.range.is_inclusive, 0);
            
            // Create range using runtime function
            LLVMValueRef range_ptr = LLVMBuildCall2(cg->builder, cg->ty_range_create, cg->fn_range_create,
                                                    (LLVMValueRef[]){start_int, end_int, inclusive}, 3, "");
            
            // Store range in BreadValue - for now use array type
            LLVMValueRef range_args[] = {cg_value_to_i8_ptr(cg, tmp), range_ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_array, cg->fn_value_set_array, range_args, 2, "");
            return tmp;
        }
        default:
            fprintf(stderr, "Codegen not implemented for expr kind %d\n", expr->kind);
            return NULL;
    }
}

int cg_build_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    if (!cg || !stmt) return 0;
    switch (stmt->kind) {
        case AST_STMT_EXPR: {
            LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.expr.expr);
            return val != NULL;
        }
        case AST_STMT_VAR_ASSIGN: {
            LLVMValueRef value = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_assign.value);
            if (!value) return 0;

            if (cg_fn) {
                CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.var_assign.var_name);
                if (!var) {
                    LLVMValueRef slot = cg_alloc_value(cg, stmt->as.var_assign.var_name);
                    cg_copy_value_into(cg, slot, value);
                    cg_scope_add_var(cg_fn->scope, stmt->as.var_assign.var_name, slot);
                    return 1;
                }
                cg_copy_value_into(cg, var->alloca, value);
                return 1;
            }

            LLVMValueRef name_str = cg_get_string_global(cg, stmt->as.var_assign.var_name);
            LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
            LLVMValueRef args[] = {name_ptr, cg_value_to_i8_ptr(cg, value)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, args, 2, "");
            return 1;
        }
        case AST_STMT_PRINT: {
            LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.print.expr);
            if (!val) return 0;
            LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, val)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_print, cg->fn_print, args, 1, "");
            return 1;
        }
        case AST_STMT_VAR_DECL: {
            LLVMValueRef init = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_decl.init);
            if (!init) return 0;

            if (cg_fn) {
                LLVMValueRef slot = cg_alloc_value(cg, stmt->as.var_decl.var_name);
                cg_copy_value_into(cg, slot, init);
                cg_scope_add_var(cg_fn->scope, stmt->as.var_decl.var_name, slot);
            } else {
                LLVMValueRef name_str = cg_get_string_global(cg, stmt->as.var_decl.var_name);
                LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
                LLVMValueRef type = LLVMConstInt(cg->i32, stmt->as.var_decl.type, 0);
                LLVMValueRef is_const = LLVMConstInt(cg->i32, stmt->as.var_decl.is_const, 0);
                LLVMValueRef args[] = {name_ptr, type, is_const, cg_value_to_i8_ptr(cg, init)};
                (void)LLVMBuildCall2(cg->builder, cg->ty_var_decl, cg->fn_var_decl, args, 4, "");
            }
            return 1;
        }
        case AST_STMT_IF: {
            LLVMValueRef cond = cg_build_expr(cg, cg_fn, val_size, stmt->as.if_stmt.condition);
            if (!cond) return 0;

            LLVMValueRef truthy_args[] = {cg_value_to_i8_ptr(cg, cond)};
            LLVMValueRef is_truthy = LLVMBuildCall2(cg->builder, cg->ty_is_truthy, cg->fn_is_truthy, truthy_args, 1, "");
            LLVMValueRef cond_val = LLVMBuildICmp(cg->builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->i32, 0, 0), "");

            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
            if (!current_block) return 0;
            LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);

            LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(fn, "then");
            LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(fn, "else");
            LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(fn, "ifcont");

            LLVMBuildCondBr(cg->builder, cond_val, then_block, else_block);

            LLVMPositionBuilderAtEnd(cg->builder, then_block);
            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.if_stmt.then_branch)) return 0;
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, merge_block);
            }

            LLVMPositionBuilderAtEnd(cg->builder, else_block);
            if (stmt->as.if_stmt.else_branch) {
                if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.if_stmt.else_branch)) return 0;
            }
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, merge_block);
            }

            LLVMPositionBuilderAtEnd(cg->builder, merge_block);
            return 1;
        }
        case AST_STMT_WHILE: {
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));
            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "while.cond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "while.body");
            LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "while.end");

            // Save previous loop context
            LLVMBasicBlockRef prev_loop_end = cg->current_loop_end;
            LLVMBasicBlockRef prev_loop_continue = cg->current_loop_continue;
            cg->current_loop_end = end_block;
            cg->current_loop_continue = cond_block;

            LLVMBuildBr(cg->builder, cond_block);

            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMValueRef cond = cg_build_expr(cg, cg_fn, val_size, stmt->as.while_stmt.condition);
            if (!cond) return 0;
            LLVMValueRef truthy_args[] = {cg_value_to_i8_ptr(cg, cond)};
            LLVMValueRef is_truthy = LLVMBuildCall2(cg->builder, cg->ty_is_truthy, cg->fn_is_truthy, truthy_args, 1, "");
            LLVMValueRef cond_val = LLVMBuildICmp(cg->builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->i32, 0, 0), "");
            LLVMBuildCondBr(cg->builder, cond_val, body_block, end_block);

            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.while_stmt.body)) return 0;
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, cond_block);
            }

            // Restore previous loop context
            cg->current_loop_end = prev_loop_end;
            cg->current_loop_continue = prev_loop_continue;

            LLVMPositionBuilderAtEnd(cg->builder, end_block);
            return 1;
        }
        case AST_STMT_FOR: {
            if (!stmt->as.for_stmt.range_expr || stmt->as.for_stmt.range_expr->kind != AST_EXPR_CALL) {
                fprintf(stderr, "Error: LLVM for-loop only supports range()\n");
                return 0;
            }
            if (strcmp(stmt->as.for_stmt.range_expr->as.call.name, "range") != 0) {
                fprintf(stderr, "Error: LLVM for-loop only supports range()\n");
                return 0;
            }
            if (stmt->as.for_stmt.range_expr->as.call.arg_count != 1) {
                fprintf(stderr, "Error: range() expects 1 argument\n");
                return 0;
            }
            ASTExpr* bound_expr = stmt->as.for_stmt.range_expr->as.call.args[0];
            if (!bound_expr || bound_expr->kind != AST_EXPR_INT) {
                fprintf(stderr, "Error: LLVM for-loop currently requires range(Int literal)\n");
                return 0;
            }

            int upper = bound_expr->as.int_val;

            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
            if (!current_block) return 0;
            LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);

            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "for.cond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "for.body");
            LLVMBasicBlockRef inc_block = LLVMAppendBasicBlock(fn, "for.inc");
            LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "for.end");

            // Save previous loop context
            LLVMBasicBlockRef prev_loop_end = cg->current_loop_end;
            LLVMBasicBlockRef prev_loop_continue = cg->current_loop_continue;
            cg->current_loop_end = end_block;
            cg->current_loop_continue = inc_block;

            LLVMValueRef i_slot = LLVMBuildAlloca(cg->builder, cg->i32, "for.i");
            LLVMBuildStore(cg->builder, LLVMConstInt(cg->i32, 0, 0), i_slot);

            LLVMValueRef name_str = cg_get_string_global(cg, stmt->as.for_stmt.var_name);
            LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
            LLVMValueRef init_tmp = cg_alloc_value(cg, "for.init");
            LLVMValueRef set_args[] = {cg_value_to_i8_ptr(cg, init_tmp), LLVMConstInt(cg->i32, 0, 0)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, set_args, 2, "");
            LLVMValueRef decl_type = LLVMConstInt(cg->i32, TYPE_INT, 0);
            LLVMValueRef decl_const = LLVMConstInt(cg->i32, 0, 0);
            LLVMValueRef decl_args[] = {name_ptr, decl_type, decl_const, cg_value_to_i8_ptr(cg, init_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");

            LLVMBuildBr(cg->builder, cond_block);

            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMValueRef i_val = LLVMBuildLoad2(cg->builder, cg->i32, i_slot, "");
            LLVMValueRef cmp = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, LLVMConstInt(cg->i32, upper, 0), "");
            LLVMBuildCondBr(cg->builder, cmp, body_block, end_block);

            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            LLVMValueRef iter_tmp = cg_alloc_value(cg, "for.iter");
            LLVMValueRef set_iter_args[] = {cg_value_to_i8_ptr(cg, iter_tmp), i_val};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, set_iter_args, 2, "");
            LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, iter_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");

            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.for_stmt.body)) return 0;
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, inc_block);
            }

            LLVMPositionBuilderAtEnd(cg->builder, inc_block);
            LLVMValueRef next_i = LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(cg->i32, 1, 0), "");
            LLVMBuildStore(cg->builder, next_i, i_slot);
            LLVMBuildBr(cg->builder, cond_block);

            // Restore previous loop context
            cg->current_loop_end = prev_loop_end;
            cg->current_loop_continue = prev_loop_continue;

            LLVMPositionBuilderAtEnd(cg->builder, end_block);
            return 1;
        }
        case AST_STMT_FOR_IN: {
            // Generate LLVM IR for for-in loops with proper phi nodes
            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
            if (!current_block) return 0;
            LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);

            LLVMBasicBlockRef setup_block = LLVMAppendBasicBlock(fn, "forin.setup");
            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "forin.cond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "forin.body");
            LLVMBasicBlockRef inc_block = LLVMAppendBasicBlock(fn, "forin.inc");
            LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "forin.end");

            // Save previous loop context
            LLVMBasicBlockRef prev_loop_end = cg->current_loop_end;
            LLVMBasicBlockRef prev_loop_continue = cg->current_loop_continue;
            cg->current_loop_end = end_block;
            cg->current_loop_continue = inc_block;

            // Jump to setup block
            LLVMBuildBr(cg->builder, setup_block);

            // Setup block: evaluate iterable and initialize loop variables
            LLVMPositionBuilderAtEnd(cg->builder, setup_block);
            LLVMValueRef iterable = cg_build_expr(cg, cg_fn, val_size, stmt->as.for_in_stmt.iterable);
            if (!iterable) return 0;

            // Create loop index variable
            LLVMValueRef index_slot = LLVMBuildAlloca(cg->builder, cg->i64, "forin.index");
            LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64, 0, 0), index_slot);

            // Get length of iterable (array or range)
            LLVMValueRef length_i32 = LLVMBuildCall2(cg->builder, cg->ty_array_length, cg->fn_array_length,
                                                     (LLVMValueRef[]){cg_value_to_i8_ptr(cg, iterable)}, 1, "");
            LLVMValueRef length = LLVMBuildSExt(cg->builder, length_i32, cg->i64, "");

            // Declare loop variable
            LLVMValueRef name_str = cg_get_string_global(cg, stmt->as.for_in_stmt.var_name);
            LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
            LLVMValueRef init_tmp = cg_alloc_value(cg, "forin.init");
            LLVMValueRef set_args[] = {cg_value_to_i8_ptr(cg, init_tmp), LLVMConstInt(cg->i32, 0, 0)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, set_args, 2, "");
            LLVMValueRef decl_type = LLVMConstInt(cg->i32, TYPE_INT, 0); // TODO: infer from iterable
            LLVMValueRef decl_const = LLVMConstInt(cg->i32, 0, 0);
            LLVMValueRef decl_args[] = {name_ptr, decl_type, decl_const, cg_value_to_i8_ptr(cg, init_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");

            LLVMBuildBr(cg->builder, cond_block);

            // Condition block: check if index < length
            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMValueRef index_val = LLVMBuildLoad2(cg->builder, cg->i64, index_slot, "");
            LLVMValueRef cmp = LLVMBuildICmp(cg->builder, LLVMIntSLT, index_val, length, "");
            LLVMBuildCondBr(cg->builder, cmp, body_block, end_block);

            // Body block: get current element and execute loop body
            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            
            // Get current element from iterable
            LLVMValueRef element_tmp = cg_alloc_value(cg, "forin.element");
            // Initialize element to nil first
            LLVMValueRef init_args[] = {cg_value_to_i8_ptr(cg, element_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, init_args, 1, "");
            
            LLVMValueRef index_i32 = LLVMBuildTrunc(cg->builder, index_val, cg->i32, "");
            LLVMValueRef get_args[] = {cg_value_to_i8_ptr(cg, iterable), index_i32, cg_value_to_i8_ptr(cg, element_tmp)};
            LLVMValueRef get_success = LLVMBuildCall2(cg->builder, cg->ty_array_get, cg->fn_array_get, get_args, 3, "");
            
            // Check if array access was successful
            LLVMValueRef success_cmp = LLVMBuildICmp(cg->builder, LLVMIntNE, get_success, LLVMConstInt(cg->i32, 0, 0), "");
            
            LLVMBasicBlockRef assign_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)), "forin.assign");
            LLVMBuildCondBr(cg->builder, success_cmp, assign_block, end_block);
            
            LLVMPositionBuilderAtEnd(cg->builder, assign_block);
            
            // Assign element to loop variable
            LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, element_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");

            // Execute loop body
            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.for_in_stmt.body)) return 0;
            
            // Jump to increment if no terminator
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, inc_block);
            }

            // Increment block: increment index
            LLVMPositionBuilderAtEnd(cg->builder, inc_block);
            LLVMValueRef current_index = LLVMBuildLoad2(cg->builder, cg->i64, index_slot, "");
            LLVMValueRef next_index = LLVMBuildAdd(cg->builder, current_index, LLVMConstInt(cg->i64, 1, 0), "");
            LLVMBuildStore(cg->builder, next_index, index_slot);
            LLVMBuildBr(cg->builder, cond_block);

            // Restore previous loop context
            cg->current_loop_end = prev_loop_end;
            cg->current_loop_continue = prev_loop_continue;

            // End block: continue after loop
            LLVMPositionBuilderAtEnd(cg->builder, end_block);
            return 1;
        }
        case AST_STMT_FUNC_DECL: {
            int param_total = stmt->as.func_decl.param_count + 1;
            LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * (size_t)param_total);
            if (!param_types) return 0;
            param_types[0] = cg->value_ptr_type;
            for (int i = 0; i < stmt->as.func_decl.param_count; i++) {
                param_types[i + 1] = cg->value_ptr_type;
            }
            LLVMTypeRef fn_type = LLVMFunctionType(cg->void_ty, param_types, (unsigned)param_total, 0);
            free(param_types);

            LLVMValueRef fn = LLVMAddFunction(cg->mod, stmt->as.func_decl.name, fn_type);

            CgFunction* cg_fn = (CgFunction*)malloc(sizeof(CgFunction));
            cg_fn->name = strdup(stmt->as.func_decl.name);
            cg_fn->fn = fn;
            cg_fn->type = fn_type;
            cg_fn->body = stmt->as.func_decl.body;
            cg_fn->param_count = stmt->as.func_decl.param_count;
            cg_fn->param_names = stmt->as.func_decl.param_names;
            cg_fn->scope = cg_scope_new(NULL); // TODO: parent scope for closures
            cg_fn->next = cg->functions;
            cg_fn->ret_slot = NULL;
            cg->functions = cg_fn;
            
            return 1;
        }
        case AST_STMT_RETURN: {
            LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.ret.expr);
            if (!val) return 0;
            if (!cg_fn || !cg_fn->ret_slot) {
                fprintf(stderr, "Error: return outside of function\n");
                return 0;
            }
            cg_copy_value_into(cg, cg_fn->ret_slot, val);
            LLVMBuildRetVoid(cg->builder);
            return 1;
        }
        case AST_STMT_BREAK: {
            if (!cg->current_loop_end) {
                fprintf(stderr, "Error: break outside of loop\n");
                return 0;
            }
            LLVMBuildBr(cg->builder, cg->current_loop_end);
            return 1;
        }
        case AST_STMT_CONTINUE: {
            if (!cg->current_loop_continue) {
                fprintf(stderr, "Error: continue outside of loop\n");
                return 0;
            }
            LLVMBuildBr(cg->builder, cg->current_loop_continue);
            return 1;
        }
        default:
            fprintf(stderr, "Codegen not implemented for stmt kind %d\n", stmt->kind);
            return 0;
    }
}

int cg_build_stmt_list(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmtList* list) {
    if (!cg || !list) return 1;
    for (ASTStmt* st = list->head; st; st = st->next) {
        if (!cg_build_stmt(cg, cg_fn, val_size, st)) {
            return 0;
        }
    }
    return 1;
}