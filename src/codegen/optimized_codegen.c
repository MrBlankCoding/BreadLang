#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen/optimized_codegen.h"

int optimized_codegen_init(OptimizedCg* cg, LLVMModuleRef mod) {
    if (!cg || !mod) return 0;

    memset(cg, 0, sizeof(*cg));

    cg->base.mod = mod;
    cg->base.builder = LLVMCreateBuilder();

    // core types
    cg->base.i1      = LLVMInt1Type();
    cg->base.i8      = LLVMInt8Type();
    cg->base.i8_ptr  = LLVMPointerType(cg->base.i8, 0);
    cg->base.i32     = LLVMInt32Type();
    cg->base.i64     = LLVMInt64Type();
    cg->base.f64     = LLVMDoubleType();
    cg->base.void_ty = LLVMVoidType();

    // Primitives
    cg->unboxed_int    = cg->base.i64;
    cg->unboxed_double = cg->base.f64;
    cg->unboxed_bool   = cg->base.i1;

    LLVMTypeRef value_ptr = cg->base.i8_ptr;

    // runtime boys
    cg->ty_value_get_int =
        LLVMFunctionType(cg->base.i64, &value_ptr, 1, 0);
    cg->fn_value_get_int =
        cg_declare_fn((Cg*)cg, "bread_value_get_int", cg->ty_value_get_int);

    cg->ty_value_get_double =
        LLVMFunctionType(cg->base.f64, &value_ptr, 1, 0);
    cg->fn_value_get_double =
        cg_declare_fn((Cg*)cg, "bread_value_get_double", cg->ty_value_get_double);

    cg->ty_value_get_bool =
        LLVMFunctionType(cg->base.i32, &value_ptr, 1, 0);
    cg->fn_value_get_bool =
        cg_declare_fn((Cg*)cg, "bread_value_get_bool", cg->ty_value_get_bool);

    cg->ty_value_get_type =
        LLVMFunctionType(cg->base.i32, &value_ptr, 1, 0);
    cg->fn_value_get_type =
        cg_declare_fn((Cg*)cg, "bread_value_get_type", cg->ty_value_get_type);

    // Track stack allocation
    cg->stack_capacity = 64;
    cg->stack_slots = calloc(cg->stack_capacity, sizeof(LLVMValueRef));
    if (!cg->stack_slots) {
        LLVMDisposeBuilder(cg->base.builder);
        return 0;
    }
    cg->stack_alloc_count = 0;
    cg->enable_unboxing    = 1;
    cg->enable_stack_alloc = 1;
    cg->enable_inlining    = 1;

    return 1;
}

void optimized_codegen_cleanup(OptimizedCg* cg) {
    if (!cg) return;

    if (cg->base.builder)
        LLVMDisposeBuilder(cg->base.builder);

    free(cg->stack_slots);
    memset(cg, 0, sizeof(*cg));
}
static OptimizedValue make_val(ValueRepresentation repr,
                              LLVMValueRef value,
                              LLVMTypeRef type) {
    OptimizedValue v;
    v.repr  = repr;
    v.value = value;
    v.type  = type;
    return v;
}

static LLVMValueRef build_truthy(OptimizedCg* cg, OptimizedValue v) {
    if (v.repr == VALUE_UNBOXED_BOOL)
        return v.value;

    LLVMValueRef boxed = (v.repr == VALUE_BOXED) ? v.value : box_value(cg, v);
    LLVMValueRef args[] = {
        LLVMBuildBitCast(cg->base.builder, boxed, cg->base.i8_ptr, "")
    };

    LLVMValueRef truth =
        LLVMBuildCall2(cg->base.builder,
                       cg->base.ty_is_truthy,
                       cg->base.fn_is_truthy,
                       args, 1, "");

    return LLVMBuildICmp(cg->base.builder,
                         LLVMIntNE,
                         truth,
                         LLVMConstInt(cg->base.i32, 0, 0),
                         "truthy");
}

OptimizedValue optimized_build_expr(OptimizedCg* cg,
                                    CgFunction* cg_fn,
                                    ASTExpr* expr) {
    if (!cg || !expr)
        return make_val(VALUE_BOXED, NULL, NULL);

    TypeStabilityInfo* stab = get_expr_stability_info(expr);

    switch (expr->kind) {

    case AST_EXPR_INT:
        if (cg->enable_unboxing &&
            stab && stab->type == TYPE_INT &&
            stab->stability >= STABILITY_CONDITIONAL) {
            return make_val(VALUE_UNBOXED_INT,
                LLVMConstInt(cg->unboxed_int, expr->as.int_val, 0),
                cg->unboxed_int);
        }
        break;

    case AST_EXPR_DOUBLE:
        if (cg->enable_unboxing &&
            stab && stab->type == TYPE_DOUBLE &&
            stab->stability >= STABILITY_CONDITIONAL) {
            return make_val(VALUE_UNBOXED_DOUBLE,
                LLVMConstReal(cg->unboxed_double, expr->as.double_val),
                cg->unboxed_double);
        }
        break;

    case AST_EXPR_BOOL:
        if (cg->enable_unboxing &&
            stab && stab->type == TYPE_BOOL &&
            stab->stability >= STABILITY_CONDITIONAL) {
            return make_val(VALUE_UNBOXED_BOOL,
                LLVMConstInt(cg->unboxed_bool,
                             expr->as.bool_val ? 1 : 0, 0),
                cg->unboxed_bool);
        }
        break;

    case AST_EXPR_BINARY: {
        OptimizedValue lhs =
            optimized_build_expr(cg, cg_fn, expr->as.binary.left);
        OptimizedValue rhs =
            optimized_build_expr(cg, cg_fn, expr->as.binary.right);

        if (lhs.repr == VALUE_UNBOXED_INT &&
            rhs.repr == VALUE_UNBOXED_INT) {

            LLVMValueRef r = NULL;
            switch (expr->as.binary.op) {
            case '+': r = LLVMBuildAdd(cg->base.builder, lhs.value, rhs.value, "add"); break;
            case '-': r = LLVMBuildSub(cg->base.builder, lhs.value, rhs.value, "sub"); break;
            case '*': r = LLVMBuildMul(cg->base.builder, lhs.value, rhs.value, "mul"); break;
            case '/': r = LLVMBuildSDiv(cg->base.builder, lhs.value, rhs.value, "div"); break;
            default: break;
            }

            if (r)
                return make_val(VALUE_UNBOXED_INT, r, cg->unboxed_int);
        }
        break;
    }

    case AST_EXPR_VAR:
        if (cg_fn && stab && stab->stability >= STABILITY_STABLE) {
            CgVar* var =
                cg_scope_find_var(cg_fn->scope, expr->as.var_name);
            if (var)
                return unbox_value(cg, var->alloca, stab->type);
        }
        break;

    default:
        break;
    }

    LLVMValueRef boxed =
        cg_build_expr((Cg*)cg, cg_fn, cg_value_size((Cg*)cg), expr);

    return make_val(VALUE_BOXED, boxed, cg->base.value_type);
}

int optimized_build_stmt(OptimizedCg* cg,
                         CgFunction* cg_fn,
                         ASTStmt* stmt) {
    if (!cg || !stmt) return 0;

    switch (stmt->kind) {

    case AST_STMT_VAR_DECL: {
        if (!stmt->as.var_decl.init)
            break;

        OptimizedValue init =
            optimized_build_expr(cg, cg_fn, stmt->as.var_decl.init);

        EscapeInfo* esc =
            get_escape_info(stmt->as.var_decl.init);

        if (cg->enable_stack_alloc && esc && esc->can_stack_allocate) {
            LLVMValueRef slot =
                alloc_stack_value(cg,
                                  stmt->as.var_decl.type,
                                  stmt->as.var_decl.var_name);

            LLVMValueRef to_store =
                (init.repr == VALUE_BOXED)
                    ? init.value
                    : box_value(cg, init);

            LLVMBuildStore(cg->base.builder, to_store, slot);

            if (cg_fn)
                cg_scope_add_var(cg_fn->scope,
                                 stmt->as.var_decl.var_name,
                                 slot);
            return 1;
        }
        break;
    }

    case AST_STMT_IF: {
        OptimizedValue cond =
            optimized_build_expr(cg, cg_fn,
                                 stmt->as.if_stmt.condition);

        LLVMValueRef cond_i1 = build_truthy(cg, cond);

        LLVMValueRef fn =
            LLVMGetBasicBlockParent(
                LLVMGetInsertBlock(cg->base.builder));

        LLVMBasicBlockRef then_bb =
            LLVMAppendBasicBlock(fn, "if.then");
        LLVMBasicBlockRef else_bb =
            LLVMAppendBasicBlock(fn, "if.else");
        LLVMBasicBlockRef merge_bb =
            LLVMAppendBasicBlock(fn, "if.end");

        LLVMBuildCondBr(cg->base.builder,
                        cond_i1, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(cg->base.builder, then_bb);
        for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next)
            optimized_build_stmt(cg, cg_fn, s);
        if (!LLVMGetBasicBlockTerminator(then_bb))
            LLVMBuildBr(cg->base.builder, merge_bb);

        LLVMPositionBuilderAtEnd(cg->base.builder, else_bb);
        if (stmt->as.if_stmt.else_branch) {
            for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next)
                optimized_build_stmt(cg, cg_fn, s);
        }
        if (!LLVMGetBasicBlockTerminator(else_bb))
            LLVMBuildBr(cg->base.builder, merge_bb);

        LLVMPositionBuilderAtEnd(cg->base.builder, merge_bb);
        return 1;
    }

    default:
        break;
    }

    /* Fallback */
    return cg_build_stmt((Cg*)cg, cg_fn,
                         cg_value_size((Cg*)cg), stmt);
}

LLVMValueRef box_value(OptimizedCg* cg, OptimizedValue v) {
    if (!cg || v.repr == VALUE_BOXED)
        return v.value;

    LLVMValueRef boxed =
        LLVMBuildAlloca(cg->base.builder,
                        cg->base.value_type,
                        "boxed");

    LLVMValueRef dst =
        LLVMBuildBitCast(cg->base.builder,
                         boxed, cg->base.i8_ptr, "");

    switch (v.repr) {
    case VALUE_UNBOXED_INT:
        LLVMBuildCall2(cg->base.builder,
            cg->base.ty_value_set_int,
            cg->base.fn_value_set_int,
            (LLVMValueRef[]){ dst, v.value }, 2, "");
        break;

    case VALUE_UNBOXED_DOUBLE:
        LLVMBuildCall2(cg->base.builder,
            cg->base.ty_value_set_double,
            cg->base.fn_value_set_double,
            (LLVMValueRef[]){ dst, v.value }, 2, "");
        break;

    case VALUE_UNBOXED_BOOL: {
        LLVMValueRef b =
            LLVMBuildZExt(cg->base.builder,
                          v.value, cg->base.i32, "");
        LLVMBuildCall2(cg->base.builder,
            cg->base.ty_value_set_bool,
            cg->base.fn_value_set_bool,
            (LLVMValueRef[]){ dst, b }, 2, "");
        break;
    }

    default:
        break;
    }

    return boxed;
}

OptimizedValue unbox_value(OptimizedCg* cg,
                           LLVMValueRef boxed,
                           VarType type) {
    LLVMValueRef arg[] = {
        LLVMBuildBitCast(cg->base.builder,
                         boxed, cg->base.i8_ptr, "")
    };

    switch (type) {
    case TYPE_INT:
        return make_val(VALUE_UNBOXED_INT,
            LLVMBuildCall2(cg->base.builder,
                cg->ty_value_get_int,
                cg->fn_value_get_int, arg, 1, ""),
            cg->unboxed_int);

    case TYPE_DOUBLE:
        return make_val(VALUE_UNBOXED_DOUBLE,
            LLVMBuildCall2(cg->base.builder,
                cg->ty_value_get_double,
                cg->fn_value_get_double, arg, 1, ""),
            cg->unboxed_double);

    case TYPE_BOOL:
        return make_val(VALUE_UNBOXED_BOOL,
            LLVMBuildCall2(cg->base.builder,
                cg->ty_value_get_bool,
                cg->fn_value_get_bool, arg, 1, ""),
            cg->unboxed_bool);

    default:
        return make_val(VALUE_BOXED, boxed, NULL);
    }
}

LLVMValueRef alloc_stack_value(OptimizedCg* cg,
                               VarType type,
                               const char* name) {
    LLVMTypeRef ty =
        (type == TYPE_INT)    ? cg->unboxed_int :
        (type == TYPE_DOUBLE) ? cg->unboxed_double :
        (type == TYPE_BOOL)   ? cg->unboxed_bool :
                                cg->base.value_type;

    LLVMValueRef slot =
        LLVMBuildAlloca(cg->base.builder,
                        ty, name ? name : "stack");

    if (cg->stack_alloc_count < cg->stack_capacity)
        cg->stack_slots[cg->stack_alloc_count++] = slot;

    return slot;
}
