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
        case TYPE_STRUCT:
            out.value.struct_val = v.value.struct_val;
            bread_struct_retain(out.value.struct_val);
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

BreadDict* bread_dict_new_with_capacity(int capacity, VarType key_type, VarType value_type) {
    BreadDict* d = (BreadDict*)bread_memory_alloc(sizeof(BreadDict), BREAD_OBJ_DICT);
    if (!d) return NULL;
    d->count = 0;
    d->capacity = capacity;
    d->key_type = key_type;      
    d->value_type = value_type;
    if (capacity > 0) {
        d->entries = calloc(capacity, sizeof(BreadDictEntry));
        if (!d->entries) {
            bread_memory_free(d);
            return NULL;
        }
    } else {
        d->entries = NULL;
    }
    return d;
}

uint32_t bread_dict_hash_key(BreadValue key) {
    switch (key.type) {
        case TYPE_INT: {
            uint32_t x = (uint32_t)key.value.int_val;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            return x;
        }
        case TYPE_DOUBLE: {
            union { double d; uint64_t i; } u;
            u.d = key.value.double_val;
            uint32_t h1 = (uint32_t)(u.i & 0xFFFFFFFF);
            uint32_t h2 = (uint32_t)(u.i >> 32);
            return h1 ^ h2;
        }
        case TYPE_STRING: {
            if (!key.value.string_val) return 0;
            const char* str = bread_string_cstr(key.value.string_val);
            size_t len = bread_string_len(key.value.string_val);
            
            // FNV-1a hash
            uint32_t hash = 2166136261u;
            for (size_t i = 0; i < len; i++) {
                hash ^= (uint32_t)str[i];
                hash *= 16777619u;
            }
            return hash;
        }
        case TYPE_BOOL:
            return key.value.bool_val ? 1 : 0;
        case TYPE_NIL:
            return 0;
        default:
            return 0;
    }
}

int bread_dict_find_slot(BreadDict* dict, BreadValue key) {
    if (!dict || dict->capacity == 0) return -1;
    
    uint32_t hash = bread_dict_hash_key(key);
    int start_slot = hash % dict->capacity;
    int slot = start_slot;
    
    do {
        BreadDictEntry* entry = &dict->entries[slot];
        if (!entry->is_occupied) {
            return slot;
        }
        
        if (entry->is_deleted) {
            return slot;
        }
        
        if (entry->key.type == key.type) {
            int keys_equal = 0;
            switch (key.type) {
                case TYPE_INT:
                    keys_equal = (entry->key.value.int_val == key.value.int_val);
                    break;
                case TYPE_DOUBLE:
                    keys_equal = (entry->key.value.double_val == key.value.double_val);
                    break;
                case TYPE_BOOL:
                    keys_equal = (entry->key.value.bool_val == key.value.bool_val);
                    break;
                case TYPE_STRING:
                    if (entry->key.value.string_val && key.value.string_val) {
                        const char* entry_str = bread_string_cstr(entry->key.value.string_val);
                        const char* key_str = bread_string_cstr(key.value.string_val);
                        keys_equal = (strcmp(entry_str, key_str) == 0);
                    }
                    break;
                case TYPE_NIL:
                    keys_equal = 1;
                    break;
                default:
                    keys_equal = 0;
                    break;
            }
            
            if (keys_equal) {
                return slot;
            }
        }
        
        slot = (slot + 1) % dict->capacity;
    } while (slot != start_slot);
    
    return -1; // Table is full
}

BreadDict* bread_dict_new_typed(VarType key_type, VarType value_type) {
    return bread_dict_new_with_capacity(0, key_type, value_type);
}

BreadDict* bread_dict_from_literal(BreadDictEntry* entries, int count) {
    if (!entries || count < 0) return NULL;
    VarType key_type = TYPE_NIL;
    VarType value_type = TYPE_NIL;
    
    if (count > 0) {
        key_type = entries[0].key.type;
        value_type = entries[0].value.type;
        for (int i = 1; i < count; i++) {
            if (entries[i].key.type != key_type) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary literal keys must have the same type");
                return NULL;
            }
            if (entries[i].value.type != value_type) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary literal values must have the same type");
                return NULL;
            }
        }
    }
    
    // Create dictionary with appropriate capacity (load factor < 0.75)
    int capacity = count > 0 ? (int)(count / 0.75) + 1 : 8;
    if (capacity < 8) capacity = 8;
    
    BreadDict* dict = bread_dict_new_with_capacity(capacity, key_type, value_type);
    if (!dict) return NULL;
    
    for (int i = 0; i < count; i++) {
        int slot = bread_dict_find_slot(dict, entries[i].key);
        if (slot >= 0) {
            dict->entries[slot].key = bread_value_clone(entries[i].key);
            dict->entries[slot].value = bread_value_clone(entries[i].value);
            dict->entries[slot].is_occupied = 1;
            dict->entries[slot].is_deleted = 0;
            dict->count++;
        }
    }
    
    return dict;
}

BreadValue* bread_dict_get_safe(BreadDict* dict, BreadValue key) {
    if (!dict) {
        BREAD_ERROR_SET_RUNTIME("Cannot access element of null dictionary");
        return NULL;
    }
    
    if (dict->key_type != TYPE_NIL && dict->key_type != key.type) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: cannot use key of type %d in dictionary with key type %d", 
                key.type, dict->key_type);
        BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
        return NULL;
    }
    
    if (dict->capacity == 0) {
        return NULL; // Empty dictionary
    }
    
    int slot = bread_dict_find_slot(dict, key);
    if (slot >= 0 && dict->entries[slot].is_occupied && !dict->entries[slot].is_deleted) {
        return &dict->entries[slot].value;
    }
    
    return NULL; // Key not found
}

BreadValue bread_dict_get_with_default(BreadDict* dict, BreadValue key, BreadValue default_val) {
    BreadValue* found = bread_dict_get_safe(dict, key);
    if (found) {
        return bread_value_clone(*found);
    }
    return bread_value_clone(default_val);
}

int bread_dict_set_safe(BreadDict* dict, BreadValue key, BreadValue value) {
    if (!dict) {
        BREAD_ERROR_SET_RUNTIME("Cannot set element of null dictionary");
        return 0;
    }
    
    if (dict->key_type != TYPE_NIL && dict->key_type != key.type) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: cannot use key of type %d in dictionary with key type %d", 
                key.type, dict->key_type);
        BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
        return 0;
    }
    
    if (dict->value_type != TYPE_NIL && dict->value_type != value.type) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: cannot assign value of type %d to dictionary with value type %d", 
                value.type, dict->value_type);
        BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
        return 0;
    }
    
    if (dict->key_type == TYPE_NIL && dict->count == 0) {
        dict->key_type = key.type;
    }
    if (dict->value_type == TYPE_NIL && dict->count == 0) {
        dict->value_type = value.type;
    }
    
    // Resize if load factor would exceed 0.75
    if (dict->capacity == 0 || (double)(dict->count + 1) / dict->capacity > 0.75) {
        int new_capacity = dict->capacity == 0 ? 8 : dict->capacity * 2;
        bread_dict_resize(dict, new_capacity);
    }
    
    int slot = bread_dict_find_slot(dict, key);
    if (slot < 0) {
        BREAD_ERROR_SET_RUNTIME("Dictionary is full and cannot be resized");
        return 0;
    }
    
    if (dict->entries[slot].is_occupied && !dict->entries[slot].is_deleted) {
        bread_value_release(&dict->entries[slot].value);
        dict->entries[slot].value = bread_value_clone(value);
        return 1;
    }
    
    dict->entries[slot].key = bread_value_clone(key);
    dict->entries[slot].value = bread_value_clone(value);
    dict->entries[slot].is_occupied = 1;
    dict->entries[slot].is_deleted = 0;
    dict->count++;
    
    return 1;
}

int bread_dict_count(BreadDict* dict) {
    if (!dict) return 0;
    return dict->count;
}

void bread_dict_resize(BreadDict* dict, int new_capacity) {
    if (!dict || new_capacity <= 0) return;
    
    BreadDictEntry* old_entries = dict->entries;
    int old_capacity = dict->capacity;
    
    // Allocate new table
    dict->entries = calloc(new_capacity, sizeof(BreadDictEntry));
    if (!dict->entries) {
        dict->entries = old_entries; // Restore on failure
        return;
    }
    
    dict->capacity = new_capacity;
    dict->count = 0;
    
    for (int i = 0; i < old_capacity; i++) {
        if (old_entries[i].is_occupied && !old_entries[i].is_deleted) {
            int slot = bread_dict_find_slot(dict, old_entries[i].key);
            if (slot >= 0) {
                dict->entries[slot] = old_entries[i];
                dict->count++;
            }
        }
    }
    
    if (old_entries) {
        free(old_entries);
    }
}

BreadArray* bread_dict_keys(BreadDict* dict) {
    if (!dict) return NULL;
    
    BreadArray* keys_array = bread_array_new_typed(dict->key_type);
    if (!keys_array) return NULL;
    
    for (int i = 0; i < dict->capacity; i++) {
        if (dict->entries[i].is_occupied && !dict->entries[i].is_deleted) {
            if (!bread_array_append(keys_array, dict->entries[i].key)) {
                bread_array_release(keys_array);
                return NULL;
            }
        }
    }
    
    return keys_array;
}

BreadArray* bread_dict_values(BreadDict* dict) {
    if (!dict) return NULL;
    
    BreadArray* values_array = bread_array_new_typed(dict->value_type);
    if (!values_array) return NULL;
    
    for (int i = 0; i < dict->capacity; i++) {
        if (dict->entries[i].is_occupied && !dict->entries[i].is_deleted) {
            if (!bread_array_append(values_array, dict->entries[i].value)) {
                bread_array_release(values_array);
                return NULL;
            }
        }
    }
    
    return values_array;
}

int bread_dict_contains_key(BreadDict* dict, BreadValue key) {
    if (!dict) return 0;
    
    if (dict->key_type != TYPE_NIL && dict->key_type != key.type) {
        return 0; // Type mismatch
    }
    
    if (dict->capacity == 0) {
        return 0; // Empty dictionary
    }
    
    int slot = bread_dict_find_slot(dict, key);
    return (slot >= 0 && dict->entries[slot].is_occupied && !dict->entries[slot].is_deleted);
}

BreadValue bread_dict_remove(BreadDict* dict, BreadValue key) {
    BreadValue null_value;
    memset(&null_value, 0, sizeof(null_value));
    null_value.type = TYPE_NIL;
    
    if (!dict) {
        BREAD_ERROR_SET_RUNTIME("Cannot remove from null dictionary");
        return null_value;
    }
    
    if (dict->key_type != TYPE_NIL && dict->key_type != key.type) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: cannot use key of type %d in dictionary with key type %d", 
                key.type, dict->key_type);
        BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
        return null_value;
    }
    
    if (dict->capacity == 0) {
        return null_value; // Empty dictionary
    }
    
    int slot = bread_dict_find_slot(dict, key);
    if (slot >= 0 && dict->entries[slot].is_occupied && !dict->entries[slot].is_deleted) {
        // Found the key, remove it
        BreadValue removed_value = bread_value_clone(dict->entries[slot].value);
        
        // Clean up the entry
        bread_value_release(&dict->entries[slot].key);
        bread_value_release(&dict->entries[slot].value);
        dict->entries[slot].is_occupied = 1; // Keep occupied for tombstone
        dict->entries[slot].is_deleted = 1;  // Mark as deleted
        dict->count--;
        
        return removed_value;
    }
    
    return null_value; // Key not found
}

void bread_dict_clear(BreadDict* dict) {
    if (!dict) return;
    
    // Release all entries
    for (int i = 0; i < dict->capacity; i++) {
        if (dict->entries[i].is_occupied && !dict->entries[i].is_deleted) {
            bread_value_release(&dict->entries[i].key);
            bread_value_release(&dict->entries[i].value);
        }
        dict->entries[i].is_occupied = 0;
        dict->entries[i].is_deleted = 0;
    }
    
    dict->count = 0;
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

// Struct implementation
BreadStruct* bread_struct_new(const char* type_name, int field_count, char** field_names) {
    if (!type_name || field_count < 0 || (field_count > 0 && !field_names)) {
        return NULL;
    }
    
    BreadStruct* s = (BreadStruct*)bread_memory_alloc(sizeof(BreadStruct), BREAD_OBJ_STRUCT);
    if (!s) return NULL;
    
    s->type_name = strdup(type_name);
    s->field_count = field_count;
    
    if (field_count > 0) {
        s->field_names = malloc(field_count * sizeof(char*));
        s->field_values = malloc(field_count * sizeof(BreadValue));
        
        if (!s->field_names || !s->field_values) {
            free(s->type_name);
            free(s->field_names);
            free(s->field_values);
            bread_memory_free(s);
            return NULL;
        }
        
        for (int i = 0; i < field_count; i++) {
            s->field_names[i] = strdup(field_names[i]);
            memset(&s->field_values[i], 0, sizeof(BreadValue));
            s->field_values[i].type = TYPE_NIL;
        }
    } else {
        s->field_names = NULL;
        s->field_values = NULL;
    }
    
    return s;
}

void bread_struct_set_field(BreadStruct* s, const char* field_name, BreadValue value) {
    if (!s || !field_name) return;
    
    int index = bread_struct_find_field_index(s, field_name);
    if (index >= 0) {
        bread_value_release(&s->field_values[index]);
        s->field_values[index] = bread_value_clone(value);
    }
}

void bread_struct_set_field_value_ptr(BreadStruct* s, const char* field_name, const BreadValue* value) {
    if (!value) return;
    bread_struct_set_field(s, field_name, *value);
}

BreadValue* bread_struct_get_field(BreadStruct* s, const char* field_name) {
    if (!s || !field_name) return NULL;
    
    int index = bread_struct_find_field_index(s, field_name);
    if (index >= 0) {
        return &s->field_values[index];
    }
    
    return NULL;
}

int bread_struct_find_field_index(BreadStruct* s, const char* field_name) {
    if (!s || !field_name) return -1;
    
    for (int i = 0; i < s->field_count; i++) {
        if (strcmp(s->field_names[i], field_name) == 0) {
            return i;
        }
    }
    
    return -1;
}

void bread_struct_retain(BreadStruct* s) {
    bread_object_retain(s);
}

void bread_struct_release(BreadStruct* s) {
    if (!s) return;
    
    BreadObjHeader* header = (BreadObjHeader*)s;
    if (header->refcount == 0) return;
    
    header->refcount--;
    if (header->refcount == 0) {
        free(s->type_name);
        
        if (s->field_names) {
            for (int i = 0; i < s->field_count; i++) {
                free(s->field_names[i]);
            }
            free(s->field_names);
        }
        
        if (s->field_values) {
            for (int i = 0; i < s->field_count; i++) {
                bread_value_release(&s->field_values[i]);
            }
            free(s->field_values);
        }
        
        bread_memory_free(s);
    }
}
