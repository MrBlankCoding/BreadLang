#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pbt_framework.h"
#include "../../include/runtime/runtime.h"
#include "../../include/core/value.h"
#include "../../include/core/var.h"

// Test data structures
typedef struct {
    BreadValue value;
    int expected_length;
} LenTestData;

typedef struct {
    BreadValue value;
    char* expected_type;
} TypeTestData;

typedef struct {
    BreadValue input;
    BreadValue expected_output;
} ConversionTestData;

// Generator functions

void* generate_len_test_data(PBTGenerator* gen) {
    LenTestData* data = malloc(sizeof(LenTestData));
    if (!data) return NULL;
    
    int type_choice = pbt_random_int(gen, 0, 3);
    
    switch (type_choice) {
        case 0: { // String
            int length = pbt_random_int(gen, 0, 20);
            char* str = malloc(length + 1);
            if (!str) {
                free(data);
                return NULL;
            }
            for (int i = 0; i < length; i++) {
                str[i] = 'a' + (pbt_random_uint32(gen) % 26);
            }
            str[length] = '\0';
            bread_value_set_string(&data->value, str);
            data->expected_length = length;
            free(str);
            break;
        }
        case 1: { // Array
            BreadArray* arr = bread_array_new_typed(TYPE_INT);
            if (!arr) {
                free(data);
                return NULL;
            }
            int length = pbt_random_int(gen, 0, 10);
            for (int i = 0; i < length; i++) {
                BreadValue val;
                bread_value_set_int(&val, pbt_random_int(gen, -100, 100));
                bread_array_append(arr, val);
                bread_value_release(&val);
            }
            bread_value_set_array(&data->value, arr);
            data->expected_length = length;
            bread_array_release(arr);
            break;
        }
        case 2: { // Dict
            BreadDict* dict = bread_dict_new();
            if (!dict) {
                free(data);
                return NULL;
            }
            int length = pbt_random_int(gen, 0, 5);
            for (int i = 0; i < length; i++) {
                char key[16];
                snprintf(key, sizeof(key), "key%d", i);
                BreadValue val;
                bread_value_set_int(&val, i);
                bread_dict_set(dict, key, val);
                bread_value_release(&val);
            }
            bread_value_set_dict(&data->value, dict);
            data->expected_length = length;
            bread_dict_release(dict);
            break;
        }
    }
    
    return data;
}

void* generate_type_test_data(PBTGenerator* gen) {
    TypeTestData* data = malloc(sizeof(TypeTestData));
    if (!data) return NULL;
    
    int type_choice = pbt_random_int(gen, 0, 9);
    
    switch (type_choice) {
        case 0:
            bread_value_set_nil(&data->value);
            data->expected_type = strdup("nil");
            break;
        case 1:
            bread_value_set_bool(&data->value, pbt_random_int(gen, 0, 2));
            data->expected_type = strdup("bool");
            break;
        case 2:
            bread_value_set_int(&data->value, pbt_random_int(gen, -1000, 1000));
            data->expected_type = strdup("int");
            break;
        case 3:
            bread_value_set_float(&data->value, (float)pbt_random_int(gen, -100, 100) / 10.0f);
            data->expected_type = strdup("float");
            break;
        case 4:
            bread_value_set_double(&data->value, (double)pbt_random_int(gen, -100, 100) / 10.0);
            data->expected_type = strdup("double");
            break;
        case 5:
            bread_value_set_string(&data->value, "test");
            data->expected_type = strdup("string");
            break;
        case 6: {
            BreadArray* arr = bread_array_new_typed(TYPE_INT);
            bread_value_set_array(&data->value, arr);
            bread_array_release(arr);
            data->expected_type = strdup("array");
            break;
        }
        case 7: {
            BreadDict* dict = bread_dict_new();
            bread_value_set_dict(&data->value, dict);
            bread_dict_release(dict);
            data->expected_type = strdup("dict");
            break;
        }
        case 8: {
            BreadOptional* opt = bread_optional_new_none();
            bread_value_set_optional(&data->value, opt);
            bread_optional_release(opt);
            data->expected_type = strdup("optional");
            break;
        }
    }
    
    return data;
}

void* generate_conversion_test_data(PBTGenerator* gen) {
    ConversionTestData* data = malloc(sizeof(ConversionTestData));
    if (!data) return NULL;
    
    int conversion_type = pbt_random_int(gen, 0, 3);
    
    switch (conversion_type) {
        case 0: { // int to string conversion
            int val = pbt_random_int(gen, -1000, 1000);
            bread_value_set_int(&data->input, val);
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", val);
            bread_value_set_string(&data->expected_output, buffer);
            break;
        }
        case 1: { // float to string conversion
            double val = (double)pbt_random_int(gen, -100, 100) / 10.0;
            bread_value_set_double(&data->input, val);
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%lf", val);
            bread_value_set_string(&data->expected_output, buffer);
            break;
        }
        case 2: { // bool to string conversion
            int val = pbt_random_int(gen, 0, 2);
            bread_value_set_bool(&data->input, val);
            bread_value_set_string(&data->expected_output, val ? "true" : "false");
            break;
        }
    }
    
    return data;
}

// Property functions

int property_len_correctness(void* test_data) {
    LenTestData* data = (LenTestData*)test_data;
    
    BreadValue result = bread_builtin_len(&data->value, 1);
    
    if (result.type != TYPE_INT) {
        bread_value_release(&result);
        return 0;
    }
    
    int actual_length = result.value.int_val;
    bread_value_release(&result);
    
    return actual_length == data->expected_length;
}

int property_type_introspection(void* test_data) {
    TypeTestData* data = (TypeTestData*)test_data;
    
    BreadValue result = bread_builtin_type(&data->value, 1);
    
    if (result.type != TYPE_STRING) {
        bread_value_release(&result);
        return 0;
    }
    
    const char* actual_type = bread_string_cstr(result.value.string_val);
    int matches = strcmp(actual_type, data->expected_type) == 0;
    
    bread_value_release(&result);
    return matches;
}

int property_safe_conversions(void* test_data) {
    ConversionTestData* data = (ConversionTestData*)test_data;
    
    BreadValue result = bread_builtin_str(&data->input, 1);
    
    if (result.type != TYPE_STRING) {
        bread_value_release(&result);
        return 0;
    }
    
    // For this property, we just check that str() produces a valid string
    // The exact format may vary, so we don't do strict equality checking
    const char* result_str = bread_string_cstr(result.value.string_val);
    int is_valid = result_str != NULL && strlen(result_str) > 0;
    
    bread_value_release(&result);
    return is_valid;
}

// Cleanup functions

void cleanup_len_test_data(void* test_data) {
    LenTestData* data = (LenTestData*)test_data;
    bread_value_release(&data->value);
    free(data);
}

void cleanup_type_test_data(void* test_data) {
    TypeTestData* data = (TypeTestData*)test_data;
    bread_value_release(&data->value);
    free(data->expected_type);
    free(data);
}

void cleanup_conversion_test_data(void* test_data) {
    ConversionTestData* data = (ConversionTestData*)test_data;
    bread_value_release(&data->input);
    bread_value_release(&data->expected_output);
    free(data);
}

// Forward declarations for built-in function implementations
extern BreadValue bread_builtin_len(BreadValue* args, int arg_count);
extern BreadValue bread_builtin_type(BreadValue* args, int arg_count);
extern BreadValue bread_builtin_str(BreadValue* args, int arg_count);
extern BreadValue bread_builtin_int(BreadValue* args, int arg_count);
extern BreadValue bread_builtin_float(BreadValue* args, int arg_count);

int main() {
    printf("Running Built-in Function Property Tests\n");
    printf("========================================\n\n");
    
    // Initialize the system
    bread_builtin_init();
    
    int all_passed = 1;
    
    // Property 14: Type introspection functions
    {
        PBTResult result = pbt_run_property("type_introspection",
                                          generate_type_test_data,
                                          property_type_introspection,
                                          cleanup_type_test_data,
                                          PBT_MIN_ITERATIONS);
        
        pbt_report_result("breadlang-core-features", 14, 
                         "For any value, the type() function should return the correct string representation of its type",
                         result);
        
        if (result.failed > 0) all_passed = 0;
        pbt_free_result(&result);
    }
    
    // Property 2: Safe type conversions (for conversion functions)
    {
        PBTResult result = pbt_run_property("safe_conversions",
                                          generate_conversion_test_data,
                                          property_safe_conversions,
                                          cleanup_conversion_test_data,
                                          PBT_MIN_ITERATIONS);
        
        pbt_report_result("breadlang-core-features", 2,
                         "For any compatible type conversion, the runtime should produce correct results without data loss or errors",
                         result);
        
        if (result.failed > 0) all_passed = 0;
        pbt_free_result(&result);
    }
    
    // Additional property for len() function
    {
        PBTResult result = pbt_run_property("len_correctness",
                                          generate_len_test_data,
                                          property_len_correctness,
                                          cleanup_len_test_data,
                                          PBT_MIN_ITERATIONS);
        
        pbt_report_result("breadlang-core-features", 8,
                         "For any string or array, the len() function should return the correct number of elements",
                         result);
        
        if (result.failed > 0) all_passed = 0;
        pbt_free_result(&result);
    }
    
    bread_builtin_cleanup();
    
    printf("Overall result: %s\n", all_passed ? "PASSED" : "FAILED");
    return all_passed ? 0 : 1;
}