#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "runtime/error.h"

static int bread_string_equals(const BreadString* bs, const char* str) {
    if (!bs || !str) return 0;
    return strcmp(bread_string_cstr(bs), str) == 0;
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