#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../include/runtime.h"
#include "../include/value.h"
#include "../include/var.h"

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
