#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/expr.h"

static int bread_value_is_number(VarType t) {
    return t == TYPE_INT || t == TYPE_FLOAT || t == TYPE_DOUBLE;
}

static double bread_value_as_double(const BreadValue* v) {
    if (!v) return 0.0;
    if (v->type == TYPE_DOUBLE) return v->value.double_val;
    if (v->type == TYPE_FLOAT) return (double)v->value.float_val;
    if (v->type == TYPE_INT) return (double)v->value.int_val;
    return 0.0;
}

int bread_add(const BreadValue* left, const BreadValue* right, BreadValue* out) {
    if (!left || !right || !out) return 0;

    memset(out, 0, sizeof(*out));
    out->type = TYPE_NIL;

    if (left->type == TYPE_STRING || right->type == TYPE_STRING) {
        if (left->type != TYPE_STRING || right->type != TYPE_STRING) {
            printf("Error: Cannot concatenate string with non-string\n");
            return 0;
        }
        BreadString* s = bread_string_concat(left->value.string_val, right->value.string_val);
        if (!s) {
            printf("Error: Out of memory\n");
            return 0;
        }
        out->type = TYPE_STRING;
        out->value.string_val = s;
        return 1;
    }

    if (bread_value_is_number(left->type) && bread_value_is_number(right->type)) {
        if (left->type == TYPE_DOUBLE || left->type == TYPE_FLOAT || right->type == TYPE_DOUBLE || right->type == TYPE_FLOAT) {
            out->type = TYPE_DOUBLE;
            out->value.double_val = bread_value_as_double(left) + bread_value_as_double(right);
            return 1;
        }
        out->type = TYPE_INT;
        out->value.int_val = left->value.int_val + right->value.int_val;
        return 1;
    }

    printf("Error: Invalid operand types for arithmetic operation\n");
    return 0;
}

int bread_eq(const BreadValue* left, const BreadValue* right, int* out_bool) {
    if (!left || !right || !out_bool) return 0;

    if (left->type != right->type) {
        *out_bool = 0;
        return 1;
    }

    switch (left->type) {
        case TYPE_NIL:
            *out_bool = 1;
            return 1;
        case TYPE_BOOL:
            *out_bool = (left->value.bool_val == right->value.bool_val);
            return 1;
        case TYPE_INT:
            *out_bool = (left->value.int_val == right->value.int_val);
            return 1;
        case TYPE_FLOAT:
            *out_bool = (left->value.float_val == right->value.float_val);
            return 1;
        case TYPE_DOUBLE:
            *out_bool = (left->value.double_val == right->value.double_val);
            return 1;
        case TYPE_STRING:
            *out_bool = bread_string_eq(left->value.string_val, right->value.string_val);
            return 1;
        case TYPE_ARRAY:
            *out_bool = (left->value.array_val == right->value.array_val);
            return 1;
        case TYPE_DICT:
            *out_bool = (left->value.dict_val == right->value.dict_val);
            return 1;
        case TYPE_OPTIONAL:
            *out_bool = (left->value.optional_val == right->value.optional_val);
            return 1;
        default:
            *out_bool = 0;
            return 1;
    }
}

static void bread_print_value_inner(const BreadValue* v) {
    if (!v) {
        printf("nil");
        return;
    }

    switch (v->type) {
        case TYPE_STRING:
            printf("%s", bread_string_cstr(v->value.string_val));
            break;
        case TYPE_INT:
            printf("%d", v->value.int_val);
            break;
        case TYPE_BOOL:
            printf("%s", v->value.bool_val ? "true" : "false");
            break;
        case TYPE_FLOAT:
            printf("%f", v->value.float_val);
            break;
        case TYPE_DOUBLE:
            printf("%lf", v->value.double_val);
            break;
        case TYPE_NIL:
            printf("nil");
            break;
        case TYPE_OPTIONAL: {
            BreadOptional* o = v->value.optional_val;
            if (!o || !o->is_some) {
                printf("nil");
            } else {
                BreadValue inner = bread_value_clone(o->value);
                bread_print_value_inner(&inner);
                bread_value_release(&inner);
            }
            break;
        }
        case TYPE_ARRAY: {
            BreadArray* a = v->value.array_val;
            printf("[");
            if (a && a->count > 0) {
                for (int i = 0; i < a->count; i++) {
                    if (i > 0) printf(", ");
                    bread_print_value_inner(&a->items[i]);
                }
            }
            printf("]");
            break;
        }
        case TYPE_DICT: {
            BreadDict* d = v->value.dict_val;
            printf("{");
            if (d && d->count > 0) {
                int first = 1;
                for (int i = 0; i < d->capacity; i++) {
                    if (d->entries[i].key) {
                        if (!first) printf(", ");
                        first = 0;
                        printf("%s: ", bread_string_cstr(d->entries[i].key));
                        bread_print_value_inner(&d->entries[i].value);
                    }
                }
            }
            printf("}");
            break;
        }
        default:
            printf("nil");
            break;
    }
}

void bread_print(const BreadValue* v) {
    bread_print_value_inner(v);
    printf("\n");
}

void bread_value_set_nil(struct BreadValue* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_NIL;
}

void bread_value_set_bool(struct BreadValue* out, int v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_BOOL;
    out->value.bool_val = v ? 1 : 0;
}

void bread_value_set_int(struct BreadValue* out, int v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_INT;
    out->value.int_val = v;
}

void bread_value_set_float(struct BreadValue* out, float v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_FLOAT;
    out->value.float_val = v;
}

void bread_value_set_double(struct BreadValue* out, double v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_DOUBLE;
    out->value.double_val = v;
}

void bread_value_set_string(struct BreadValue* out, const char* cstr) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_STRING;
    out->value.string_val = bread_string_new(cstr ? cstr : "");
    if (!out->value.string_val) {
        printf("Error: Out of memory\n");
        out->type = TYPE_NIL;
    }
}

void bread_value_set_array(struct BreadValue* out, struct BreadArray* a) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_ARRAY;
    out->value.array_val = (BreadArray*)a;
    bread_array_retain(out->value.array_val);
}

void bread_value_set_dict(struct BreadValue* out, struct BreadDict* d) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_DICT;
    out->value.dict_val = (BreadDict*)d;
    bread_dict_retain(out->value.dict_val);
}

void bread_value_set_optional(struct BreadValue* out, struct BreadOptional* o) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_OPTIONAL;
    out->value.optional_val = (BreadOptional*)o;
    bread_optional_retain(out->value.optional_val);
}

size_t bread_value_size(void) {
    return sizeof(BreadValue);
}

void bread_value_copy(const struct BreadValue* in, struct BreadValue* out) {
    if (!in || !out) return;
    bread_value_release(out);
    *out = bread_value_clone(*in);
}

void bread_value_release_value(struct BreadValue* v) {
    bread_value_release(v);
}

int bread_is_truthy(const BreadValue* v) {
    if (!v) return 0;
    switch (v->type) {
        case TYPE_NIL:
            return 0;
        case TYPE_BOOL:
            return v->value.bool_val;
        case TYPE_INT:
            return v->value.int_val != 0;
        case TYPE_FLOAT:
            return v->value.float_val != 0.0f;
        case TYPE_DOUBLE:
            return v->value.double_val != 0.0;
        case TYPE_STRING:
            return bread_string_len(v->value.string_val) > 0;
        case TYPE_ARRAY:
            return 1;
        case TYPE_DICT:
            return 1;
        case TYPE_OPTIONAL: {
            BreadOptional* o = v->value.optional_val;
            return o && o->is_some;
        }
        default:
            return 0;
    }
}

int bread_unary_not(const BreadValue* in, BreadValue* out) {
    if (!in || !out) return 0;
    if (in->type != TYPE_BOOL) {
        printf("Error: Logical NOT requires boolean operand\n");
        return 0;
    }
    bread_value_set_bool(out, !in->value.bool_val);
    return 1;
}

static int bread_binary_numeric_promote(const BreadValue* left, const BreadValue* right, double* out_l, double* out_r) {
    if (!left || !right || !out_l || !out_r) return 0;
    *out_l = 0.0;
    *out_r = 0.0;

    if (left->type == TYPE_DOUBLE) *out_l = left->value.double_val;
    else if (left->type == TYPE_FLOAT) *out_l = (double)left->value.float_val;
    else if (left->type == TYPE_INT) *out_l = (double)left->value.int_val;
    else return 0;

    if (right->type == TYPE_DOUBLE) *out_r = right->value.double_val;
    else if (right->type == TYPE_FLOAT) *out_r = (double)right->value.float_val;
    else if (right->type == TYPE_INT) *out_r = (double)right->value.int_val;
    else return 0;

    return 1;
}

int bread_binary_op(char op, const BreadValue* left, const BreadValue* right, BreadValue* out) {
    if (!left || !right || !out) return 0;

    if (op == '+') {
        return bread_add(left, right, out);
    }

    if (op == '-' || op == '*' || op == '/' || op == '%') {
        if (left->type == TYPE_INT && right->type == TYPE_INT) {
            if (op == '/' && right->value.int_val == 0) {
                printf("Error: Division by zero\n");
                return 0;
            }
            if (op == '%' && right->value.int_val == 0) {
                printf("Error: Modulo by zero\n");
                return 0;
            }
            int r = 0;
            if (op == '-') r = left->value.int_val - right->value.int_val;
            else if (op == '*') r = left->value.int_val * right->value.int_val;
            else if (op == '/') r = left->value.int_val / right->value.int_val;
            else if (op == '%') r = left->value.int_val % right->value.int_val;
            bread_value_set_int(out, r);
            return 1;
        }

        if (left->type == TYPE_DOUBLE || left->type == TYPE_FLOAT || right->type == TYPE_DOUBLE || right->type == TYPE_FLOAT) {
            if (op == '%') {
                printf("Error: Modulo operation not supported for floating point numbers\n");
                return 0;
            }
            double l = 0.0;
            double r = 0.0;
            if (!bread_binary_numeric_promote(left, right, &l, &r)) {
                printf("Error: Invalid operand types for arithmetic operation\n");
                return 0;
            }
            if (op == '/' && r == 0.0) {
                printf("Error: Division by zero\n");
                return 0;
            }
            double res = 0.0;
            if (op == '-') res = l - r;
            else if (op == '*') res = l * r;
            else if (op == '/') res = l / r;
            bread_value_set_double(out, res);
            return 1;
        }

        printf("Error: Invalid operand types for arithmetic operation\n");
        return 0;
    }

    // Note: '<=' and '>=' are encoded as 'l' and 'g' respectively.
    if (op == '=' || op == '!' || op == '<' || op == '>' || op == 'l' || op == 'g') {
        int result_val = 0;
        if (left->type == TYPE_DOUBLE && right->type == TYPE_DOUBLE) {
            if (op == '=') result_val = left->value.double_val == right->value.double_val;
            else if (op == '!') result_val = left->value.double_val != right->value.double_val;
            else if (op == '<') result_val = left->value.double_val < right->value.double_val;
            else if (op == '>') result_val = left->value.double_val > right->value.double_val;
            else if (op == 'l') result_val = left->value.double_val <= right->value.double_val;
            else if (op == 'g') result_val = left->value.double_val >= right->value.double_val;
        } else if (left->type == TYPE_INT && right->type == TYPE_INT) {
            if (op == '=') result_val = left->value.int_val == right->value.int_val;
            else if (op == '!') result_val = left->value.int_val != right->value.int_val;
            else if (op == '<') result_val = left->value.int_val < right->value.int_val;
            else if (op == '>') result_val = left->value.int_val > right->value.int_val;
            else if (op == 'l') result_val = left->value.int_val <= right->value.int_val;
            else if (op == 'g') result_val = left->value.int_val >= right->value.int_val;
        } else if (left->type == TYPE_BOOL && right->type == TYPE_BOOL) {
            if (op == '=') result_val = left->value.bool_val == right->value.bool_val;
            else if (op == '!') result_val = left->value.bool_val != right->value.bool_val;
            else if (op == '<') result_val = left->value.bool_val < right->value.bool_val;
            else if (op == '>') result_val = left->value.bool_val > right->value.bool_val;
            else if (op == 'l') result_val = left->value.bool_val <= right->value.bool_val;
            else if (op == 'g') result_val = left->value.bool_val >= right->value.bool_val;
        } else if (left->type == TYPE_STRING && right->type == TYPE_STRING) {
            int cmp = bread_string_cmp(left->value.string_val, right->value.string_val);
            if (op == '=') result_val = cmp == 0;
            else if (op == '!') result_val = cmp != 0;
            else if (op == '<') result_val = cmp < 0;
            else if (op == '>') result_val = cmp > 0;
            else if (op == 'l') result_val = cmp <= 0;
            else if (op == 'g') result_val = cmp >= 0;
        } else {
            printf("Error: Cannot compare different types\n");
            return 0;
        }
        bread_value_set_bool(out, result_val);
        return 1;
    }

    if (op == '&' || op == '|') {
        if (left->type != TYPE_BOOL || right->type != TYPE_BOOL) {
            printf("Error: Logical operations require boolean operands\n");
            return 0;
        }
        int res = 0;
        if (op == '&') res = (left->value.bool_val && right->value.bool_val);
        else if (op == '|') res = (left->value.bool_val || right->value.bool_val);
        bread_value_set_bool(out, res);
        return 1;
    }

    printf("Error: Unknown binary operator '%c'\n", op);
    return 0;
}

int bread_coerce_value(VarType target, const BreadValue* in, BreadValue* out) {
    if (!in || !out) return 0;

    if (target == TYPE_OPTIONAL && in->type == TYPE_NIL) {
        BreadOptional* o = bread_optional_new_none();
        if (!o) {
            printf("Error: Out of memory\n");
            return 0;
        }
        bread_value_set_optional(out, o);
        bread_optional_release(o);
        return 1;
    }

    if (target == TYPE_OPTIONAL && in->type != TYPE_OPTIONAL) {
        BreadOptional* o = bread_optional_new_some(*in);
        if (!o) {
            printf("Error: Out of memory\n");
            return 0;
        }
        bread_value_set_optional(out, o);
        bread_optional_release(o);
        return 1;
    }

    if (target == in->type) {
        *out = bread_value_clone(*in);
        return 1;
    }

    if (target == TYPE_DOUBLE && in->type == TYPE_INT) {
        bread_value_set_double(out, (double)in->value.int_val);
        return 1;
    }
    if (target == TYPE_DOUBLE && in->type == TYPE_FLOAT) {
        bread_value_set_double(out, (double)in->value.float_val);
        return 1;
    }
    if (target == TYPE_FLOAT && in->type == TYPE_INT) {
        bread_value_set_float(out, (float)in->value.int_val);
        return 1;
    }
    if (target == TYPE_FLOAT && in->type == TYPE_DOUBLE) {
        bread_value_set_float(out, (float)in->value.double_val);
        return 1;
    }
    if (target == TYPE_INT && in->type == TYPE_DOUBLE) {
        bread_value_set_int(out, (int)in->value.double_val);
        return 1;
    }
    if (target == TYPE_INT && in->type == TYPE_FLOAT) {
        bread_value_set_int(out, (int)in->value.float_val);
        return 1;
    }

    printf("Error: Type mismatch\n");
    return 0;
}