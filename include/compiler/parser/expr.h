#ifndef EXPR_H
#define EXPR_H

#include "core/var.h"

typedef struct ExprResult {
    VarType type;
    VarValue value;
    int is_error;
} ExprResult;

ExprResult evaluate_expression(const char* expr);
ExprResult evaluate_binary_op(ExprResult left, ExprResult right, char op);
ExprResult evaluate_unary_op(ExprResult operand, char op);
#endif