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