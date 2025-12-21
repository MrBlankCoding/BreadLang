#include "../framework/pbt_framework.h"
#include "core/value.h"
#include "runtime/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Property: Value creation and cleanup should not leak memory
static bool test_value_memory_safety(void) {
    bread_memory_init();
    
    for (int i = 0; i < 1000; i++) {
        BreadValue* val = bread_value_create_int(i);
        if (!val) return false;
        
        if (bread_value_get_int(val) != i) {
            bread_value_free(val);
            return false;
        }
        
        bread_value_free(val);
    }
    
    bread_memory_cleanup();
    return true;
}

// Property: String values should preserve content
static bool test_string_value_preservation(void) {
    bread_memory_init();
    
    const char* test_strings[] = {
        "hello",
        "world",
        "",
        "a very long string that should be preserved exactly",
        "special chars: !@#$%^&*()",
        NULL
    };
    
    for (int i = 0; test_strings[i]; i++) {
        BreadValue* val = bread_value_create_string(test_strings[i]);
        if (!val) return false;
        
        const char* retrieved = bread_value_get_string(val);
        if (strcmp(retrieved, test_strings[i]) != 0) {
            bread_value_free(val);
            return false;
        }
        
        bread_value_free(val);
    }
    
    bread_memory_cleanup();
    return true;
}

// Property: Array values should maintain element order
static bool test_array_value_ordering(void) {
    bread_memory_init();
    
    BreadValue* arr = bread_value_create_array();
    if (!arr) return false;
    
    // Add elements in order
    for (int i = 0; i < 10; i++) {
        BreadValue* elem = bread_value_create_int(i * 2);
        if (!elem) {
            bread_value_free(arr);
            return false;
        }
        bread_value_array_append(arr, elem);
    }
    
    // Verify order is preserved
    for (int i = 0; i < 10; i++) {
        BreadValue* elem = bread_value_array_get(arr, i);
        if (!elem || bread_value_get_int(elem) != i * 2) {
            bread_value_free(arr);
            return false;
        }
    }
    
    bread_value_free(arr);
    bread_memory_cleanup();
    return true;
}

// Property: Type checking should be consistent
static bool test_value_type_consistency(void) {
    bread_memory_init();
    
    BreadValue* int_val = bread_value_create_int(42);
    BreadValue* str_val = bread_value_create_string("test");
    BreadValue* arr_val = bread_value_create_array();
    BreadValue* dict_val = bread_value_create_dict();
    
    bool result = true;
    
    if (bread_value_get_type(int_val) != BREAD_TYPE_INT) result = false;
    if (bread_value_get_type(str_val) != BREAD_TYPE_STRING) result = false;
    if (bread_value_get_type(arr_val) != BREAD_TYPE_ARRAY) result = false;
    if (bread_value_get_type(dict_val) != BREAD_TYPE_DICT) result = false;
    
    bread_value_free(int_val);
    bread_value_free(str_val);
    bread_value_free(arr_val);
    bread_value_free(dict_val);
    
    bread_memory_cleanup();
    return result;
}

int main(void) {
    pbt_init("Value Properties");
    
    pbt_property("Value memory safety", test_value_memory_safety);
    pbt_property("String value preservation", test_string_value_preservation);
    pbt_property("Array value ordering", test_array_value_ordering);
    pbt_property("Value type consistency", test_value_type_consistency);
    
    return pbt_run();
}