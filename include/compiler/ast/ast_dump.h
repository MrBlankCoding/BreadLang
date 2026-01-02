#ifndef AST_DUMP_H
#define AST_DUMP_H

#include <stdio.h>
#include "compiler/ast/ast.h"

// AST dumping/printing functions
void ast_dump_stmt_list(const ASTStmtList* stmts, FILE* out);

#endif