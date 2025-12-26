#ifndef AST_MEMORY_H
#define AST_MEMORY_H

#include "compiler/ast/ast.h"

void* xmalloc(size_t n);
ASTStmtList* ast_stmt_list_new(void);
void ast_stmt_list_add(ASTStmtList* list, ASTStmt* stmt);
ASTExpr* ast_expr_new(ASTExprKind kind, SourceLoc loc);
ASTStmt* ast_stmt_new(ASTStmtKind kind, SourceLoc loc);
void ast_free_expr(ASTExpr* e);
void ast_free_expr_list(ASTExpr** items, int count);
void ast_free_stmt(ASTStmt* stmt);

#endif