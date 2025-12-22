#include "codegen_internal.h"

CgValue cg_build_expr_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* expr) {
    if (!cg || !expr) {
        return cg_create_value(CG_VALUE_BOXED, NULL, NULL);
    }
    
    // should we unbox the exp
    if (!cg_can_unbox_expr(cg, expr)) {
        // Fall back to boxed
        LLVMValueRef boxed = cg_build_expr(cg, cg_fn, cg_value_size(cg), expr);
        return cg_create_value(CG_VALUE_BOXED, boxed, cg->value_type);
    }
    
    switch (expr->kind) {
        case AST_EXPR_INT: {
            LLVMValueRef int_val = LLVMConstInt(cg->i32, expr->as.int_val, 0);
            return cg_create_value(CG_VALUE_UNBOXED_INT, int_val, cg->i32);
        }
        
        case AST_EXPR_DOUBLE: {
            LLVMValueRef double_val = LLVMConstReal(cg->f64, expr->as.double_val);
            return cg_create_value(CG_VALUE_UNBOXED_DOUBLE, double_val, cg->f64);
        }
        
        case AST_EXPR_BOOL: {
            LLVMValueRef bool_val = LLVMConstInt(cg->i1, expr->as.bool_val ? 1 : 0, 0);
            return cg_create_value(CG_VALUE_UNBOXED_BOOL, bool_val, cg->i1);
        }
        
        case AST_EXPR_VAR: {
            if (cg_fn) {
                CgVar* var = cg_scope_find_var(cg_fn->scope, expr->as.var_name);
                if (var && var->unboxed_type != UNBOXED_NONE) {
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
                            load_type = cg->value_type;
                            result_type = CG_VALUE_BOXED;
                            break;
                    }
                    
                    LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, load_type, var->alloca, var->name);
                    return cg_create_value(result_type, loaded, load_type);
                }
            }
            
            LLVMValueRef boxed = cg_build_expr(cg, cg_fn, cg_value_size(cg), expr);
            if (!boxed) return cg_create_value(CG_VALUE_BOXED, NULL, NULL);
            
            // Try to determine the type and unbox
            // For now, assume int - in a full implementation, we'd use type analysis
            return cg_unbox_value(cg, boxed, TYPE_INT);
        }
        
        case AST_EXPR_BINARY: {
            return cg_build_binary_unboxed(cg, cg_fn, expr->as.binary.left, expr->as.binary.right, expr->as.binary.op);
        }
        
        case AST_EXPR_UNARY: {
            return cg_build_unary_unboxed(cg, cg_fn, expr->as.unary.operand, expr->as.unary.op);
        }
        
        default: {
            LLVMValueRef boxed = cg_build_expr(cg, cg_fn, cg_value_size(cg), expr);
            return cg_create_value(CG_VALUE_BOXED, boxed, cg->value_type);
        }
    }
}

CgValue cg_create_value(CgValueType type, LLVMValueRef value, LLVMTypeRef llvm_type) {
    CgValue result;
    result.type = type;
    result.value = value;
    result.llvm_type = llvm_type;
    return result;
}

int cg_can_unbox_expr(Cg* cg, ASTExpr* expr) {
    (void)cg;
    if (!expr) return 0;
    
    // Simple heuristics for common cases (disable type stability analysis for now)
    switch (expr->kind) {
        case AST_EXPR_INT:
        case AST_EXPR_BOOL:
        case AST_EXPR_DOUBLE:
            return 1;
        case AST_EXPR_BINARY:
            if (expr->as.binary.op == '+' || expr->as.binary.op == '-' || 
                expr->as.binary.op == '*' || expr->as.binary.op == '/' ||
                expr->as.binary.op == '%' || expr->as.binary.op == '=' ||
                expr->as.binary.op == '<' || expr->as.binary.op == '>') {
                return cg_can_unbox_expr(cg, expr->as.binary.left) && 
                       cg_can_unbox_expr(cg, expr->as.binary.right);
            }
            return 0;
        case AST_EXPR_UNARY:
            if (expr->as.unary.op == '-' || expr->as.unary.op == '!') {
                return cg_can_unbox_expr(cg, expr->as.unary.operand);
            }
            return 0;
        case AST_EXPR_VAR:
            // Arrays, dicts, and other complex types cannot be unboxed
            // For now, assume variables can be unboxed only for primitive types
            // This would need proper type analysis in a full implementation
            return 0;  // Disable variable unboxing for now to ensure proper equality
        default:
            return 0;
    }
}

LLVMValueRef cg_box_value(Cg* cg, CgValue val) {
    if (val.type == CG_VALUE_BOXED) {
        return val.value;
    }
    
    LLVMValueRef boxed = cg_alloc_value(cg, "boxed");
    
    switch (val.type) {
        case CG_VALUE_UNBOXED_INT: {
            LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, boxed), val.value};
            LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, args, 2, "");
            break;
        }
        case CG_VALUE_UNBOXED_DOUBLE: {
            LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, boxed), val.value};
            LLVMBuildCall2(cg->builder, cg->ty_value_set_double, cg->fn_value_set_double, args, 2, "");
            break;
        }
        case CG_VALUE_UNBOXED_BOOL: {
            // Convert i1 to i32 for the runtime function
            LLVMValueRef bool_i32 = LLVMBuildZExt(cg->builder, val.value, cg->i32, "bool_i32");
            LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, boxed), bool_i32};
            LLVMBuildCall2(cg->builder, cg->ty_value_set_bool, cg->fn_value_set_bool, args, 2, "");
            break;
        }
        default:
            break;
    }
    
    return boxed;
}

CgValue cg_unbox_value(Cg* cg, LLVMValueRef boxed_val, VarType expected_type) {
    if (!boxed_val) {
        return cg_create_value(CG_VALUE_BOXED, boxed_val, NULL);
    }
    
    LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, boxed_val)};
    
    switch (expected_type) {
        case TYPE_INT: {
            LLVMValueRef val = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int, args, 1, "unboxed_int");
            return cg_create_value(CG_VALUE_UNBOXED_INT, val, cg->i32);
        }
        case TYPE_DOUBLE: {
            LLVMValueRef val = LLVMBuildCall2(cg->builder, cg->ty_value_get_double, cg->fn_value_get_double, args, 1, "unboxed_double");
            return cg_create_value(CG_VALUE_UNBOXED_DOUBLE, val, cg->f64);
        }
        case TYPE_BOOL: {
            LLVMValueRef val = LLVMBuildCall2(cg->builder, cg->ty_value_get_bool, cg->fn_value_get_bool, args, 1, "unboxed_bool_i32");
            // Convert i32 to i1
            LLVMValueRef bool_i1 = LLVMBuildICmp(cg->builder, LLVMIntNE, val, LLVMConstInt(cg->i32, 0, 0), "unboxed_bool");
            return cg_create_value(CG_VALUE_UNBOXED_BOOL, bool_i1, cg->i1);
        }
        default:
            return cg_create_value(CG_VALUE_BOXED, boxed_val, NULL);
    }
}

CgValue cg_build_binary_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* left, ASTExpr* right, char op) {
    CgValue left_val = cg_build_expr_unboxed(cg, cg_fn, left);
    CgValue right_val = cg_build_expr_unboxed(cg, cg_fn, right);
    
    // If both are unboxed integers, use native arithmetic
    if (left_val.type == CG_VALUE_UNBOXED_INT && right_val.type == CG_VALUE_UNBOXED_INT) {
        LLVMValueRef result = NULL;
        switch (op) {
            case '+':
                result = LLVMBuildAdd(cg->builder, left_val.value, right_val.value, "add");
                break;
            case '-':
                result = LLVMBuildSub(cg->builder, left_val.value, right_val.value, "sub");
                break;
            case '*':
                result = LLVMBuildMul(cg->builder, left_val.value, right_val.value, "mul");
                break;
            case '/':
                result = LLVMBuildSDiv(cg->builder, left_val.value, right_val.value, "div");
                break;
            case '%':
                result = LLVMBuildSRem(cg->builder, left_val.value, right_val.value, "mod");
                break;
            case '=': // ==
                result = LLVMBuildICmp(cg->builder, LLVMIntEQ, left_val.value, right_val.value, "eq");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
            case '<':
                result = LLVMBuildICmp(cg->builder, LLVMIntSLT, left_val.value, right_val.value, "lt");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
            case '>':
                result = LLVMBuildICmp(cg->builder, LLVMIntSGT, left_val.value, right_val.value, "gt");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
        }
        if (result) {
            return cg_create_value(CG_VALUE_UNBOXED_INT, result, cg->i32);
        }
    }
    
    // If both are unboxed doubles, use native floating point arithmetic
    if (left_val.type == CG_VALUE_UNBOXED_DOUBLE && right_val.type == CG_VALUE_UNBOXED_DOUBLE) {
        LLVMValueRef result = NULL;
        switch (op) {
            case '+':
                result = LLVMBuildFAdd(cg->builder, left_val.value, right_val.value, "fadd");
                break;
            case '-':
                result = LLVMBuildFSub(cg->builder, left_val.value, right_val.value, "fsub");
                break;
            case '*':
                result = LLVMBuildFMul(cg->builder, left_val.value, right_val.value, "fmul");
                break;
            case '/':
                result = LLVMBuildFDiv(cg->builder, left_val.value, right_val.value, "fdiv");
                break;
            case '=': // ==
                result = LLVMBuildFCmp(cg->builder, LLVMRealOEQ, left_val.value, right_val.value, "feq");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
            case '<':
                result = LLVMBuildFCmp(cg->builder, LLVMRealOLT, left_val.value, right_val.value, "flt");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
            case '>':
                result = LLVMBuildFCmp(cg->builder, LLVMRealOGT, left_val.value, right_val.value, "fgt");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
        }
        if (result) {
            return cg_create_value(CG_VALUE_UNBOXED_DOUBLE, result, cg->f64);
        }
    }
    
    // If both are unboxed bools, use native boolean operations
    if (left_val.type == CG_VALUE_UNBOXED_BOOL && right_val.type == CG_VALUE_UNBOXED_BOOL) {
        LLVMValueRef result = NULL;
        switch (op) {
            case '&': // &&
                result = LLVMBuildAnd(cg->builder, left_val.value, right_val.value, "and");
                break;
            case '|': // ||
                result = LLVMBuildOr(cg->builder, left_val.value, right_val.value, "or");
                break;
            case '=': // ==
                result = LLVMBuildICmp(cg->builder, LLVMIntEQ, left_val.value, right_val.value, "beq");
                break;
        }
        if (result) {
            return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
        }
    }
    
    // fall back
    LLVMValueRef left_boxed = cg_box_value(cg, left_val);
    LLVMValueRef right_boxed = cg_box_value(cg, right_val);
    LLVMValueRef result_boxed = cg_alloc_value(cg, "binary_result");
    
    LLVMValueRef op_val = LLVMConstInt(cg->i8, op, 0);
    LLVMValueRef args[] = {
        op_val,
        cg_value_to_i8_ptr(cg, left_boxed),
        cg_value_to_i8_ptr(cg, right_boxed),
        cg_value_to_i8_ptr(cg, result_boxed)
    };
    LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
    
    return cg_create_value(CG_VALUE_BOXED, result_boxed, cg->value_type);
}

CgValue cg_build_unary_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* operand, char op) {
    CgValue operand_val = cg_build_expr_unboxed(cg, cg_fn, operand);
    
    switch (op) {
        case '-':
            if (operand_val.type == CG_VALUE_UNBOXED_INT) {
                LLVMValueRef zero = LLVMConstInt(cg->i32, 0, 0);
                LLVMValueRef result = LLVMBuildSub(cg->builder, zero, operand_val.value, "neg");
                return cg_create_value(CG_VALUE_UNBOXED_INT, result, cg->i32);
            }
            if (operand_val.type == CG_VALUE_UNBOXED_DOUBLE) {
                LLVMValueRef result = LLVMBuildFNeg(cg->builder, operand_val.value, "fneg");
                return cg_create_value(CG_VALUE_UNBOXED_DOUBLE, result, cg->f64);
            }
            break;
        case '!':
            if (operand_val.type == CG_VALUE_UNBOXED_BOOL) {
                LLVMValueRef result = LLVMBuildNot(cg->builder, operand_val.value, "not");
                return cg_create_value(CG_VALUE_UNBOXED_BOOL, result, cg->i1);
            }
            break;
    }
    
    // Fall back trooops!!!!!!!
    LLVMValueRef operand_boxed = cg_box_value(cg, operand_val);
    LLVMValueRef result_boxed = cg_alloc_value(cg, "unary_result");
    
    if (op == '!') {
        LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, operand_boxed), cg_value_to_i8_ptr(cg, result_boxed)};
        LLVMBuildCall2(cg->builder, cg->ty_unary_not, cg->fn_unary_not, args, 2, "");
    } else if (op == '-') {
        // Implement unary minus as (0 - operand)
        LLVMValueRef zero = cg_alloc_value(cg, "zero");
        LLVMValueRef zero_i = LLVMConstInt(cg->i32, 0, 0);
        LLVMValueRef zargs[] = {cg_value_to_i8_ptr(cg, zero), zero_i};
        LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, zargs, 2, "");
        
        LLVMValueRef op_val = LLVMConstInt(cg->i8, '-', 0);
        LLVMValueRef args[] = {
            op_val,
            cg_value_to_i8_ptr(cg, zero),
            cg_value_to_i8_ptr(cg, operand_boxed),
            cg_value_to_i8_ptr(cg, result_boxed)
        };
        LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
    }
    
    return cg_create_value(CG_VALUE_BOXED, result_boxed, cg->value_type);
}
