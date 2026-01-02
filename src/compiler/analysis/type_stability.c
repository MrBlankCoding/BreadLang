#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/analysis/type_stability.h"

#define MAX_TRACKED_VARS 256
#define INITIAL_EXPR_CAPACITY 64

typedef struct {
    char* name;
    int   depth;
    int   is_const;
    int   is_local;
    int   mutation_count;
    int   usage_count;
    int   declared_in_loop;
} TrackedVar;

typedef struct {
    TrackedVar vars[MAX_TRACKED_VARS];
    int        count;
    int        depth;
} VarTracker;

static TypeStabilityCtx* g_ctx = NULL;
static VarTracker g_vars;
static TrackedVar* find_var(const char* name) {
    if (!name) return NULL;

    for (int i = g_vars.count - 1; i >= 0; i--) {
        if (g_vars.vars[i].name &&
            strcmp(g_vars.vars[i].name, name) == 0) {
            return &g_vars.vars[i];
        }
    }
    return NULL;
}

static void declare_var(const char* name, int is_const, int is_local) {
    if (!name || g_vars.count >= MAX_TRACKED_VARS) return;

    TrackedVar* v = &g_vars.vars[g_vars.count++];
    memset(v, 0, sizeof(*v));

    v->name = strdup(name);
    v->depth = g_vars.depth;
    v->is_const = is_const;
    v->is_local = is_local;
    v->declared_in_loop = (g_ctx && g_ctx->in_loop > 0);
}

static void mark_mutation(const char* name) {
    TrackedVar* v = find_var(name);
    if (v) v->mutation_count++;
}

static void mark_usage(const char* name) {
    TrackedVar* v = find_var(name);
    if (v) v->usage_count++;
}

static void enter_scope(void) {
    g_vars.depth++;
}

static void leave_scope(void) {
    while (g_vars.count > 0) {
        TrackedVar* v = &g_vars.vars[g_vars.count - 1];
        if (v->depth < g_vars.depth) break;

        free(v->name);
        memset(v, 0, sizeof(*v));
        g_vars.count--;
    }

    if (g_vars.depth > 0)
        g_vars.depth--;
}

static void reset_var_tracker(void) {
    for (int i = 0; i < g_vars.count; i++) {
        free(g_vars.vars[i].name);
    }
    memset(&g_vars, 0, sizeof(g_vars));
}

static TypeStabilityInfo* alloc_info(void) {
    if (!g_ctx) return NULL;

    if (g_ctx->expr_count >= g_ctx->expr_capacity) {
        int new_cap = g_ctx->expr_capacity * 2;
        if (new_cap < INITIAL_EXPR_CAPACITY)
            new_cap = INITIAL_EXPR_CAPACITY;

        TypeStabilityInfo** resized =
            realloc(g_ctx->expr_info, new_cap * sizeof(TypeStabilityInfo*));

        if (!resized) return NULL;

        g_ctx->expr_info = resized;
        g_ctx->expr_capacity = new_cap;
    }

    TypeStabilityInfo* info = calloc(1, sizeof(TypeStabilityInfo));
    if (!info) return NULL;
    g_ctx->expr_info[g_ctx->expr_count++] = info;

    info->type = TYPE_NIL;
    info->stability = STABILITY_UNKNOWN;
    return info;
}

static void analyze_expr(ASTExpr* expr);

static void analyze_binary(ASTExpr* expr) {
    analyze_expr(expr->as.binary.left);
    analyze_expr(expr->as.binary.right);

    TypeStabilityInfo* l = expr->as.binary.left->stability_info;
    TypeStabilityInfo* r = expr->as.binary.right->stability_info;
    TypeStabilityInfo* info = expr->stability_info;

    if (!l || !r) return;

    if (l->stability == STABILITY_STABLE &&
        r->stability == STABILITY_STABLE) {
        info->stability = STABILITY_STABLE;
        info->is_constant = l->is_constant && r->is_constant;
    } else if (l->stability >= STABILITY_CONDITIONAL &&
               r->stability >= STABILITY_CONDITIONAL) {
        info->stability = STABILITY_CONDITIONAL;
    } else {
        info->stability = STABILITY_UNSTABLE;
    }

    if (l->type == TYPE_INT && r->type == TYPE_INT) {
        info->type = TYPE_INT;
    } else if ((l->type == TYPE_DOUBLE || l->type == TYPE_FLOAT) &&
               (r->type == TYPE_DOUBLE || r->type == TYPE_FLOAT)) {
        info->type = TYPE_DOUBLE;
    }
}

static void analyze_expr(ASTExpr* expr) {
    if (!expr) return;

    TypeStabilityInfo* info = alloc_info();
    if (!info) return;
    expr->stability_info = info;

    switch (expr->kind) {

    case AST_EXPR_NIL:
    case AST_EXPR_BOOL:
    case AST_EXPR_INT:
    case AST_EXPR_DOUBLE:
    case AST_EXPR_STRING:
    case AST_EXPR_STRING_LITERAL:
        info->stability = STABILITY_STABLE;
        info->is_constant = 1;
        if (expr->tag.is_known)
            info->type = expr->tag.type;
        break;

    case AST_EXPR_VAR: {
        info->stability = STABILITY_UNSTABLE;
        info->is_constant = 0;

        mark_usage(expr->as.var_name);
        TrackedVar* v = find_var(expr->as.var_name);

        if (v) {
            info->is_local = v->is_local;
            info->mutation_count = v->mutation_count;
            info->usage_count = v->usage_count;

            if (v->is_const)
                info->stability = STABILITY_STABLE;
            else if (v->mutation_count == 0 && v->is_local)
                info->stability = STABILITY_STABLE;
            else if (v->mutation_count <= 1 &&
                     !v->declared_in_loop &&
                     g_ctx->in_loop == 0)
                info->stability = STABILITY_CONDITIONAL;
        }

        if (expr->tag.is_known) {
            info->type = expr->tag.type;
            if (info->stability == STABILITY_UNSTABLE)
                info->stability = STABILITY_CONDITIONAL;
        }
        break;
    }

    case AST_EXPR_UNARY:
        analyze_expr(expr->as.unary.operand);
        if (expr->as.unary.operand->stability_info)
            *info = *(TypeStabilityInfo*)expr->as.unary.operand->stability_info;
        break;

    case AST_EXPR_BINARY:
        analyze_binary(expr);
        break;

    case AST_EXPR_CALL:
    case AST_EXPR_METHOD_CALL:
    case AST_EXPR_INDEX:
    case AST_EXPR_MEMBER:
        info->stability = STABILITY_UNSTABLE;
        break;

    case AST_EXPR_ARRAY:
    case AST_EXPR_ARRAY_LITERAL:
    case AST_EXPR_DICT:
    case AST_EXPR_STRUCT_LITERAL:
    case AST_EXPR_CLASS_LITERAL:
        info->stability = STABILITY_UNSTABLE;
        break;

    default:
        info->stability = STABILITY_UNSTABLE;
        break;
    }
}

static void analyze_stmt(ASTStmt* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {

    case AST_STMT_VAR_DECL:
        if (stmt->as.var_decl.init)
            analyze_expr(stmt->as.var_decl.init);
        declare_var(
            stmt->as.var_decl.var_name,
            stmt->as.var_decl.is_const,
            g_ctx->current_function_depth > 0
        );
        break;

    case AST_STMT_VAR_ASSIGN:
        analyze_expr(stmt->as.var_assign.value);
        mark_mutation(stmt->as.var_assign.var_name);
        break;

    case AST_STMT_EXPR:
        analyze_expr(stmt->as.expr.expr);
        break;

    case AST_STMT_RETURN:
        if (stmt->as.ret.expr)
            analyze_expr(stmt->as.ret.expr);
        break;

    case AST_STMT_IF:
        analyze_expr(stmt->as.if_stmt.condition);
        enter_scope();
        for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next)
            analyze_stmt(s);
        leave_scope();
        break;

    case AST_STMT_WHILE:
        g_ctx->in_loop++;
        analyze_expr(stmt->as.while_stmt.condition);
        enter_scope();
        for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next)
            analyze_stmt(s);
        leave_scope();
        g_ctx->in_loop--;
        break;

    case AST_STMT_FUNC_DECL:
        g_ctx->current_function_depth++;
        enter_scope();
        for (int i = 0; i < stmt->as.func_decl.param_count; i++)
            declare_var(stmt->as.func_decl.param_names[i], 1, 1);
        for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next)
            analyze_stmt(s);
        leave_scope();
        g_ctx->current_function_depth--;
        break;

    default:
        break;
    }
}

// public api. 
int type_stability_analyze(ASTStmtList* program) {
    if (!program) return 0;

    g_ctx = calloc(1, sizeof(TypeStabilityCtx));
    if (!g_ctx) return 0;

    g_ctx->expr_capacity = INITIAL_EXPR_CAPACITY;
    g_ctx->expr_info =
        calloc(g_ctx->expr_capacity, sizeof(TypeStabilityInfo*));

    if (!g_ctx->expr_info) {
        free(g_ctx);
        g_ctx = NULL;
        return 0;
    }

    reset_var_tracker();

    for (ASTStmt* s = program->head; s; s = s->next)
        analyze_stmt(s);

    reset_var_tracker();
    return 1;
}

TypeStabilityInfo* get_expr_stability_info(ASTExpr* expr) {
    return expr ? expr->stability_info : NULL;
}

int is_type_stable(ASTExpr* expr) {
    TypeStabilityInfo* info = get_expr_stability_info(expr);
    return info && info->stability >= STABILITY_CONDITIONAL;
}

int can_unbox_integer(ASTExpr* expr) {
    TypeStabilityInfo* info = get_expr_stability_info(expr);
    return info &&
           info->type == TYPE_INT &&
           info->stability >= STABILITY_CONDITIONAL;
}
