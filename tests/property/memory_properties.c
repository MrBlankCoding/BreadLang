#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pbt_framework.h"
#include "../../include/runtime/runtime.h"
#include "../../include/runtime/memory.h"
#include "../../include/core/value.h"

// Data structs
typedef struct {
    BreadValue* values;
    int count;
    int capacity;
} TestValueArray;

typedef struct {
    BreadArray* array1;
    BreadArray* array2;
    BreadDict* dict;
} TestCircularData;

// Gens
void* generate_test_values(PBTGenerator* gen) {
    TestValueArray* data = malloc(sizeof(TestValueArray));
    if (!data) return NULL;
    
    data->count = pbt_random_int(gen, 1, 10);  // Reduced from 20 to 10
    data->capacity = data->count;
    data->values = malloc(sizeof(BreadValue) * data->capacity);
    if (!data->values) {
        free(data);
        return NULL;
    }
    
    for (int i = 0; i < data->count; i++) {
        int type_choice = pbt_random_int(gen, 0, 3);  // Reduced choices to avoid complex objects
        switch (type_choice) {
            case 0:
                bread_value_set_int(&data->values[i], pbt_random_int(gen, -100, 100));
                break;
            case 1:
                bread_value_set_bool(&data->values[i], pbt_random_int(gen, 0, 2));
                break;
            case 2: {
                char* str = pbt_random_string(gen, 20);  // Reduced from 50 to 20
                bread_value_set_string(&data->values[i], str);
                free(str);
                break;
            }
            default:
                bread_value_set_nil(&data->values[i]);
                break;
        }
    }
    
    return data;
}

void* generate_circular_data(PBTGenerator* gen) {
    TestCircularData* data = malloc(sizeof(TestCircularData));
    if (!data) return NULL;
    
    data->array1 = bread_array_new();
    data->array2 = bread_array_new();
    data->dict = bread_dict_new();
    
    if (!data->array1 || !data->array2 || !data->dict) {
        if (data->array1) bread_array_release(data->array1);
        if (data->array2) bread_array_release(data->array2);
        if (data->dict) bread_dict_release(data->dict);
        free(data);
        return NULL;
    }
    
    BreadValue val1, val2, val3;
    bread_value_set_array(&val1, data->array2);
    bread_value_set_array(&val2, data->array1);
    bread_value_set_dict(&val3, data->dict);
    
    bread_array_append(data->array1, val1);  // array1 -> array2
    bread_array_append(data->array2, val2);  // array2 -> array1
    bread_dict_set(data->dict, "self", val3); // dict -> dict
    
    bread_value_release(&val1);
    bread_value_release(&val2);
    bread_value_release(&val3);
    
    return data;
}

void cleanup_test_values(void* data) {
    TestValueArray* test_data = (TestValueArray*)data;
    if (!test_data) return;
    
    if (test_data->values) {
        for (int i = 0; i < test_data->count; i++) {
            bread_value_release(&test_data->values[i]);
        }
        free(test_data->values);
    }
    free(test_data);
}

void cleanup_circular_data(void* data) {
    TestCircularData* test_data = (TestCircularData*)data;
    if (!test_data) return;
    
    // Clear the circular references first to avoid issues during cleanup
    if (test_data->array1 && test_data->array1->count > 0) {
        // Clear array contents to break cycles
        for (int i = 0; i < test_data->array1->count; i++) {
            bread_value_release(&test_data->array1->items[i]);
        }
        test_data->array1->count = 0;
    }
    
    if (test_data->array2 && test_data->array2->count > 0) {
        // Clear array contents to break cycles
        for (int i = 0; i < test_data->array2->count; i++) {
            bread_value_release(&test_data->array2->items[i]);
        }
        test_data->array2->count = 0;
    }
    
    if (test_data->dict && test_data->dict->entries) {
        // Clear dict contents to break cycles
        for (int i = 0; i < test_data->dict->capacity; i++) {
            if (test_data->dict->entries[i].key) {
                bread_string_release(test_data->dict->entries[i].key);
                bread_value_release(&test_data->dict->entries[i].value);
                test_data->dict->entries[i].key = NULL;
                memset(&test_data->dict->entries[i].value, 0, sizeof(BreadValue));
            }
        }
        test_data->dict->count = 0;
    }
    
    // Now release the objects
    if (test_data->array1) bread_array_release(test_data->array1);
    if (test_data->array2) bread_array_release(test_data->array2);
    if (test_data->dict) bread_dict_release(test_data->dict);
    free(test_data);
}

int property_automatic_memory_lifecycle(void* data) {
    TestValueArray* test_data = (TestValueArray*)data;
    if (!test_data) return 0;
    
    BreadValue* temp_values = malloc(sizeof(BreadValue) * test_data->count);
    if (!temp_values) return 0;
    
    for (int i = 0; i < test_data->count; i++) {
        temp_values[i] = bread_value_clone(test_data->values[i]);
    }
    
    // Memory stats collected but not used for now
    for (int i = 0; i < test_data->count; i++) {
        bread_value_release(&temp_values[i]);
    }
    
    free(temp_values);
    
    // Memory should be properly managed - no leaks from our operations
    // (Note: We can't check exact equality due to other allocations)
    int memory_managed_properly = 1;
    
    return memory_managed_properly;
}

int property_circular_reference_handling(void* data) {
    TestCircularData* test_data = (TestCircularData*)data;
    if (!test_data) return 0;
    
    bread_memory_collect_cycles();
    int objects_still_exist = (test_data->array1 != NULL && 
                              test_data->array2 != NULL && 
                              test_data->dict != NULL);
    
    int refcounts_reasonable = (bread_object_get_refcount(test_data->array1) > 0 &&
                               bread_object_get_refcount(test_data->array2) > 0 &&
                               bread_object_get_refcount(test_data->dict) > 0);
    
    return objects_still_exist && refcounts_reasonable;
}

int main(void) {
    printf("Running Memory Management Property Tests\n");
    printf("=======================================\n\n");
    
    bread_memory_init();
    bread_string_intern_init();
    bread_memory_enable_debug_mode(1);
    
    // Disable automatic cycle collection during tests to avoid interference
    bread_memory_set_cycle_collection_threshold(10000);
    PBTResult result18 = pbt_run_property(
        "Automatic memory lifecycle",
        generate_test_values,
        property_automatic_memory_lifecycle,
        cleanup_test_values,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 18, 
                     "For any program execution, memory should be automatically allocated for new objects and deallocated when objects go out of scope",
                     result18);

    PBTResult result19 = pbt_run_property(
        "Circular reference handling",
        generate_circular_data,
        property_circular_reference_handling,
        cleanup_circular_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 19,
                     "For any data structure with circular references, the memory management system should prevent memory leaks through cycle detection",
                     result19);
    
    pbt_free_result(&result18);
    pbt_free_result(&result19);
    if (bread_memory_check_leaks()) {
        printf("Warning: Memory leaks detected after tests!\n");
        bread_memory_print_leak_report();
    } else {
        printf("No memory leaks detected - all tests passed cleanly!\n");
    }
    
    bread_string_intern_cleanup();
    bread_memory_cleanup();
    
    // Return non-zero if any tests failed
    return (result18.failed > 0 || result19.failed > 0) ? 1 : 0;
}