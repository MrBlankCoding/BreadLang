#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "compiler/parser/expr.h"

static int bread_string_equals(const BreadString* bs, const char* str) {
    if (!bs || !str) return 0;
    const char* bs_str = ((const char*)bs) + sizeof(BreadObjHeader);
    return strcmp(bs_str, str) == 0;
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
    BreadArray* a = (BreadArray*)bread_memory_alloc(sizeof(BreadArray), BREAD_OBJ_ARRAY);
    if (!a) return NULL;
    a->count = 0;
    a->capacity = 0;
    a->element_type = TYPE_NIL;  // No type constraint by default
    a->items = NULL;
    return a;
}

BreadArray* bread_array_new_typed(VarType element_type) {
    BreadArray* a = (BreadArray*)bread_memory_alloc(sizeof(BreadArray), BREAD_OBJ_ARRAY);
    if (!a) return NULL;
    a->count = 0;
    a->capacity = 0;
    a->element_type = element_type;  // Set type constraint
    a->items = NULL;
    return a;
}

void bread_array_retain(BreadArray* a) {
    bread_object_retain(a);
}

void bread_array_release(BreadArray* a) {
    if (!a) return;
    
    BreadObjHeader* header = (BreadObjHeader*)a;
    if (header->refcount == 0) return;  // Already freed
    
    header->refcount--;
    if (header->refcount == 0) {
        if (a->items) {
            for (int i = 0; i < a->count; i++) {
                bread_value_release(&a->items[i]);
            }
            free(a->items);
        }
        bread_memory_free(a);
    }
}

int bread_array_append(BreadArray* a, BreadValue v) {
    if (!a) return 0;
    
    // Type constraint checking
    if (a->element_type != TYPE_NIL && a->element_type != v.type) {
        return 0;  // Type mismatch
    }
    
    // Set element type from first element if not already set
    if (a->element_type == TYPE_NIL && a->count == 0) {
        a->element_type = v.type;
    }
    
    if (a->count >= a->capacity) {
        int new_cap = a->capacity == 0 ? 8 : a->capacity * 2;
        BreadValue* new_items = realloc(a->items, sizeof(BreadValue) * new_cap);
        if (!new_items) return 0;
        a->items = new_items;
        a->capacity = new_cap;
    }
    a->items[a->count++] = bread_value_clone(v);
    return 1;
}

BreadValue* bread_array_get(BreadArray* a, int idx) {
    if (!a || idx < 0 || idx >= a->count) return NULL;
    return &a->items[idx];
}

int bread_array_set(BreadArray* a, int idx, BreadValue v) {
    if (!a || idx < 0 || idx >= a->count) return 0;
    
    // Type constraint checking
    if (a->element_type != TYPE_NIL && a->element_type != v.type) {
        return 0;  // Type mismatch
    }
    
    // Release old value and set new one
    bread_value_release(&a->items[idx]);
    a->items[idx] = bread_value_clone(v);
    return 1;
}

int bread_array_length(BreadArray* a) {
    if (!a) return 0;
    return a->count;
}

BreadDict* bread_dict_new(void) {
    BreadDict* d = (BreadDict*)bread_memory_alloc(sizeof(BreadDict), BREAD_OBJ_DICT);
    if (!d) return NULL;
    d->count = 0;
    d->capacity = 0;
    d->entries = NULL;
    return d;
}

void bread_dict_retain(BreadDict* d) {
    bread_object_retain(d);
}

void bread_dict_release(BreadDict* d) {
    if (!d) return;
    
    BreadObjHeader* header = (BreadObjHeader*)d;
    if (header->refcount == 0) return;  // Already freed
    
    header->refcount--;
    if (header->refcount == 0) {
        if (d->entries) {
            for (int i = 0; i < d->capacity; i++) {
                if (d->entries[i].key) {
                    bread_string_release(d->entries[i].key);
                    bread_value_release(&d->entries[i].value);
                }
            }
            free(d->entries);
        }
        bread_memory_free(d);
    }
}

int bread_dict_set(BreadDict* d, const char* key, BreadValue v) {
    if (!d || !key) return 0;
    
    // Check if key already exists and update it
    for (int i = 0; i < d->count; i++) {
        if (bread_string_equals(d->entries[i].key, key)) {
            bread_value_release(&d->entries[i].value);
            d->entries[i].value = bread_value_clone(v);
            return 1;
        }
    }
    
    // Resize if needed
    if (d->count >= d->capacity) {
        int new_capacity = d->capacity == 0 ? 8 : d->capacity * 2;
        BreadDictEntry* new_entries = realloc(d->entries, new_capacity * sizeof(BreadDictEntry));
        if (!new_entries) return 0;
        d->entries = new_entries;
        d->capacity = new_capacity;
    }
    
    // Add new entry
    d->entries[d->count].key = bread_string_new(key);
    d->entries[d->count].value = bread_value_clone(v);
    d->count++;
    
    return 1;
}

BreadValue* bread_dict_get(BreadDict* d, const char* key) {
    if (!d || !key) return NULL;
    
    for (int i = 0; i < d->count; i++) {
        if (bread_string_equals(d->entries[i].key, key)) {
            return &d->entries[i].value;
        }
    }
    
    return NULL;
}

BreadOptional* bread_optional_new_none(void) {
    BreadOptional* o = (BreadOptional*)bread_memory_alloc(sizeof(BreadOptional), BREAD_OBJ_OPTIONAL);
    if (!o) return NULL;
    o->is_some = 0;
    memset(&o->value, 0, sizeof(BreadValue));
    return o;
}

BreadOptional* bread_optional_new_some(BreadValue v) {
    BreadOptional* o = (BreadOptional*)bread_memory_alloc(sizeof(BreadOptional), BREAD_OBJ_OPTIONAL);
    if (!o) return NULL;
    o->is_some = 1;
    o->value = v;
    return o;
}

void bread_optional_retain(BreadOptional* o) {
    bread_object_retain(o);
}

void bread_optional_release(BreadOptional* o) {
    if (!o) return;
    
    BreadObjHeader* header = (BreadObjHeader*)o;
    if (header->refcount == 0) return;  // Already freed
    
    header->refcount--;
    if (header->refcount == 0) {
        if (o->is_some) {
            bread_value_release(&o->value);
        }
        bread_memory_free(o);
    }
}

int bread_value_get_int(BreadValue* v) {
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
