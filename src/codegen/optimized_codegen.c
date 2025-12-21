#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen/optimized_codegen.h"

int optimized_codegen_init(OptimizedCg* cg, LLVMModuleRef mod) {
    if (!cg || !mod) return 0;
    
    memset(cg, 0, sizeof(OptimizedCg));
    
    cg->base.mod = mod;
    cg->base.builder = LLVMCreateBuilder();
    
    cg->base.i1 = LLVMInt1Type();
    cg->base.i8 = LLVMInt8Type();
    cg->base.i8_ptr = LLVMPointerType(cg->base.i8, 0);
    cg->base.i32 = LLVMInt32Type();
    cg->base.i64 = LLVMInt64Type();
    cg->base.f64 = LLVMDoubleType();
    cg->base.void_ty = LLVMVoidType();
    cg->unboxed_int = cg->base.i32;
    cg->unboxed_double = cg->base.f64;
    cg->unboxed_bool = cg->base.i1;
    
    LLVMTypeRef ptr_to_value = cg->base.i8_ptr;

    LLVMTypeRef get_int_params[] = { ptr_to_value };
    cg->ty_value_get_int = LLVMFunctionType(cg->base.i32, get_int_params, 1, 0);
    cg->fn_value_get_int = cg_declare_fn((Cg*)cg, "bread_value_get_int", cg->ty_value_get_int);

    LLVMTypeRef get_double_params[] = { ptr_to_value };
    cg->ty_value_get_double = LLVMFunctionType(cg->base.f64, get_double_params, 1, 0);
    cg->fn_value_get_double = cg_declare_fn((Cg*)cg, "bread_value_get_double", cg->ty_value_get_double);

    LLVMTypeRef get_bool_params[] = { ptr_to_value };
    cg->ty_value_get_bool = LLVMFunctionType(cg->base.i32, get_bool_params, 1, 0);
    cg->fn_value_get_bool = cg_declare_fn((Cg*)cg, "bread_value_get_bool", cg->ty_value_get_bool);
    
    LLVMTypeRef get_type_params[] = { ptr_to_value };
    cg->ty_value_get_type = LLVMFunctionType(cg->base.i32, get_type_params, 1, 0);
    cg->fn_value_get_type = cg_declare_fn((Cg*)cg, "bread_value_get_type", cg->ty_value_get_type);

    cg->stack_capacity = 64;
    cg->stack_slots = malloc(cg->stack_capacity * sizeof(LLVMValueRef));
    if (!cg->stack_slots) {
        LLVMDisposeBuilder(cg->base.builder);
        return 0;
    }
    
    cg->enable_unboxing = 1;
    cg->enable_stack_alloc = 1;
    cg->enable_inlining = 1;
    
    return 1;
}

void optimized_codegen_cleanup(OptimizedCg* cg) {
    if (!cg) return;
    
    if (cg->base.builder) {
        LLVMDisposeBuilder(cg->base.builder);
    }
    
    free(cg->stack_slots);
    memset(cg, 0, sizeof(OptimizedCg));
}

static OptimizedValue create_optimized_value(ValueRepresentation repr, LLVMValueRef value, LLVMTypeRef type) {
    OptimizedValue val;
    val.repr = repr;
    val.value = value;
    val.type = type;
    return val;
}

OptimizedValue optimized_build_expr(OptimizedCg* cg, CgFunction* cg_fn, ASTExpr* expr) {
    if (!cg || !expr) {
        return create_optimized_value(VALUE_BOXED, NULL, NULL);
    }
    
    // Check if we can unbox this expression
    TypeStabilityInfo* stability = get_expr_stability_info(expr);
    EscapeInfo* escape = get_escape_info(expr);
    (void)escape;
    
    switch (expr->kind) {
        case AST_EXPR_INT:
            if (cg->enable_unboxing && stability && stability->type == TYPE_INT && 
                stability->stability >= STABILITY_CONDITIONAL) {
                // Generate unboxed integer
                LLVMValueRef int_val = LLVMConstInt(cg->unboxed_int, expr->as.int_val, 0);
                return create_optimized_value(VALUE_UNBOXED_INT, int_val, cg->unboxed_int);
            }
            break;
            
        case AST_EXPR_DOUBLE:
            if (cg->enable_unboxing && stability && stability->type == TYPE_DOUBLE &&
                stability->stability >= STABILITY_CONDITIONAL) {
                // Generate unboxed double
                LLVMValueRef double_val = LLVMConstReal(cg->unboxed_double, expr->as.double_val);
                return create_optimized_value(VALUE_UNBOXED_DOUBLE, double_val, cg->unboxed_double);
            }
            break;
            
        case AST_EXPR_BOOL:
            if (cg->enable_unboxing && stability && stability->type == TYPE_BOOL &&
                stability->stability >= STABILITY_CONDITIONAL) {
                // Generate unboxed boolean
                LLVMValueRef bool_val = LLVMConstInt(cg->unboxed_bool, expr->as.bool_val ? 1 : 0, 0);
                return create_optimized_value(VALUE_UNBOXED_BOOL, bool_val, cg->unboxed_bool);
            }
            break;
            
        case AST_EXPR_BINARY:
            // Check if we can generate unboxed binary operations
            if (cg->enable_unboxing && stability && stability->type == TYPE_INT &&
                stability->stability >= STABILITY_CONDITIONAL) {
                
                OptimizedValue left = optimized_build_expr(cg, cg_fn, expr->as.binary.left);
                OptimizedValue right = optimized_build_expr(cg, cg_fn, expr->as.binary.right);
                
                if (left.repr == VALUE_UNBOXED_INT && right.repr == VALUE_UNBOXED_INT) {
                    // Generate native integer arithmetic
                    LLVMValueRef result = NULL;
                    
                    switch (expr->as.binary.op) {
                        case '+':
                            result = LLVMBuildAdd(cg->base.builder, left.value, right.value, "add");
                            break;
                        case '-':
                            result = LLVMBuildSub(cg->base.builder, left.value, right.value, "sub");
                            break;
                        case '*':
                            result = LLVMBuildMul(cg->base.builder, left.value, right.value, "mul");
                            break;
                        case '/':
                            result = LLVMBuildSDiv(cg->base.builder, left.value, right.value, "div");
                            break;
                        default:
                            // Fall back to boxed operation
                            break;
                    }
                    
                    if (result) {
                        return create_optimized_value(VALUE_UNBOXED_INT, result, cg->unboxed_int);
                    }
                }
            }
            break;
            
        case AST_EXPR_VAR:
            // Variable access - check if we can use unboxed representation
            if (cg->enable_unboxing && stability && stability->type == TYPE_INT &&
                stability->stability >= STABILITY_STABLE) {
                // Look up variable in optimized symbol table
                if (cg_fn) {
                    CgVar* var = cg_scope_find_var(cg_fn->scope, expr->as.var_name);
                    if (var) {
                        // Check if this variable was stack-allocated as unboxed
                        // For now, assume it's boxed and unbox it
                        OptimizedValue unboxed = unbox_value(cg, var->alloca, stability->type);
                        return unboxed;
                    }
                }
            }
            break;
            
        default:
            break;
    }
    
    // Fall back to traditional boxed codegen
    // Integrate with existing cg_build_expr
    LLVMValueRef boxed_result = cg_build_expr((Cg*)cg, cg_fn, cg_value_size((Cg*)cg), expr);
    if (boxed_result) {
        return create_optimized_value(VALUE_BOXED, boxed_result, cg->base.value_type);
    }
    
    return create_optimized_value(VALUE_BOXED, NULL, NULL);
}

int optimized_build_stmt(OptimizedCg* cg, CgFunction* cg_fn, ASTStmt* stmt) {
    if (!cg || !stmt) return 0;
    
    OptimizationHints* hints = get_stmt_hints(stmt);
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL:
            if (stmt->as.var_decl.init) {
                OptimizedValue init_val = optimized_build_expr(cg, cg_fn, stmt->as.var_decl.init);
                
                // Check if we can stack-allocate this variable
                EscapeInfo* escape = get_escape_info(stmt->as.var_decl.init);
                if (cg->enable_stack_alloc && escape && escape->can_stack_allocate) {
                    // Use stack allocation instead of heap
                    LLVMValueRef stack_slot = alloc_stack_value(cg, stmt->as.var_decl.type, 
                                                              stmt->as.var_decl.var_name);
                    
                    if (init_val.repr != VALUE_BOXED) {
                        // Store unboxed value directly
                        LLVMBuildStore(cg->base.builder, init_val.value, stack_slot);
                    } else {
                        // Box the value and store
                        LLVMValueRef boxed = box_value(cg, init_val);
                        LLVMBuildStore(cg->base.builder, boxed, stack_slot);
                    }
                    
                    // Register variable in scope
                    if (cg_fn) {
                        cg_scope_add_var(cg_fn->scope, stmt->as.var_decl.var_name, stack_slot);
                    }
                    
                    return 1;
                }
            }
            break;
            
        case AST_STMT_IF: {
            // Apply branch prediction hints
            LLVMValueRef cond = optimized_build_expr(cg, cg_fn, stmt->as.if_stmt.condition).value;
            if (!cond) return 0; // Or fallback
            
            // If condition is unboxed bool, we can use it directly
            // Otherwise we need to unbox or check truthiness
            // For now, let's assume we can fallback if complex, or handle simple unboxed bool
            
            // Since we are inside optimized codegen, let's try to use fallback logic but with metadata
            // But fallback logic uses cg_build_expr which returns boxed value.
            // So we should probably just use cg_build_stmt for the whole IF if we don't want to reimplement it fully.
            // But we want to add metadata.
            
            // Re-implement IF here
            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->base.builder);
            LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);
            
            LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(fn, "then");
            LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(fn, "else");
            LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(fn, "ifcont");
            
            // We need an i1 for CondBr.
            // If cond is boxed, use is_truthy. If unboxed bool, use it.
            LLVMValueRef cond_i1 = NULL;
            // For simplicity, let's assume boxed for now or we need to check the expr result type again.
            // We called optimized_build_expr above, but we only got .value, lost .repr
            // Rerun optimized_build_expr
            OptimizedValue cond_val = optimized_build_expr(cg, cg_fn, stmt->as.if_stmt.condition);
            
            if (cond_val.repr == VALUE_UNBOXED_BOOL) {
                cond_i1 = cond_val.value;
            } else if (cond_val.repr == VALUE_BOXED) {
                LLVMValueRef truthy_args[] = { LLVMBuildBitCast(cg->base.builder, cond_val.value, cg->base.i8_ptr, "") };
                LLVMValueRef is_truthy = LLVMBuildCall2(cg->base.builder, cg->base.ty_is_truthy, cg->base.fn_is_truthy, truthy_args, 1, "");
                cond_i1 = LLVMBuildICmp(cg->base.builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->base.i32, 0, 0), "");
            } else {
                 // Convert int/double to bool
                 cond_i1 = LLVMConstInt(cg->base.i1, 1, 0); // Placeholder
            }
            
            LLVMValueRef br = LLVMBuildCondBr(cg->base.builder, cond_i1, then_block, else_block);
            apply_branch_hints(cg, br, hints);
            
            LLVMPositionBuilderAtEnd(cg->base.builder, then_block);
            // optimized_build_stmt(cg, cg_fn, stmt->as.if_stmt.then_branch); // Recurse? No, stmt list
            // We need optimized_build_stmt_list. But we only have optimized_build_stmt.
            // We can iterate list here or add optimized_build_stmt_list
            // Let's use fallback for body for now to save time, or iterate
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    optimized_build_stmt(cg, cg_fn, s);
                }
            }
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->base.builder)) == NULL) {
                LLVMBuildBr(cg->base.builder, merge_block);
            }
            
            LLVMPositionBuilderAtEnd(cg->base.builder, else_block);
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    optimized_build_stmt(cg, cg_fn, s);
                }
            }
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->base.builder)) == NULL) {
                LLVMBuildBr(cg->base.builder, merge_block);
            }
            
            LLVMPositionBuilderAtEnd(cg->base.builder, merge_block);
            return 1;
        }
            
        case AST_STMT_WHILE:
        case AST_STMT_FOR:
            // Mark loop as hot path for LLVM
            // Just fallback to cg_build_stmt for logic but we can't easily attach metadata to the loop backedge
            // unless we reimplement it.
            // Implementing WHILE
            if (stmt->kind == AST_STMT_WHILE) {
                LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->base.builder));
                LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "while.cond");
                LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "while.body");
                LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "while.end");
                
                LLVMBuildBr(cg->base.builder, cond_block);
                
                LLVMPositionBuilderAtEnd(cg->base.builder, cond_block);
                OptimizedValue cond_val = optimized_build_expr(cg, cg_fn, stmt->as.while_stmt.condition);
                LLVMValueRef cond_i1 = NULL;
                if (cond_val.repr == VALUE_UNBOXED_BOOL) {
                    cond_i1 = cond_val.value;
                } else {
                    // Boxed assumption
                     LLVMValueRef truthy_args[] = { LLVMBuildBitCast(cg->base.builder, box_value(cg, cond_val), cg->base.i8_ptr, "") };
                    LLVMValueRef is_truthy = LLVMBuildCall2(cg->base.builder, cg->base.ty_is_truthy, cg->base.fn_is_truthy, truthy_args, 1, "");
                    cond_i1 = LLVMBuildICmp(cg->base.builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->base.i32, 0, 0), "");
                }
                LLVMBuildCondBr(cg->base.builder, cond_i1, body_block, end_block);
                
                LLVMPositionBuilderAtEnd(cg->base.builder, body_block);
                if (stmt->as.while_stmt.body) {
                    for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                        optimized_build_stmt(cg, cg_fn, s);
                    }
                }
                
                LLVMValueRef loop_br = LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->base.builder));
                if (loop_br == NULL) {
                    loop_br = LLVMBuildBr(cg->base.builder, cond_block);
                }
                
                if (hints && hints->is_hot_path) {
                    // Add loop metadata to the backedge
                     LLVMContextRef ctx = LLVMGetModuleContext(cg->base.mod);
                     LLVMValueRef id_md = LLVMMDStringInContext(ctx, "llvm.loop", 9);
                     LLVMValueRef loop_md = LLVMMDNodeInContext(ctx, &id_md, 1);
                     LLVMSetMetadata(loop_br, LLVMGetMDKindIDInContext(ctx, "llvm.loop", 9), loop_md);
                }
                
                LLVMPositionBuilderAtEnd(cg->base.builder, end_block);
                return 1;
            }
            break;
            
        case AST_STMT_FUNC_DECL: {
            // Apply function attributes
            FunctionOptInfo* func_info = get_function_opt_info(&stmt->as.func_decl);
            // We need to create the function first.
            // cg_build_stmt handles creation.
            // We can call fallback to create it, then find it and apply attributes.
            cg_build_stmt((Cg*)cg, cg_fn, cg_value_size((Cg*)cg), stmt);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->base.mod, stmt->as.func_decl.name);
            if (func_info && fn) {
                apply_function_attributes(cg, fn, func_info);
            }
            return 1;
        }
            
        default:
            break;
    }
    
    // Fall back to traditional statement codegen
    // Integrate with existing cg_build_stmt
    return cg_build_stmt((Cg*)cg, cg_fn, cg_value_size((Cg*)cg), stmt);
}

LLVMValueRef box_value(OptimizedCg* cg, OptimizedValue val) {
    if (!cg || val.repr == VALUE_BOXED) {
        return val.value;
    }
    
    // Allocate boxed value
    LLVMValueRef boxed = LLVMBuildAlloca(cg->base.builder, cg->base.value_type, "boxed");
    
    switch (val.repr) {
        case VALUE_UNBOXED_INT: {
            // Call bread_value_set_int
            LLVMValueRef args[] = {
                LLVMBuildBitCast(cg->base.builder, boxed, cg->base.i8_ptr, ""),
                val.value
            };
            LLVMBuildCall2(cg->base.builder, cg->base.ty_value_set_int, 
                          cg->base.fn_value_set_int, args, 2, "");
            break;
        }
            
        case VALUE_UNBOXED_DOUBLE: {
            // Call bread_value_set_double
            LLVMValueRef double_args[] = {
                LLVMBuildBitCast(cg->base.builder, boxed, cg->base.i8_ptr, ""),
                val.value
            };
            LLVMBuildCall2(cg->base.builder, cg->base.ty_value_set_double,
                          cg->base.fn_value_set_double, double_args, 2, "");
            break;
        }
            
        case VALUE_UNBOXED_BOOL: {
            // Call bread_value_set_bool
            LLVMValueRef bool_val = LLVMBuildZExt(cg->base.builder, val.value, cg->base.i32, "");
            LLVMValueRef bool_args[] = {
                LLVMBuildBitCast(cg->base.builder, boxed, cg->base.i8_ptr, ""),
                bool_val
            };
            LLVMBuildCall2(cg->base.builder, cg->base.ty_value_set_bool,
                          cg->base.fn_value_set_bool, bool_args, 2, "");
            break;
        }
            
        default:
            break;
    }
    
    return boxed;
}

OptimizedValue unbox_value(OptimizedCg* cg, LLVMValueRef boxed_val, VarType expected_type) {
    if (!cg || !boxed_val) {
        return create_optimized_value(VALUE_BOXED, boxed_val, NULL);
    }
    
    // Call runtime getters based on type
    LLVMValueRef args[] = { LLVMBuildBitCast(cg->base.builder, boxed_val, cg->base.i8_ptr, "") };
    
    switch (expected_type) {
        case TYPE_INT: {
            LLVMValueRef val = LLVMBuildCall2(cg->base.builder, cg->ty_value_get_int, cg->fn_value_get_int, args, 1, "unboxed_int");
            return create_optimized_value(VALUE_UNBOXED_INT, val, cg->unboxed_int);
        }
        case TYPE_DOUBLE: {
            LLVMValueRef val = LLVMBuildCall2(cg->base.builder, cg->ty_value_get_double, cg->fn_value_get_double, args, 1, "unboxed_double");
            return create_optimized_value(VALUE_UNBOXED_DOUBLE, val, cg->unboxed_double);
        }
        case TYPE_BOOL: {
            LLVMValueRef val = LLVMBuildCall2(cg->base.builder, cg->ty_value_get_bool, cg->fn_value_get_bool, args, 1, "unboxed_bool");
            return create_optimized_value(VALUE_UNBOXED_BOOL, val, cg->unboxed_bool);
        }
        default:
            return create_optimized_value(VALUE_BOXED, boxed_val, NULL);
    }
}

LLVMValueRef alloc_stack_value(OptimizedCg* cg, VarType type, const char* name) {
    if (!cg) return NULL;
    
    LLVMTypeRef alloc_type;
    
    switch (type) {
        case TYPE_INT:
            alloc_type = cg->unboxed_int;
            break;
        case TYPE_DOUBLE:
            alloc_type = cg->unboxed_double;
            break;
        case TYPE_BOOL:
            alloc_type = cg->unboxed_bool;
            break;
        default:
            alloc_type = cg->base.value_type; // Fall back to boxed
            break;
    }
    
    LLVMValueRef stack_slot = LLVMBuildAlloca(cg->base.builder, alloc_type, name ? name : "stack_val");
    
    // Track stack allocation
    if (cg->stack_alloc_count < cg->stack_capacity) {
        cg->stack_slots[cg->stack_alloc_count++] = stack_slot;
    }
    
    return stack_slot;
}

void release_stack_value(OptimizedCg* cg, LLVMValueRef stack_val) {
    // Stack values are automatically released when function returns
    // This is mainly for bookkeeping
    (void)cg;
    (void)stack_val;
}

void apply_function_attributes(OptimizedCg* cg, LLVMValueRef function, FunctionOptInfo* info) {
    if (!cg || !function || !info) return;
    
    LLVMContextRef ctx = LLVMGetModuleContext(cg->base.mod);

    // Apply inline hint
    switch (info->inline_hint) {
        case INLINE_ALWAYS:
            LLVMAddAttributeAtIndex(function, LLVMAttributeFunctionIndex, 
                                    LLVMCreateEnumAttribute(ctx, LLVMGetEnumAttributeKindForName("alwaysinline", 12), 0));
            break;
        case INLINE_NEVER:
            LLVMAddAttributeAtIndex(function, LLVMAttributeFunctionIndex, 
                                    LLVMCreateEnumAttribute(ctx, LLVMGetEnumAttributeKindForName("noinline", 8), 0));
            break;
        case INLINE_COLD:
            LLVMAddAttributeAtIndex(function, LLVMAttributeFunctionIndex, 
                                    LLVMCreateEnumAttribute(ctx, LLVMGetEnumAttributeKindForName("cold", 4), 0));
            break;
        default:
            break;
    }
    
    // Apply other attributes based on analysis
    if (info->is_leaf) {
        // No unwind
        LLVMAddAttributeAtIndex(function, LLVMAttributeFunctionIndex, 
                                LLVMCreateEnumAttribute(ctx, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));
    }
    
    if (!info->has_side_effects) {
        // Readonly or Readnone? For now maybe just readonly
        LLVMAddAttributeAtIndex(function, LLVMAttributeFunctionIndex, 
                                LLVMCreateEnumAttribute(ctx, LLVMGetEnumAttributeKindForName("readonly", 8), 0));
    }
}

void apply_branch_hints(OptimizedCg* cg, LLVMValueRef branch, OptimizationHints* hints) {
    if (!cg || !branch || !hints) return;
    
    if (hints->branch_probability > 0) {
        // Add branch weight metadata
        LLVMContextRef ctx = LLVMGetModuleContext(cg->base.mod);
        
        int true_weight = hints->branch_probability;
        int false_weight = 100 - hints->branch_probability;
        
        LLVMValueRef weights[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx), true_weight, 0),
            LLVMConstInt(LLVMInt32TypeInContext(ctx), false_weight, 0)
        };
        
        LLVMValueRef md = LLVMMDNodeInContext(ctx, weights, 2);
        LLVMSetMetadata(branch, LLVMGetMDKindIDInContext(ctx, "prof", 4), md);
    }
}