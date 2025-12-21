#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../framework/pbt_framework.h"
#include "../../include/core/var.h"
#include "../../include/core/value.h"
#include "../../include/runtime/runtime.h"
#include "../../include/runtime/error.h"

// Test data structure for Property 1: Array Literal Creation Preserves Elements
typedef struct {
    VarType element_type;
    int* int_values;
    char** string_values;
    int count;
} ArrayLiteralData;

// Test data structure for Property 2: Array Constructor Creates Correct Repetitions
typedef struct {
    BreadValue value;
    int count;
} ArrayRepeatingData;

// Test data structure for Property 3: Array Indexing Round Trip
typedef struct {
    BreadArray* array;
    int index;
    BreadValue new_value;
} ArrayIndexingData;

// Test data structure for Property 4: Array Negative Indexing Equivalence
typedef struct {
    BreadArray* array;
    int negative_index;
} ArrayNegativeIndexData;

// Test data structure for Property 5: Array Bounds Checking
typedef struct {
    BreadArray* array;
    int out_of_bounds_index;
} ArrayBoundsData;

// Test data structure for Property 6: Array Append Increases Length
typedef struct {
    BreadArray* array;
    BreadValue element;
} ArrayAppendData;

// Test data structure for Property 7: Array Insert Preserves Order
typedef struct {
    BreadArray* array;
    BreadValue element;
    int insert_index;
} ArrayInsertData;

// Test data structure for Property 8: Array Remove Decreases Length
typedef struct {
    BreadArray* array;
    int remove_index;
} ArrayRemoveData;

// Test data structure for Property 9: Array Contains Correctness
typedef struct {
    BreadArray* array;
    BreadValue search_element;
    int should_contain;
} ArrayContainsData;

// Test data structure for Property 10: Array IndexOf Correctness
typedef struct {
    BreadArray* array;
    BreadValue search_element;
    int expected_index;
} ArrayIndexOfData;

// Test data structure for Property 11: Dictionary Literal Creation Preserves Pairs
typedef struct {
    VarType key_type;
    VarType value_type;
    BreadValue* keys;
    BreadValue* values;
    int count;
} DictLiteralData;

// Test data structure for Property 12: Dictionary Access Round Trip
typedef struct {
    BreadDict* dict;
    BreadValue key;
    BreadValue new_value;
} DictAccessData;

// Test data structure for Property 13: Dictionary Get With Default
typedef struct {
    BreadDict* dict;
    BreadValue existing_key;
    BreadValue missing_key;
    BreadValue default_value;
} DictGetDefaultData;

// Test data structure for Property 14: Dictionary Keys and Values Consistency
typedef struct {
    BreadDict* dict;
} DictKeysValuesData;

// Test data structure for Property 15: Dictionary Remove Decreases Count
typedef struct {
    BreadDict* dict;
    BreadValue key_to_remove;
} DictRemoveData;

// Test data structure for Property 16: Dictionary Clear Empties Collection
typedef struct {
    BreadDict* dict;
} DictClearData;

// Initialize runtime for tests
static void init_runtime() {
    static int initialized = 0;
    if (!initialized) {
        bread_string_intern_init();
        initialized = 1;
    }
}

// Generator for Property 1: Array Literal Creation Preserves Elements
void* generate_array_literal_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayLiteralData* data = malloc(sizeof(ArrayLiteralData));
    if (!data) return NULL;
    
    // Generate either int or string arrays
    data->element_type = (pbt_random_uint32(gen) % 2 == 0) ? TYPE_INT : TYPE_STRING;
    data->count = pbt_random_int(gen, 1, 10);
    data->int_values = NULL;
    data->string_values = NULL;
    
    if (data->element_type == TYPE_INT) {
        data->int_values = malloc(sizeof(int) * data->count);
        if (!data->int_values) {
            free(data);
            return NULL;
        }
        for (int i = 0; i < data->count; i++) {
            data->int_values[i] = pbt_random_int(gen, -100, 100);
        }
    } else {
        data->string_values = malloc(sizeof(char*) * data->count);
        if (!data->string_values) {
            free(data);
            return NULL;
        }
        for (int i = 0; i < data->count; i++) {
            data->string_values[i] = pbt_random_string(gen, 10);
            if (!data->string_values[i]) {
                // Cleanup on failure
                for (int j = 0; j < i; j++) {
                    free(data->string_values[j]);
                }
                free(data->string_values);
                free(data);
                return NULL;
            }
        }
    }
    
    return data;
}

// Generator for Property 2: Array Constructor Creates Correct Repetitions
void* generate_array_repeating_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayRepeatingData* data = malloc(sizeof(ArrayRepeatingData));
    if (!data) return NULL;
    
    // Generate a random value and count
    VarType value_type = (pbt_random_uint32(gen) % 2 == 0) ? TYPE_INT : TYPE_STRING;
    data->count = pbt_random_int(gen, 0, 10);
    
    if (value_type == TYPE_INT) {
        bread_value_set_int(&data->value, pbt_random_int(gen, -100, 100));
    } else {
        char* str = pbt_random_string(gen, 10);
        if (!str) {
            free(data);
            return NULL;
        }
        bread_value_set_string(&data->value, str);
        free(str);
    }
    
    return data;
}

// Generator for Property 3: Array Indexing Round Trip
void* generate_array_indexing_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayIndexingData* data = malloc(sizeof(ArrayIndexingData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with random elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, pbt_random_int(gen, 0, 100));
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose a valid index and new value
    data->index = pbt_random_int(gen, 0, count - 1);
    bread_value_set_int(&data->new_value, pbt_random_int(gen, 200, 300));
    
    return data;
}

// Generator for Property 4: Array Negative Indexing Equivalence
void* generate_array_negative_index_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayNegativeIndexData* data = malloc(sizeof(ArrayNegativeIndexData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with random elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, pbt_random_int(gen, 0, 100));
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose a negative index that should be valid
    data->negative_index = pbt_random_int(gen, -count, -1);
    
    return data;
}

// Generator for Property 5: Array Bounds Checking
void* generate_array_bounds_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayBoundsData* data = malloc(sizeof(ArrayBoundsData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with random elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, pbt_random_int(gen, 0, 100));
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose an out-of-bounds index (either too high or too low)
    if (pbt_random_uint32(gen) % 2 == 0) {
        data->out_of_bounds_index = count + pbt_random_int(gen, 1, 5);
    } else {
        data->out_of_bounds_index = -(count + pbt_random_int(gen, 1, 5));
    }
    
    return data;
}

// Generator for Property 6: Array Append Increases Length
void* generate_array_append_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayAppendData* data = malloc(sizeof(ArrayAppendData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with random elements
    int count = pbt_random_int(gen, 0, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, pbt_random_int(gen, 0, 100));
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Create element to append
    bread_value_set_int(&data->element, pbt_random_int(gen, 200, 300));
    
    return data;
}

// Generator for Property 7: Array Insert Preserves Order
void* generate_array_insert_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayInsertData* data = malloc(sizeof(ArrayInsertData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with random elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, i * 10); // Use predictable values for order checking
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose insert position and element
    data->insert_index = pbt_random_int(gen, 0, count);
    bread_value_set_int(&data->element, 999); // Distinctive value
    
    return data;
}

// Generator for Property 8: Array Remove Decreases Length
void* generate_array_remove_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayRemoveData* data = malloc(sizeof(ArrayRemoveData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with random elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, pbt_random_int(gen, 0, 100));
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose valid remove index
    data->remove_index = pbt_random_int(gen, 0, count - 1);
    
    return data;
}

// Generator for Property 9: Array Contains Correctness
void* generate_array_contains_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayContainsData* data = malloc(sizeof(ArrayContainsData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with known elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, i * 10);
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose element to search for
    data->should_contain = (pbt_random_uint32(gen) % 2 == 0);
    if (data->should_contain) {
        // Pick an element that exists
        int existing_index = pbt_random_int(gen, 0, count - 1);
        bread_value_set_int(&data->search_element, existing_index * 10);
    } else {
        // Pick an element that doesn't exist
        bread_value_set_int(&data->search_element, 999);
    }
    
    return data;
}

// Generator for Property 10: Array IndexOf Correctness
void* generate_array_index_of_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayIndexOfData* data = malloc(sizeof(ArrayIndexOfData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
    // Create array with known elements
    int count = pbt_random_int(gen, 1, 10);
    for (int i = 0; i < count; i++) {
        BreadValue val;
        bread_value_set_int(&val, i * 10);
        if (!bread_array_append(data->array, val)) {
            bread_value_release(&val);
            bread_array_release(data->array);
            free(data);
            return NULL;
        }
        bread_value_release(&val);
    }
    
    // Choose element to search for
    if (pbt_random_uint32(gen) % 2 == 0) {
        // Pick an element that exists
        data->expected_index = pbt_random_int(gen, 0, count - 1);
        bread_value_set_int(&data->search_element, data->expected_index * 10);
    } else {
        // Pick an element that doesn't exist
        data->expected_index = -1;
        bread_value_set_int(&data->search_element, 999);
    }
    
    return data;
}

// Generator for Property 11: Dictionary Literal Creation Preserves Pairs
void* generate_dict_literal_test_data(PBTGenerator* gen) {
    init_runtime();
    
    DictLiteralData* data = malloc(sizeof(DictLiteralData));
    if (!data) return NULL;
    
    // Generate either int->string or string->int dictionaries
    data->key_type = (pbt_random_uint32(gen) % 2 == 0) ? TYPE_INT : TYPE_STRING;
    data->value_type = (data->key_type == TYPE_INT) ? TYPE_STRING : TYPE_INT;
    data->count = pbt_random_int(gen, 1, 5);
    
    data->keys = malloc(sizeof(BreadValue) * data->count);
    data->values = malloc(sizeof(BreadValue) * data->count);
    if (!data->keys || !data->values) {
        if (data->keys) free(data->keys);
        if (data->values) free(data->values);
        free(data);
        return NULL;
    }
    
    // Generate key-value pairs
    for (int i = 0; i < data->count; i++) {
        if (data->key_type == TYPE_INT) {
            bread_value_set_int(&data->keys[i], i * 10); // Unique keys
            char* str = pbt_random_string(gen, 8);
            if (!str) {
                // Cleanup on failure
                for (int j = 0; j < i; j++) {
                    bread_value_release(&data->keys[j]);
                    bread_value_release(&data->values[j]);
                }
                free(data->keys);
                free(data->values);
                free(data);
                return NULL;
            }
            bread_value_set_string(&data->values[i], str);
            free(str);
        } else {
            char key_str[16];
            snprintf(key_str, sizeof(key_str), "key%d", i);
            bread_value_set_string(&data->keys[i], key_str);
            bread_value_set_int(&data->values[i], pbt_random_int(gen, 0, 100));
        }
    }
    
    return data;
}

// Generator for Property 12: Dictionary Access Round Trip
void* generate_dict_access_test_data(PBTGenerator* gen) {
    init_runtime();
    
    DictAccessData* data = malloc(sizeof(DictAccessData));
    if (!data) return NULL;
    
    data->dict = bread_dict_new_typed(TYPE_INT, TYPE_STRING);
    if (!data->dict) {
        free(data);
        return NULL;
    }
    
    // Add some initial entries
    int count = pbt_random_int(gen, 1, 5);
    for (int i = 0; i < count; i++) {
        BreadValue key, value;
        bread_value_set_int(&key, i * 10);
        char* str = pbt_random_string(gen, 8);
        if (!str) {
            bread_value_release(&key);
            bread_dict_release(data->dict);
            free(data);
            return NULL;
        }
        bread_value_set_string(&value, str);
        free(str);
        
        if (!bread_dict_set_safe(data->dict, key, value)) {
            bread_value_release(&key);
            bread_value_release(&value);
            bread_dict_release(data->dict);
            free(data);
            return NULL;
        }
        bread_value_release(&key);
        bread_value_release(&value);
    }
    
    // Choose a key to test with and new value
    bread_value_set_int(&data->key, pbt_random_int(gen, 0, (count - 1) * 10));
    char* new_str = pbt_random_string(gen, 10);
    if (!new_str) {
        bread_dict_release(data->dict);
        free(data);
        return NULL;
    }
    bread_value_set_string(&data->new_value, new_str);
    free(new_str);
    
    return data;
}

// Generator for Property 13: Dictionary Get With Default
void* generate_dict_get_default_test_data(PBTGenerator* gen) {
    init_runtime();
    
    DictGetDefaultData* data = malloc(sizeof(DictGetDefaultData));
    if (!data) return NULL;
    
    data->dict = bread_dict_new_typed(TYPE_STRING, TYPE_INT);
    if (!data->dict) {
        free(data);
        return NULL;
    }
    
    // Add one entry
    bread_value_set_string(&data->existing_key, "existing");
    BreadValue existing_value;
    bread_value_set_int(&existing_value, 42);
    
    if (!bread_dict_set_safe(data->dict, data->existing_key, existing_value)) {
        bread_value_release(&existing_value);
        bread_dict_release(data->dict);
        free(data);
        return NULL;
    }
    bread_value_release(&existing_value);
    
    // Set up missing key and default value
    bread_value_set_string(&data->missing_key, "missing");
    bread_value_set_int(&data->default_value, 999);
    
    return data;
}

// Generator for Property 14: Dictionary Keys and Values Consistency
void* generate_dict_keys_values_test_data(PBTGenerator* gen) {
    init_runtime();
    
    DictKeysValuesData* data = malloc(sizeof(DictKeysValuesData));
    if (!data) return NULL;
    
    data->dict = bread_dict_new_typed(TYPE_STRING, TYPE_INT);
    if (!data->dict) {
        free(data);
        return NULL;
    }
    
    // Add several entries
    int count = pbt_random_int(gen, 1, 5);
    for (int i = 0; i < count; i++) {
        BreadValue key, value;
        char key_str[16];
        snprintf(key_str, sizeof(key_str), "key%d", i);
        bread_value_set_string(&key, key_str);
        bread_value_set_int(&value, i * 10);
        
        if (!bread_dict_set_safe(data->dict, key, value)) {
            bread_value_release(&key);
            bread_value_release(&value);
            bread_dict_release(data->dict);
            free(data);
            return NULL;
        }
        bread_value_release(&key);
        bread_value_release(&value);
    }
    
    return data;
}

// Generator for Property 15: Dictionary Remove Decreases Count
void* generate_dict_remove_test_data(PBTGenerator* gen) {
    init_runtime();
    
    DictRemoveData* data = malloc(sizeof(DictRemoveData));
    if (!data) return NULL;
    
    data->dict = bread_dict_new_typed(TYPE_INT, TYPE_STRING);
    if (!data->dict) {
        free(data);
        return NULL;
    }
    
    // Add several entries
    int count = pbt_random_int(gen, 1, 5);
    for (int i = 0; i < count; i++) {
        BreadValue key, value;
        bread_value_set_int(&key, i * 10);
        char* str = pbt_random_string(gen, 8);
        if (!str) {
            bread_value_release(&key);
            bread_dict_release(data->dict);
            free(data);
            return NULL;
        }
        bread_value_set_string(&value, str);
        free(str);
        
        if (!bread_dict_set_safe(data->dict, key, value)) {
            bread_value_release(&key);
            bread_value_release(&value);
            bread_dict_release(data->dict);
            free(data);
            return NULL;
        }
        bread_value_release(&key);
        bread_value_release(&value);
    }
    
    // Choose a key to remove (pick one that exists)
    int remove_index = pbt_random_int(gen, 0, count - 1);
    bread_value_set_int(&data->key_to_remove, remove_index * 10);
    
    return data;
}

// Generator for Property 16: Dictionary Clear Empties Collection
void* generate_dict_clear_test_data(PBTGenerator* gen) {
    init_runtime();
    
    DictClearData* data = malloc(sizeof(DictClearData));
    if (!data) return NULL;
    
    data->dict = bread_dict_new_typed(TYPE_STRING, TYPE_INT);
    if (!data->dict) {
        free(data);
        return NULL;
    }
    
    // Add several entries
    int count = pbt_random_int(gen, 1, 5);
    for (int i = 0; i < count; i++) {
        BreadValue key, value;
        char key_str[16];
        snprintf(key_str, sizeof(key_str), "key%d", i);
        bread_value_set_string(&key, key_str);
        bread_value_set_int(&value, pbt_random_int(gen, 0, 100));
        
        if (!bread_dict_set_safe(data->dict, key, value)) {
            bread_value_release(&key);
            bread_value_release(&value);
            bread_dict_release(data->dict);
            free(data);
            return NULL;
        }
        bread_value_release(&key);
        bread_value_release(&value);
    }
    
    return data;
}

// Cleanup function for array literal test data
void cleanup_array_literal_data(void* test_data) {
    ArrayLiteralData* data = (ArrayLiteralData*)test_data;
    if (!data) return;
    
    if (data->int_values) free(data->int_values);
    if (data->string_values) {
        for (int i = 0; i < data->count; i++) {
            if (data->string_values[i]) free(data->string_values[i]);
        }
        free(data->string_values);
    }
    free(data);
}

// Cleanup function for array repeating test data
void cleanup_array_repeating_data(void* test_data) {
    ArrayRepeatingData* data = (ArrayRepeatingData*)test_data;
    if (!data) return;
    
    bread_value_release(&data->value);
    free(data);
}

// Cleanup function for array indexing test data
void cleanup_array_indexing_data(void* test_data) {
    ArrayIndexingData* data = (ArrayIndexingData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    bread_value_release(&data->new_value);
    free(data);
}

// Cleanup function for array negative index test data
void cleanup_array_negative_index_data(void* test_data) {
    ArrayNegativeIndexData* data = (ArrayNegativeIndexData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    free(data);
}

// Cleanup function for array bounds test data
void cleanup_array_bounds_data(void* test_data) {
    ArrayBoundsData* data = (ArrayBoundsData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    free(data);
}

// Cleanup function for array append test data
void cleanup_array_append_data(void* test_data) {
    ArrayAppendData* data = (ArrayAppendData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    bread_value_release(&data->element);
    free(data);
}

// Cleanup function for array insert test data
void cleanup_array_insert_data(void* test_data) {
    ArrayInsertData* data = (ArrayInsertData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    bread_value_release(&data->element);
    free(data);
}

// Cleanup function for array remove test data
void cleanup_array_remove_data(void* test_data) {
    ArrayRemoveData* data = (ArrayRemoveData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    free(data);
}

// Cleanup function for array contains test data
void cleanup_array_contains_data(void* test_data) {
    ArrayContainsData* data = (ArrayContainsData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    bread_value_release(&data->search_element);
    free(data);
}

// Cleanup function for array index of test data
void cleanup_array_index_of_data(void* test_data) {
    ArrayIndexOfData* data = (ArrayIndexOfData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    bread_value_release(&data->search_element);
    free(data);
}

// Cleanup function for dictionary literal test data
void cleanup_dict_literal_data(void* test_data) {
    DictLiteralData* data = (DictLiteralData*)test_data;
    if (!data) return;
    
    if (data->keys) {
        for (int i = 0; i < data->count; i++) {
            bread_value_release(&data->keys[i]);
        }
        free(data->keys);
    }
    if (data->values) {
        for (int i = 0; i < data->count; i++) {
            bread_value_release(&data->values[i]);
        }
        free(data->values);
    }
    free(data);
}

// Cleanup function for dictionary access test data
void cleanup_dict_access_data(void* test_data) {
    DictAccessData* data = (DictAccessData*)test_data;
    if (!data) return;
    
    if (data->dict) bread_dict_release(data->dict);
    bread_value_release(&data->key);
    bread_value_release(&data->new_value);
    free(data);
}

// Cleanup function for dictionary get default test data
void cleanup_dict_get_default_data(void* test_data) {
    DictGetDefaultData* data = (DictGetDefaultData*)test_data;
    if (!data) return;
    
    if (data->dict) bread_dict_release(data->dict);
    bread_value_release(&data->existing_key);
    bread_value_release(&data->missing_key);
    bread_value_release(&data->default_value);
    free(data);
}

// Cleanup function for dictionary keys values test data
void cleanup_dict_keys_values_data(void* test_data) {
    DictKeysValuesData* data = (DictKeysValuesData*)test_data;
    if (!data) return;
    
    if (data->dict) bread_dict_release(data->dict);
    free(data);
}

// Cleanup function for dictionary remove test data
void cleanup_dict_remove_data(void* test_data) {
    DictRemoveData* data = (DictRemoveData*)test_data;
    if (!data) return;
    
    if (data->dict) bread_dict_release(data->dict);
    bread_value_release(&data->key_to_remove);
    free(data);
}

// Cleanup function for dictionary clear test data
void cleanup_dict_clear_data(void* test_data) {
    DictClearData* data = (DictClearData*)test_data;
    if (!data) return;
    
    if (data->dict) bread_dict_release(data->dict);
    free(data);
}

// Property 1: Array Literal Creation Preserves Elements
// For any valid array literal with elements of the same type, creating an array from the literal 
// should result in an array containing exactly those elements in the same order.
int property_array_literal_creation_preserves_elements(void* test_data) {
    ArrayLiteralData* data = (ArrayLiteralData*)test_data;
    if (!data) return 0;
    
    // Create BreadValue array from test data
    BreadValue* elements = malloc(sizeof(BreadValue) * data->count);
    if (!elements) return 0;
    
    // Initialize elements based on type
    for (int i = 0; i < data->count; i++) {
        if (data->element_type == TYPE_INT) {
            bread_value_set_int(&elements[i], data->int_values[i]);
        } else {
            bread_value_set_string(&elements[i], data->string_values[i]);
        }
    }
    
    // Create array from literal
    BreadArray* array = bread_array_from_literal(elements, data->count);
    if (!array) {
        // Cleanup elements
        for (int i = 0; i < data->count; i++) {
            bread_value_release(&elements[i]);
        }
        free(elements);
        return 0;
    }
    
    // Verify array length matches input count
    if (bread_array_length(array) != data->count) {
        bread_array_release(array);
        for (int i = 0; i < data->count; i++) {
            bread_value_release(&elements[i]);
        }
        free(elements);
        return 0;
    }
    
    // Verify each element is preserved in the same order
    for (int i = 0; i < data->count; i++) {
        BreadValue* array_element = bread_array_get(array, i);
        if (!array_element) {
            bread_array_release(array);
            for (int j = 0; j < data->count; j++) {
                bread_value_release(&elements[j]);
            }
            free(elements);
            return 0;
        }
        
        // Check type matches
        if (array_element->type != elements[i].type) {
            bread_array_release(array);
            for (int j = 0; j < data->count; j++) {
                bread_value_release(&elements[j]);
            }
            free(elements);
            return 0;
        }
        
        // Check value matches based on type
        if (data->element_type == TYPE_INT) {
            if (array_element->value.int_val != elements[i].value.int_val) {
                bread_array_release(array);
                for (int j = 0; j < data->count; j++) {
                    bread_value_release(&elements[j]);
                }
                free(elements);
                return 0;
            }
        } else {
            const char* array_str = bread_string_cstr(array_element->value.string_val);
            const char* original_str = bread_string_cstr(elements[i].value.string_val);
            if (strcmp(array_str, original_str) != 0) {
                bread_array_release(array);
                for (int j = 0; j < data->count; j++) {
                    bread_value_release(&elements[j]);
                }
                free(elements);
                return 0;
            }
        }
    }
    
    // Cleanup
    bread_array_release(array);
    for (int i = 0; i < data->count; i++) {
        bread_value_release(&elements[i]);
    }
    free(elements);
    
    return 1; // Property holds
}

// Property 2: Array Constructor Creates Correct Repetitions
// For any value and positive count, creating an array with Array(repeating: value, count: n) 
// should result in an array of length n where every element equals the repeated value.
int property_array_constructor_creates_correct_repetitions(void* test_data) {
    ArrayRepeatingData* data = (ArrayRepeatingData*)test_data;
    if (!data) return 0;
    
    // Create array using repeating constructor
    BreadArray* array = bread_array_repeating(data->value, data->count);
    if (!array) {
        // For count 0 or negative, this should fail
        return (data->count <= 0);
    }
    
    // Verify array length matches count
    if (bread_array_length(array) != data->count) {
        bread_array_release(array);
        return 0;
    }
    
    // Verify every element equals the repeated value
    for (int i = 0; i < data->count; i++) {
        BreadValue* element = bread_array_get(array, i);
        if (!element) {
            bread_array_release(array);
            return 0;
        }
        
        // Check type matches
        if (element->type != data->value.type) {
            bread_array_release(array);
            return 0;
        }
        
        // Check value matches based on type
        if (data->value.type == TYPE_INT) {
            if (element->value.int_val != data->value.value.int_val) {
                bread_array_release(array);
                return 0;
            }
        } else if (data->value.type == TYPE_STRING) {
            const char* element_str = bread_string_cstr(element->value.string_val);
            const char* original_str = bread_string_cstr(data->value.value.string_val);
            if (strcmp(element_str, original_str) != 0) {
                bread_array_release(array);
                return 0;
            }
        }
    }
    
    bread_array_release(array);
    return 1; // Property holds
}

// Property 3: Array Indexing Round Trip
// For any array and valid index, setting array[index] = value then accessing array[index] 
// should return the same value.
int property_array_indexing_round_trip(void* test_data) {
    ArrayIndexingData* data = (ArrayIndexingData*)test_data;
    if (!data || !data->array) return 0;
    
    // Set value at index using safe setter
    if (!bread_array_set_safe(data->array, data->index, data->new_value)) {
        return 0; // Should succeed for valid index
    }
    
    // Get value back using safe getter
    BreadValue* retrieved = bread_array_get_safe(data->array, data->index);
    if (!retrieved) {
        return 0; // Should succeed for valid index
    }
    
    // Verify the retrieved value matches what we set
    if (retrieved->type != data->new_value.type) {
        return 0;
    }
    
    if (data->new_value.type == TYPE_INT) {
        return (retrieved->value.int_val == data->new_value.value.int_val);
    } else if (data->new_value.type == TYPE_STRING) {
        const char* retrieved_str = bread_string_cstr(retrieved->value.string_val);
        const char* original_str = bread_string_cstr(data->new_value.value.string_val);
        return (strcmp(retrieved_str, original_str) == 0);
    }
    
    return 1; // Property holds
}

// Property 4: Array Negative Indexing Equivalence
// For any array and valid negative index, array[negative_index] should equal 
// array[array.length + negative_index].
int property_array_negative_indexing_equivalence(void* test_data) {
    ArrayNegativeIndexData* data = (ArrayNegativeIndexData*)test_data;
    if (!data || !data->array) return 0;
    
    int length = bread_array_length(data->array);
    int positive_equivalent = length + data->negative_index;
    
    // Both indices should be valid for this property to make sense
    if (positive_equivalent < 0 || positive_equivalent >= length) {
        return 1; // Skip invalid cases
    }
    
    // Get value using negative index
    BreadValue* negative_result = bread_array_get_safe(data->array, data->negative_index);
    if (!negative_result) {
        return 0; // Should succeed for valid negative index
    }
    
    // Get value using positive equivalent
    BreadValue* positive_result = bread_array_get_safe(data->array, positive_equivalent);
    if (!positive_result) {
        return 0; // Should succeed for valid positive index
    }
    
    // Values should be identical
    if (negative_result->type != positive_result->type) {
        return 0;
    }
    
    if (negative_result->type == TYPE_INT) {
        return (negative_result->value.int_val == positive_result->value.int_val);
    } else if (negative_result->type == TYPE_STRING) {
        const char* neg_str = bread_string_cstr(negative_result->value.string_val);
        const char* pos_str = bread_string_cstr(positive_result->value.string_val);
        return (strcmp(neg_str, pos_str) == 0);
    }
    
    return 1; // Property holds
}

// Property 5: Array Bounds Checking
// For any array and out-of-bounds index (positive or negative), accessing the array 
// should raise a runtime error.
int property_array_bounds_checking(void* test_data) {
    ArrayBoundsData* data = (ArrayBoundsData*)test_data;
    if (!data || !data->array) return 0;
    
    // Clear any existing errors
    bread_error_clear();
    
    // Try to access out-of-bounds index
    BreadValue* result = bread_array_get_safe(data->array, data->out_of_bounds_index);
    
    // Should return NULL and set an error
    if (result != NULL) {
        return 0; // Should have failed
    }
    
    // Check that an error was set
    if (!bread_error_has_error()) {
        return 0; // Should have set an error
    }
    
    // Check that it's the right type of error
    BreadErrorType error_type = bread_error_get_type();
    if (error_type != BREAD_ERROR_INDEX_OUT_OF_BOUNDS) {
        return 0; // Should be bounds error
    }
    
    // Clear error for next test
    bread_error_clear();
    
    // Also test setting out-of-bounds
    BreadValue test_value;
    bread_value_set_int(&test_value, 999);
    
    int set_result = bread_array_set_safe(data->array, data->out_of_bounds_index, test_value);
    bread_value_release(&test_value);
    
    // Should fail and set error
    if (set_result != 0) {
        return 0; // Should have failed
    }
    
    if (!bread_error_has_error()) {
        return 0; // Should have set an error
    }
    
    error_type = bread_error_get_type();
    if (error_type != BREAD_ERROR_INDEX_OUT_OF_BOUNDS) {
        return 0; // Should be bounds error
    }
    
    bread_error_clear();
    return 1; // Property holds
}

// Property 6: Array Append Increases Length
// For any array and compatible element, appending the element should increase the array 
// length by exactly one and place the element at the end.
int property_array_append_increases_length(void* test_data) {
    ArrayAppendData* data = (ArrayAppendData*)test_data;
    if (!data || !data->array) return 0;
    
    int original_length = bread_array_length(data->array);
    
    // Append the element
    if (!bread_array_append(data->array, data->element)) {
        return 0; // Should succeed for compatible element
    }
    
    int new_length = bread_array_length(data->array);
    
    // Check length increased by exactly one
    if (new_length != original_length + 1) {
        return 0;
    }
    
    // Check element is at the end
    BreadValue* last_element = bread_array_get(data->array, new_length - 1);
    if (!last_element) {
        return 0;
    }
    
    // Check the value matches
    if (last_element->type != data->element.type) {
        return 0;
    }
    
    if (data->element.type == TYPE_INT) {
        return (last_element->value.int_val == data->element.value.int_val);
    }
    
    return 1; // Property holds
}

// Property 7: Array Insert Preserves Order
// For any array, valid index, and compatible element, inserting the element at the index 
// should place it at that position while preserving the relative order of all other elements.
int property_array_insert_preserves_order(void* test_data) {
    ArrayInsertData* data = (ArrayInsertData*)test_data;
    if (!data || !data->array) return 0;
    
    int original_length = bread_array_length(data->array);
    
    // Store original elements before insertion point
    BreadValue* before_elements = malloc(sizeof(BreadValue) * data->insert_index);
    for (int i = 0; i < data->insert_index; i++) {
        BreadValue* elem = bread_array_get(data->array, i);
        if (!elem) {
            free(before_elements);
            return 0;
        }
        before_elements[i] = bread_value_clone(*elem);
    }
    
    // Store original elements after insertion point
    int after_count = original_length - data->insert_index;
    BreadValue* after_elements = malloc(sizeof(BreadValue) * after_count);
    for (int i = 0; i < after_count; i++) {
        BreadValue* elem = bread_array_get(data->array, data->insert_index + i);
        if (!elem) {
            // Cleanup
            for (int j = 0; j < data->insert_index; j++) {
                bread_value_release(&before_elements[j]);
            }
            free(before_elements);
            free(after_elements);
            return 0;
        }
        after_elements[i] = bread_value_clone(*elem);
    }
    
    // Insert the element
    if (!bread_array_insert(data->array, data->element, data->insert_index)) {
        // Cleanup
        for (int i = 0; i < data->insert_index; i++) {
            bread_value_release(&before_elements[i]);
        }
        for (int i = 0; i < after_count; i++) {
            bread_value_release(&after_elements[i]);
        }
        free(before_elements);
        free(after_elements);
        return 0;
    }
    
    // Check length increased by one
    if (bread_array_length(data->array) != original_length + 1) {
        // Cleanup
        for (int i = 0; i < data->insert_index; i++) {
            bread_value_release(&before_elements[i]);
        }
        for (int i = 0; i < after_count; i++) {
            bread_value_release(&after_elements[i]);
        }
        free(before_elements);
        free(after_elements);
        return 0;
    }
    
    // Check inserted element is at correct position
    BreadValue* inserted = bread_array_get(data->array, data->insert_index);
    if (!inserted || inserted->type != data->element.type || 
        inserted->value.int_val != data->element.value.int_val) {
        // Cleanup
        for (int i = 0; i < data->insert_index; i++) {
            bread_value_release(&before_elements[i]);
        }
        for (int i = 0; i < after_count; i++) {
            bread_value_release(&after_elements[i]);
        }
        free(before_elements);
        free(after_elements);
        return 0;
    }
    
    // Check elements before insertion point are preserved
    for (int i = 0; i < data->insert_index; i++) {
        BreadValue* elem = bread_array_get(data->array, i);
        if (!elem || elem->type != before_elements[i].type || 
            elem->value.int_val != before_elements[i].value.int_val) {
            // Cleanup
            for (int j = 0; j < data->insert_index; j++) {
                bread_value_release(&before_elements[j]);
            }
            for (int j = 0; j < after_count; j++) {
                bread_value_release(&after_elements[j]);
            }
            free(before_elements);
            free(after_elements);
            return 0;
        }
    }
    
    // Check elements after insertion point are preserved and shifted
    for (int i = 0; i < after_count; i++) {
        BreadValue* elem = bread_array_get(data->array, data->insert_index + 1 + i);
        if (!elem || elem->type != after_elements[i].type || 
            elem->value.int_val != after_elements[i].value.int_val) {
            // Cleanup
            for (int j = 0; j < data->insert_index; j++) {
                bread_value_release(&before_elements[j]);
            }
            for (int j = 0; j < after_count; j++) {
                bread_value_release(&after_elements[j]);
            }
            free(before_elements);
            free(after_elements);
            return 0;
        }
    }
    
    // Cleanup
    for (int i = 0; i < data->insert_index; i++) {
        bread_value_release(&before_elements[i]);
    }
    for (int i = 0; i < after_count; i++) {
        bread_value_release(&after_elements[i]);
    }
    free(before_elements);
    free(after_elements);
    
    return 1; // Property holds
}

// Property 8: Array Remove Decreases Length
// For any array and valid index, removing the element at that index should decrease the 
// array length by exactly one and return the removed element.
int property_array_remove_decreases_length(void* test_data) {
    ArrayRemoveData* data = (ArrayRemoveData*)test_data;
    if (!data || !data->array) return 0;
    
    int original_length = bread_array_length(data->array);
    
    // Get the element that will be removed
    BreadValue* to_remove = bread_array_get(data->array, data->remove_index);
    if (!to_remove) {
        return 0; // Should be valid index
    }
    BreadValue expected_removed = bread_value_clone(*to_remove);
    
    // Remove the element
    BreadValue removed = bread_array_remove_at(data->array, data->remove_index);
    
    // Check length decreased by exactly one
    int new_length = bread_array_length(data->array);
    if (new_length != original_length - 1) {
        bread_value_release(&expected_removed);
        bread_value_release(&removed);
        return 0;
    }
    
    // Check returned element matches what was removed
    if (removed.type != expected_removed.type) {
        bread_value_release(&expected_removed);
        bread_value_release(&removed);
        return 0;
    }
    
    int values_match = 0;
    if (removed.type == TYPE_INT) {
        values_match = (removed.value.int_val == expected_removed.value.int_val);
    } else {
        values_match = 1; // For other types, just check type match for now
    }
    
    bread_value_release(&expected_removed);
    bread_value_release(&removed);
    
    return values_match;
}

// Property 9: Array Contains Correctness
// For any array, array.contains(element) should return true if and only if there exists 
// an index where array[index] equals the element.
int property_array_contains_correctness(void* test_data) {
    ArrayContainsData* data = (ArrayContainsData*)test_data;
    if (!data || !data->array) return 0;
    
    int contains_result = bread_array_contains(data->array, data->search_element);
    
    // Manually check if element exists
    int manual_found = 0;
    int length = bread_array_length(data->array);
    for (int i = 0; i < length; i++) {
        BreadValue* elem = bread_array_get(data->array, i);
        if (elem && elem->type == data->search_element.type) {
            if (data->search_element.type == TYPE_INT && 
                elem->value.int_val == data->search_element.value.int_val) {
                manual_found = 1;
                break;
            }
        }
    }
    
    // Results should match
    return (contains_result != 0) == (manual_found != 0);
}

// Property 10: Array IndexOf Correctness
// For any array and element, array.indexOf(element) should return the smallest valid index 
// where array[index] equals the element, or -1 if no such index exists.
int property_array_index_of_correctness(void* test_data) {
    ArrayIndexOfData* data = (ArrayIndexOfData*)test_data;
    if (!data || !data->array) return 0;
    
    int index_of_result = bread_array_index_of(data->array, data->search_element);
    
    // Manually find the first occurrence
    int manual_index = -1;
    int length = bread_array_length(data->array);
    for (int i = 0; i < length; i++) {
        BreadValue* elem = bread_array_get(data->array, i);
        if (elem && elem->type == data->search_element.type) {
            if (data->search_element.type == TYPE_INT && 
                elem->value.int_val == data->search_element.value.int_val) {
                manual_index = i;
                break;
            }
        }
    }
    
    // Results should match
    return (index_of_result == manual_index);
}

// Property 11: Dictionary Literal Creation Preserves Pairs
// For any valid dictionary literal with consistent key and value types, creating a dictionary 
// should result in a dictionary containing exactly those key-value pairs.
int property_dict_literal_creation_preserves_pairs(void* test_data) {
    DictLiteralData* data = (DictLiteralData*)test_data;
    if (!data) return 0;
    
    // Create BreadDictEntry array from test data
    BreadDictEntry* entries = malloc(sizeof(BreadDictEntry) * data->count);
    if (!entries) return 0;
    
    for (int i = 0; i < data->count; i++) {
        entries[i].key = data->keys[i];
        entries[i].value = data->values[i];
        entries[i].is_occupied = 1;
        entries[i].is_deleted = 0;
    }
    
    // Create dictionary from literal
    BreadDict* dict = bread_dict_from_literal(entries, data->count);
    if (!dict) {
        free(entries);
        return 0;
    }
    
    // Verify dictionary count matches input count
    if (bread_dict_count(dict) != data->count) {
        bread_dict_release(dict);
        free(entries);
        return 0;
    }
    
    // Verify each key-value pair is preserved
    for (int i = 0; i < data->count; i++) {
        BreadValue* retrieved = bread_dict_get_safe(dict, data->keys[i]);
        if (!retrieved) {
            bread_dict_release(dict);
            free(entries);
            return 0;
        }
        
        // Check value matches
        if (retrieved->type != data->values[i].type) {
            bread_dict_release(dict);
            free(entries);
            return 0;
        }
        
        if (data->value_type == TYPE_INT) {
            if (retrieved->value.int_val != data->values[i].value.int_val) {
                bread_dict_release(dict);
                free(entries);
                return 0;
            }
        } else if (data->value_type == TYPE_STRING) {
            const char* retrieved_str = bread_string_cstr(retrieved->value.string_val);
            const char* original_str = bread_string_cstr(data->values[i].value.string_val);
            if (strcmp(retrieved_str, original_str) != 0) {
                bread_dict_release(dict);
                free(entries);
                return 0;
            }
        }
    }
    
    bread_dict_release(dict);
    free(entries);
    return 1; // Property holds
}

// Property 12: Dictionary Access Round Trip
// For any dictionary and key, setting dictionary[key] = value then accessing dictionary[key] 
// should return the same value.
int property_dict_access_round_trip(void* test_data) {
    DictAccessData* data = (DictAccessData*)test_data;
    if (!data || !data->dict) return 0;
    
    // Set value at key using safe setter
    if (!bread_dict_set_safe(data->dict, data->key, data->new_value)) {
        return 0; // Should succeed for compatible types
    }
    
    // Get value back using safe getter
    BreadValue* retrieved = bread_dict_get_safe(data->dict, data->key);
    if (!retrieved) {
        return 0; // Should succeed for existing key
    }
    
    // Verify the retrieved value matches what we set
    if (retrieved->type != data->new_value.type) {
        return 0;
    }
    
    if (data->new_value.type == TYPE_STRING) {
        const char* retrieved_str = bread_string_cstr(retrieved->value.string_val);
        const char* original_str = bread_string_cstr(data->new_value.value.string_val);
        return (strcmp(retrieved_str, original_str) == 0);
    }
    
    return 1; // Property holds
}

// Property 13: Dictionary Get With Default
// For any dictionary, key, and default value, dictionary.get(key, default) should return 
// the associated value if the key exists, otherwise the default value.
int property_dict_get_with_default(void* test_data) {
    DictGetDefaultData* data = (DictGetDefaultData*)test_data;
    if (!data || !data->dict) return 0;
    
    // Test with existing key - should return the stored value
    BreadValue result_existing = bread_dict_get_with_default(data->dict, data->existing_key, data->default_value);
    if (result_existing.type != TYPE_INT || result_existing.value.int_val != 42) {
        bread_value_release(&result_existing);
        return 0;
    }
    bread_value_release(&result_existing);
    
    // Test with missing key - should return the default value
    BreadValue result_missing = bread_dict_get_with_default(data->dict, data->missing_key, data->default_value);
    if (result_missing.type != TYPE_INT || result_missing.value.int_val != 999) {
        bread_value_release(&result_missing);
        return 0;
    }
    bread_value_release(&result_missing);
    
    return 1; // Property holds
}

// Property 14: Dictionary Keys and Values Consistency
// For any dictionary, the arrays returned by dictionary.keys and dictionary.values should have 
// the same length as dictionary.count, and each key should map to the corresponding value at the same index.
int property_dict_keys_values_consistency(void* test_data) {
    DictKeysValuesData* data = (DictKeysValuesData*)test_data;
    if (!data || !data->dict) return 0;
    
    int dict_count = bread_dict_count(data->dict);
    
    // Get keys and values arrays
    BreadArray* keys = bread_dict_keys(data->dict);
    BreadArray* values = bread_dict_values(data->dict);
    
    if (!keys || !values) {
        if (keys) bread_array_release(keys);
        if (values) bread_array_release(values);
        return 0;
    }
    
    // Check lengths match dictionary count
    int keys_length = bread_array_length(keys);
    int values_length = bread_array_length(values);
    
    if (keys_length != dict_count || values_length != dict_count) {
        bread_array_release(keys);
        bread_array_release(values);
        return 0;
    }
    
    // Check that each key maps to the corresponding value
    for (int i = 0; i < keys_length; i++) {
        BreadValue* key = bread_array_get(keys, i);
        BreadValue* expected_value = bread_array_get(values, i);
        
        if (!key || !expected_value) {
            bread_array_release(keys);
            bread_array_release(values);
            return 0;
        }
        
        // Look up the key in the dictionary
        BreadValue* actual_value = bread_dict_get_safe(data->dict, *key);
        if (!actual_value) {
            bread_array_release(keys);
            bread_array_release(values);
            return 0;
        }
        
        // Check values match
        if (actual_value->type != expected_value->type) {
            bread_array_release(keys);
            bread_array_release(values);
            return 0;
        }
        
        if (expected_value->type == TYPE_INT) {
            if (actual_value->value.int_val != expected_value->value.int_val) {
                bread_array_release(keys);
                bread_array_release(values);
                return 0;
            }
        } else if (expected_value->type == TYPE_STRING) {
            const char* actual_str = bread_string_cstr(actual_value->value.string_val);
            const char* expected_str = bread_string_cstr(expected_value->value.string_val);
            if (strcmp(actual_str, expected_str) != 0) {
                bread_array_release(keys);
                bread_array_release(values);
                return 0;
            }
        }
    }
    
    bread_array_release(keys);
    bread_array_release(values);
    return 1; // Property holds
}

// Property 15: Dictionary Remove Decreases Count
// For any dictionary and existing key, removing the key should decrease the count by exactly one 
// and return the associated value.
int property_dict_remove_decreases_count(void* test_data) {
    DictRemoveData* data = (DictRemoveData*)test_data;
    if (!data || !data->dict) return 0;
    
    int original_count = bread_dict_count(data->dict);
    
    // Get the expected value before removal
    BreadValue* expected_value = bread_dict_get_safe(data->dict, data->key_to_remove);
    if (!expected_value) {
        return 1; // Key doesn't exist, skip this test case
    }
    BreadValue expected_clone = bread_value_clone(*expected_value);
    
    // Remove the key
    BreadValue removed_value = bread_dict_remove(data->dict, data->key_to_remove);
    
    // Check count decreased by exactly one
    int new_count = bread_dict_count(data->dict);
    if (new_count != original_count - 1) {
        bread_value_release(&expected_clone);
        bread_value_release(&removed_value);
        return 0;
    }
    
    // Check returned value matches what was stored
    if (removed_value.type != expected_clone.type) {
        bread_value_release(&expected_clone);
        bread_value_release(&removed_value);
        return 0;
    }
    
    int values_match = 0;
    if (removed_value.type == TYPE_INT) {
        values_match = (removed_value.value.int_val == expected_clone.value.int_val);
    } else if (removed_value.type == TYPE_STRING) {
        const char* removed_str = bread_string_cstr(removed_value.value.string_val);
        const char* expected_str = bread_string_cstr(expected_clone.value.string_val);
        values_match = (strcmp(removed_str, expected_str) == 0);
    } else {
        values_match = 1; // For other types, just check type match
    }
    
    // Check key no longer exists
    BreadValue* lookup_result = bread_dict_get_safe(data->dict, data->key_to_remove);
    int key_removed = (lookup_result == NULL);
    
    bread_value_release(&expected_clone);
    bread_value_release(&removed_value);
    
    return values_match && key_removed;
}

// Property 16: Dictionary Clear Empties Collection
// For any dictionary, calling clear() should result in a dictionary with count zero and no keys.
int property_dict_clear_empties_collection(void* test_data) {
    DictClearData* data = (DictClearData*)test_data;
    if (!data || !data->dict) return 0;
    
    // Clear the dictionary
    bread_dict_clear(data->dict);
    
    // Check count is zero
    int count = bread_dict_count(data->dict);
    if (count != 0) {
        return 0;
    }
    
    // Check keys array is empty
    BreadArray* keys = bread_dict_keys(data->dict);
    if (!keys) {
        return 0; // Should return empty array, not NULL
    }
    
    int keys_length = bread_array_length(keys);
    bread_array_release(keys);
    
    if (keys_length != 0) {
        return 0;
    }
    
    // Check values array is empty
    BreadArray* values = bread_dict_values(data->dict);
    if (!values) {
        return 0; // Should return empty array, not NULL
    }
    
    int values_length = bread_array_length(values);
    bread_array_release(values);
    
    return (values_length == 0);
}

int run_collection_tests() {
    printf("Running Advanced Collections Property Tests\n");
    printf("==========================================\n\n");
    
    int all_passed = 1;
    
    // Property 1: Array Literal Creation Preserves Elements
    PBTResult result1 = pbt_run_property(
        "Array Literal Creation Preserves Elements",
        generate_array_literal_test_data,
        property_array_literal_creation_preserves_elements,
        cleanup_array_literal_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 1, 
                     "Array Literal Creation Preserves Elements", result1);
    
    if (result1.failed > 0) all_passed = 0;
    
    // Property 2: Array Constructor Creates Correct Repetitions
    PBTResult result2 = pbt_run_property(
        "Array Constructor Creates Correct Repetitions",
        generate_array_repeating_test_data,
        property_array_constructor_creates_correct_repetitions,
        cleanup_array_repeating_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 2, 
                     "Array Constructor Creates Correct Repetitions", result2);
    
    if (result2.failed > 0) all_passed = 0;
    
    // Property 3: Array Indexing Round Trip
    PBTResult result3 = pbt_run_property(
        "Array Indexing Round Trip",
        generate_array_indexing_test_data,
        property_array_indexing_round_trip,
        cleanup_array_indexing_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 3, 
                     "Array Indexing Round Trip", result3);
    
    if (result3.failed > 0) all_passed = 0;
    
    // Property 4: Array Negative Indexing Equivalence
    PBTResult result4 = pbt_run_property(
        "Array Negative Indexing Equivalence",
        generate_array_negative_index_test_data,
        property_array_negative_indexing_equivalence,
        cleanup_array_negative_index_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 4, 
                     "Array Negative Indexing Equivalence", result4);
    
    if (result4.failed > 0) all_passed = 0;
    
    // Property 5: Array Bounds Checking
    PBTResult result5 = pbt_run_property(
        "Array Bounds Checking",
        generate_array_bounds_test_data,
        property_array_bounds_checking,
        cleanup_array_bounds_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 5, 
                     "Array Bounds Checking", result5);
    
    if (result5.failed > 0) all_passed = 0;
    
    // Property 6: Array Append Increases Length
    PBTResult result6 = pbt_run_property(
        "Array Append Increases Length",
        generate_array_append_test_data,
        property_array_append_increases_length,
        cleanup_array_append_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 6, 
                     "Array Append Increases Length", result6);
    
    if (result6.failed > 0) all_passed = 0;
    
    // Property 7: Array Insert Preserves Order
    PBTResult result7 = pbt_run_property(
        "Array Insert Preserves Order",
        generate_array_insert_test_data,
        property_array_insert_preserves_order,
        cleanup_array_insert_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 7, 
                     "Array Insert Preserves Order", result7);
    
    if (result7.failed > 0) all_passed = 0;
    
    // Property 8: Array Remove Decreases Length
    PBTResult result8 = pbt_run_property(
        "Array Remove Decreases Length",
        generate_array_remove_test_data,
        property_array_remove_decreases_length,
        cleanup_array_remove_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 8, 
                     "Array Remove Decreases Length", result8);
    
    if (result8.failed > 0) all_passed = 0;
    
    // Property 9: Array Contains Correctness
    PBTResult result9 = pbt_run_property(
        "Array Contains Correctness",
        generate_array_contains_test_data,
        property_array_contains_correctness,
        cleanup_array_contains_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 9, 
                     "Array Contains Correctness", result9);
    
    if (result9.failed > 0) all_passed = 0;
    
    // Property 10: Array IndexOf Correctness
    PBTResult result10 = pbt_run_property(
        "Array IndexOf Correctness",
        generate_array_index_of_test_data,
        property_array_index_of_correctness,
        cleanup_array_index_of_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 10, 
                     "Array IndexOf Correctness", result10);
    
    if (result10.failed > 0) all_passed = 0;
    
    // Property 11: Dictionary Literal Creation Preserves Pairs
    PBTResult result11 = pbt_run_property(
        "Dictionary Literal Creation Preserves Pairs",
        generate_dict_literal_test_data,
        property_dict_literal_creation_preserves_pairs,
        cleanup_dict_literal_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 11, 
                     "Dictionary Literal Creation Preserves Pairs", result11);
    
    if (result11.failed > 0) all_passed = 0;
    
    // Property 12: Dictionary Access Round Trip
    PBTResult result12 = pbt_run_property(
        "Dictionary Access Round Trip",
        generate_dict_access_test_data,
        property_dict_access_round_trip,
        cleanup_dict_access_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 12, 
                     "Dictionary Access Round Trip", result12);
    
    if (result12.failed > 0) all_passed = 0;
    
    // Property 13: Dictionary Get With Default
    PBTResult result13 = pbt_run_property(
        "Dictionary Get With Default",
        generate_dict_get_default_test_data,
        property_dict_get_with_default,
        cleanup_dict_get_default_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 13, 
                     "Dictionary Get With Default", result13);
    
    if (result13.failed > 0) all_passed = 0;
    
    // Property 14: Dictionary Keys and Values Consistency
    PBTResult result14 = pbt_run_property(
        "Dictionary Keys and Values Consistency",
        generate_dict_keys_values_test_data,
        property_dict_keys_values_consistency,
        cleanup_dict_keys_values_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 14, 
                     "Dictionary Keys and Values Consistency", result14);
    
    if (result14.failed > 0) all_passed = 0;
    
    // Property 15: Dictionary Remove Decreases Count
    PBTResult result15 = pbt_run_property(
        "Dictionary Remove Decreases Count",
        generate_dict_remove_test_data,
        property_dict_remove_decreases_count,
        cleanup_dict_remove_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 15, 
                     "Dictionary Remove Decreases Count", result15);
    
    if (result15.failed > 0) all_passed = 0;
    
    // Property 16: Dictionary Clear Empties Collection
    PBTResult result16 = pbt_run_property(
        "Dictionary Clear Empties Collection",
        generate_dict_clear_test_data,
        property_dict_clear_empties_collection,
        cleanup_dict_clear_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("advanced-collections", 16, 
                     "Dictionary Clear Empties Collection", result16);
    
    if (result16.failed > 0) all_passed = 0;
    
    pbt_free_result(&result1);
    pbt_free_result(&result2);
    pbt_free_result(&result3);
    pbt_free_result(&result4);
    pbt_free_result(&result5);
    pbt_free_result(&result6);
    pbt_free_result(&result7);
    pbt_free_result(&result8);
    pbt_free_result(&result9);
    pbt_free_result(&result10);
    pbt_free_result(&result11);
    pbt_free_result(&result12);
    pbt_free_result(&result13);
    pbt_free_result(&result14);
    pbt_free_result(&result15);
    pbt_free_result(&result16);
    
    return all_passed;
}

int main() {
    return run_collection_tests() ? 0 : 1;
}