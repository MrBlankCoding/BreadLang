#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen/codegen.h"
#include "core/value.h"
#include "runtime/runtime.h"

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

        case AST_EXPR_UNARY: {
            LLVMValueRef operand = cg_build_expr(cg, cg_fn, val_size, expr->as.unary.operand);
            if (!operand) return NULL;

            tmp = cg_alloc_value(cg, "unarytmp");

            if (expr->as.unary.op == '!') {
                LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, operand), cg_value_to_i8_ptr(cg, tmp)};
                (void)LLVMBuildCall2(cg->builder, cg->ty_unary_not, cg->fn_unary_not, args, 2, "");
                return tmp;
            }

            if (expr->as.unary.op == '-') {
                // Implement unary minus as (0 - operand)
                LLVMValueRef zero = cg_alloc_value(cg, "zerotmp");
                LLVMValueRef zero_i = LLVMConstInt(cg->i32, 0, 0);
                LLVMValueRef zargs[] = {cg_value_to_i8_ptr(cg, zero), zero_i};
                (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, zargs, 2, "");

                LLVMValueRef op = LLVMConstInt(cg->i8, '-', 0);
                LLVMValueRef args[] = {
                    op,
                    cg_value_to_i8_ptr(cg, zero),
                    cg_value_to_i8_ptr(cg, operand),
                    cg_value_to_i8_ptr(cg, tmp)
                };
                (void)LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
                return tmp;
            }

            fprintf(stderr, "Codegen not implemented for unary op '%c'\n", expr->as.unary.op);
            return NULL;
        }

        case AST_EXPR_INDEX: {
            LLVMValueRef target = cg_build_expr(cg, cg_fn, val_size, expr->as.index.target);
            if (!target) return NULL;
            LLVMValueRef index = cg_build_expr(cg, cg_fn, val_size, expr->as.index.index);
            if (!index) return NULL;

            tmp = cg_alloc_value(cg, "idxtmp");
            LLVMValueRef args[] = {
                cg_value_to_i8_ptr(cg, target),
                cg_value_to_i8_ptr(cg, index),
                cg_value_to_i8_ptr(cg, tmp)
            };
            (void)LLVMBuildCall2(cg->builder, cg->ty_index_op, cg->fn_index_op, args, 3, "");
            return tmp;
        }

        case AST_EXPR_MEMBER: {
            LLVMValueRef target = cg_build_expr(cg, cg_fn, val_size, expr->as.member.target);
            if (!target) return NULL;

            tmp = cg_alloc_value(cg, "membertmp");
            const char* member = expr->as.member.member ? expr->as.member.member : "";
            LLVMValueRef member_glob = cg_get_string_global(cg, member);
            LLVMValueRef member_ptr = LLVMBuildBitCast(cg->builder, member_glob, cg->i8_ptr, "");
            LLVMValueRef is_opt = LLVMConstInt(cg->i32, expr->as.member.is_optional_chain ? 1 : 0, 0);

            LLVMValueRef args[] = {
                cg_value_to_i8_ptr(cg, target),
                member_ptr,
                is_opt,
                cg_value_to_i8_ptr(cg, tmp)
            };
            (void)LLVMBuildCall2(cg->builder, cg->ty_member_op, cg->fn_member_op, args, 4, "");
            return tmp;
        }

        case AST_EXPR_METHOD_CALL: {
            LLVMValueRef target = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.target);
            if (!target) return NULL;

            tmp = cg_alloc_value(cg, "methodtmp");

            const char* name = expr->as.method_call.name ? expr->as.method_call.name : "";
            LLVMValueRef name_glob = cg_get_string_global(cg, name);
            LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_glob, cg->i8_ptr, "");
            LLVMValueRef is_opt = LLVMConstInt(cg->i32, expr->as.method_call.is_optional_chain ? 1 : 0, 0);
            LLVMValueRef argc = LLVMConstInt(cg->i32, expr->as.method_call.arg_count, 0);

            LLVMValueRef args_ptr = LLVMConstNull(cg->i8_ptr);
            if (expr->as.method_call.arg_count > 0) {
                LLVMTypeRef args_arr_ty = LLVMArrayType(cg->value_type, (unsigned)expr->as.method_call.arg_count);
                LLVMValueRef args_alloca = LLVMBuildAlloca(cg->builder, args_arr_ty, "method_args");
                LLVMSetAlignment(args_alloca, 16);

                for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                    LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[i]);
                    if (!arg_val) return NULL;

                    LLVMValueRef slot = LLVMBuildGEP2(
                        cg->builder,
                        args_arr_ty,
                        args_alloca,
                        (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, i, 0)},
                        2,
                        "method_arg_slot");
                    cg_copy_value_into(cg, slot, arg_val);
                }

                args_ptr = LLVMBuildBitCast(cg->builder, args_alloca, cg->i8_ptr, "");
            }

            LLVMValueRef args[] = {
                cg_value_to_i8_ptr(cg, target),
                name_ptr,
                argc,
                args_ptr,
                is_opt,
                cg_value_to_i8_ptr(cg, tmp)
            };
            (void)LLVMBuildCall2(cg->builder, cg->ty_method_call_op, cg->fn_method_call_op, args, 6, "");
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
            
            // Check for built-in functions
            const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
            if (builtin) {
                if (builtin->param_count != expr->as.call.arg_count) {
                    fprintf(stderr, "Error: Built-in function '%s' expects %d arguments, got %d\n", 
                           expr->as.call.name, builtin->param_count, expr->as.call.arg_count);
                    return NULL;
                }
                
                tmp = cg_alloc_value(cg, "builtintmp");
                
                // Prepare arguments for built-in function call
                LLVMValueRef* arg_vals = NULL;
                if (expr->as.call.arg_count > 0) {
                    arg_vals = malloc(sizeof(LLVMValueRef) * expr->as.call.arg_count);
                    if (!arg_vals) return NULL;
                    
                    for (int i = 0; i < expr->as.call.arg_count; i++) {
                        arg_vals[i] = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[i]);
                        if (!arg_vals[i]) {
                            free(arg_vals);
                            return NULL;
                        }
                    }
                }
                
                // Call bread_builtin_call_llvm wrapper function
                // This is a simplified implementation - a full implementation would
                // generate optimized LLVM IR for each built-in function
                
                // For now, we'll use a placeholder that sets the result to nil
                // TODO: Implement proper built-in function call handling
                LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, 
                              (LLVMValueRef[]){cg_value_to_i8_ptr(cg, tmp)}, 1, "");
                
                if (arg_vals) free(arg_vals);
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

            // Append each element into the array
            for (int i = 0; i < expr->as.array_literal.element_count; i++) {
                LLVMValueRef elem_val = cg_build_expr(cg, cg_fn, val_size, expr->as.array_literal.elements[i]);
                if (!elem_val) return NULL;
                LLVMValueRef append_args[] = {array_ptr, cg_value_to_i8_ptr(cg, elem_val)};
                (void)LLVMBuildCall2(cg->builder, cg->ty_array_append_value, cg->fn_array_append_value, append_args, 2, "");
            }

            LLVMValueRef array_args[] = {cg_value_to_i8_ptr(cg, tmp), array_ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_array, cg->fn_value_set_array, array_args, 2, "");
            return tmp;
        }
        case AST_EXPR_DICT: {
            tmp = cg_alloc_value(cg, "dicttmp");

            LLVMValueRef dict_ptr = LLVMBuildCall2(cg->builder, cg->ty_dict_new, cg->fn_dict_new, NULL, 0, "");

            for (int i = 0; i < expr->as.dict.entry_count; i++) {
                ASTDictEntry* entry = &expr->as.dict.entries[i];
                if (!entry->key || !entry->value) return NULL;

                LLVMValueRef key_val = cg_build_expr(cg, cg_fn, val_size, entry->key);
                if (!key_val) return NULL;
                LLVMValueRef value_val = cg_build_expr(cg, cg_fn, val_size, entry->value);
                if (!value_val) return NULL;

                LLVMValueRef set_args[] = {
                    dict_ptr,
                    cg_value_to_i8_ptr(cg, key_val),
                    cg_value_to_i8_ptr(cg, value_val)
                };
                (void)LLVMBuildCall2(cg->builder, cg->ty_dict_set_value, cg->fn_dict_set_value, set_args, 3, "");
            }

            LLVMValueRef dict_args[] = {cg_value_to_i8_ptr(cg, tmp), dict_ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_dict, cg->fn_value_set_dict, dict_args, 2, "");
            return tmp;
        }
        case AST_EXPR_RANGE: {
            tmp = cg_alloc_value(cg, "rangetmp");
            
            // Evaluate start and end expressions
            LLVMValueRef start_val = cg_build_expr(cg, cg_fn, val_size, expr->as.range.start);
            LLVMValueRef end_val = cg_build_expr(cg, cg_fn, val_size, expr->as.range.end);
            if (!start_val || !end_val) return NULL;
            
            // Extract integer values from BreadValues for range bounds
            LLVMValueRef start_int = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int,
                                                   (LLVMValueRef[]){cg_value_to_i8_ptr(cg, start_val)}, 1, "range_start");
            LLVMValueRef end_int = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int,
                                                 (LLVMValueRef[]){cg_value_to_i8_ptr(cg, end_val)}, 1, "range_end");
            
            // Handle inclusive vs exclusive ranges by adjusting end value
            // For inclusive ranges (start..end), we need end+1 for the exclusive bread_range_create
            // For exclusive ranges (start..<end), we use end as-is
            LLVMValueRef adjusted_end;
            if (expr->as.range.is_inclusive) {
                adjusted_end = LLVMBuildAdd(cg->builder, end_int, LLVMConstInt(cg->i32, 1, 0), "range_end_inclusive");
            } else {
                adjusted_end = end_int;
            }
            
            // Create range using runtime function with step=1 (bread_range_create expects step parameter)
            LLVMValueRef step = LLVMConstInt(cg->i32, 1, 0);
            LLVMValueRef range_ptr = LLVMBuildCall2(cg->builder, cg->ty_range_create, cg->fn_range_create,
                                                    (LLVMValueRef[]){start_int, adjusted_end, step}, 3, "range_array");
            
            // Store range array in BreadValue
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

        case AST_STMT_INDEX_ASSIGN: {
            LLVMValueRef idx = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.index);
            if (!idx) return 0;
            LLVMValueRef value = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.value);
            if (!value) return 0;

            LLVMValueRef target_ptr = NULL;

            if (stmt->as.index_assign.target && stmt->as.index_assign.target->kind == AST_EXPR_VAR && cg_fn) {
                CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.index_assign.target->as.var_name);
                if (var) {
                    target_ptr = var->alloca;
                }
            }

            // Fallback: evaluate target as an rvalue. This may not persist mutations, but still provides defined behavior.
            if (!target_ptr) {
                target_ptr = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.target);
                if (!target_ptr) return 0;
            }

            LLVMValueRef args[] = {
                cg_value_to_i8_ptr(cg, target_ptr),
                cg_value_to_i8_ptr(cg, idx),
                cg_value_to_i8_ptr(cg, value)
            };
            (void)LLVMBuildCall2(cg->builder, cg->ty_index_set_op, cg->fn_index_set_op, args, 3, "");
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
            // Generate LLVM IR for for-in loops with proper bounds checking and phi nodes
            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
            if (!current_block) return 0;
            LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);

            LLVMBasicBlockRef setup_block = LLVMAppendBasicBlock(fn, "forin.setup");
            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "forin.cond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "forin.body");
            LLVMBasicBlockRef inc_block = LLVMAppendBasicBlock(fn, "forin.inc");
            LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "forin.end");

            // Save previous loop context for break/continue support
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

            // Create loop index variable with proper phi node setup
            LLVMValueRef index_slot = LLVMBuildAlloca(cg->builder, cg->i32, "forin.index");
            LLVMBuildStore(cg->builder, LLVMConstInt(cg->i32, 0, 0), index_slot);

            // Get length of iterable (works for both arrays and ranges)
            LLVMValueRef length = LLVMBuildCall2(cg->builder, cg->ty_array_length, cg->fn_array_length,
                                                (LLVMValueRef[]){cg_value_to_i8_ptr(cg, iterable)}, 1, "forin.length");

            // Bounds check: ensure length is non-negative
            LLVMValueRef length_check = LLVMBuildICmp(cg->builder, LLVMIntSGT, length, LLVMConstInt(cg->i32, 0, 0), "");
            LLVMBasicBlockRef valid_length_block = LLVMAppendBasicBlock(fn, "forin.valid_length");
            LLVMBuildCondBr(cg->builder, length_check, valid_length_block, end_block);

            LLVMPositionBuilderAtEnd(cg->builder, valid_length_block);

            // Declare loop variable with proper type inference
            LLVMValueRef name_str = cg_get_string_global(cg, stmt->as.for_in_stmt.var_name);
            LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
            LLVMValueRef init_tmp = cg_alloc_value(cg, "forin.init");
            LLVMValueRef nil_args[] = {cg_value_to_i8_ptr(cg, init_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, nil_args, 1, "");
            LLVMValueRef decl_type = LLVMConstInt(cg->i32, TYPE_NIL, 0);
            LLVMValueRef decl_const = LLVMConstInt(cg->i32, 0, 0);
            LLVMValueRef decl_args[] = {name_ptr, decl_type, decl_const, cg_value_to_i8_ptr(cg, init_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");

            LLVMBuildBr(cg->builder, cond_block);

            // Condition block: check if index < length with phi node
            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMValueRef index_phi = LLVMBuildPhi(cg->builder, cg->i32, "forin.index.phi");
            LLVMValueRef phi_vals[] = {LLVMConstInt(cg->i32, 0, 0)};
            LLVMBasicBlockRef phi_blocks[] = {valid_length_block};
            LLVMAddIncoming(index_phi, phi_vals, phi_blocks, 1);

            LLVMValueRef cmp = LLVMBuildICmp(cg->builder, LLVMIntSLT, index_phi, length, "forin.cond");
            LLVMBuildCondBr(cg->builder, cmp, body_block, end_block);

            // Body block: get current element with bounds checking and execute loop body
            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            
            // Get current element from iterable with proper error handling
            LLVMValueRef element_tmp = cg_alloc_value(cg, "forin.element");
            LLVMValueRef get_args[] = {cg_value_to_i8_ptr(cg, iterable), index_phi, cg_value_to_i8_ptr(cg, element_tmp)};
            LLVMValueRef get_success = LLVMBuildCall2(cg->builder, cg->ty_array_get, cg->fn_array_get, get_args, 3, "");
            
            // Bounds checking: verify array access was successful
            LLVMValueRef success_cmp = LLVMBuildICmp(cg->builder, LLVMIntNE, get_success, LLVMConstInt(cg->i32, 0, 0), "");
            LLVMBasicBlockRef assign_block = LLVMAppendBasicBlock(fn, "forin.assign");
            LLVMBuildCondBr(cg->builder, success_cmp, assign_block, end_block);
            
            LLVMPositionBuilderAtEnd(cg->builder, assign_block);
            
            // Assign element to loop variable
            LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, element_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");

            // Execute loop body with proper scope management
            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.for_in_stmt.body)) return 0;
            
            // Jump to increment if no terminator (handles break/continue properly)
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, inc_block);
            }

            // Increment block: increment index and update phi node
            LLVMPositionBuilderAtEnd(cg->builder, inc_block);
            LLVMValueRef next_index = LLVMBuildAdd(cg->builder, index_phi, LLVMConstInt(cg->i32, 1, 0), "forin.next");
            
            // Update phi node with incremented value
            LLVMValueRef inc_phi_vals[] = {next_index};
            LLVMBasicBlockRef inc_phi_blocks[] = {inc_block};
            LLVMAddIncoming(index_phi, inc_phi_vals, inc_phi_blocks, 1);
            
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