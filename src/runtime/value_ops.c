#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/parser/expr.h"

static inline int bread_value_is_number(VarType t) {
    return t == TYPE_INT || t == TYPE_FLOAT || t == TYPE_DOUBLE;
}

static inline int bread_value_is_numeric_type(const BreadValue* v) {
    return v && bread_value_is_number(v->type);
}

static double bread_value_as_double(const BreadValue* v) {
    if (!v) return 0.0;
    
    switch (v->type) {
        case TYPE_DOUBLE: return v->value.double_val;
        case TYPE_FLOAT:  return (double)v->value.float_val;
        case TYPE_INT:    return (double)v->value.int_val;
        default:          return 0.0;
    }
}

static int bread_binary_numeric_promote(const BreadValue* left, const BreadValue* right, 
                                        double* out_l, double* out_r) {
    if (!left || !right || !out_l || !out_r) return 0;
    
    if (!bread_value_is_numeric_type(left) || !bread_value_is_numeric_type(right)) {
        return 0;
    }
    
    *out_l = bread_value_as_double(left);
    *out_r = bread_value_as_double(right);
    return 1;
}

int bread_add(const BreadValue* left, const BreadValue* right, BreadValue* out) {
    if (!left || !right || !out) return 0;

    memset(out, 0, sizeof(*out));
    out->type = TYPE_NIL;

    if (left->type == TYPE_STRING || right->type == TYPE_STRING) {
        if (left->type != TYPE_STRING || right->type != TYPE_STRING) {
            BREAD_ERROR_SET_TYPE_MISMATCH("Cannot concatenate string with non-string");
            return 0;
        }
        BreadString* s = bread_string_concat(left->value.string_val, right->value.string_val);
        if (!s) {
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory during string concatenation");
            return 0;
        }
        out->type = TYPE_STRING;
        out->value.string_val = s;
        return 1;
    }

    if (!bread_value_is_numeric_type(left) || !bread_value_is_numeric_type(right)) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Invalid operand types for arithmetic operation");
        return 0;
    }

    if (left->type == TYPE_INT && right->type == TYPE_INT) {
        out->type = TYPE_INT;
        out->value.int_val = left->value.int_val + right->value.int_val;
        return 1;
    }

    out->type = TYPE_DOUBLE;
    out->value.double_val = bread_value_as_double(left) + bread_value_as_double(right);
    return 1;
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
            *out_bool = (fabsf(left->value.float_val - right->value.float_val) < 1e-6f);
            return 1;
            
        case TYPE_DOUBLE:
            *out_bool = (fabs(left->value.double_val - right->value.double_val) < 1e-9);
            return 1;
            
        case TYPE_STRING:
            *out_bool = bread_string_eq(left->value.string_val, right->value.string_val);
            return 1;
            
        case TYPE_ARRAY:
        case TYPE_DICT:
        case TYPE_OPTIONAL:
        case TYPE_STRUCT:
        case TYPE_CLASS:
            *out_bool = (left->value.array_val == right->value.array_val);
            return 1;
            
        default:
            *out_bool = 0;
            return 1;
    }
}

static void bread_format_float(char* buf, size_t size, double value) {
    snprintf(buf, size, "%.15g", value);
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E')) {
        size_t len = strlen(buf);
        if (len + 2 < size) {
            strncat(buf, ".0", size - len - 1);
        }
    }
}

static void bread_print_value_recursive(const BreadValue* v, int compact) {
    if (!v) {
        printf("nil");
        return;
    }

    switch (v->type) {
        case TYPE_NIL:
            printf("nil");
            break;
            
        case TYPE_BOOL:
            printf("%s", v->value.bool_val ? "true" : "false");
            break;
            
        case TYPE_INT:
            printf("%lld", (long long)v->value.int_val);
            break;
            
        case TYPE_FLOAT: {
            if (compact) {
                char buf[64];
                bread_format_float(buf, sizeof(buf), (double)v->value.float_val);
                printf("%s", buf);
            } else {
                printf("%f", v->value.float_val);
            }
            break;
        }
            
        case TYPE_DOUBLE: {
            if (compact) {
                char buf[64];
                bread_format_float(buf, sizeof(buf), v->value.double_val);
                printf("%s", buf);
            } else {
                printf("%lf", v->value.double_val);
            }
            break;
        }
            
        case TYPE_STRING:
            printf("%s", bread_string_cstr(v->value.string_val));
            break;
            
        case TYPE_OPTIONAL: {
            BreadOptional* o = v->value.optional_val;
            if (!o || !o->is_some) {
                printf("nil");
            } else {
                BreadValue inner = bread_value_clone(o->value);
                bread_print_value_recursive(&inner, compact);
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
                    bread_print_value_recursive(&a->items[i], compact);
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
                    if (d->entries[i].is_occupied && !d->entries[i].is_deleted) {
                        if (!first) printf(", ");
                        first = 0;
                        if (d->entries[i].key.type == TYPE_STRING) {
                            printf("%s: ", bread_string_cstr(d->entries[i].key.value.string_val));
                        } else {
                            printf("key: ");
                        }
                        bread_print_value_recursive(&d->entries[i].value, compact);
                    }
                }
            }
            printf("}");
            break;
        }
        
        case TYPE_STRUCT: {
            BreadStruct* s = v->value.struct_val;
            if (!s) {
                printf("nil");
                break;
            }
            printf("%s { ", s->type_name);
            for (int i = 0; i < s->field_count; i++) {
                if (i > 0) printf(", ");
                printf("%s: ", s->field_names[i]);
                bread_print_value_recursive(&s->field_values[i], compact);
            }
            printf(" }");
            break;
        }
        
        case TYPE_CLASS: {
            BreadClass* c = v->value.class_val;
            if (!c) {
                printf("nil");
                break;
            }
            printf("%s { ", c->class_name);
            for (int i = 0; i < c->field_count; i++) {
                if (i > 0) printf(", ");
                printf("%s: ", c->field_names[i]);
                bread_print_value_recursive(&c->field_values[i], compact);
            }
            printf(" }");
            break;
        }
        
        default:
            printf("nil");
            break;
    }
}

void bread_print(const BreadValue* v) {
    bread_print_value_recursive(v, 0);
    printf("\n");
}

void bread_print_compact(const BreadValue* v) {
    bread_print_value_recursive(v, 1);
    printf("\n");
}

void bread_value_set_nil(BreadValue* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_NIL;
}

void bread_value_set_bool(BreadValue* out, int v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_BOOL;
    out->value.bool_val = v ? 1 : 0;
}

void bread_value_set_int(BreadValue* out, int64_t v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_INT;
    out->value.int_val = v;
}

void bread_value_set_float(BreadValue* out, float v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_FLOAT;
    out->value.float_val = v;
}

void bread_value_set_double(BreadValue* out, double v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_DOUBLE;
    out->value.double_val = v;
}

void bread_value_set_string(BreadValue* out, const char* cstr) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_STRING;
    out->value.string_val = bread_string_new(cstr ? cstr : "");
    if (!out->value.string_val) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory creating string");
        out->type = TYPE_NIL;
    }
}

void bread_value_set_array(BreadValue* out, BreadArray* a) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!a) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory creating array");
        out->type = TYPE_NIL;
        return;
    }
    out->type = TYPE_ARRAY;
    out->value.array_val = a;
    bread_array_retain(a);
}

void bread_value_set_dict(BreadValue* out, BreadDict* d) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_DICT;
    out->value.dict_val = d;
    if (d) bread_dict_retain(d);
}

void bread_value_set_optional(BreadValue* out, BreadOptional* o) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_OPTIONAL;
    out->value.optional_val = o;
    if (o) bread_optional_retain(o);
}

void bread_value_set_struct(BreadValue* out, BreadStruct* s) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_STRUCT;
    out->value.struct_val = s;
    if (s) bread_struct_retain(s);
}

void bread_value_set_class(BreadValue* out, BreadClass* c) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->type = TYPE_CLASS;
    out->value.class_val = c;
    if (c) bread_class_retain(c);
}

size_t bread_value_size(void) {
    return sizeof(BreadValue);
}

void bread_value_copy(const BreadValue* in, BreadValue* out) {
    if (!in || !out || in == out) return;
    bread_value_release(out);
    *out = bread_value_clone(*in);
}

void bread_value_release_value(BreadValue* v) {
    bread_value_release(v);
}

int bread_value_assign(BreadValue* target, const BreadValue* source) {
    if (!target || !source) return 0;
    if (target == source) return 1;
    
    BreadValue new_value = bread_value_clone(*source);
    bread_value_release(target);
    *target = new_value;
    
    return 1;
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
        case TYPE_OPTIONAL: {
            BreadOptional* o = v->value.optional_val;
            return o && o->is_some;
        }
        case TYPE_ARRAY:
        case TYPE_DICT:
        case TYPE_STRUCT:
        case TYPE_CLASS:
            return v->value.array_val != NULL;
        default:
            return 0;
    }
}

int bread_unary_not(const BreadValue* in, BreadValue* out) {
    if (!in || !out) return 0;
    if (in->type != TYPE_BOOL) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Logical NOT requires boolean operand");
        return 0;
    }
    bread_value_set_bool(out, !in->value.bool_val);
    return 1;
}

static int bread_compare_values(const BreadValue* left, const BreadValue* right, 
                                int* result, char op) {
    if (left->type != right->type) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Cannot compare different types");
        return 0;
    }

    switch (left->type) {
        case TYPE_INT:
            switch (op) {
                case '=': *result = left->value.int_val == right->value.int_val; break;
                case '!': *result = left->value.int_val != right->value.int_val; break;
                case '<': *result = left->value.int_val < right->value.int_val; break;
                case '>': *result = left->value.int_val > right->value.int_val; break;
                case 'l': *result = left->value.int_val <= right->value.int_val; break;
                case 'g': *result = left->value.int_val >= right->value.int_val; break;
                default: return 0;
            }
            return 1;

        case TYPE_DOUBLE:
            switch (op) {
                case '=': *result = left->value.double_val == right->value.double_val; break;
                case '!': *result = left->value.double_val != right->value.double_val; break;
                case '<': *result = left->value.double_val < right->value.double_val; break;
                case '>': *result = left->value.double_val > right->value.double_val; break;
                case 'l': *result = left->value.double_val <= right->value.double_val; break;
                case 'g': *result = left->value.double_val >= right->value.double_val; break;
                default: return 0;
            }
            return 1;

        case TYPE_FLOAT:
            switch (op) {
                case '=': *result = left->value.float_val == right->value.float_val; break;
                case '!': *result = left->value.float_val != right->value.float_val; break;
                case '<': *result = left->value.float_val < right->value.float_val; break;
                case '>': *result = left->value.float_val > right->value.float_val; break;
                case 'l': *result = left->value.float_val <= right->value.float_val; break;
                case 'g': *result = left->value.float_val >= right->value.float_val; break;
                default: return 0;
            }
            return 1;

        case TYPE_BOOL:
            switch (op) {
                case '=': *result = left->value.bool_val == right->value.bool_val; break;
                case '!': *result = left->value.bool_val != right->value.bool_val; break;
                case '<': *result = left->value.bool_val < right->value.bool_val; break;
                case '>': *result = left->value.bool_val > right->value.bool_val; break;
                case 'l': *result = left->value.bool_val <= right->value.bool_val; break;
                case 'g': *result = left->value.bool_val >= right->value.bool_val; break;
                default: return 0;
            }
            return 1;

        case TYPE_STRING: {
            int cmp = bread_string_cmp(left->value.string_val, right->value.string_val);
            switch (op) {
                case '=': *result = cmp == 0; break;
                case '!': *result = cmp != 0; break;
                case '<': *result = cmp < 0; break;
                case '>': *result = cmp > 0; break;
                case 'l': *result = cmp <= 0; break;
                case 'g': *result = cmp >= 0; break;
                default: return 0;
            }
            return 1;
        }

        case TYPE_ARRAY:
        case TYPE_DICT:
        case TYPE_OPTIONAL:
        case TYPE_STRUCT:
        case TYPE_CLASS:
            if (op != '=' && op != '!') {
                BREAD_ERROR_SET_TYPE_MISMATCH("Complex types only support == and != comparison");
                return 0;
            }
            *result = (op == '=') ? 
                (left->value.array_val == right->value.array_val) :
                (left->value.array_val != right->value.array_val);
            return 1;

        default:
            BREAD_ERROR_SET_TYPE_MISMATCH("Cannot compare this type");
            return 0;
    }
}

static int bread_arithmetic_op(char op, const BreadValue* left, const BreadValue* right, BreadValue* out) {
    if (!bread_value_is_numeric_type(left) || !bread_value_is_numeric_type(right)) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Invalid operand types for arithmetic operation");
        return 0;
    }

    if (left->type == TYPE_INT && right->type == TYPE_INT) {
        if ((op == '/' || op == '%') && right->value.int_val == 0) {
            BREAD_ERROR_SET_DIVISION_BY_ZERO(
                op == '/' ? "Integer division by zero" : "Integer modulo by zero");
            return 0;
        }
        
        int64_t result;
        switch (op) {
            case '-': result = left->value.int_val - right->value.int_val; break;
            case '*': result = left->value.int_val * right->value.int_val; break;
            case '/': result = left->value.int_val / right->value.int_val; break;
            case '%': result = left->value.int_val % right->value.int_val; break;
            default: return 0;
        }
        bread_value_set_int(out, result);
        return 1;
    }

    if (op == '%') {
        BREAD_ERROR_SET_TYPE_MISMATCH("Modulo operation not supported for floating point numbers");
        return 0;
    }

    double l, r;
    if (!bread_binary_numeric_promote(left, right, &l, &r)) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Invalid operand types for arithmetic operation");
        return 0;
    }

    if (op == '/' && r == 0.0) {
        BREAD_ERROR_SET_DIVISION_BY_ZERO("Floating point division by zero");
        return 0;
    }

    double result;
    switch (op) {
        case '-': result = l - r; break;
        case '*': result = l * r; break;
        case '/': result = l / r; break;
        default: return 0;
    }
    bread_value_set_double(out, result);
    return 1;
}

int bread_binary_op(char op, const BreadValue* left, const BreadValue* right, BreadValue* out) {
    if (!left || !right || !out) return 0;
    
    switch (op) {
        case '+':
            return bread_add(left, right, out);
            
        case '-':
        case '*':
        case '/':
        case '%':
            return bread_arithmetic_op(op, left, right, out);
            
        case '=':
        case '!':
        case '<':
        case '>':
        case 'l':
        case 'g': {
            int result;
            if (!bread_compare_values(left, right, &result, op)) {
                return 0;
            }
            bread_value_set_bool(out, result);
            return 1;
        }
            
        case '&':
        case '|':
            if (left->type != TYPE_BOOL || right->type != TYPE_BOOL) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Logical operations require boolean operands");
                return 0;
            }
            bread_value_set_bool(out, (op == '&') ? 
                (left->value.bool_val && right->value.bool_val) :
                (left->value.bool_val || right->value.bool_val));
            return 1;
            
        default: {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "Unknown binary operator '%c'", op);
            BREAD_ERROR_SET_RUNTIME(error_msg);
            return 0;
        }
    }
}

int bread_coerce_value(VarType target, const BreadValue* in, BreadValue* out) {
    if (!in || !out) return 0;

    if (target == TYPE_OPTIONAL) {
        if (in->type == TYPE_NIL) {
            BreadOptional* o = bread_optional_new_none();
            if (!o) {
                BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory creating optional value");
                return 0;
            }
            bread_value_set_optional(out, o);
            bread_optional_release(o);
            return 1;
        }
        
        if (in->type != TYPE_OPTIONAL) {
            BreadOptional* o = bread_optional_new_some(*in);
            if (!o) {
                BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory creating optional value");
                return 0;
            }
            bread_value_set_optional(out, o);
            bread_optional_release(o);
            return 1;
        }
    }

    if (target == in->type) {
        *out = bread_value_clone(*in);
        return 1;
    }

    switch (target) {
        case TYPE_DOUBLE:
            if (in->type == TYPE_INT) {
                bread_value_set_double(out, (double)in->value.int_val);
                return 1;
            }
            if (in->type == TYPE_FLOAT) {
                bread_value_set_double(out, (double)in->value.float_val);
                return 1;
            }
            break;
            
        case TYPE_FLOAT:
            if (in->type == TYPE_INT) {
                bread_value_set_float(out, (float)in->value.int_val);
                return 1;
            }
            if (in->type == TYPE_DOUBLE) {
                bread_value_set_float(out, (float)in->value.double_val);
                return 1;
            }
            break;
            
        case TYPE_INT:
            if (in->type == TYPE_DOUBLE) {
                bread_value_set_int(out, (int64_t)in->value.double_val);
                return 1;
            }
            if (in->type == TYPE_FLOAT) {
                bread_value_set_int(out, (int64_t)in->value.float_val);
                return 1;
            }
            break;
            
        default:
            break;
    }

    BREAD_ERROR_SET_TYPE_MISMATCH("Type mismatch in coercion");
    return 0;
}