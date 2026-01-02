#ifndef AST_EXPR_PARSER_H
#define AST_EXPR_PARSER_H

#include "compiler/ast/ast.h"

// Expression parsing functions
ASTExpr* parse_expression(const char** expr);
ASTExpr* parse_expression_str_as_ast(const char** code);

#endif