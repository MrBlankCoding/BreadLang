#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "compiler/ast/ast.h"
#include "compiler/parser/expr.h"
#include "runtime/print.h"
#include "runtime/error.h"
#include "core/var.h"
#include "core/function.h"
#include "core/value.h"
#include "runtime/runtime.h"

void ast_free_expr_list(ASTExpr** items, int count);

// Memory management and utility functions

void* xmalloc(size_t n) {
    void* p = malloc(n);
    if (!p) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
    }
    return p;
}

ASTStmtList* ast_stmt_list_new(void) {
    ASTStmtList* list = xmalloc(sizeof(ASTStmtList));
    if (!list) return NULL;
    list->head = NULL;
    list->tail = NULL;
    return list;
}

void ast_stmt_list_add(ASTStmtList* list, ASTStmt* stmt) {
    if (!list || !stmt) return;
    stmt->next = NULL;
    if (list->tail) {
        list->tail->next = stmt;
        list->tail = stmt;
    } else {
        list->head = stmt;
        list->tail = stmt;
    }
}

ASTExpr* ast_expr_new(ASTExprKind kind) {
    ASTExpr* e = xmalloc(sizeof(ASTExpr));
    if (!e) return NULL;
    memset(e, 0, sizeof(ASTExpr));
    e->kind = kind;
    e->tag.is_known = 0;
    e->tag.type = TYPE_NIL;
    return e;
}

ASTStmt* ast_stmt_new(ASTStmtKind kind) {
    ASTStmt* s = xmalloc(sizeof(ASTStmt));
    if (!s) return NULL;
    memset(s, 0, sizeof(ASTStmt));
    s->kind = kind;
    return s;
}

void ast_free_expr(ASTExpr* e) {
    if (!e) return;

    type_descriptor_free(e->tag.type_desc);

    switch (e->kind) {
        case AST_EXPR_STRING:
            free(e->as.string_val);
            break;
        case AST_EXPR_STRING_LITERAL:
            free(e->as.string_literal.value);
            break;
        case AST_EXPR_VAR:
            free(e->as.var_name);
            break;
        case AST_EXPR_BINARY:
            ast_free_expr(e->as.binary.left);
            ast_free_expr(e->as.binary.right);
            break;
        case AST_EXPR_UNARY:
            ast_free_expr(e->as.unary.operand);
            break;
        case AST_EXPR_CALL:
            free(e->as.call.name);
            ast_free_expr_list(e->as.call.args, e->as.call.arg_count);
            break;
        case AST_EXPR_ARRAY:
            ast_free_expr_list(e->as.array.items, e->as.array.item_count);
            break;
        case AST_EXPR_ARRAY_LITERAL:
            ast_free_expr_list(e->as.array_literal.elements, e->as.array_literal.element_count);
            break;
        case AST_EXPR_DICT:
            for (int i = 0; i < e->as.dict.entry_count; i++) {
                ast_free_expr(e->as.dict.entries[i].key);
                ast_free_expr(e->as.dict.entries[i].value);
            }
            free(e->as.dict.entries);
            break;
        case AST_EXPR_INDEX:
            ast_free_expr(e->as.index.target);
            ast_free_expr(e->as.index.index);
            break;
        case AST_EXPR_MEMBER:
            ast_free_expr(e->as.member.target);
            free(e->as.member.member);
            break;
        case AST_EXPR_METHOD_CALL:
            ast_free_expr(e->as.method_call.target);
            free(e->as.method_call.name);
            ast_free_expr_list(e->as.method_call.args, e->as.method_call.arg_count);
            break;
        default:
            break;
    }

    free(e);
}

void ast_free_expr_list(ASTExpr** items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        ast_free_expr(items[i]);
    }
    free(items);
}

void ast_free_stmt_list(ASTStmtList* stmts) {
    if (!stmts) return;
    ASTStmt* cur = stmts->head;
    while (cur) {
        ASTStmt* next = cur->next;
        switch (cur->kind) {
            case AST_STMT_VAR_DECL:
                free(cur->as.var_decl.var_name);
                type_descriptor_free(cur->as.var_decl.type_desc);
                ast_free_expr(cur->as.var_decl.init);
                break;
            case AST_STMT_VAR_ASSIGN:
                free(cur->as.var_assign.var_name);
                ast_free_expr(cur->as.var_assign.value);
                break;
            case AST_STMT_INDEX_ASSIGN:
                ast_free_expr(cur->as.index_assign.target);
                ast_free_expr(cur->as.index_assign.index);
                ast_free_expr(cur->as.index_assign.value);
                break;
            case AST_STMT_PRINT:
                ast_free_expr(cur->as.print.expr);
                break;
            case AST_STMT_EXPR:
                ast_free_expr(cur->as.expr.expr);
                break;
            case AST_STMT_IF:
                ast_free_expr(cur->as.if_stmt.condition);
                ast_free_stmt_list(cur->as.if_stmt.then_branch);
                if (cur->as.if_stmt.else_branch) ast_free_stmt_list(cur->as.if_stmt.else_branch);
                break;
            case AST_STMT_WHILE:
                ast_free_expr(cur->as.while_stmt.condition);
                ast_free_stmt_list(cur->as.while_stmt.body);
                break;
            case AST_STMT_FOR:
                free(cur->as.for_stmt.var_name);
                ast_free_expr(cur->as.for_stmt.range_expr);
                ast_free_stmt_list(cur->as.for_stmt.body);
                break;
            case AST_STMT_FOR_IN:
                free(cur->as.for_in_stmt.var_name);
                ast_free_expr(cur->as.for_in_stmt.iterable);
                ast_free_stmt_list(cur->as.for_in_stmt.body);
                break;
            case AST_STMT_FUNC_DECL:
                free(cur->as.func_decl.name);
                for (int i = 0; i < cur->as.func_decl.param_count; i++) {
                    free(cur->as.func_decl.param_names[i]);
                    if (cur->as.func_decl.param_type_descs && cur->as.func_decl.param_type_descs[i]) {
                        type_descriptor_free(cur->as.func_decl.param_type_descs[i]);
                    }
                    if (cur->as.func_decl.param_defaults && cur->as.func_decl.param_defaults[i]) {
                        ast_free_expr(cur->as.func_decl.param_defaults[i]);
                    }
                }
                free(cur->as.func_decl.param_names);
                free(cur->as.func_decl.param_type_descs);
                free(cur->as.func_decl.param_defaults);
                type_descriptor_free(cur->as.func_decl.return_type_desc);
                ast_free_stmt_list(cur->as.func_decl.body);
                break;
            case AST_STMT_RETURN:
                ast_free_expr(cur->as.ret.expr);
                break;
            default:
                break;
        }
        free(cur);
        cur = next;
    }
    free(stmts);
}