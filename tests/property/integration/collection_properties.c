#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pbt_framework.h"
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
    
    return all_passed;
}

int main() {
    return run_collection_tests() ? 0 : 1;
}