#include "codegen_internal.h"

static inline LLVMValueRef cg_ensure_boxed(Cg* cg, CgValue v) {
    return v.type == CG_VALUE_BOXED ? v.value : cg_box_value(cg, v);
}

CgValue cg_create_value(CgValueType type, LLVMValueRef value, LLVMTypeRef llvm_type) {
    CgValue result;
    result.type = type;
    result.value = value;
    result.llvm_type = llvm_type;
    return result;
}

CgValue cg_unbox_value(Cg* cg, LLVMValueRef boxed_val, VarType expected_type) {
    if (!cg || !boxed_val) {
        return cg_create_value(CG_VALUE_BOXED, NULL, NULL);
    }

    LLVMValueRef boxed_ptr = cg_value_to_i8_ptr(cg, boxed_val);

    switch (expected_type) {
        case TYPE_INT: {
            LLVMValueRef v = LLVMBuildCall2(cg->builder, cg->ty_value_get_int, cg->fn_value_get_int,
                                            (LLVMValueRef[]){boxed_ptr}, 1, "unbox_int");
            return cg_create_value(CG_VALUE_UNBOXED_INT, v, cg->i64);
        }
        case TYPE_DOUBLE: {
            LLVMValueRef v = LLVMBuildCall2(cg->builder, cg->ty_value_get_double, cg->fn_value_get_double,
                                            (LLVMValueRef[]){boxed_ptr}, 1, "unbox_double");
            return cg_create_value(CG_VALUE_UNBOXED_DOUBLE, v, cg->f64);
        }
        case TYPE_BOOL: {
            LLVMValueRef v32 = LLVMBuildCall2(cg->builder, cg->ty_value_get_bool, cg->fn_value_get_bool,
                                              (LLVMValueRef[]){boxed_ptr}, 1, "unbox_bool");
            LLVMValueRef v1 = LLVMBuildICmp(cg->builder, LLVMIntNE, v32, LLVMConstInt(cg->i32, 0, 0), "unbox_bool_i1");
            return cg_create_value(CG_VALUE_UNBOXED_BOOL, v1, cg->i1);
        }
        default:
            break;
    }

    // Unknown / non-primitive expected type; keep boxed.
    return cg_create_value(CG_VALUE_BOXED, boxed_val, cg->value_type);
}

int cg_can_unbox_expr(Cg* cg, ASTExpr* expr) {
    (void)cg;
    if (!expr) return 0;

    switch (expr->kind) {
        case AST_EXPR_INT:
        case AST_EXPR_BOOL:
        case AST_EXPR_DOUBLE:
            return 1;

        case AST_EXPR_VAR: {
            // Only unbox vars that are SPECIFICLLY UNBOXED
            return 1;
        }

        case AST_EXPR_BINARY:
            return cg_can_unbox_expr(cg, expr->as.binary.left) &&
                   cg_can_unbox_expr(cg, expr->as.binary.right);

        case AST_EXPR_UNARY:
            return cg_can_unbox_expr(cg, expr->as.unary.operand);

        default:
            return 0;
    }
}

CgValue cg_build_expr_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* expr) {
    if (!cg || !expr) {
        return cg_create_value(CG_VALUE_BOXED, NULL, NULL);
    }

    if (!cg_can_unbox_expr(cg, expr)) {
        LLVMValueRef boxed = cg_build_expr(cg, cg_fn, cg_value_size(cg), expr);
        return cg_create_value(CG_VALUE_BOXED, boxed, cg->value_type);
    }

    switch (expr->kind) {
        case AST_EXPR_INT:
            return cg_create_value(
                CG_VALUE_UNBOXED_INT,
                LLVMConstInt(cg->i64, expr->as.int_val, 0),
                cg->i64
            );

        case AST_EXPR_DOUBLE:
            return cg_create_value(
                CG_VALUE_UNBOXED_DOUBLE,
                LLVMConstReal(cg->f64, expr->as.double_val),
                cg->f64
            );

        case AST_EXPR_BOOL:
            return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMConstInt(cg->i1, expr->as.bool_val ? 1 : 0, 0),
                cg->i1
            );

        case AST_EXPR_VAR: {
            if (cg_fn) {
                CgVar* var = cg_scope_find_var(cg_fn->scope, expr->as.var_name);
                if (var && var->unboxed_type != UNBOXED_NONE) {
                    LLVMTypeRef ty = NULL;
                    CgValueType vt = CG_VALUE_BOXED;

                    switch (var->unboxed_type) {
                        case UNBOXED_INT:
                            ty = cg->i64; vt = CG_VALUE_UNBOXED_INT; break;
                        case UNBOXED_DOUBLE:
                            ty = cg->f64; vt = CG_VALUE_UNBOXED_DOUBLE; break;
                        case UNBOXED_BOOL:
                            ty = cg->i1; vt = CG_VALUE_UNBOXED_BOOL; break;
                        default:
                            break;
                    }

                    if (ty) {
                        LLVMValueRef load =
                            LLVMBuildLoad2(cg->builder, ty, var->alloca, var->name);
                        return cg_create_value(vt, load, ty);
                    }
                }
            }

            LLVMValueRef boxed = cg_build_expr(cg, cg_fn, cg_value_size(cg), expr);
            return cg_create_value(CG_VALUE_BOXED, boxed, cg->value_type);
        }

        case AST_EXPR_BINARY:
            return cg_build_binary_unboxed(
                cg, cg_fn,
                expr->as.binary.left,
                expr->as.binary.right,
                expr->as.binary.op
            );

        case AST_EXPR_UNARY:
            return cg_build_unary_unboxed(
                cg, cg_fn,
                expr->as.unary.operand,
                expr->as.unary.op
            );

        default: {
            LLVMValueRef boxed = cg_build_expr(cg, cg_fn, cg_value_size(cg), expr);
            return cg_create_value(CG_VALUE_BOXED, boxed, cg->value_type);
        }
    }
}
// the "fun part"
LLVMValueRef cg_box_value(Cg* cg, CgValue val) {
    if (val.type == CG_VALUE_BOXED)
        return val.value;

    LLVMValueRef boxed = cg_alloc_value(cg, "boxed");

    switch (val.type) {
        case CG_VALUE_UNBOXED_INT: {
            LLVMValueRef args[] = { cg_value_to_i8_ptr(cg, boxed), val.value };
            LLVMBuildCall2(cg->builder, cg->ty_value_set_int,
                           cg->fn_value_set_int, args, 2, "");
            break;
        }
        case CG_VALUE_UNBOXED_DOUBLE: {
            LLVMValueRef args[] = { cg_value_to_i8_ptr(cg, boxed), val.value };
            LLVMBuildCall2(cg->builder, cg->ty_value_set_double,
                           cg->fn_value_set_double, args, 2, "");
            break;
        }
        case CG_VALUE_UNBOXED_BOOL: {
            LLVMValueRef b32 = LLVMBuildZExt(cg->builder, val.value, cg->i32, "");
            LLVMValueRef args[] = { cg_value_to_i8_ptr(cg, boxed), b32 };
            LLVMBuildCall2(cg->builder, cg->ty_value_set_bool,
                           cg->fn_value_set_bool, args, 2, "");
            break;
        }
        default:
            break;
    }

    return boxed;
}

CgValue cg_build_binary_unboxed(
    Cg* cg,
    CgFunction* cg_fn,
    ASTExpr* left,
    ASTExpr* right,
    char op
) {
    CgValue l = cg_build_expr_unboxed(cg, cg_fn, left);
    CgValue r = cg_build_expr_unboxed(cg, cg_fn, right);

    // int ops
    if (l.type == CG_VALUE_UNBOXED_INT && r.type == CG_VALUE_UNBOXED_INT) {
        LLVMValueRef v = NULL;
        switch (op) {
            case '+': v = LLVMBuildAdd(cg->builder, l.value, r.value, "add"); break;
            case '-': v = LLVMBuildSub(cg->builder, l.value, r.value, "sub"); break;
            case '*': v = LLVMBuildMul(cg->builder, l.value, r.value, "mul"); break;
            case '/': v = LLVMBuildSDiv(cg->builder, l.value, r.value, "div"); break;
            case '%': v = LLVMBuildSRem(cg->builder, l.value, r.value, "mod"); break;
            case '=': return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMBuildICmp(cg->builder, LLVMIntEQ, l.value, r.value, "eq"),
                cg->i1
            );
            case '<': return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMBuildICmp(cg->builder, LLVMIntSLT, l.value, r.value, "lt"),
                cg->i1
            );
            case '>': return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMBuildICmp(cg->builder, LLVMIntSGT, l.value, r.value, "gt"),
                cg->i1
            );
        }
        if (v) return cg_create_value(CG_VALUE_UNBOXED_INT, v, cg->i64);
    }

    // double ops
    if (l.type == CG_VALUE_UNBOXED_DOUBLE && r.type == CG_VALUE_UNBOXED_DOUBLE) {
        LLVMValueRef v = NULL;
        switch (op) {
            case '+': v = LLVMBuildFAdd(cg->builder, l.value, r.value, "fadd"); break;
            case '-': v = LLVMBuildFSub(cg->builder, l.value, r.value, "fsub"); break;
            case '*': v = LLVMBuildFMul(cg->builder, l.value, r.value, "fmul"); break;
            case '/': v = LLVMBuildFDiv(cg->builder, l.value, r.value, "fdiv"); break;
            case '=': return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMBuildFCmp(cg->builder, LLVMRealOEQ, l.value, r.value, "feq"),
                cg->i1
            );
            case '<': return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMBuildFCmp(cg->builder, LLVMRealOLT, l.value, r.value, "flt"),
                cg->i1
            );
            case '>': return cg_create_value(
                CG_VALUE_UNBOXED_BOOL,
                LLVMBuildFCmp(cg->builder, LLVMRealOGT, l.value, r.value, "fgt"),
                cg->i1
            );
        }
        if (v) return cg_create_value(CG_VALUE_UNBOXED_DOUBLE, v, cg->f64);
    }

    // fucking fallbacks. 
    LLVMValueRef lb = cg_ensure_boxed(cg, l);
    LLVMValueRef rb = cg_ensure_boxed(cg, r);
    LLVMValueRef out = cg_alloc_value(cg, "bin_result");

    LLVMValueRef args[] = {
        LLVMConstInt(cg->i8, op, 0),
        cg_value_to_i8_ptr(cg, lb),
        cg_value_to_i8_ptr(cg, rb),
        cg_value_to_i8_ptr(cg, out)
    };

    LLVMBuildCall2(cg->builder, cg->ty_binary_op,
                   cg->fn_binary_op, args, 4, "");

    return cg_create_value(CG_VALUE_BOXED, out, cg->value_type);
}

CgValue cg_build_unary_unboxed(
    Cg* cg,
    CgFunction* cg_fn,
    ASTExpr* operand,
    char op
) {
    CgValue v = cg_build_expr_unboxed(cg, cg_fn, operand);

    if (op == '-') {
        if (v.type == CG_VALUE_UNBOXED_INT)
            return cg_create_value(
                CG_VALUE_UNBOXED_INT,
                LLVMBuildSub(cg->builder,
                             LLVMConstInt(cg->i64, 0, 0),
                             v.value, "neg"),
                cg->i32
            );

        if (v.type == CG_VALUE_UNBOXED_DOUBLE)
            return cg_create_value(
                CG_VALUE_UNBOXED_DOUBLE,
                LLVMBuildFNeg(cg->builder, v.value, "fneg"),
                cg->f64
            );
    }

    if (op == '!' && v.type == CG_VALUE_UNBOXED_BOOL)
        return cg_create_value(
            CG_VALUE_UNBOXED_BOOL,
            LLVMBuildNot(cg->builder, v.value, "not"),
            cg->i1
        );

    // ... no comment.
    LLVMValueRef boxed = cg_ensure_boxed(cg, v);
    LLVMValueRef out = cg_alloc_value(cg, "unary_result");

    if (op == '!') {
        LLVMValueRef args[] = {
            cg_value_to_i8_ptr(cg, boxed),
            cg_value_to_i8_ptr(cg, out)
        };
        LLVMBuildCall2(cg->builder, cg->ty_unary_not,
                       cg->fn_unary_not, args, 2, "");
    }

    return cg_create_value(CG_VALUE_BOXED, out, cg->value_type);
}
