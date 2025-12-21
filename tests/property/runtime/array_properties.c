#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pbt_framework.h"
#include "../../include/core/var.h"
#include "../../include/core/value.h"
#include "../../include/runtime/runtime.h"

typedef struct {
    VarType element_type;
    int* int_values;
    char** string_values;
    int count;
} ArrayTypeData;

typedef struct {
    BreadArray* array;
    int index;
    BreadValue new_value;
} ArrayIndexData;

typedef struct {
    BreadArray* array1;
    BreadArray* array2;
} ArrayLengthData;

// Initialize runtime for tests
static void init_runtime() {
    static int initialized = 0;
    if (!initialized) {
        bread_string_intern_init();
        initialized = 1;
    }
}

void* generate_array_type_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayTypeData* data = malloc(sizeof(ArrayTypeData));
    if (!data) return NULL;
    
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

void* generate_array_index_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayIndexData* data = malloc(sizeof(ArrayIndexData));
    if (!data) return NULL;
    
    data->array = bread_array_new_typed(TYPE_INT);
    if (!data->array) {
        free(data);
        return NULL;
    }
    
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
    
    data->index = pbt_random_int(gen, -5, count + 5);
    bread_value_set_int(&data->new_value, pbt_random_int(gen, 200, 300));
    
    return data;
}

void* generate_array_length_test_data(PBTGenerator* gen) {
    init_runtime();
    
    ArrayLengthData* data = malloc(sizeof(ArrayLengthData));
    if (!data) return NULL;
    
    data->array1 = bread_array_new_typed(TYPE_INT);
    data->array2 = bread_array_new_typed(TYPE_STRING);
    
    if (!data->array1 || !data->array2) {
        if (data->array1) bread_array_release(data->array1);
        if (data->array2) bread_array_release(data->array2);
        free(data);
        return NULL;
    }
    
    int count1 = pbt_random_int(gen, 0, 15);
    for (int i = 0; i < count1; i++) {
        BreadValue val;
        bread_value_set_int(&val, i);
        bread_array_append(data->array1, val);
        bread_value_release(&val);
    }
    
    int count2 = pbt_random_int(gen, 0, 15);
    for (int i = 0; i < count2; i++) {
        BreadValue val;
        char str[10];
        snprintf(str, sizeof(str), "str%d", i);
        bread_value_set_string(&val, str);
        bread_array_append(data->array2, val);
        bread_value_release(&val);
    }
    
    return data;
}

void cleanup_array_type_data(void* test_data) {
    ArrayTypeData* data = (ArrayTypeData*)test_data;
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

void cleanup_array_index_data(void* test_data) {
    ArrayIndexData* data = (ArrayIndexData*)test_data;
    if (!data) return;
    
    if (data->array) bread_array_release(data->array);
    bread_value_release(&data->new_value);
    free(data);
}

void cleanup_array_length_data(void* test_data) {
    ArrayLengthData* data = (ArrayLengthData*)test_data;
    if (!data) return;
    
    if (data->array1) bread_array_release(data->array1);
    if (data->array2) bread_array_release(data->array2);
    free(data);
}

int property_array_type_enforcement(void* test_data) {
    ArrayTypeData* data = (ArrayTypeData*)test_data;
    if (!data) return 0;
    BreadArray* array = bread_array_new_typed(data->element_type);
    if (!array) return 0;
    
    for (int i = 0; i < data->count; i++) {
        BreadValue val;
        if (data->element_type == TYPE_INT) {
            bread_value_set_int(&val, data->int_values[i]);
        } else {
            bread_value_set_string(&val, data->string_values[i]);
        }
        
        if (!bread_array_append(array, val)) {
            bread_value_release(&val);
            bread_array_release(array);
            return 0;
        }
        bread_value_release(&val);
    }
    
    for (int i = 0; i < data->count; i++) {
        BreadValue* elem = bread_array_get(array, i);
        if (!elem || elem->type != data->element_type) {
            bread_array_release(array);
            return 0;
        }
    }
    
    BreadValue wrong_val;
    if (data->element_type == TYPE_INT) {
        bread_value_set_string(&wrong_val, "wrong_type");
    } else {
        bread_value_set_int(&wrong_val, 42);
    }
    
    int should_fail = bread_array_append(array, wrong_val);
    bread_value_release(&wrong_val);
    int expected_length = data->count;
    int actual_length = bread_array_length(array);
    bread_array_release(array);
    return (!should_fail && actual_length == expected_length);
}

int property_array_indexing_modification(void* test_data) {
    ArrayIndexData* data = (ArrayIndexData*)test_data;
    if (!data || !data->array) return 0;
    
    int length = bread_array_length(data->array);
    int index = data->index;
    
    // Handle negative indices (Python-style)
    int adjusted_index = index;
    if (adjusted_index < 0) {
        adjusted_index = length + adjusted_index;
    }
    
    if (adjusted_index >= 0 && adjusted_index < length) {
        BreadValue* original = bread_array_get(data->array, adjusted_index);
        if (!original) return 0;
        
        BreadValue original_copy = bread_value_clone(*original);
        int set_result = bread_array_set(data->array, adjusted_index, data->new_value);
        if (!set_result) {
            bread_value_release(&original_copy);
            return 0;
        }
        
        BreadValue* retrieved = bread_array_get(data->array, adjusted_index);
        if (!retrieved) {
            bread_value_release(&original_copy);
            return 0;
        }
        int values_match = (retrieved->type == data->new_value.type);
        if (values_match && retrieved->type == TYPE_INT) {
            values_match = (retrieved->value.int_val == data->new_value.value.int_val);
        }
        
        bread_value_release(&original_copy);
        return values_match;
        
    } else {
        BreadValue* result = bread_array_get(data->array, index);
        if (result != NULL) return 0;
        int set_result = bread_array_set(data->array, index, data->new_value);
        if (set_result != 0) return 0;
        int new_length = bread_array_length(data->array);
        return (new_length == length);
    }
}

int property_collection_length_consistency(void* test_data) {
    ArrayLengthData* data = (ArrayLengthData*)test_data;
    if (!data || !data->array1 || !data->array2) return 0;
    int length1 = bread_array_length(data->array1);
    int length2 = bread_array_length(data->array2);
    
    if (length1 < 0 || length2 < 0) return 0;
    int manual_count1 = 0;
    for (int i = 0; i < length1; i++) {
        if (bread_array_get(data->array1, i)) {
            manual_count1++;
        }
    }
    
    int manual_count2 = 0;
    for (int i = 0; i < length2; i++) {
        if (bread_array_get(data->array2, i)) {
            manual_count2++;
        }
    }
    
    if (length1 != manual_count1 || length2 != manual_count2) return 0;
    BreadValue new_val;
    bread_value_set_int(&new_val, 999);
    
    int old_length = bread_array_length(data->array1);
    if (bread_array_append(data->array1, new_val)) {
        int new_length = bread_array_length(data->array1);
        bread_value_release(&new_val);
        return (new_length == old_length + 1);
    }
    
    bread_value_release(&new_val);
    return 0;
}

int run_array_tests() {
    printf("Running Array Property Tests\n");
    printf("============================\n\n");
    
    int all_passed = 1;
    PBTResult result6 = pbt_run_property(
        "Array type enforcement",
        generate_array_type_test_data,
        property_array_type_enforcement,
        cleanup_array_type_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 6, 
                     "Array type enforcement", result6);
    
    if (result6.failed > 0) all_passed = 0;
    PBTResult result7 = pbt_run_property(
        "Array indexing and modification",
        generate_array_index_test_data,
        property_array_indexing_modification,
        cleanup_array_index_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 7, 
                     "Array indexing and modification", result7);
    
    if (result7.failed > 0) all_passed = 0;
    PBTResult result8 = pbt_run_property(
        "Collection length consistency",
        generate_array_length_test_data,
        property_collection_length_consistency,
        cleanup_array_length_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 8, 
                     "Collection length consistency", result8);
    
    if (result8.failed > 0) all_passed = 0;
    
    pbt_free_result(&result6);
    pbt_free_result(&result7);
    pbt_free_result(&result8);
    
    return all_passed;
}

int main() {
    return run_array_tests() ? 0 : 1;
}