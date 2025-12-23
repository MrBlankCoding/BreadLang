#ifndef AST_STMT_PARSER_H
#define AST_STMT_PARSER_H

#include "compiler/ast/ast.h"

// Statement parsing functions
ASTStmt* parse_stmt(const char** code);
ASTStmtList* parse_block(const char** code);

#endif