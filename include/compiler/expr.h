#ifndef EXPR_H
#define EXPR_H

#include "core/var.h"

// Expression evaluation result
typedef struct ExprResult {
    VarType type;
    VarValue value;
    int is_error;
} ExprResult;

// Expression evaluation functions
ExprResult evaluate_expression(const char* expr);
ExprResult evaluate_binary_op(ExprResult left, ExprResult right, char op);
ExprResult evaluate_unary_op(ExprResult operand, char op);

// Helper functions
int is_operator(char c);
int get_operator_precedence(char op);
int is_unary_operator(char op);

#endif