#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"

// Built-in range function implementation
BreadArray* bread_range_create(int start, int end, int step) {
    if (step == 0) return NULL;
    if (step > 0 && start >= end) return bread_array_new_typed(TYPE_INT);
    if (step < 0 && start <= end) return bread_array_new_typed(TYPE_INT);
    
    BreadArray* arr = bread_array_new_typed(TYPE_INT);
    if (!arr) return NULL;
    
    for (int i = start; (step > 0) ? (i < end) : (i > end); i += step) {
        BreadValue val;
        bread_value_set_int(&val, i);
        if (!bread_array_append(arr, val)) {
            bread_value_release(&val);
            bread_array_release(arr);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    return arr;
}

BreadArray* bread_range(int n) {
    return bread_range_create(0, n, 1);
}


int bread_array_get_value(BreadArray* a, int idx, BreadValue* out) {
    if (!out) return 0;
    bread_value_set_nil(out);
    
    if (!a) {
        BREAD_ERROR_SET_RUNTIME("Array is null");
        return 0;
    }
    
    if (idx < 0 || idx >= a->count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Array index %d out of bounds (array length: %d)", idx, a->count);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return 0;
    }
    
    *out = bread_value_clone(a->items[idx]);
    return 1;
}

int bread_value_array_get(BreadValue* array_val, int idx, BreadValue* out) {
    if (!array_val || !out) return 0;
    bread_value_set_nil(out);
    
    if (array_val->type != TYPE_ARRAY) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Expected array type for indexing operation");
        return 0;
    }
    
    return bread_array_get_value(array_val->value.array_val, idx, out);
}

int bread_value_array_length(BreadValue* array_val) {
    if (!array_val || array_val->type != TYPE_ARRAY) return 0;
    return bread_array_length(array_val->value.array_val);
}

BreadArray* bread_value_dict_keys(BreadValue* dict_val) {
    if (!dict_val || dict_val->type != TYPE_DICT) return NULL;
    return bread_dict_keys(dict_val->value.dict_val);
}

int bread_value_dict_keys_as_value(BreadValue* dict_val, BreadValue* out) {
    if (!dict_val || dict_val->type != TYPE_DICT || !out) return 0;
    
    BreadArray* keys = bread_dict_keys(dict_val->value.dict_val);
    if (!keys) return 0;
    
    bread_value_set_array(out, keys);
    return 1;
}