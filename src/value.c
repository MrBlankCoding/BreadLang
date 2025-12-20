#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../include/value.h"
#include "../include/expr.h"

static BreadValue bread_value_make_nil(void) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = TYPE_NIL;
    return v;
}

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
        default:
            break;
    }
    v->type = TYPE_NIL;
}

BreadArray* bread_array_new(void) {
    BreadArray* a = malloc(sizeof(BreadArray));
    if (!a) return NULL;
    a->header.kind = BREAD_OBJ_ARRAY;
    a->header.refcount = 1;
    a->count = 0;
    a->capacity = 0;
    a->items = NULL;
    return a;
}

void bread_array_retain(BreadArray* a) {
    if (a) a->header.refcount++;
}

void bread_array_release(BreadArray* a) {
    if (!a) return;
    a->header.refcount--;
    if (a->header.refcount > 0) return;

    for (int i = 0; i < a->count; i++) {
        bread_value_release(&a->items[i]);
    }
    free(a->items);
    free(a);
}

static int bread_array_ensure_cap(BreadArray* a, int need) {
    if (!a) return 0;
    if (a->capacity >= need) return 1;
    int new_cap = a->capacity == 0 ? 4 : a->capacity;
    while (new_cap < need) new_cap *= 2;
    BreadValue* new_items = realloc(a->items, sizeof(BreadValue) * (size_t)new_cap);
    if (!new_items) return 0;
    a->items = new_items;
    a->capacity = new_cap;
    return 1;
}

int bread_array_append(BreadArray* a, BreadValue v) {
    if (!a) return 0;
    if (!bread_array_ensure_cap(a, a->count + 1)) return 0;
    BreadValue cloned = bread_value_clone(v);
    a->items[a->count++] = cloned;
    return 1;
}

BreadValue* bread_array_get(BreadArray* a, int idx) {
    if (!a) return NULL;
    if (idx < 0 || idx >= a->count) return NULL;
    return &a->items[idx];
}

BreadDict* bread_dict_new(void) {
    BreadDict* d = malloc(sizeof(BreadDict));
    if (!d) return NULL;
    d->header.kind = BREAD_OBJ_DICT;
    d->header.refcount = 1;
    d->count = 0;
    d->capacity = 0;
    d->entries = NULL;
    return d;
}

void bread_dict_retain(BreadDict* d) {
    if (d) d->header.refcount++;
}

void bread_dict_release(BreadDict* d) {
    if (!d) return;
    d->header.refcount--;
    if (d->header.refcount > 0) return;

    for (int i = 0; i < d->count; i++) {
        bread_string_release(d->entries[i].key);
        bread_value_release(&d->entries[i].value);
    }
    free(d->entries);
    free(d);
}

static int bread_dict_ensure_cap(BreadDict* d, int need) {
    if (!d) return 0;
    if (d->capacity >= need) return 1;
    int new_cap = d->capacity == 0 ? 4 : d->capacity;
    while (new_cap < need) new_cap *= 2;
    BreadDictEntry* new_entries = realloc(d->entries, sizeof(BreadDictEntry) * (size_t)new_cap);
    if (!new_entries) return 0;
    d->entries = new_entries;
    d->capacity = new_cap;
    return 1;
}

BreadValue* bread_dict_get(BreadDict* d, const char* key) {
    if (!d || !key) return NULL;
    for (int i = 0; i < d->count; i++) {
        if (strcmp(bread_string_cstr(d->entries[i].key), key) == 0) {
            return &d->entries[i].value;
        }
    }
    return NULL;
}

int bread_dict_set(BreadDict* d, const char* key, BreadValue v) {
    if (!d || !key) return 0;

    BreadValue* existing = bread_dict_get(d, key);
    if (existing) {
        bread_value_release(existing);
        *existing = bread_value_clone(v);
        return 1;
    }

    if (!bread_dict_ensure_cap(d, d->count + 1)) return 0;

    d->entries[d->count].key = bread_string_new(key);
    if (!d->entries[d->count].key) return 0;
    d->entries[d->count].value = bread_value_clone(v);
    d->count++;
    return 1;
}

BreadOptional* bread_optional_new_none(void) {
    BreadOptional* o = malloc(sizeof(BreadOptional));
    if (!o) return NULL;
    o->header.kind = BREAD_OBJ_OPTIONAL;
    o->header.refcount = 1;
    o->is_some = 0;
    o->value = bread_value_make_nil();
    return o;
}

BreadOptional* bread_optional_new_some(BreadValue v) {
    BreadOptional* o = malloc(sizeof(BreadOptional));
    if (!o) return NULL;
    o->header.kind = BREAD_OBJ_OPTIONAL;
    o->header.refcount = 1;
    o->is_some = 1;
    o->value = bread_value_clone(v);
    return o;
}

void bread_optional_retain(BreadOptional* o) {
    if (o) o->header.refcount++;
}

void bread_optional_release(BreadOptional* o) {
    if (!o) return;
    o->header.refcount--;
    if (o->header.refcount > 0) return;
    if (o->is_some) {
        bread_value_release(&o->value);
    }
    free(o);
}
