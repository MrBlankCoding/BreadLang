#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "runtime/error.h"
#include "compiler/parser/expr.h"

static int bread_string_equals(const BreadString* bs, const char* str) {
    if (!bs || !str) return 0;
    return strcmp(bread_string_cstr(bs), str) == 0;
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
    a->element_type = TYPE_NIL;
    a->items = NULL;
    return a;
}

BreadArray* bread_array_new_typed(VarType element_type) {
    BreadArray* a = (BreadArray*)bread_memory_alloc(sizeof(BreadArray), BREAD_OBJ_ARRAY);
    if (!a) return NULL;
    a->count = 0;
    a->capacity = 0;
    a->element_type = element_type;
    a->items = NULL;
    return a;
}

BreadArray* bread_array_new_with_capacity(int capacity, VarType element_type) {
    BreadArray* a = (BreadArray*)bread_memory_alloc(sizeof(BreadArray), BREAD_OBJ_ARRAY);
    if (!a) return NULL;
    a->count = 0;
    a->capacity = capacity;
    a->element_type = element_type;
    if (capacity > 0) {
        a->items = malloc(sizeof(BreadValue) * capacity);
        if (!a->items) {
            bread_memory_free(a);
            return NULL;
        }
    } else {
        a->items = NULL;
    }
    return a;
}

void bread_array_retain(BreadArray* a) {
    bread_object_retain(a);
}

void bread_array_release(BreadArray* a) {
    if (!a) return;
    
    BreadObjHeader* header = (BreadObjHeader*)a;
    if (header->refcount == 0) return;
    
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
    
    if (a->element_type != TYPE_NIL && a->element_type != v.type) {
        return 0;
    }
    
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
    if (a->element_type != TYPE_NIL && a->element_type != v.type) {
        return 0;
    }
    
    bread_value_release(&a->items[idx]);
    a->items[idx] = bread_value_clone(v);
    return 1;
}

int bread_array_length(BreadArray* a) {
    if (!a) return 0;
    return a->count;
}

BreadArray* bread_array_from_literal(BreadValue* elements, int count) {
    if (!elements || count < 0) return NULL;
    VarType element_type = (count > 0) ? elements[0].type : TYPE_NIL;
    
    for (int i = 1; i < count; i++) {
        if (elements[i].type != element_type) {
            BREAD_ERROR_SET_TYPE_MISMATCH("Array literal elements must have the same type");
            return NULL;
        }
    }
    
    BreadArray* array = bread_array_new_with_capacity(count, element_type);
    if (!array) return NULL;
    for (int i = 0; i < count; i++) {
        array->items[i] = bread_value_clone(elements[i]);
        array->count++;
    }
    
    return array;
}

BreadArray* bread_array_repeating(BreadValue value, int count) {
    if (count < 0) {
        BREAD_ERROR_SET_RUNTIME("Array repeat count cannot be negative");
        return NULL;
    }
    
    BreadArray* array = bread_array_new_with_capacity(count, value.type);
    if (!array) return NULL;
    for (int i = 0; i < count; i++) {
        array->items[i] = bread_value_clone(value);
        array->count++;
    }
    
    return array;
}

BreadValue* bread_array_get_safe(BreadArray* array, int index) {
    if (!array) {
        BREAD_ERROR_SET_RUNTIME("Cannot access element of null array");
        return NULL;
    }
    
    if (index < 0) {
        index = array->count + index;
    }
    if (index < 0 || index >= array->count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Array index %d out of bounds for array of length %d", 
                index, array->count);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return NULL;
    }
    
    return &array->items[index];
}

int bread_array_set_safe(BreadArray* array, int index, BreadValue value) {
    if (!array) {
        BREAD_ERROR_SET_RUNTIME("Cannot set element of null array");
        return 0;
    }
    
    if (index < 0) {
        index = array->count + index;
    }
    
    if (index < 0 || index >= array->count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Array index %d out of bounds for array of length %d", 
                index, array->count);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return 0;
    }
    
    if (array->element_type != TYPE_NIL && array->element_type != value.type) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: cannot assign value of type %d to array of type %d", 
                value.type, array->element_type);
        BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
        return 0;
    }
    
    bread_value_release(&array->items[index]);
    array->items[index] = bread_value_clone(value);
    return 1;
}

int bread_array_negative_index(BreadArray* array, int index) {
    if (!array) return -1;
    
    if (index < 0) {
        return array->count + index;
    }
    return index;
}

int bread_array_insert(BreadArray* array, BreadValue value, int index) {
    if (!array) {
        BREAD_ERROR_SET_RUNTIME("Cannot insert into null array");
        return 0;
    }
    
    if (index < 0) {
        index = array->count + index;
    }
    
    if (index < 0 || index > array->count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Insert index %d out of bounds for array of length %d", 
                index, array->count);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return 0;
    }
    
    if (array->element_type != TYPE_NIL && array->element_type != value.type) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: cannot insert value of type %d into array of type %d", 
                value.type, array->element_type);
        BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
        return 0;
    }
    
    if (array->element_type == TYPE_NIL && array->count == 0) {
        array->element_type = value.type;
    }
    
    if (array->count >= array->capacity) {
        int new_cap = array->capacity == 0 ? 8 : array->capacity * 2;
        BreadValue* new_items = realloc(array->items, sizeof(BreadValue) * new_cap);
        if (!new_items) {
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Failed to allocate memory for array growth");
            return 0;
        }
        array->items = new_items;
        array->capacity = new_cap;
    }
    
    for (int i = array->count; i > index; i--) {
        array->items[i] = array->items[i - 1];
    }
    
    array->items[index] = bread_value_clone(value);
    array->count++;
    
    return 1;
}

BreadValue bread_array_remove_at(BreadArray* array, int index) {
    BreadValue null_value;
    memset(&null_value, 0, sizeof(null_value));
    null_value.type = TYPE_NIL;
    
    if (!array) {
        BREAD_ERROR_SET_RUNTIME("Cannot remove from null array");
        return null_value;
    }
    
    if (index < 0) {
        index = array->count + index;
    }
    
    if (index < 0 || index >= array->count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Remove index %d out of bounds for array of length %d", 
                index, array->count);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return null_value;
    }
    
    BreadValue removed_value = bread_value_clone(array->items[index]);
    bread_value_release(&array->items[index]);
    
    for (int i = index; i < array->count - 1; i++) {
        array->items[i] = array->items[i + 1];
    }
    
    array->count--;
    
    return removed_value;
}

int bread_array_contains(BreadArray* array, BreadValue value) {
    if (!array) return 0;
    
    for (int i = 0; i < array->count; i++) {
        BreadValue* element = &array->items[i];
        if (element->type != value.type) continue;
        
        switch (value.type) {
            case TYPE_INT:
                if (element->value.int_val == value.value.int_val) {
                    return 1;
                }
                break;
            case TYPE_DOUBLE:
                if (element->value.double_val == value.value.double_val) {
                    return 1;
                }
                break;
            case TYPE_BOOL:
                if (element->value.bool_val == value.value.bool_val) {
                    return 1;
                }
                break;
            case TYPE_STRING:
                if (element->value.string_val && value.value.string_val) {
                    const char* elem_str = bread_string_cstr(element->value.string_val);
                    const char* val_str = bread_string_cstr(value.value.string_val);
                    if (strcmp(elem_str, val_str) == 0) {
                        return 1;
                    }
                }
                break;
            case TYPE_NIL:
                return 1; // NIL equals NIL
            default:
                break;
        }
    }
    
    return 0;
}

int bread_array_index_of(BreadArray* array, BreadValue value) {
    if (!array) return -1;
    
    for (int i = 0; i < array->count; i++) {
        BreadValue* element = &array->items[i];
        if (element->type != value.type) continue;
        
        switch (value.type) {
            case TYPE_INT:
                if (element->value.int_val == value.value.int_val) {
                    return i;
                }
                break;
            case TYPE_DOUBLE:
                if (element->value.double_val == value.value.double_val) {
                    return i;
                }
                break;
            case TYPE_BOOL:
                if (element->value.bool_val == value.value.bool_val) {
                    return i;
                }
                break;
            case TYPE_STRING:
                if (element->value.string_val && value.value.string_val) {
                    const char* elem_str = bread_string_cstr(element->value.string_val);
                    const char* val_str = bread_string_cstr(value.value.string_val);
                    if (strcmp(elem_str, val_str) == 0) {
                        return i;
                    }
                }
                break;
            case TYPE_NIL:
                return i; 
            default:
                break;
        }
    }
    
    return -1; // Not found
}

BreadDict* bread_dict_new(void) {
    BreadDict* d = (BreadDict*)bread_memory_alloc(sizeof(BreadDict), BREAD_OBJ_DICT);
    if (!d) return NULL;
    d->count = 0;
    d->capacity = 0;
    d->key_type = TYPE_NIL;   
    d->value_type = TYPE_NIL; 
    d->entries = NULL;
    return d;
}

BreadDict* bread_dict_new_typed(VarType key_type, VarType value_type) {
    BreadDict* d = (BreadDict*)bread_memory_alloc(sizeof(BreadDict), BREAD_OBJ_DICT);
    if (!d) return NULL;
    d->count = 0;
    d->capacity = 0;
    d->key_type = key_type;      
    d->value_type = value_type;
    d->entries = NULL;
    return d;
}

void bread_dict_retain(BreadDict* d) {
    bread_object_retain(d);
}

void bread_dict_release(BreadDict* d) {
    if (!d) return;
    
    BreadObjHeader* header = (BreadObjHeader*)d;
    if (header->refcount == 0) return;  
    
    header->refcount--;
    if (header->refcount == 0) {
        if (d->entries) {
            for (int i = 0; i < d->capacity; i++) {
                if (d->entries[i].is_occupied && !d->entries[i].is_deleted) {
                    bread_value_release(&d->entries[i].key);
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
    BreadValue key_val;
    key_val.type = TYPE_STRING;
    key_val.value.string_val = bread_string_new(key);
    if (d->key_type != TYPE_NIL && d->key_type != TYPE_STRING) {
        bread_string_release(key_val.value.string_val);
        return 0;
    }
    
    if (d->value_type != TYPE_NIL && d->value_type != v.type) {
        bread_string_release(key_val.value.string_val);
        return 0;
    }
    
    if (d->key_type == TYPE_NIL && d->count == 0) {
        d->key_type = TYPE_STRING;
    }
    if (d->value_type == TYPE_NIL && d->count == 0) {
        d->value_type = v.type;
    }
    
    for (int i = 0; i < d->capacity; i++) {
        if (d->entries[i].is_occupied && !d->entries[i].is_deleted) {
            if (d->entries[i].key.type == TYPE_STRING && 
                bread_string_equals(d->entries[i].key.value.string_val, key)) {
                bread_value_release(&d->entries[i].value);
                d->entries[i].value = bread_value_clone(v);
                bread_string_release(key_val.value.string_val);
                return 1;
            }
        }
    }
    
    if (d->count >= d->capacity * 0.75) {
        int new_capacity = d->capacity == 0 ? 8 : d->capacity * 2;
        BreadDictEntry* new_entries = calloc(new_capacity, sizeof(BreadDictEntry));
        if (!new_entries) {
            bread_string_release(key_val.value.string_val);
            return 0;
        }
        
        BreadDictEntry* old_entries = d->entries;
        int old_capacity = d->capacity;
        d->entries = new_entries;
        d->capacity = new_capacity;
        d->count = 0;
        
        for (int i = 0; i < old_capacity; i++) {
            if (old_entries[i].is_occupied && !old_entries[i].is_deleted) {
                // Find slot in new table
                int slot = -1;
                for (int j = 0; j < new_capacity; j++) {
                    if (!d->entries[j].is_occupied) {
                        slot = j;
                        break;
                    }
                }
                if (slot >= 0) {
                    d->entries[slot] = old_entries[i];
                    d->count++;
                }
            }
        }
        
        if (old_entries) free(old_entries);
    }
    
    int slot = -1;
    for (int i = 0; i < d->capacity; i++) {
        if (!d->entries[i].is_occupied || d->entries[i].is_deleted) {
            slot = i;
            break;
        }
    }
    
    if (slot >= 0) {
        d->entries[slot].key = key_val;
        d->entries[slot].value = bread_value_clone(v);
        d->entries[slot].is_occupied = 1;
        d->entries[slot].is_deleted = 0;
        d->count++;
        return 1;
    }
    
    bread_string_release(key_val.value.string_val);
    return 0;
}

BreadValue* bread_dict_get(BreadDict* d, const char* key) {
    if (!d || !key) return NULL;
    
    for (int i = 0; i < d->capacity; i++) {
        if (d->entries[i].is_occupied && !d->entries[i].is_deleted) {
            if (d->entries[i].key.type == TYPE_STRING && 
                bread_string_equals(d->entries[i].key.value.string_val, key)) {
                return &d->entries[i].value;
            }
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
    o->value = bread_value_clone(v);
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
