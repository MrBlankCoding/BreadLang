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
            LLVMValueRef i = LLVMConstInt(cg->i64, expr->as.int_val, 0);
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
            LLVMValueRef ptr = cg_get_string_ptr(cg, s);
            LLVMValueRef str_args[] = {cg_value_to_i8_ptr(cg, tmp), ptr};
            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_string, cg->fn_value_set_string, str_args, 2, "");
            return tmp;
        }
        case AST_EXPR_STRING_LITERAL: {
            tmp = cg_alloc_value(cg, "strlittmp");
            const char* s = expr->as.string_literal.value ? expr->as.string_literal.value : "";
            
            // llvm string const
            LLVMValueRef ptr = cg_get_string_ptr(cg, s);
            
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
                // Check if its stored unboxed
                if (var->unboxed_type != UNBOXED_NONE) {
                    LLVMTypeRef load_type;
                    CgValueType result_type;
                    
                    switch (var->unboxed_type) {
                        case UNBOXED_INT:
                            load_type = cg->i64;
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
                            // Fallbacked to a clone of the box
                            return cg_clone_value(cg, var->alloca, expr->as.var_name);
                    }
                    
                    LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, load_type, var->alloca, var->name);
                    CgValue unboxed_val = cg_create_value(result_type, loaded, load_type);
                    return cg_box_value(cg, unboxed_val);
                }
                
                return cg_clone_value(cg, var->alloca, expr->as.var_name);
            }

            // treat as self.<field> access.
            if (cg_fn && cg_fn->is_method && cg_fn->self_param && cg_fn->current_class && expr->as.var_name) {
                int is_field = 0;
                int depth_limit = 64;
                CgClass* cls = NULL;
                for (CgClass* c = cg->classes; c; c = c->next) {
                    if (c == cg_fn->current_class) {
                        cls = c;
                        break;
                    }
                }
                if ((uintptr_t)cls < 4096) {
                    cls = NULL;
                }
                for (; cls && !is_field && depth_limit-- > 0; ) {
                    int fc = cls->field_count;
                    if (fc <= 0 || fc > 4096 || !cls->field_names || (uintptr_t)cls->field_names < 4096) {
                        fc = 0;
                    }
                    for (int i = 0; i < fc; i++) {
                        const char* fname = cls->field_names[i];
                        if (fname && strcmp(fname, expr->as.var_name) == 0) {
                            is_field = 1;
                            break;
                        }
                    }

                    if (!is_field && cls->parent_name) {
                        CgClass* parent = cg_find_class(cg, cls->parent_name);
                        if (!parent || (uintptr_t)parent < 4096) {
                            break;
                        }
                        cls = parent;
                    } else {
                        break;
                    }
                }

                if (is_field) {
                    LLVMValueRef self_param = cg_fn->self_param ? cg_fn->self_param : LLVMGetParam(cg_fn->fn, 1);
                    tmp = cg_alloc_value(cg, "membertmp");
                    LLVMValueRef member_ptr = cg_get_string_ptr(cg, expr->as.var_name);
                    LLVMValueRef is_opt = LLVMConstInt(cg->i32, 0, 0);

                    LLVMValueRef args[] = {
                        self_param,
                        member_ptr,
                        is_opt,
                        cg_value_to_i8_ptr(cg, tmp)
                    };
                    (void)LLVMBuildCall2(cg->builder, cg->ty_member_op, cg->fn_member_op, args, 4, "");
                    return tmp;
                }
            }

            tmp = cg_alloc_value(cg, expr->as.var_name);
            LLVMValueRef name_ptr = cg_get_string_ptr(cg, expr->as.var_name);
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
            
            // Handle concatanation. Muy especial. 
            if (expr->as.binary.op == '+') {
                int left_is_builtin = (expr->as.binary.left->kind == AST_EXPR_CALL && 
                                     bread_builtin_lookup(expr->as.binary.left->as.call.name) != NULL);
                int right_is_builtin = (expr->as.binary.right->kind == AST_EXPR_CALL && 
                                       bread_builtin_lookup(expr->as.binary.right->as.call.name) != NULL);
                
                if (left_is_builtin || right_is_builtin) {
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
                // This needs more support down the line. 
                LLVMValueRef zero = cg_alloc_value(cg, "zerotmp");
                LLVMValueRef zero_i = LLVMConstInt(cg->i64, 0, 0);
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
            LLVMValueRef member_ptr = cg_get_string_ptr(cg, member);
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
            // Check if its a supr call. 
            int is_super_call = (expr->as.method_call.target && 
                                expr->as.method_call.target->kind == AST_EXPR_SUPER);
            
            LLVMValueRef target = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.target);
            if (!target) return NULL;

            tmp = cg_alloc_value(cg, "methodtmp");

            const char* name = expr->as.method_call.name ? expr->as.method_call.name : "";
            
            // Call the parent class. 
            if (is_super_call && cg_fn && cg_fn->current_class && cg_fn->current_class->parent_name) {
                // Not using arrays. Is this the best choice? Idk. :) 
                if (strcmp(name, "init") == 0) {
                    LLVMValueRef parent_name_str = cg_get_string_global(cg, cg_fn->current_class->parent_name);
                    LLVMValueRef parent_name_ptr = LLVMBuildBitCast(cg->builder, parent_name_str, cg->i8_ptr, "");
                    LLVMValueRef self_param = LLVMGetParam(cg_fn->fn, 1);
                    
                    if (expr->as.method_call.arg_count == 0) {
                        // No args??? Crazy work. 
                        LLVMTypeRef ty_super_init_0 = LLVMFunctionType(
                            cg->void_ty,
                            (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},  // self, parent_name, result
                            3,
                            0
                        );
                        LLVMValueRef fn_super_init_0 = cg_declare_fn(cg, "bread_super_init_0", ty_super_init_0);
                        
                        LLVMValueRef super_args[] = {
                            self_param,
                            parent_name_ptr,
                            cg_value_to_i8_ptr(cg, tmp)
                        };
                        (void)LLVMBuildCall2(cg->builder, ty_super_init_0, fn_super_init_0, super_args, 3, "");
                    } else if (expr->as.method_call.arg_count == 1) {
                        // One argument
                        LLVMValueRef arg0 = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[0]);
                        if (!arg0) return NULL;
                        
                        LLVMTypeRef ty_super_init_1 = LLVMFunctionType(
                            cg->void_ty,
                            (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},  // self, parent_name, arg0, result
                            4,
                            0
                        );
                        LLVMValueRef fn_super_init_1 = cg_declare_fn(cg, "bread_super_init_1", ty_super_init_1);
                        
                        LLVMValueRef super_args[] = {
                            self_param,
                            parent_name_ptr,
                            cg_value_to_i8_ptr(cg, arg0),
                            cg_value_to_i8_ptr(cg, tmp)
                        };
                        (void)LLVMBuildCall2(cg->builder, ty_super_init_1, fn_super_init_1, super_args, 4, "");
                    } else if (expr->as.method_call.arg_count == 2) {
                        // Two arguments
                        LLVMValueRef arg0 = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[0]);
                        if (!arg0) return NULL;
                        LLVMValueRef arg1 = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[1]);
                        if (!arg1) return NULL;
                        
                        LLVMTypeRef ty_super_init_2 = LLVMFunctionType(
                            cg->void_ty,
                            (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},  // self, parent_name, arg0, arg1, result
                            5,
                            0
                        );
                        LLVMValueRef fn_super_init_2 = cg_declare_fn(cg, "bread_super_init_2", ty_super_init_2);
                        
                        LLVMValueRef super_args[] = {
                            self_param,
                            parent_name_ptr,
                            cg_value_to_i8_ptr(cg, arg0),
                            cg_value_to_i8_ptr(cg, arg1),
                            cg_value_to_i8_ptr(cg, tmp)
                        };
                        (void)LLVMBuildCall2(cg->builder, ty_super_init_2, fn_super_init_2, super_args, 5, "");
                    } else if (expr->as.method_call.arg_count == 3) {
                        // Three arguments
                        LLVMValueRef arg0 = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[0]);
                        if (!arg0) return NULL;
                        LLVMValueRef arg1 = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[1]);
                        if (!arg1) return NULL;
                        LLVMValueRef arg2 = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[2]);
                        if (!arg2) return NULL;
                        
                        LLVMTypeRef ty_super_init_3 = LLVMFunctionType(
                            cg->void_ty,
                            (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},  // self, parent_name, arg0, arg1, arg2, result
                            6,
                            0
                        );
                        LLVMValueRef fn_super_init_3 = cg_declare_fn(cg, "bread_super_init_3", ty_super_init_3);
                        
                        LLVMValueRef super_args[] = {
                            self_param,
                            parent_name_ptr,
                            cg_value_to_i8_ptr(cg, arg0),
                            cg_value_to_i8_ptr(cg, arg1),
                            cg_value_to_i8_ptr(cg, arg2),
                            cg_value_to_i8_ptr(cg, tmp)
                        };
                        (void)LLVMBuildCall2(cg->builder, ty_super_init_3, fn_super_init_3, super_args, 6, "");
                    } else {
                        // Okay fall back to arrays if we have more then 3 args. Op type shit. Still have no idea what im doing. 
                        LLVMValueRef args_ptr = LLVMConstNull(cg->i8_ptr);
                        if (expr->as.method_call.arg_count > 0) {
                            LLVMTypeRef args_arr_ty = LLVMArrayType(cg->value_type, (unsigned)expr->as.method_call.arg_count);
                            LLVMValueRef args_alloca = LLVMBuildAlloca(cg->builder, args_arr_ty, "super_init_args");
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
                                    "super_init_arg_slot");
                                LLVMValueRef slot_nil_args[] = {cg_value_to_i8_ptr(cg, slot)};
                                (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, slot_nil_args, 1, "");
                                cg_copy_value_into(cg, slot, arg_val);
                            }

                            args_ptr = LLVMBuildBitCast(cg->builder, args_alloca, cg->i8_ptr, "");
                        }
                        
                        LLVMTypeRef ty_super_init_simple = LLVMFunctionType(
                            cg->void_ty,
                            (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr, cg->i8_ptr},  // self, parent_name, argc, args, result
                            5,
                            0
                        );
                        LLVMValueRef fn_super_init_simple = cg_declare_fn(cg, "bread_super_init_simple", ty_super_init_simple);
                        
                        LLVMValueRef super_args[] = {
                            self_param,
                            parent_name_ptr,
                            LLVMConstInt(cg->i32, expr->as.method_call.arg_count, 0),
                            args_ptr,
                            cg_value_to_i8_ptr(cg, tmp)
                        };
                        (void)LLVMBuildCall2(cg->builder, ty_super_init_simple, fn_super_init_simple, super_args, 5, "");
                    }
                    
                    LLVMValueRef nil_args[] = {cg_value_to_i8_ptr(cg, tmp)};
                    (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, nil_args, 1, "");
                    return tmp;
                }
            }
            
            // Reg method calls
            // Try to generate direct call so we can determine the target by runtime
            int generated_direct_call = 0;
            if (expr->as.method_call.target && expr->as.method_call.target->kind == AST_EXPR_VAR) {
                // Does the car have a known class type. 
                if (cg_fn) {
                    // TODO: Use the found variable for type checking when implementing full type inference
                    (void)cg_scope_find_var(cg_fn->scope, expr->as.method_call.target->as.var_name);
                }
                
                // For now, we'll try to infer the class from the variable name or context
                // This is a simplified approach - a full implementation would track types more carefully
                const char* inferred_class_name = NULL;
                
                // Simple heuristic: if the variable name contains a class name, assume that's the type
                for (CgClass* cls = cg->classes; cls; cls = cls->next) {
                    if (strstr(expr->as.method_call.target->as.var_name, cls->name) != NULL) {
                        inferred_class_name = cls->name;
                        break;
                    }
                }
                
                if (inferred_class_name) {
                    // Look for class and method. 
                    CgClass* target_class = cg_find_class(cg, inferred_class_name);
                    if (target_class) {
                        // Method inside of a class. Wait thats how that works. :mindblown:
                        int method_index = -1;
                        for (int i = 0; i < target_class->method_count; i++) {
                            if (target_class->method_names[i] && strcmp(target_class->method_names[i], name) == 0) {
                                method_index = i;
                                break;
                            }
                        }
                        
                        if (method_index >= 0 && target_class->method_functions && target_class->method_functions[method_index]) {
                            LLVMValueRef method_fn = target_class->method_functions[method_index];
                            int total_args = expr->as.method_call.arg_count + 2;
                            LLVMValueRef* call_args = malloc(sizeof(LLVMValueRef) * total_args);
                            if (call_args) {
                                call_args[0] = cg_value_to_i8_ptr(cg, tmp);  // return slot
                                call_args[1] = cg_value_to_i8_ptr(cg, target);  // self
                                
                                // Method args. 
                                for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                                    LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, expr->as.method_call.args[i]);
                                    if (!arg_val) {
                                        free(call_args);
                                        return NULL;
                                    }
                                    call_args[i + 2] = cg_value_to_i8_ptr(cg, arg_val);
                                }
                                
                                // Create call
                                LLVMTypeRef method_type = LLVMGetElementType(LLVMTypeOf(method_fn));
                                (void)LLVMBuildCall2(cg->builder, method_type, method_fn, call_args, total_args, "");
                                
                                free(call_args);
                                generated_direct_call = 1;
                                printf("Generated direct call to %s::%s\n", inferred_class_name, name);
                            }
                        }
                    }
                }
            }
            
            // Fallback to runtime call. Hopefully this doesnt happen. 
            if (!generated_direct_call) {
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
                        LLVMValueRef slot_nil_args[] = {cg_value_to_i8_ptr(cg, slot)};
                        (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, slot_nil_args, 1, "");
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
            }
            
            return tmp;
        }

        case AST_EXPR_CALL: {
            // WE LOVE OUR RRANGE FUNCTION!!!!!!! #Python4Ever!
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                int arg_count = expr->as.call.arg_count;
                if (arg_count < 1 || arg_count > 3) {
                    fprintf(stderr, "Error: range() expects 1-3 arguments\n");
                    return NULL;
                }
                
                tmp = cg_alloc_value(cg, "rangetmp");
                
                if (arg_count == 1) {
                    // range(end) -> for i in range(10)
                    LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[0]);
                    if (!arg_val) return NULL;
                    
                    LLVMValueRef int_val = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int, 
                                                         (LLVMValueRef[]){cg_value_to_i8_ptr(cg, arg_val)}, 1, "range_n");
                    
                    LLVMValueRef range_array = LLVMBuildCall2(cg->builder, cg->ty_range_simple, cg->fn_range_simple,
                                                             (LLVMValueRef[]){int_val}, 1, "range_array");
                    
                    LLVMValueRef array_args[] = {cg_value_to_i8_ptr(cg, tmp), range_array};
                    (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_array, cg->fn_value_set_array, array_args, 2, "");
                } else {
                    // range(start, end) or range(start, end, step) - use range_create function (for i in range(1,30,2))
                    LLVMValueRef start_val = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[0]);
                    LLVMValueRef end_val = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[1]);
                    if (!start_val || !end_val) return NULL;
                    
                    LLVMValueRef start_int = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int, 
                                                           (LLVMValueRef[]){cg_value_to_i8_ptr(cg, start_val)}, 1, "range_start");
                    LLVMValueRef end_int = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int, 
                                                         (LLVMValueRef[]){cg_value_to_i8_ptr(cg, end_val)}, 1, "range_end");
                    
                    LLVMValueRef step_int;
                    if (arg_count == 3) {
                        LLVMValueRef step_val = cg_build_expr(cg, cg_fn, val_size, expr->as.call.args[2]);
                        if (!step_val) return NULL;
                        step_int = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int, 
                                                 (LLVMValueRef[]){cg_value_to_i8_ptr(cg, step_val)}, 1, "range_step");
                    } else {
                        step_int = LLVMConstInt(cg->i32, 1, 0); // default step = 1
                    }
                    
                    LLVMValueRef range_array = LLVMBuildCall2(cg->builder, cg->ty_range_create, cg->fn_range_create,
                                                             (LLVMValueRef[]){start_int, end_int, step_int}, 3, "range_array");
                    
                    LLVMValueRef array_args[] = {cg_value_to_i8_ptr(cg, tmp), range_array};
                    (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_array, cg->fn_value_set_array, array_args, 2, "");
                }
                
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
                    LLVMSetAlignment(args_arr, 16);

                    LLVMValueRef zero = LLVMConstInt(cg->i32, 0, 0);
                    for (int i = 0; i < expr->as.call.arg_count; i++) {
                        LLVMValueRef idx = LLVMConstInt(cg->i32, (unsigned)i, 0);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, args_arr_ty, args_arr, (LLVMValueRef[]){zero, idx}, 2, "builtin.arg.ptr");
                        LLVMValueRef elem_nil_args[] = {cg_value_to_i8_ptr(cg, elem_ptr)};
                        (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, elem_nil_args, 1, "");
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
            CgClass* callee_class = NULL;
            
            // Check the funcs the user created
            for (CgFunction* f = cg->functions; f; f = f->next) {
                if (strcmp(f->name, expr->as.call.name) == 0) {
                    callee_fn = f;
                    break;
                }
            }
            
            // If not look at classes. 
            if (!callee_fn) {
                for (CgClass* c = cg->classes; c; c = c->next) {
                    if (strcmp(c->name, expr->as.call.name) == 0) {
                        callee_class = c;
                        break;
                    }
                }
            }

            if (callee_class) {
                tmp = cg_alloc_value(cg, "constructortmp");
                LLVMValueRef class_name_str = cg_get_string_global(cg, callee_class->name);
                LLVMValueRef class_name_ptr = LLVMBuildBitCast(cg->builder, class_name_str, cg->i8_ptr, "");
                LLVMTypeRef i8_ptr_ptr = LLVMPointerType(cg->i8_ptr, 0);
                LLVMValueRef field_names_ptr = LLVMConstNull(i8_ptr_ptr);
                
                // collect all the fields
                char** all_field_names;
                int total_field_count;
                if (!cg_collect_all_fields(cg, callee_class, &all_field_names, &total_field_count)) {
                    return NULL;
                }
                
                if (total_field_count > 0) {
                    LLVMTypeRef field_names_arr_ty = LLVMArrayType(cg->i8_ptr, (unsigned)total_field_count);
                    LLVMValueRef field_names_arr = LLVMBuildAlloca(cg->builder, field_names_arr_ty, "class_field_names");
                    
                    for (int i = 0; i < total_field_count; i++) {
                        LLVMValueRef field_name_str = cg_get_string_global(cg, all_field_names[i]);
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

                    LLVMValueRef first = LLVMBuildGEP2(
                        cg->builder,
                        field_names_arr_ty,
                        field_names_arr,
                        (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, 0, 0)},
                        2,
                        "field_names_first");
                    field_names_ptr = LLVMBuildBitCast(cg->builder, first, i8_ptr_ptr, "");
                }
                
                // Free the temp names. 
                for (int i = 0; i < total_field_count; i++) {
                    free(all_field_names[i]);
                }
                free(all_field_names);
                LLVMTypeRef ty_class_new = LLVMFunctionType(
                    cg->i8_ptr,  // Returns BreadClass*
                    (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, i8_ptr_ptr, cg->i32, i8_ptr_ptr},  // name, parent_name, field_count, field_names, method_count, method_names
                    6,
                    0
                );
                LLVMValueRef fn_class_new = cg_declare_fn(cg, "bread_class_create_instance", ty_class_new);
                
                LLVMValueRef field_count = LLVMConstInt(cg->i32, total_field_count, 0);
                LLVMValueRef parent_name_ptr = callee_class->parent_name ? 
                    LLVMBuildBitCast(cg->builder, cg_get_string_global(cg, callee_class->parent_name), cg->i8_ptr, "") :
                    LLVMConstNull(cg->i8_ptr);
                
                LLVMValueRef method_names_ptr = LLVMConstNull(i8_ptr_ptr);
                LLVMValueRef method_count = LLVMConstInt(cg->i32, callee_class->method_count, 0);
                if (callee_class->method_count > 0) {
                    LLVMTypeRef method_names_arr_ty = LLVMArrayType(cg->i8_ptr, (unsigned)callee_class->method_count);
                    LLVMValueRef method_names_arr = LLVMBuildAlloca(cg->builder, method_names_arr_ty, "class_method_names");
                    
                    for (int i = 0; i < callee_class->method_count; i++) {
                        LLVMValueRef method_name_str = cg_get_string_global(cg, callee_class->method_names[i]);
                        LLVMValueRef method_name_ptr = LLVMBuildBitCast(cg->builder, method_name_str, cg->i8_ptr, "");
                        LLVMValueRef method_slot = LLVMBuildGEP2(
                            cg->builder,
                            method_names_arr_ty,
                            method_names_arr,
                            (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, i, 0)},
                            2,
                            "method_name_slot");
                        LLVMBuildStore(cg->builder, method_name_ptr, method_slot);
                    }

                    LLVMValueRef first = LLVMBuildGEP2(
                        cg->builder,
                        method_names_arr_ty,
                        method_names_arr,
                        (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, 0, 0)},
                        2,
                        "method_names_first");
                    method_names_ptr = LLVMBuildBitCast(cg->builder, first, i8_ptr_ptr, "");
                }
                
                LLVMValueRef class_ptr = LLVMBuildCall2(cg->builder, ty_class_new, fn_class_new,
                    (LLVMValueRef[]){class_name_ptr, parent_name_ptr, field_count, field_names_ptr, method_count, method_names_ptr}, 6, "class_instance");
                
                // Set the result value to the class BEFORE calling constructor
                LLVMTypeRef ty_value_set_class = LLVMFunctionType(
                    cg->void_ty,
                    (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr},  // BreadValue*, BreadClass*
                    2,
                    0
                );
                LLVMValueRef fn_value_set_class = cg_declare_fn(cg, "bread_value_set_class", ty_value_set_class);
                
                LLVMValueRef class_args[] = {cg_value_to_i8_ptr(cg, tmp), class_ptr};
                (void)LLVMBuildCall2(cg->builder, ty_value_set_class, fn_value_set_class, class_args, 2, "");
                if (callee_class->constructor) {
                    LLVMTypeRef ty_method_call = LLVMFunctionType(
                        cg->void_ty,
                        (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr, cg->i32, cg->i8_ptr},
                        6,
                        0
                    );
                    LLVMValueRef fn_method_call = cg_declare_fn(cg, "bread_method_call_op", ty_method_call);
                    LLVMValueRef constructor_name_str = cg_get_string_global(cg, "init");
                    LLVMValueRef constructor_name_ptr = LLVMBuildBitCast(cg->builder, constructor_name_str, cg->i8_ptr, "");
                    
                    // Args array (pad omitted args with defaults)
                    int provided = expr->as.call.arg_count;
                    int expected = callee_class->constructor->param_count;
                    int final_argc = expected;
                    LLVMValueRef args_ptr = LLVMConstNull(cg->i8_ptr);
                    if (final_argc > 0) {
                        LLVMTypeRef args_arr_ty = LLVMArrayType(cg->value_type, (unsigned)final_argc);
                        LLVMValueRef args_alloca = LLVMBuildAlloca(cg->builder, args_arr_ty, "constructor_args");
                        LLVMSetAlignment(args_alloca, 16);

                        for (int i = 0; i < final_argc; i++) {
                            ASTExpr* arg_expr = NULL;
                            if (i < provided) {
                                arg_expr = expr->as.call.args[i];
                            } else if (callee_class->constructor->param_defaults) {
                                arg_expr = callee_class->constructor->param_defaults[i];
                            }

                            if (!arg_expr) {
                                fprintf(stderr, "Error: Missing argument %d for constructor '%s' and no default provided\n", i + 1, expr->as.call.name);
                                return NULL;
                            }

                            LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, arg_expr);
                            if (!arg_val) return NULL;

                            LLVMValueRef slot = LLVMBuildGEP2(
                                cg->builder,
                                args_arr_ty,
                                args_alloca,
                                (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, i, 0)},
                                2,
                                "constructor_arg_slot");
                            LLVMValueRef slot_nil_args[] = {cg_value_to_i8_ptr(cg, slot)};
                            (void)LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, slot_nil_args, 1, "");
                            cg_copy_value_into(cg, slot, arg_val);
                        }

                        args_ptr = LLVMBuildBitCast(cg->builder, args_alloca, cg->i8_ptr, "");
                    }
                    
                    LLVMValueRef method_result = cg_alloc_value(cg, "constructor_result");
                    LLVMValueRef method_args[] = {
                        cg_value_to_i8_ptr(cg, tmp),  // The class instance (self)
                        constructor_name_ptr,          // Method name ("init")
                        LLVMConstInt(cg->i32, final_argc, 0),  // Argument count
                        args_ptr,                      // Arguments array
                        LLVMConstInt(cg->i32, 0, 0),  // is_optional_chain (false)
                        cg_value_to_i8_ptr(cg, method_result)  // Result (ignored)
                    };
                    (void)LLVMBuildCall2(cg->builder, ty_method_call, fn_method_call, method_args, 6, "");
                }
                
                return tmp;
            }

            if (!callee_fn) {
                fprintf(stderr, "Error: Unknown function or class '%s'\n", expr->as.call.name);
                return NULL;
            }

            // NOTE: Function bodies are generated in a separate pass
            // (bread_llvm_generate_function_bodies). Do not lazily generate bodies here,
            // because that path bypasses the runtime scope prologue/epilogue and can leak
            // function-local variables into the caller scope.

            int provided = expr->as.call.arg_count;
            int expected = callee_fn->param_count;
            int total_args = expected + 1;
            LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * (size_t)total_args);
            if (!args) return NULL;
            tmp = cg_alloc_value(cg, "calltmp");
            args[0] = tmp;

            for (int i = 0; i < expected; i++) {
                ASTExpr* arg_expr = NULL;
                if (i < provided) {
                    arg_expr = expr->as.call.args[i];
                } else if (callee_fn->param_defaults) {
                    arg_expr = callee_fn->param_defaults[i];
                }

                if (!arg_expr) {
                    fprintf(stderr, "Error: Missing argument %d for function '%s' and no default provided\n", i + 1, expr->as.call.name);
                    free(args);
                    return NULL;
                }

                LLVMValueRef arg_val = cg_build_expr(cg, cg_fn, val_size, arg_expr);
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
            LLVMValueRef struct_name_str = cg_get_string_global(cg, expr->as.struct_literal.struct_name);
            LLVMValueRef struct_name_ptr = LLVMBuildBitCast(cg->builder, struct_name_str, cg->i8_ptr, "");
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
            
            // Set the struct to the result. 
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
        case AST_EXPR_CLASS_LITERAL: {
            tmp = cg_alloc_value(cg, "classlittmp");
            LLVMValueRef class_name_str = cg_get_string_global(cg, expr->as.class_literal.class_name);
            LLVMValueRef class_name_ptr = LLVMBuildBitCast(cg->builder, class_name_str, cg->i8_ptr, "");
            LLVMTypeRef i8_ptr_ptr = LLVMPointerType(cg->i8_ptr, 0);
            LLVMValueRef field_names_ptr = LLVMConstNull(i8_ptr_ptr);
            if (expr->as.class_literal.field_count > 0) {
                LLVMTypeRef field_names_arr_ty = LLVMArrayType(cg->i8_ptr, (unsigned)expr->as.class_literal.field_count);
                LLVMValueRef field_names_arr = LLVMBuildAlloca(cg->builder, field_names_arr_ty, "class_field_names");
                
                for (int i = 0; i < expr->as.class_literal.field_count; i++) {
                    LLVMValueRef field_name_str = cg_get_string_global(cg, expr->as.class_literal.field_names[i]);
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

                LLVMValueRef first = LLVMBuildGEP2(
                    cg->builder,
                    field_names_arr_ty,
                    field_names_arr,
                    (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, 0, 0)},
                    2,
                    "field_names_first");
                field_names_ptr = LLVMBuildBitCast(cg->builder, first, i8_ptr_ptr, "");
            }
            
            LLVMTypeRef ty_class_new = LLVMFunctionType(
                cg->i8_ptr,  // Returns BreadClass*
                (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, i8_ptr_ptr},  // name, parent_name, field_count, field_names
                4,
                0
            );
            LLVMValueRef fn_class_new = cg_declare_fn(cg, "bread_class_new", ty_class_new);
            
            LLVMValueRef field_count = LLVMConstInt(cg->i32, expr->as.class_literal.field_count, 0);
            LLVMValueRef parent_name_ptr = LLVMConstNull(cg->i8_ptr);  // No parent for literals
            LLVMValueRef class_ptr = LLVMBuildCall2(cg->builder, ty_class_new, fn_class_new,
                (LLVMValueRef[]){class_name_ptr, parent_name_ptr, field_count, field_names_ptr}, 4, "class_instance");
            
            // Set field values
            if (expr->as.class_literal.field_count > 0) {
                LLVMTypeRef ty_class_set_field = LLVMFunctionType(
                    cg->void_ty,
                    (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},  // class*, field_name, value*
                    3,
                    0
                );
                LLVMValueRef fn_class_set_field = cg_declare_fn(cg, "bread_class_set_field_value_ptr", ty_class_set_field);
                
                for (int i = 0; i < expr->as.class_literal.field_count; i++) {
                    LLVMValueRef field_value = cg_build_expr(cg, cg_fn, val_size, expr->as.class_literal.field_values[i]);
                    if (!field_value) return NULL;
                    
                    LLVMValueRef field_name_str = cg_get_string_global(cg, expr->as.class_literal.field_names[i]);
                    LLVMValueRef field_name_ptr = LLVMBuildBitCast(cg->builder, field_name_str, cg->i8_ptr, "");
                    
                    LLVMValueRef set_args[] = {
                        class_ptr,
                        field_name_ptr,
                        cg_value_to_i8_ptr(cg, field_value)
                    };
                    (void)LLVMBuildCall2(cg->builder, ty_class_set_field, fn_class_set_field, set_args, 3, "");
                }
            }
            LLVMTypeRef ty_value_set_class = LLVMFunctionType(
                cg->void_ty,
                (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr},  // BreadValue*, BreadClass*
                2,
                0
            );
            LLVMValueRef fn_value_set_class = cg_declare_fn(cg, "bread_value_set_class", ty_value_set_class);
            
            LLVMValueRef class_args[] = {cg_value_to_i8_ptr(cg, tmp), class_ptr};
            (void)LLVMBuildCall2(cg->builder, ty_value_set_class, fn_value_set_class, class_args, 2, "");
            
            return tmp;
        }
        
        case AST_EXPR_SELF: {
            if (!cg_fn || !cg_fn->self_param) {
                fprintf(stderr, "Error: 'self' used outside of method context\n");
                return NULL;
            }
            return cg_clone_value(cg, cg_fn->self_param, "self");
        }
        
        case AST_EXPR_SUPER: {
            if (!cg_fn || !cg_fn->self_param || !cg_fn->current_class || !cg_fn->current_class->parent_name) {
                fprintf(stderr, "Error: 'super' used outside of method context or class has no parent\n");
                return NULL;
            }
            return cg_clone_value(cg, cg_fn->self_param, "super");
        }
        default:
            fprintf(stderr, "Codegen not implemented for expr kind %d\n", expr->kind);
            return NULL;
    }
}
