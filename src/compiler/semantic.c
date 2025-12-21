#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "compiler/semantic.h"
#include "core/function.h"

#define MAX_SEM_SYMBOLS 512

typedef enum {
    SEM_SYM_VAR,
    SEM_SYM_FUNC
} SemSymbolKind;

typedef struct {
    SemSymbolKind kind;
    char* name;
    int depth;
    VarType type;
    int is_const;
    int arity;
} SemSymbol;

typedef struct {
    SemSymbol syms[MAX_SEM_SYMBOLS];
    int count;
    int depth;
    int had_error;
} SemCtx;

static int sem_same_name(const char* a, const char* b) {
    return strcmp(a ? a : "", b ? b : "") == 0;
}

static void sem_error(SemCtx* ctx, const char* msg, const char* name) {
    if (name) printf("Error: %s '%s'\n", msg, name);
    else printf("Error: %s\n", msg);
    if (ctx) ctx->had_error = 1;
}

static void sem_enter_scope(SemCtx* ctx) {
    if (ctx) ctx->depth++;
}

static void sem_leave_scope(SemCtx* ctx) {
    if (!ctx) return;
    int new_count = ctx->count;
    while (new_count > 0) {
        SemSymbol* s = &ctx->syms[new_count - 1];
        if (s->depth < ctx->depth) break;
        free(s->name);
        memset(s, 0, sizeof(*s));
        new_count--;
    }
    ctx->count = new_count;
    if (ctx->depth > 0) ctx->depth--;
}

static SemSymbol* sem_find(SemCtx* ctx, SemSymbolKind kind, const char* name) {
    if (!ctx || !name) return NULL;
    for (int i = ctx->count - 1; i >= 0; i--) {
        SemSymbol* s = &ctx->syms[i];
        if (s->kind == kind && sem_same_name(s->name, name)) return s;
    }
    return NULL;
}

static int sem_declare_var(SemCtx* ctx, const char* name, VarType type, int is_const) {
    if (!ctx || !name) return 0;

    for (int i = ctx->count - 1; i >= 0; i--) {
        SemSymbol* s = &ctx->syms[i];
        if (s->depth != ctx->depth) break;
        if (s->kind == SEM_SYM_VAR && sem_same_name(s->name, name)) {
            sem_error(ctx, "Variable already declared", name);
            return 0;
        }
    }

    if (ctx->count >= MAX_SEM_SYMBOLS) {
        sem_error(ctx, "Too many symbols", NULL);
        return 0;
    }

    SemSymbol* s = &ctx->syms[ctx->count++];
    memset(s, 0, sizeof(*s));
    s->kind = SEM_SYM_VAR;
    s->name = strdup(name);
    s->depth = ctx->depth;
    s->type = type;
    s->is_const = is_const;
    if (!s->name) {
        ctx->count--;
        sem_error(ctx, "Out of memory", NULL);
        return 0;
    }
    return 1;
}

static int sem_declare_func(SemCtx* ctx, const char* name, int arity) {
    if (!ctx || !name) return 0;

    for (int i = 0; i < ctx->count; i++) {
        SemSymbol* s = &ctx->syms[i];
        if (s->kind == SEM_SYM_FUNC && sem_same_name(s->name, name)) {
            sem_error(ctx, "Function already declared", name);
            return 0;
        }
    }

    if (ctx->count >= MAX_SEM_SYMBOLS) {
        sem_error(ctx, "Too many symbols", NULL);
        return 0;
    }

    SemSymbol* s = &ctx->syms[ctx->count++];
    memset(s, 0, sizeof(*s));
    s->kind = SEM_SYM_FUNC;
    s->name = strdup(name);
    s->depth = 0;
    s->arity = arity;
    if (!s->name) {
        ctx->count--;
        sem_error(ctx, "Out of memory", NULL);
        return 0;
    }
    return 1;
}

static void sem_tag(ASTExpr* e, VarType t) {
    if (!e) return;
    e->tag.is_known = 1;
    e->tag.type = t;
}

static void sem_visit_expr(SemCtx* ctx, ASTExpr* e);
static void sem_visit_stmt_list(SemCtx* ctx, ASTStmtList* stmts);

static void sem_hoist_funcs(SemCtx* ctx, ASTStmtList* stmts) {
    if (!ctx || !stmts) return;
    for (ASTStmt* st = stmts->head; st; st = st->next) {
        if (st->kind == AST_STMT_FUNC_DECL) {
            (void)sem_declare_func(ctx, st->as.func_decl.name, st->as.func_decl.param_count);
        }
        if (st->kind == AST_STMT_IF) {
            sem_hoist_funcs(ctx, st->as.if_stmt.then_branch);
            if (st->as.if_stmt.else_branch) sem_hoist_funcs(ctx, st->as.if_stmt.else_branch);
        } else if (st->kind == AST_STMT_WHILE) {
            sem_hoist_funcs(ctx, st->as.while_stmt.body);
        } else if (st->kind == AST_STMT_FOR) {
            sem_hoist_funcs(ctx, st->as.for_stmt.body);
        }
    }
}

static void sem_visit_expr(SemCtx* ctx, ASTExpr* e) {
    if (!e) return;

    switch (e->kind) {
        case AST_EXPR_NIL:
            sem_tag(e, TYPE_NIL);
            break;
        case AST_EXPR_BOOL:
            sem_tag(e, TYPE_BOOL);
            break;
        case AST_EXPR_INT:
            sem_tag(e, TYPE_INT);
            break;
        case AST_EXPR_DOUBLE:
            sem_tag(e, TYPE_DOUBLE);
            break;
        case AST_EXPR_STRING:
            sem_tag(e, TYPE_STRING);
            break;
        case AST_EXPR_STRING_LITERAL:
            sem_tag(e, TYPE_STRING);
            break;
        case AST_EXPR_VAR: {
            SemSymbol* v = sem_find(ctx, SEM_SYM_VAR, e->as.var_name);
            if (!v) {
                sem_error(ctx, "Unknown variable", e->as.var_name);
                return;
            }
            sem_tag(e, v->type);
            break;
        }
        case AST_EXPR_UNARY:
            sem_visit_expr(ctx, e->as.unary.operand);
            if (e->as.unary.op == '!') sem_tag(e, TYPE_BOOL);
            break;
        case AST_EXPR_BINARY:
            sem_visit_expr(ctx, e->as.binary.left);
            sem_visit_expr(ctx, e->as.binary.right);
            if (e->as.binary.op == '&' || e->as.binary.op == '|' || e->as.binary.op == '=' || e->as.binary.op == '!' || e->as.binary.op == '<' || e->as.binary.op == '>') {
                sem_tag(e, TYPE_BOOL);
            } else if (e->as.binary.op == '+' || e->as.binary.op == '-' || e->as.binary.op == '*' || e->as.binary.op == '/' || e->as.binary.op == '%') {
                if (e->as.binary.left && e->as.binary.left->tag.is_known && e->as.binary.left->tag.type == TYPE_STRING) sem_tag(e, TYPE_STRING);
                else sem_tag(e, TYPE_DOUBLE);
            }
            break;
        case AST_EXPR_CALL: {
            if (e->as.call.name && strcmp(e->as.call.name, "range") == 0) {
                if (e->as.call.arg_count != 1) {
                    printf("Error: Function '%s' expected %d args but got %d\n", e->as.call.name, 1, e->as.call.arg_count);
                    ctx->had_error = 1;
                    return;
                }
                for (int i = 0; i < e->as.call.arg_count; i++) {
                    sem_visit_expr(ctx, e->as.call.args[i]);
                }
                return;
            }
            SemSymbol* f = sem_find(ctx, SEM_SYM_FUNC, e->as.call.name);
            if (!f) {
                sem_error(ctx, "Unknown function", e->as.call.name);
                return;
            }
            if (f->arity != e->as.call.arg_count) {
                printf("Error: Function '%s' expected %d args but got %d\n", e->as.call.name ? e->as.call.name : "", f->arity, e->as.call.arg_count);
                ctx->had_error = 1;
                return;
            }
            for (int i = 0; i < e->as.call.arg_count; i++) {
                sem_visit_expr(ctx, e->as.call.args[i]);
            }
            break;
        }
        case AST_EXPR_ARRAY:
            for (int i = 0; i < e->as.array.item_count; i++) sem_visit_expr(ctx, e->as.array.items[i]);
            sem_tag(e, TYPE_ARRAY);
            break;
        case AST_EXPR_ARRAY_LITERAL:
            for (int i = 0; i < e->as.array_literal.element_count; i++) sem_visit_expr(ctx, e->as.array_literal.elements[i]);
            sem_tag(e, TYPE_ARRAY);
            break;
        case AST_EXPR_RANGE:
            sem_visit_expr(ctx, e->as.range.start);
            sem_visit_expr(ctx, e->as.range.end);
            sem_tag(e, TYPE_ARRAY); // Ranges are treated as arrays for iteration
            break;
        case AST_EXPR_DICT:
            for (int i = 0; i < e->as.dict.entry_count; i++) {
                sem_visit_expr(ctx, e->as.dict.entries[i].key);
                sem_visit_expr(ctx, e->as.dict.entries[i].value);
            }
            sem_tag(e, TYPE_DICT);
            break;
        case AST_EXPR_INDEX:
            sem_visit_expr(ctx, e->as.index.target);
            sem_visit_expr(ctx, e->as.index.index);
            break;
        case AST_EXPR_MEMBER:
            sem_visit_expr(ctx, e->as.member.target);
            if (e->as.member.member && strcmp(e->as.member.member, "length") == 0) sem_tag(e, TYPE_INT);
            break;
        case AST_EXPR_METHOD_CALL:
            sem_visit_expr(ctx, e->as.method_call.target);
            for (int i = 0; i < e->as.method_call.arg_count; i++) sem_visit_expr(ctx, e->as.method_call.args[i]);
            sem_tag(e, TYPE_NIL);
            break;
        default:
            break;
    }
}

static void sem_visit_stmt_list(SemCtx* ctx, ASTStmtList* stmts) {
    if (!ctx || !stmts) return;

    for (ASTStmt* st = stmts->head; st; st = st->next) {
        switch (st->kind) {
            case AST_STMT_VAR_DECL:
                sem_visit_expr(ctx, st->as.var_decl.init);
                {
                    VarType decl_t = st->as.var_decl.type;
                    if (st->as.var_decl.type_str) {
                        size_t n = strlen(st->as.var_decl.type_str);
                        if (n > 0 && st->as.var_decl.type_str[n - 1] == '?') {
                            decl_t = TYPE_OPTIONAL;
                        }
                    }
                    (void)sem_declare_var(ctx, st->as.var_decl.var_name, decl_t, st->as.var_decl.is_const);
                }
                break;
            case AST_STMT_VAR_ASSIGN: {
                SemSymbol* v = sem_find(ctx, SEM_SYM_VAR, st->as.var_assign.var_name);
                if (!v) {
                    sem_error(ctx, "Unknown variable", st->as.var_assign.var_name);
                    break;
                }
                sem_visit_expr(ctx, st->as.var_assign.value);
                break;
            }
            case AST_STMT_PRINT:
                sem_visit_expr(ctx, st->as.print.expr);
                break;
            case AST_STMT_EXPR:
                sem_visit_expr(ctx, st->as.expr.expr);
                break;
            case AST_STMT_IF:
                sem_visit_expr(ctx, st->as.if_stmt.condition);
                sem_enter_scope(ctx);
                sem_visit_stmt_list(ctx, st->as.if_stmt.then_branch);
                sem_leave_scope(ctx);
                if (st->as.if_stmt.else_branch) {
                    sem_enter_scope(ctx);
                    sem_visit_stmt_list(ctx, st->as.if_stmt.else_branch);
                    sem_leave_scope(ctx);
                }
                break;
            case AST_STMT_WHILE:
                sem_visit_expr(ctx, st->as.while_stmt.condition);
                sem_enter_scope(ctx);
                sem_visit_stmt_list(ctx, st->as.while_stmt.body);
                sem_leave_scope(ctx);
                break;
            case AST_STMT_FOR:
                sem_visit_expr(ctx, st->as.for_stmt.range_expr);
                sem_enter_scope(ctx);
                (void)sem_declare_var(ctx, st->as.for_stmt.var_name, TYPE_INT, 0);
                sem_visit_stmt_list(ctx, st->as.for_stmt.body);
                sem_leave_scope(ctx);
                break;
            case AST_STMT_FUNC_DECL:
                sem_enter_scope(ctx);
                for (int i = 0; i < st->as.func_decl.param_count; i++) {
                    (void)sem_declare_var(ctx, st->as.func_decl.param_names[i], st->as.func_decl.param_types[i], 1);
                }
                sem_visit_stmt_list(ctx, st->as.func_decl.body);
                sem_leave_scope(ctx);
                break;
            case AST_STMT_RETURN:
                sem_visit_expr(ctx, st->as.ret.expr);
                break;
            case AST_STMT_BREAK:
            case AST_STMT_CONTINUE:
                break;
            default:
                break;
        }
    }
}

int semantic_analyze(ASTStmtList* program) {
    SemCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.depth = 0;

    (void)sem_declare_func(&ctx, "range", 1);

    sem_hoist_funcs(&ctx, program);
    sem_visit_stmt_list(&ctx, program);

    while (ctx.depth > 0) sem_leave_scope(&ctx);
    for (int i = 0; i < ctx.count; i++) {
        free(ctx.syms[i].name);
    }

    return ctx.had_error ? 0 : 1;
}
