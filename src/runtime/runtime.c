#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/expr.h"

void* bread_alloc(size_t size) {
    return malloc(size);
}

void* bread_realloc(void* ptr, size_t new_size) {
    return realloc(ptr, new_size);
}

void bread_free(void* ptr) {
    free(ptr);
}

static BreadString* bread_string_alloc(size_t len) {
    size_t total = sizeof(BreadString) + len + 1;
    BreadString* s = (BreadString*)bread_alloc(total);
    if (!s) return NULL;
    s->header.kind = BREAD_OBJ_STRING;
    s->header.refcount = 1;
    s->len = (uint32_t)len;
    s->data[len] = '\0';
    return s;
}

BreadString* bread_string_new_len(const char* data, size_t len) {
    BreadString* s = bread_string_alloc(len);
    if (!s) return NULL;
    if (len > 0 && data) {
        memcpy(s->data, data, len);
    }
    s->data[len] = '\0';
    return s;
}

BreadString* bread_string_new(const char* cstr) {
    if (!cstr) cstr = "";
    return bread_string_new_len(cstr, strlen(cstr));
}

const char* bread_string_cstr(const BreadString* s) {
    return s ? (const char*)s->data : "";
}

size_t bread_string_len(const BreadString* s) {
    return s ? (size_t)s->len : 0;
}

void bread_string_retain(BreadString* s) {
    if (s) s->header.refcount++;
}

void bread_string_release(BreadString* s) {
    if (!s) return;
    if (s->header.refcount == 0) return;
    s->header.refcount--;
    if (s->header.refcount > 0) return;
    bread_free(s);
}

BreadString* bread_string_concat(const BreadString* a, const BreadString* b) {
    size_t la = bread_string_len(a);
    size_t lb = bread_string_len(b);
    BreadString* out = bread_string_alloc(la + lb);
    if (!out) return NULL;
    if (la) memcpy(out->data, bread_string_cstr(a), la);
    if (lb) memcpy(out->data + la, bread_string_cstr(b), lb);
    out->data[la + lb] = '\0';
    return out;
}

int bread_string_eq(const BreadString* a, const BreadString* b) {
    size_t la = bread_string_len(a);
    size_t lb = bread_string_len(b);
    if (la != lb) return 0;
    if (la == 0) return 1;
    return memcmp(bread_string_cstr(a), bread_string_cstr(b), la) == 0;
}

int bread_string_cmp(const BreadString* a, const BreadString* b) {
    return strcmp(bread_string_cstr(a), bread_string_cstr(b));
}

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
    if (v->type != TYPE_BOOL) return 0;
    return v->value.bool_val ? 1 : 0;
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

    if (op == '=' || op == '!' || op == '<' || op == '>') {
        int result_val = 0;
        if (left->type == TYPE_DOUBLE && right->type == TYPE_DOUBLE) {
            if (op == '=') result_val = left->value.double_val == right->value.double_val;
            else if (op == '!') result_val = left->value.double_val != right->value.double_val;
            else if (op == '<') result_val = left->value.double_val < right->value.double_val;
            else if (op == '>') result_val = left->value.double_val > right->value.double_val;
        } else if (left->type == TYPE_INT && right->type == TYPE_INT) {
            if (op == '=') result_val = left->value.int_val == right->value.int_val;
            else if (op == '!') result_val = left->value.int_val != right->value.int_val;
            else if (op == '<') result_val = left->value.int_val < right->value.int_val;
            else if (op == '>') result_val = left->value.int_val > right->value.int_val;
        } else if (left->type == TYPE_BOOL && right->type == TYPE_BOOL) {
            if (op == '=') result_val = left->value.bool_val == right->value.bool_val;
            else if (op == '!') result_val = left->value.bool_val != right->value.bool_val;
            else if (op == '<') result_val = left->value.bool_val < right->value.bool_val;
            else if (op == '>') result_val = left->value.bool_val > right->value.bool_val;
        } else if (left->type == TYPE_STRING && right->type == TYPE_STRING) {
            int cmp = bread_string_cmp(left->value.string_val, right->value.string_val);
            if (op == '=') result_val = cmp == 0;
            else if (op == '!') result_val = cmp != 0;
            else if (op == '<') result_val = cmp < 0;
            else if (op == '>') result_val = cmp > 0;
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

int bread_index_op(const BreadValue* target, const BreadValue* idx, BreadValue* out) {
    if (!target || !idx || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;
    if (real_target.type == TYPE_OPTIONAL) {
        BreadOptional* o = real_target.value.optional_val;
        if (!o || !o->is_some) {
            bread_value_set_nil(out);
            return 1;
        }
        real_target = bread_value_clone(o->value);
        target_owned = 1;
    }

    bread_value_set_nil(out);

    if (real_target.type == TYPE_ARRAY) {
        if (idx->type != TYPE_INT) {
            printf("Error: Array index must be Int\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        BreadValue* at = bread_array_get(real_target.value.array_val, idx->value.int_val);
        if (at) *out = bread_value_clone(*at);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_DICT) {
        if (idx->type != TYPE_STRING) {
            printf("Error: Dictionary key must be String\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        BreadValue* v = bread_dict_get(real_target.value.dict_val, bread_string_cstr(idx->value.string_val));
        if (v) *out = bread_value_clone(*v);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    printf("Error: Type does not support indexing\n");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_member_op(const BreadValue* target, const char* member, int is_opt, BreadValue* out) {
    if (!target || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;

    if (is_opt) {
        if (real_target.type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (real_target.type == TYPE_OPTIONAL) {
            BreadOptional* o = real_target.value.optional_val;
            if (!o || !o->is_some) {
                bread_value_set_nil(out);
                return 1;
            }
            real_target = bread_value_clone(o->value);
            target_owned = 1;
        }
    }

    bread_value_set_nil(out);

    if (member && strcmp(member, "length") == 0) {
        if (real_target.type == TYPE_ARRAY) {
            bread_value_set_int(out, real_target.value.array_val ? real_target.value.array_val->count : 0);
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }
        if (real_target.type == TYPE_STRING) {
            bread_value_set_int(out, (int)bread_string_len(real_target.value.string_val));
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }
        if (real_target.type == TYPE_DICT) {
            bread_value_set_int(out, real_target.value.dict_val ? real_target.value.dict_val->count : 0);
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }

        printf("Error: Unsupported member access\n");
        if (target_owned) bread_value_release(&real_target);
        return 0;
    }

    if (real_target.type == TYPE_DICT) {
        BreadValue* v = bread_dict_get(real_target.value.dict_val, member ? member : "");
        if (v) *out = bread_value_clone(*v);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (is_opt) {
        bread_value_set_nil(out);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    printf("Error: Unsupported member access\n");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_method_call_op(const BreadValue* target, const char* name, int argc, const BreadValue* args, int is_opt, BreadValue* out) {
    if (!target || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;

    if (is_opt) {
        if (real_target.type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (real_target.type == TYPE_OPTIONAL) {
            BreadOptional* o = real_target.value.optional_val;
            if (!o || !o->is_some) {
                bread_value_set_nil(out);
                return 1;
            }
            real_target = bread_value_clone(o->value);
            target_owned = 1;
        }
    }

    bread_value_set_nil(out);

    if (name && strcmp(name, "append") == 0) {
        if (real_target.type != TYPE_ARRAY) {
            printf("Error: append() is only supported on arrays\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (argc != 1 || !args) {
            printf("Error: append() expects 1 argument\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (!bread_array_append(real_target.value.array_val, args[0])) {
            printf("Error: Out of memory\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        bread_value_set_nil(out);
        return 1;
    }

    printf("Error: Unsupported method call\n");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_dict_set_value(struct BreadDict* d, const BreadValue* key, const BreadValue* val) {
    if (!d || !key || !val) return 0;
    if (key->type != TYPE_STRING) {
        printf("Error: Dictionary keys must be strings\n");
        return 0;
    }
    return bread_dict_set((BreadDict*)d, bread_string_cstr(key->value.string_val), *val);
}

int bread_array_append_value(struct BreadArray* a, const BreadValue* v) {
    if (!a || !v) return 0;
    return bread_array_append((BreadArray*)a, *v);
}

int bread_var_decl(const char* name, VarType type, int is_const, const BreadValue* init) {
    if (!name) return 0;
    VarValue zero;
    memset(&zero, 0, sizeof(zero));
    if (!declare_variable_raw(name, type, zero, is_const ? 1 : 0)) return 0;
    if (!init) return 1;

    ExprResult r;
    memset(&r, 0, sizeof(r));
    r.is_error = 0;
    r.type = init->type;
    r.value = init->value;
    return bread_init_variable_from_expr_result(name, &r);
}

int bread_var_decl_if_missing(const char* name, VarType type, int is_const, const BreadValue* init) {
    if (!name) return 0;
    if (get_variable((char*)name)) return 1;
    return bread_var_decl(name, type, is_const, init);
}

int bread_var_assign(const char* name, const BreadValue* value) {
    if (!name || !value) return 0;
    ExprResult r;
    memset(&r, 0, sizeof(r));
    r.is_error = 0;
    r.type = value->type;
    r.value = value->value;
    return bread_assign_variable_from_expr_result(name, &r);
}

int bread_var_load(const char* name, BreadValue* out) {
    if (!name || !out) return 0;
    Variable* var = get_variable((char*)name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name);
        return 0;
    }

    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = var->type;
    v.value = var->value;
    *out = bread_value_clone(v);
    return 1;
}

void bread_push_scope(void) {
    push_scope();
}

void bread_pop_scope(void) {
    pop_scope();
}
