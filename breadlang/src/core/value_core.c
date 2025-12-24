#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "runtime/error.h"
#include "compiler/parser/expr.h"

BreadValue bread_value_from_expr_result(ExprResult r) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = r.type;
    v.value = r.value;
    return v;
}

ExprResult bread_expr_result_from_value(BreadValue v) {
    ExprResult r;
    memset(&r, 0, sizeof(r));
    r.is_error = 0;
    r.type = v.type;
    r.value = v.value;
    return r;
}

BreadValue bread_value_clone(BreadValue v) {
    BreadValue out;
    memset(&out, 0, sizeof(out));
    out.type = v.type;

    switch (v.type) {
        case TYPE_STRING:
            out.value.string_val = v.value.string_val;
            bread_string_retain(out.value.string_val);
            break;
        case TYPE_ARRAY:
            out.value.array_val = v.value.array_val;
            bread_array_retain(out.value.array_val);
            break;
        case TYPE_DICT:
            out.value.dict_val = v.value.dict_val;
            bread_dict_retain(out.value.dict_val);
            break;
        case TYPE_OPTIONAL:
            out.value.optional_val = v.value.optional_val;
            bread_optional_retain(out.value.optional_val);
            break;
        case TYPE_STRUCT:
            out.value.struct_val = v.value.struct_val;
            bread_struct_retain(out.value.struct_val);
            break;
        case TYPE_CLASS:
            out.value.class_val = v.value.class_val;
            bread_class_retain(out.value.class_val);
            break;
        default:
            out.value = v.value;
            break;
    }

    return out;
}

void bread_value_release(BreadValue* v) {
    if (!v) return;
    switch (v->type) {
        case TYPE_STRING:
            bread_string_release(v->value.string_val);
            v->value.string_val = NULL;
            break;
        case TYPE_ARRAY:
            bread_array_release(v->value.array_val);
            v->value.array_val = NULL;
            break;
        case TYPE_DICT:
            bread_dict_release(v->value.dict_val);
            v->value.dict_val = NULL;
            break;
        case TYPE_OPTIONAL:
            bread_optional_release(v->value.optional_val);
            v->value.optional_val = NULL;
            break;
        case TYPE_STRUCT:
            bread_struct_release(v->value.struct_val);
            v->value.struct_val = NULL;
            break;
        case TYPE_CLASS:
            bread_class_release(v->value.class_val);
            v->value.class_val = NULL;
            break;
        default:
            break;
    }
    v->type = TYPE_NIL;
}

int64_t bread_value_get_int(BreadValue* v) {
    if (!v || v->type != TYPE_INT) return 0;
    return v->value.int_val;
}

double bread_value_get_double(BreadValue* v) {
    if (!v) return 0.0;
    if (v->type == TYPE_DOUBLE) return v->value.double_val;
    if (v->type == TYPE_INT) return (double)v->value.int_val;
    return 0.0;
}

int bread_value_get_bool(BreadValue* v) {
    if (!v || v->type != TYPE_BOOL) return 0;
    return v->value.bool_val;
}

int bread_value_get_type(BreadValue* v) {
    if (!v) return TYPE_NIL;
    return v->type;
}