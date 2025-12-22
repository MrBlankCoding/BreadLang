#include "codegen_internal.h"

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
            if (cg_can_unbox_expr(cg, expr)) {
                CgValue unboxed = cg_build_expr_unboxed(cg, cg_fn, expr);
                if (unboxed.type == CG_VALUE_UNBOXED_BOOL) {
                    return cg_box_value(cg, unboxed);
                }
            }
            
            tmp = cg_alloc_value(cg, "booltmp");
            LLVMValueRef b = LLVMConstInt(cg->i32, expr->as.bool_val, 0);
            LLVMValueRef bool_args[] = {cg_value_to_i8_ptr(cg, tmp), b};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_bool, cg->fn_value_set_bool, bool_args, 2, "");
            return tmp;
        case AST_EXPR_INT:
            if (cg_can_unbox_expr(cg, expr)) {
                CgValue unboxed = cg_build_expr_unboxed(cg, cg_fn, expr);
                if (unboxed.type == CG_VALUE_UNBOXED_INT) {
                    return cg_box_value(cg, unboxed);
                }
            }
            
            tmp = cg_alloc_value(cg, "inttmp");
            LLVMValueRef i = LLVMConstInt(cg->i32, expr->as.int_val, 0);
            LLVMValueRef int_args[] = {cg_value_to_i8_ptr(cg, tmp), i};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, int_args, 2, "");
            return tmp;
        case AST_EXPR_DOUBLE:
            if (cg_can_unbox_expr(cg, expr)) {
                CgValue unboxed = cg_build_expr_unboxed(cg, cg_fn, expr);
                if (unboxed.type == CG_VALUE_UNBOXED_DOUBLE) {
                    return cg_box_value(cg, unboxed);
                }
            }
            
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
            
            // llvm string const
            LLVMValueRef str_glob = cg_get_string_global(cg, s);
            LLVMValueRef ptr = LLVMBuildBitCast(cg->builder, str_glob, cg->i8_ptr, "");
            
            // I love bread value
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
                // Check if the variable is stored unboxed
                if (var->unboxed_type != UNBOXED_NONE) {
                    // Directly load the unboxed value and box it using cg_box_value
                    LLVMTypeRef load_type;
                    CgValueType result_type;
                    
                    switch (var->unboxed_type) {
                        case UNBOXED_INT:
                            load_type = cg->i32;
                            result_type = CG_VALUE_UNBOXED_INT;
                            break;
                        case UNBOXED_DOUBLE:
                            load_type = cg->f64;
                            result_type = CG_VALUE_UNBOXED_DOUBLE;
                            break;
                        case UNBOXED_BOOL:
                            load_type = cg->i1;
                            result_type = CG_VALUE_UNBOXED_BOOL;
                            break;
                        default:
                            // Fallback to boxed clone
                            return cg_clone_value(cg, var->alloca, expr->as.var_name);
                    }
                    
                    LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, load_type, var->alloca, var->name);
                    CgValue unboxed_val = cg_create_value(result_type, loaded, load_type);
                    return cg_box_value(cg, unboxed_val);
                }
                
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
            // Try unboxed binary operations first
            if (cg_can_unbox_expr(cg, expr)) {
                CgValue unboxed_result = cg_build_binary_unboxed(cg, cg_fn, expr->as.binary.left, expr->as.binary.right, expr->as.binary.op);
                if (unboxed_result.type != CG_VALUE_BOXED) {
                    return cg_box_value(cg, unboxed_result);
                }
                if (unboxed_result.value) {
                    return unboxed_result.value;
                }
            }
            
            // Special handling for string concatenation with builtin calls
            if (expr->as.binary.op == '+') {
                // Check if either operand is a builtin function call
                int left_is_builtin = (expr->as.binary.left->kind == AST_EXPR_CALL && 
                                     bread_builtin_lookup(expr->as.binary.left->as.call.name) != NULL);
                int right_is_builtin = (expr->as.binary.right->kind == AST_EXPR_CALL && 
                                       bread_builtin_lookup(expr->as.binary.right->as.call.name) != NULL);
                
                if (left_is_builtin || right_is_builtin) {
                    // Evaluate operands and store in temporary variables first
                    LLVMValueRef left = cg_build_expr(cg, cg_fn, val_size, expr->as.binary.left);
                    if (!left) return NULL;
                    LLVMValueRef left_temp = cg_clone_value(cg, left, "left_temp");
                    
                    LLVMValueRef right = cg_build_expr(cg, cg_fn, val_size, expr->as.binary.right);
                    if (!right) return NULL;
                    LLVMValueRef right_temp = cg_clone_value(cg, right, "right_temp");
                    
                    tmp = cg_alloc_value(cg, "bintmp");
                    LLVMValueRef op = LLVMConstInt(cg->i8, expr->as.binary.op, 0);
                    LLVMValueRef args[] = {
                        op,
                        cg_value_to_i8_ptr(cg, left_temp),
                        cg_value_to_i8_ptr(cg, right_temp),
                        cg_value_to_i8_ptr(cg, tmp)
                    };
                    (void)LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
                    return tmp;
                }
            }
            
            // FALL BACK !
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
            // Try unboxed unary operations first
            if (cg_can_unbox_expr(cg, expr)) {
                CgValue unboxed_result = cg_build_unary_unboxed(cg, cg_fn, expr->as.unary.operand, expr->as.unary.op);
                if (unboxed_result.type != CG_VALUE_BOXED) {
                    return cg_box_value(cg, unboxed_result);
                }
                if (unboxed_result.value) {
                    return unboxed_result.value;
                }
            }
            
            // FALL BACK !
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
            
            // Note: Array indexing returns boxed values from collections
            // If the result needs to be unboxed, it will be handled by the caller
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

            const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
            if (builtin) {
                if (builtin->param_count != expr->as.call.arg_count) {
                    fprintf(stderr, "Error: Built-in function '%s' expects %d arguments, got %d\n", 
                           expr->as.call.name, builtin->param_count, expr->as.call.arg_count);
                    return NULL;
                }
                
                tmp = cg_alloc_value(cg, "builtintmp");
                
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

                LLVMTypeRef ty_builtin_call = LLVMFunctionType(
                    cg->void_ty,
                    (LLVMTypeRef[]){cg->i8_ptr, cg->value_ptr_type, cg->i32, cg->value_ptr_type},
                    4,
                    0
                );
                LLVMValueRef fn_builtin_call = cg_declare_fn(cg, "bread_builtin_call_out", ty_builtin_call);

                LLVMValueRef name_str = cg_get_string_global(cg, expr->as.call.name);
                LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");

                LLVMValueRef args_ptr = NULL;
                if (expr->as.call.arg_count > 0) {
                    LLVMTypeRef args_arr_ty = LLVMArrayType(cg->value_type, (unsigned)expr->as.call.arg_count);
                    LLVMValueRef args_arr = LLVMBuildAlloca(cg->builder, args_arr_ty, "builtin.args");

                    LLVMValueRef zero = LLVMConstInt(cg->i32, 0, 0);
                    for (int i = 0; i < expr->as.call.arg_count; i++) {
                        LLVMValueRef idx = LLVMConstInt(cg->i32, (unsigned)i, 0);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, args_arr_ty, args_arr, (LLVMValueRef[]){zero, idx}, 2, "builtin.arg.ptr");
                        cg_copy_value_into(cg, elem_ptr, arg_vals[i]);
                    }

                    args_ptr = LLVMBuildGEP2(cg->builder, args_arr_ty, args_arr, (LLVMValueRef[]){zero, zero}, 2, "builtin.args.ptr");
                } else {
                    args_ptr = LLVMConstNull(cg->value_ptr_type);
                }

                LLVMValueRef argc = LLVMConstInt(cg->i32, (unsigned)expr->as.call.arg_count, 0);
                (void)LLVMBuildCall2(
                    cg->builder,
                    ty_builtin_call,
                    fn_builtin_call,
                    (LLVMValueRef[]){name_ptr, args_ptr, argc, tmp},
                    4,
                    ""
                );
                
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
                    
                    // Copy parameter value into local variable
                    LLVMValueRef copy_args[] = {
                        LLVMBuildBitCast(cg->builder, param_val, cg->i8_ptr, ""),
                        LLVMBuildBitCast(cg->builder, alloca, cg->i8_ptr, "")
                    };
                    (void)LLVMBuildCall2(cg->builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
                }

                if (!cg_build_stmt_list(cg, callee_fn, val_size, callee_fn->body)) {
                    LLVMPositionBuilderAtEnd(cg->builder, current_block);
                    return NULL;
                }
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
            
            LLVMValueRef array_ptr = LLVMBuildCall2(cg->builder, cg->ty_array_new, cg->fn_array_new, NULL, 0, "");
            for (int i = 0; i < expr->as.array_literal.element_count; i++) {
                CgValue elem_unboxed = cg_build_expr_unboxed(cg, cg_fn, expr->as.array_literal.elements[i]);
                LLVMValueRef elem_val = NULL;
                
                if (elem_unboxed.type != CG_VALUE_BOXED) {
                    elem_val = cg_box_value(cg, elem_unboxed);
                } else if (elem_unboxed.value) {
                    elem_val = elem_unboxed.value;
                } else {
                    elem_val = cg_build_expr(cg, cg_fn, val_size, expr->as.array_literal.elements[i]);
                }
                
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
        case AST_EXPR_STRUCT_LITERAL: {
            tmp = cg_alloc_value(cg, "structlittmp");
            
            // Create struct instance
            LLVMValueRef struct_name_str = cg_get_string_global(cg, expr->as.struct_literal.struct_name);
            LLVMValueRef struct_name_ptr = LLVMBuildBitCast(cg->builder, struct_name_str, cg->i8_ptr, "");
            
            // Create field names array
            LLVMTypeRef i8_ptr_ptr = LLVMPointerType(cg->i8_ptr, 0);
            LLVMValueRef field_names_ptr = LLVMConstNull(i8_ptr_ptr);
            if (expr->as.struct_literal.field_count > 0) {
                LLVMTypeRef field_names_arr_ty = LLVMArrayType(cg->i8_ptr, (unsigned)expr->as.struct_literal.field_count);
                LLVMValueRef field_names_arr = LLVMBuildAlloca(cg->builder, field_names_arr_ty, "struct_field_names");
                
                for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                    LLVMValueRef field_name_str = cg_get_string_global(cg, expr->as.struct_literal.field_names[i]);
                    LLVMValueRef field_name_ptr = LLVMBuildBitCast(cg->builder, field_name_str, cg->i8_ptr, "");
                    LLVMValueRef field_slot = LLVMBuildGEP2(
                        cg->builder,
                        field_names_arr_ty,
                        field_names_arr,
                        (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, i, 0)},
                        2,
                        "field_name_slot");
                    LLVMBuildStore(cg->builder, field_name_ptr, field_slot);
                }

                // Pass `char**` (i8**) to runtime. Use pointer to first element.
                LLVMValueRef first = LLVMBuildGEP2(
                    cg->builder,
                    field_names_arr_ty,
                    field_names_arr,
                    (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, 0, 0)},
                    2,
                    "field_names_first");
                field_names_ptr = LLVMBuildBitCast(cg->builder, first, i8_ptr_ptr, "");
            }
            
            // Create the struct using a runtime function
            LLVMTypeRef ty_struct_new = LLVMFunctionType(
                cg->i8_ptr,  // Returns BreadStruct*
                (LLVMTypeRef[]){cg->i8_ptr, cg->i32, i8_ptr_ptr},  // name, field_count, field_names
                3,
                0
            );
            LLVMValueRef fn_struct_new = cg_declare_fn(cg, "bread_struct_new", ty_struct_new);
            
            LLVMValueRef field_count = LLVMConstInt(cg->i32, expr->as.struct_literal.field_count, 0);
            LLVMValueRef struct_ptr = LLVMBuildCall2(cg->builder, ty_struct_new, fn_struct_new,
                (LLVMValueRef[]){struct_name_ptr, field_count, field_names_ptr}, 3, "struct_instance");
            
            // Set field values
            if (expr->as.struct_literal.field_count > 0) {
                LLVMTypeRef ty_struct_set_field = LLVMFunctionType(
                    cg->void_ty,
                    (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},  // struct*, field_name, value*
                    3,
                    0
                );
                LLVMValueRef fn_struct_set_field = cg_declare_fn(cg, "bread_struct_set_field_value_ptr", ty_struct_set_field);
                
                for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                    LLVMValueRef field_value = cg_build_expr(cg, cg_fn, val_size, expr->as.struct_literal.field_values[i]);
                    if (!field_value) return NULL;
                    
                    LLVMValueRef field_name_str = cg_get_string_global(cg, expr->as.struct_literal.field_names[i]);
                    LLVMValueRef field_name_ptr = LLVMBuildBitCast(cg->builder, field_name_str, cg->i8_ptr, "");
                    
                    LLVMValueRef set_args[] = {
                        struct_ptr,
                        field_name_ptr,
                        cg_value_to_i8_ptr(cg, field_value)
                    };
                    (void)LLVMBuildCall2(cg->builder, ty_struct_set_field, fn_struct_set_field, set_args, 3, "");
                }
            }
            
            // Set the result value to the struct
            LLVMTypeRef ty_value_set_struct = LLVMFunctionType(
                cg->void_ty,
                (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr},  // BreadValue*, BreadStruct*
                2,
                0
            );
            LLVMValueRef fn_value_set_struct = cg_declare_fn(cg, "bread_value_set_struct", ty_value_set_struct);
            
            LLVMValueRef struct_args[] = {cg_value_to_i8_ptr(cg, tmp), struct_ptr};
            (void)LLVMBuildCall2(cg->builder, ty_value_set_struct, fn_value_set_struct, struct_args, 2, "");
            
            return tmp;
        }
        default:
            fprintf(stderr, "Codegen not implemented for expr kind %d\n", expr->kind);
            return NULL;
    }
}
