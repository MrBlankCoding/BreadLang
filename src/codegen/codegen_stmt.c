#include "codegen_internal.h"
#include "core/type_descriptor.h"

int cg_build_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    if (!cg || !stmt) return 0;
    switch (stmt->kind) {
        case AST_STMT_EXPR: {
            LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.expr.expr);
            return val != NULL;
        }
        case AST_STMT_VAR_ASSIGN: {
            if (cg_fn) {
                CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.var_assign.var_name);
                if (var && var->unboxed_type != UNBOXED_NONE) {
                    // Handle unboxed variable assignment
                    CgValue value_unboxed = cg_build_expr_unboxed(cg, cg_fn, stmt->as.var_assign.value);
                    
                    if (value_unboxed.type != CG_VALUE_BOXED) {
                        // Direct unboxed assignment
                        LLVMBuildStore(cg->builder, value_unboxed.value, var->alloca);
                    } else {
                        // Unbox the value and store
                        CgValue unboxed = cg_unbox_value(cg, value_unboxed.value, var->type);
                        LLVMBuildStore(cg->builder, unboxed.value, var->alloca);
                    }
                    return 1;
                }
                
                // Handle boxed variable assignment (existing logic)
                LLVMValueRef value = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_assign.value);
                if (!value) return 0;
                
                if (!var) {
                    LLVMValueRef slot = cg_alloc_value(cg, stmt->as.var_assign.var_name);
                    cg_copy_value_into(cg, slot, value);
                    cg_scope_add_var(cg_fn->scope, stmt->as.var_assign.var_name, slot);
                    return 1;
                }
                cg_copy_value_into(cg, var->alloca, value);
                return 1;
            }

            // Global variable assignment (always boxed)
            LLVMValueRef value = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_assign.value);
            if (!value) return 0;
            
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
            // Check if we can store this variable unboxed
            int can_unbox = 0;
            UnboxedType unboxed_type = UNBOXED_NONE;
            
            if (stmt->as.var_decl.init && var_type_can_unbox(stmt->as.var_decl.type)) {
                // Check if the initializer can be unboxed
                if (cg_can_unbox_expr(cg, stmt->as.var_decl.init)) {
                    can_unbox = 1;
                    unboxed_type = var_type_to_unboxed(stmt->as.var_decl.type);
                }
            }
            
            if (can_unbox && cg_fn) {
                CgValue init_val = cg_build_expr_unboxed(cg, cg_fn, stmt->as.var_decl.init);
                
                LLVMTypeRef alloc_type;
                switch (unboxed_type) {
                    case UNBOXED_INT:
                        alloc_type = cg->i32;
                        break;
                    case UNBOXED_DOUBLE:
                        alloc_type = cg->f64;
                        break;
                    case UNBOXED_BOOL:
                        alloc_type = cg->i1;
                        break;
                    default:
                        alloc_type = cg->value_type;
                        break;
                }
                
                LLVMValueRef slot = LLVMBuildAlloca(cg->builder, alloc_type, stmt->as.var_decl.var_name);
                
                // Store the unboxed value directly
                if (init_val.type != CG_VALUE_BOXED) {
                    LLVMBuildStore(cg->builder, init_val.value, slot);
                } else {
                    CgValue unboxed = cg_unbox_value(cg, init_val.value, stmt->as.var_decl.type);
                    LLVMBuildStore(cg->builder, unboxed.value, slot);
                }
                
                CgVar* var = cg_scope_add_var(cg_fn->scope, stmt->as.var_decl.var_name, slot);
                if (var) {
                    var->type = stmt->as.var_decl.type;
                    var->unboxed_type = unboxed_type;
                    var->is_const = stmt->as.var_decl.is_const;
                    var->is_initialized = 1;
                }
            } else {
                LLVMValueRef init = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_decl.init);
                if (!init) return 0;

                if (cg_fn) {
                    LLVMValueRef slot = cg_alloc_value(cg, stmt->as.var_decl.var_name);
                    cg_copy_value_into(cg, slot, init);
                    CgVar* var = cg_scope_add_var(cg_fn->scope, stmt->as.var_decl.var_name, slot);
                    if (var) {
                        var->type = stmt->as.var_decl.type;
                        var->unboxed_type = UNBOXED_NONE;
                        var->is_const = stmt->as.var_decl.is_const;
                        var->is_initialized = 1;
                    }
                } else {
                    LLVMValueRef name_str = cg_get_string_global(cg, stmt->as.var_decl.var_name);
                    LLVMValueRef name_ptr = LLVMBuildBitCast(cg->builder, name_str, cg->i8_ptr, "");
                    LLVMValueRef type = LLVMConstInt(cg->i32, stmt->as.var_decl.type, 0);
                    LLVMValueRef is_const = LLVMConstInt(cg->i32, stmt->as.var_decl.is_const, 0);
                    LLVMValueRef args[] = {name_ptr, type, is_const, cg_value_to_i8_ptr(cg, init)};
                    (void)LLVMBuildCall2(cg->builder, cg->ty_var_decl, cg->fn_var_decl, args, 4, "");
                }
            }
            return 1;
        }
        case AST_STMT_IF: {
            CgValue cond_val = cg_build_expr_unboxed(cg, cg_fn, stmt->as.if_stmt.condition);
            LLVMValueRef cond_i1 = NULL;
            
            if (cond_val.type == CG_VALUE_UNBOXED_BOOL) {
                cond_i1 = cond_val.value;
            } else {
                // Fall back to boxed condition evaluation
                LLVMValueRef cond = NULL;
                if (cond_val.type == CG_VALUE_BOXED && cond_val.value) {
                    cond = cond_val.value;
                } else {
                    cond = cg_build_expr(cg, cg_fn, val_size, stmt->as.if_stmt.condition);
                }
                if (!cond) return 0;

                LLVMValueRef truthy_args[] = {cg_value_to_i8_ptr(cg, cond)};
                LLVMValueRef is_truthy = LLVMBuildCall2(cg->builder, cg->ty_is_truthy, cg->fn_is_truthy, truthy_args, 1, "");
                cond_i1 = LLVMBuildICmp(cg->builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->i32, 0, 0), "");
            }

            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
            if (!current_block) return 0;
            LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);

            LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(fn, "then");
            LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(fn, "else");
            LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(fn, "ifcont");

            LLVMBuildCondBr(cg->builder, cond_i1, then_block, else_block);

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

            LLVMBasicBlockRef prev_loop_end = cg->current_loop_end;
            LLVMBasicBlockRef prev_loop_continue = cg->current_loop_continue;
            cg->current_loop_end = end_block;
            cg->current_loop_continue = cond_block;

            LLVMBuildBr(cg->builder, cond_block);

            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            
            CgValue cond_val = cg_build_expr_unboxed(cg, cg_fn, stmt->as.while_stmt.condition);
            LLVMValueRef cond_i1 = NULL;
            
            if (cond_val.type == CG_VALUE_UNBOXED_BOOL) {
                cond_i1 = cond_val.value;
            } else {
                LLVMValueRef cond = NULL;
                if (cond_val.type == CG_VALUE_BOXED && cond_val.value) {
                    cond = cond_val.value;
                } else {
                    cond = cg_build_expr(cg, cg_fn, val_size, stmt->as.while_stmt.condition);
                }
                if (!cond) return 0;
                
                LLVMValueRef truthy_args[] = {cg_value_to_i8_ptr(cg, cond)};
                LLVMValueRef is_truthy = LLVMBuildCall2(cg->builder, cg->ty_is_truthy, cg->fn_is_truthy, truthy_args, 1, "");
                cond_i1 = LLVMBuildICmp(cg->builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->i32, 0, 0), "");
            }
            
            LLVMBuildCondBr(cg->builder, cond_i1, body_block, end_block);

            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.while_stmt.body)) return 0;
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, cond_block);
            }

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

            LLVMBasicBlockRef prev_loop_end = cg->current_loop_end;
            LLVMBasicBlockRef prev_loop_continue = cg->current_loop_continue;
            cg->current_loop_end = end_block;
            cg->current_loop_continue = inc_block;

            LLVMBuildBr(cg->builder, setup_block);

            LLVMPositionBuilderAtEnd(cg->builder, setup_block);
            LLVMValueRef iterable = cg_build_expr(cg, cg_fn, val_size, stmt->as.for_in_stmt.iterable);
            if (!iterable) return 0;

            // Determine the type of the iterable
            TypeDescriptor* iterable_type = cg_infer_expr_type_desc_simple(cg, stmt->as.for_in_stmt.iterable);
            if (!iterable_type) return 0;

            LLVMValueRef keys_array = NULL;
            LLVMValueRef actual_iterable = iterable;

            // If it's a dictionary, get the keys array to iterate over
            if (iterable_type->base_type == TYPE_DICT) {
                keys_array = cg_alloc_value(cg, "dict.keys");
                LLVMValueRef get_keys_args[] = {cg_value_to_i8_ptr(cg, iterable), cg_value_to_i8_ptr(cg, keys_array)};
                LLVMValueRef success = LLVMBuildCall2(cg->builder, cg->ty_dict_keys, cg->fn_dict_keys, get_keys_args, 2, "");
                
                // Check if getting keys was successful
                LLVMValueRef success_check = LLVMBuildICmp(cg->builder, LLVMIntNE, success, LLVMConstInt(cg->i32, 0, 0), "");
                LLVMBasicBlockRef keys_success_block = LLVMAppendBasicBlock(fn, "keys.success");
                LLVMBuildCondBr(cg->builder, success_check, keys_success_block, end_block);
                LLVMPositionBuilderAtEnd(cg->builder, keys_success_block);
                
                actual_iterable = keys_array;
            } else if (iterable_type->base_type != TYPE_ARRAY) {
                type_descriptor_free(iterable_type);
                return 0; // Only arrays and dictionaries are iterable
            }

            LLVMValueRef index_slot = LLVMBuildAlloca(cg->builder, cg->i32, "forin.index");
            LLVMBuildStore(cg->builder, LLVMConstInt(cg->i32, 0, 0), index_slot);

            LLVMValueRef length = LLVMBuildCall2(cg->builder, cg->ty_array_length, cg->fn_array_length,
                                                (LLVMValueRef[]){cg_value_to_i8_ptr(cg, actual_iterable)}, 1, "forin.length");

            LLVMValueRef length_check = LLVMBuildICmp(cg->builder, LLVMIntSGT, length, LLVMConstInt(cg->i32, 0, 0), "");
            LLVMBasicBlockRef valid_length_block = LLVMAppendBasicBlock(fn, "forin.valid_length");
            LLVMBuildCondBr(cg->builder, length_check, valid_length_block, end_block);

            LLVMPositionBuilderAtEnd(cg->builder, valid_length_block);

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

            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMValueRef index_phi = LLVMBuildPhi(cg->builder, cg->i32, "forin.index.phi");
            LLVMValueRef phi_vals[] = {LLVMConstInt(cg->i32, 0, 0)};
            LLVMBasicBlockRef phi_blocks[] = {valid_length_block};
            LLVMAddIncoming(index_phi, phi_vals, phi_blocks, 1);

            LLVMValueRef cmp = LLVMBuildICmp(cg->builder, LLVMIntSLT, index_phi, length, "forin.cond");
            LLVMBuildCondBr(cg->builder, cmp, body_block, end_block);

            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            
            LLVMValueRef element_tmp = cg_alloc_value(cg, "forin.element");
            LLVMValueRef get_args[] = {cg_value_to_i8_ptr(cg, actual_iterable), index_phi, cg_value_to_i8_ptr(cg, element_tmp)};
            LLVMValueRef get_success = LLVMBuildCall2(cg->builder, cg->ty_array_get, cg->fn_array_get, get_args, 3, "");
            
            LLVMValueRef success_cmp = LLVMBuildICmp(cg->builder, LLVMIntNE, get_success, LLVMConstInt(cg->i32, 0, 0), "");
            LLVMBasicBlockRef assign_block = LLVMAppendBasicBlock(fn, "forin.assign");
            LLVMBuildCondBr(cg->builder, success_cmp, assign_block, end_block);
            
            LLVMPositionBuilderAtEnd(cg->builder, assign_block);
            LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, element_tmp)};
            (void)LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");
            if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.for_in_stmt.body)) return 0;
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
                LLVMBuildBr(cg->builder, inc_block);
            }
            LLVMPositionBuilderAtEnd(cg->builder, inc_block);

            LLVMValueRef next_index = LLVMBuildAdd(cg->builder, index_phi, LLVMConstInt(cg->i32, 1, 0), "forin.next");
            LLVMValueRef inc_phi_vals[] = {next_index};
            LLVMBasicBlockRef inc_phi_blocks[] = {inc_block};
            
            LLVMAddIncoming(index_phi, inc_phi_vals, inc_phi_blocks, 1);
            LLVMBuildBr(cg->builder, cond_block);
            
            cg->current_loop_end = prev_loop_end;
            cg->current_loop_continue = prev_loop_continue;

            type_descriptor_free(iterable_type);
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

            CgFunction* new_cg_fn = (CgFunction*)malloc(sizeof(CgFunction));
            new_cg_fn->name = strdup(stmt->as.func_decl.name);
            new_cg_fn->fn = fn;
            new_cg_fn->type = fn_type;
            new_cg_fn->body = stmt->as.func_decl.body;
            new_cg_fn->param_count = stmt->as.func_decl.param_count;
            new_cg_fn->param_names = stmt->as.func_decl.param_names;
            new_cg_fn->scope = cg_scope_new(NULL);
            new_cg_fn->next = cg->functions;
            new_cg_fn->ret_slot = NULL;
            cg->functions = new_cg_fn;
            
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
            
            // Pop the scope before returning from the function
            // (void)LLVMBuildCall2(cg->builder, cg->ty_pop_scope, cg->fn_pop_scope, NULL, 0, "");
            
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
