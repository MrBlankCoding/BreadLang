#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/parser/expr.h"
#include "core/var.h"
#include "runtime/runtime.h"
#include "runtime/error.h"

static ExprResult ops_create_error_result(void) {
    ExprResult result;
    result.is_error = 1;
    return result;
}

static ExprResult ops_create_int_result(int val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_INT;
    result.value.int_val = val;
    return result;
}

static ExprResult ops_create_bool_result(int val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_BOOL;
    result.value.bool_val = val;
    return result;
}

static ExprResult ops_create_double_result(double val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_DOUBLE;
    result.value.double_val = val;
    return result;
}

static ExprResult ops_create_string_result(BreadString* val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_STRING;
    result.value.string_val = val;
    return result;
}

ExprResult evaluate_binary_op(ExprResult left, ExprResult right, char op) {
    // Handle string concatenation
    if (op == '+' && (left.type == TYPE_STRING || right.type == TYPE_STRING)) {
        if (left.type != TYPE_STRING || right.type != TYPE_STRING) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Cannot concatenate %s with %s using + operator", 
                    (left.type == TYPE_STRING) ? "string" : "non-string",
                    (right.type == TYPE_STRING) ? "string" : "non-string");
            BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
            return ops_create_error_result();
        }
        BreadString* result = bread_string_concat(left.value.string_val, right.value.string_val);
        if (!result) {
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory during string concatenation");
            return ops_create_error_result();
        }
        bread_string_release(left.value.string_val);
        bread_string_release(right.value.string_val);
        return ops_create_string_result(result);
    }

    // Convert types for arithmetic operations
    if (left.type == TYPE_INT && right.type == TYPE_DOUBLE) {
        left.type = TYPE_DOUBLE;
        left.value.double_val = left.value.int_val;
    } else if (left.type == TYPE_DOUBLE && right.type == TYPE_INT) {
        right.type = TYPE_DOUBLE;
        right.value.double_val = right.value.int_val;
    } else if (left.type == TYPE_INT && right.type == TYPE_FLOAT) {
        left.type = TYPE_DOUBLE;
        left.value.double_val = left.value.int_val;
        right.type = TYPE_DOUBLE;
        right.value.double_val = right.value.float_val;
    } else if (left.type == TYPE_FLOAT && right.type == TYPE_INT) {
        left.type = TYPE_DOUBLE;
        left.value.double_val = left.value.float_val;
        right.type = TYPE_DOUBLE;
        right.value.double_val = right.value.int_val;
    } else if (left.type == TYPE_FLOAT && right.type == TYPE_DOUBLE) {
        left.type = TYPE_DOUBLE;
        left.value.double_val = left.value.float_val;
    } else if (left.type == TYPE_DOUBLE && right.type == TYPE_FLOAT) {
        right.type = TYPE_DOUBLE;
        right.value.double_val = right.value.float_val;
    }

    // Arithmetic operations
    if (op == '+' || op == '-' || op == '*' || op == '/' || op == '%') {
        if (left.type == TYPE_DOUBLE && right.type == TYPE_DOUBLE) {
            double result_val;
            switch (op) {
                case '+': result_val = left.value.double_val + right.value.double_val; break;
                case '-': result_val = left.value.double_val - right.value.double_val; break;
                case '*': result_val = left.value.double_val * right.value.double_val; break;
                case '/':
                    if (right.value.double_val == 0) {
                        printf("Error: Division by zero\n");
                        return ops_create_error_result();
                    }
                    result_val = left.value.double_val / right.value.double_val;
                    break;
                case '%':
                    printf("Error: Modulo operation not supported for floating point numbers\n");
                    return ops_create_error_result();
                default: return ops_create_error_result();
            }
            return ops_create_double_result(result_val);
        } else if (left.type == TYPE_INT && right.type == TYPE_INT) {
            if (op == '/' && right.value.int_val == 0) {
                printf("Error: Division by zero\n");
                return ops_create_error_result();
            }
            if (op == '%' && right.value.int_val == 0) {
                printf("Error: Modulo by zero\n");
                return ops_create_error_result();
            }
            int result_val;
            switch (op) {
                case '+': result_val = left.value.int_val + right.value.int_val; break;
                case '-': result_val = left.value.int_val - right.value.int_val; break;
                case '*': result_val = left.value.int_val * right.value.int_val; break;
                case '/': result_val = left.value.int_val / right.value.int_val; break;
                case '%': result_val = left.value.int_val % right.value.int_val; break;
                default: return ops_create_error_result();
            }
            return ops_create_int_result(result_val);
        } else {
            printf("Error: Invalid operand types for arithmetic operation\n");
            return ops_create_error_result();
        }
    }

    // Comparison operations
    // Note: '<=' and '>=' are encoded as 'l' and 'g' respectively.
    if (op == '=' || op == '!' || op == '<' || op == '>' || op == 'l' || op == 'g') {
        int result_val = 0;
        if (left.type == TYPE_DOUBLE && right.type == TYPE_DOUBLE) {
            switch (op) {
                case '=': result_val = left.value.double_val == right.value.double_val; break;
                case '!': result_val = left.value.double_val != right.value.double_val; break;
                case '<': result_val = left.value.double_val < right.value.double_val; break;
                case '>': result_val = left.value.double_val > right.value.double_val; break;
                case 'l': result_val = left.value.double_val <= right.value.double_val; break;
                case 'g': result_val = left.value.double_val >= right.value.double_val; break;
            }
        } else if (left.type == TYPE_INT && right.type == TYPE_INT) {
            switch (op) {
                case '=': result_val = left.value.int_val == right.value.int_val; break;
                case '!': result_val = left.value.int_val != right.value.int_val; break;
                case '<': result_val = left.value.int_val < right.value.int_val; break;
                case '>': result_val = left.value.int_val > right.value.int_val; break;
                case 'l': result_val = left.value.int_val <= right.value.int_val; break;
                case 'g': result_val = left.value.int_val >= right.value.int_val; break;
            }
        } else if (left.type == TYPE_BOOL && right.type == TYPE_BOOL) {
            switch (op) {
                case '=': result_val = left.value.bool_val == right.value.bool_val; break;
                case '!': result_val = left.value.bool_val != right.value.bool_val; break;
                case '<': result_val = left.value.bool_val < right.value.bool_val; break;
                case '>': result_val = left.value.bool_val > right.value.bool_val; break;
                case 'l': result_val = left.value.bool_val <= right.value.bool_val; break;
                case 'g': result_val = left.value.bool_val >= right.value.bool_val; break;
            }
        } else if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
            int cmp = bread_string_cmp(left.value.string_val, right.value.string_val);
            switch (op) {
                case '=': result_val = cmp == 0; break;
                case '!': result_val = cmp != 0; break;
                case '<': result_val = cmp < 0; break;
                case '>': result_val = cmp > 0; break;
                case 'l': result_val = cmp <= 0; break;
                case 'g': result_val = cmp >= 0; break;
            }
        } else {
            printf("Error: Cannot compare different types\n");
            return ops_create_error_result();
        }
        return ops_create_bool_result(result_val);
    }

    // Logical operations
    if (op == '&' || op == '|') {
        if (left.type != TYPE_BOOL || right.type != TYPE_BOOL) {
            printf("Error: Logical operations require boolean operands\n");
            return ops_create_error_result();
        }
        int result_val;
        switch (op) {
            case '&': result_val = left.value.bool_val && right.value.bool_val; break;
            case '|': result_val = left.value.bool_val || right.value.bool_val; break;
            default: return ops_create_error_result();
        }
        return ops_create_bool_result(result_val);
    }

    printf("Error: Unknown binary operator '%c'\n", op);
    return ops_create_error_result();
}

ExprResult evaluate_unary_op(ExprResult operand, char op) {
    if (op == '!') {
        if (operand.type != TYPE_BOOL) {
            printf("Error: Logical NOT requires boolean operand\n");
            return ops_create_error_result();
        }
        return ops_create_bool_result(!operand.value.bool_val);
    }

    printf("Error: Unknown unary operator '%c'\n", op);
    return ops_create_error_result();
}
