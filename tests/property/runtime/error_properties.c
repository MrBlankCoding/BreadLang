#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pbt_framework.h"
#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/ast/ast_expr_parser.h"

typedef struct {
    int dividend;
    int divisor;
} DivisionTestData;

typedef struct {
    char* array_data;
    int array_length;
    int access_index;
} BoundsTestData;

typedef struct {
    char* variable_name;
    int should_exist;
} VariableTestData;

typedef struct {
    char* left_type_name;
    char* right_type_name;
    char operator;
} TypeMismatchData;

// Generator functions

void* generate_division_test_data(PBTGenerator* gen) {
    DivisionTestData* data = malloc(sizeof(DivisionTestData));
    if (!data) return NULL;
    
    data->dividend = pbt_random_int(gen, -1000, 1000);
    data->divisor = pbt_random_int(gen, -10, 10);
    
    return data;
}

void* generate_bounds_test_data(PBTGenerator* gen) {
    BoundsTestData* data = malloc(sizeof(BoundsTestData));
    if (!data) return NULL;
    data->array_length = pbt_random_int(gen, 0, 20);
    data->array_data = malloc(data->array_length + 1);
    if (!data->array_data) {
        free(data);
        return NULL;
    }
    
    for (int i = 0; i < data->array_length; i++) {
        data->array_data[i] = 'a' + (pbt_random_int(gen, 0, 25));
    }
    data->array_data[data->array_length] = '\0';
    data->access_index = pbt_random_int(gen, -5, data->array_length + 5);
    
    return data;
}

void* generate_variable_test_data(PBTGenerator* gen) {
    VariableTestData* data = malloc(sizeof(VariableTestData));
    if (!data) return NULL;
    
    int name_len = pbt_random_int(gen, 1, 15);
    data->variable_name = malloc(name_len + 1);
    if (!data->variable_name) {
        free(data);
        return NULL;
    }
    
    for (int i = 0; i < name_len; i++) {
        if (i == 0) {
            data->variable_name[i] = 'a' + pbt_random_int(gen, 0, 25);
        } else {
            int choice = pbt_random_int(gen, 0, 2);
            if (choice == 0) {
                data->variable_name[i] = 'a' + pbt_random_int(gen, 0, 25);
            } else if (choice == 1) {
                data->variable_name[i] = 'A' + pbt_random_int(gen, 0, 25);
            } else {
                data->variable_name[i] = '0' + pbt_random_int(gen, 0, 9);
            }
        }
    }
    data->variable_name[name_len] = '\0';
    
    data->should_exist = pbt_random_int(gen, 0, 1);
    
    return data;
}

void* generate_type_mismatch_data(PBTGenerator* gen) {
    TypeMismatchData* data = malloc(sizeof(TypeMismatchData));
    if (!data) return NULL;
    
    const char* types[] = {"int", "string", "bool", "float"};
    int type_count = sizeof(types) / sizeof(types[0]);
    
    int left_idx = pbt_random_int(gen, 0, type_count - 1);
    int right_idx = pbt_random_int(gen, 0, type_count - 1);
    
    data->left_type_name = malloc(strlen(types[left_idx]) + 1);
    data->right_type_name = malloc(strlen(types[right_idx]) + 1);
    
    if (!data->left_type_name || !data->right_type_name) {
        if (data->left_type_name) free(data->left_type_name);
        if (data->right_type_name) free(data->right_type_name);
        free(data);
        return NULL;
    }
    
    strcpy(data->left_type_name, types[left_idx]);
    strcpy(data->right_type_name, types[right_idx]);
    
    char operators[] = {'+', '-', '*', '/', '%', '=', '!', '<', '>'};
    int op_idx = pbt_random_int(gen, 0, sizeof(operators) - 1);
    data->operator = operators[op_idx];
    
    return data;
}


void cleanup_division_data(void* test_data) {
    if (test_data) free(test_data);
}

void cleanup_bounds_data(void* test_data) {
    BoundsTestData* data = (BoundsTestData*)test_data;
    if (!data) return;
    if (data->array_data) free(data->array_data);
    free(data);
}

void cleanup_variable_data(void* test_data) {
    VariableTestData* data = (VariableTestData*)test_data;
    if (!data) return;
    if (data->variable_name) free(data->variable_name);
    free(data);
}

void cleanup_type_mismatch_data(void* test_data) {
    TypeMismatchData* data = (TypeMismatchData*)test_data;
    if (!data) return;
    if (data->left_type_name) free(data->left_type_name);
    if (data->right_type_name) free(data->right_type_name);
    free(data);
}

int property_runtime_error_reporting(void* test_data) {
    DivisionTestData* data = (DivisionTestData*)test_data;
    if (!data) return 0;
    
    bread_error_clear();
    
    if (data->divisor == 0) {
        BreadValue left, right, result;
        bread_value_set_int(&left, data->dividend);
        bread_value_set_int(&right, 0);
        
        int success = bread_binary_op('/', &left, &right, &result);
        
        if (success) {
            bread_value_release(&left);
            bread_value_release(&right);
            bread_value_release(&result);
            return 0; // Should have failed
        }
        
        if (!bread_error_has_error()) {
            bread_value_release(&left);
            bread_value_release(&right);
            return 0; // Should have set an error
        }
        
        BreadErrorType error_type = bread_error_get_type();
        if (error_type != BREAD_ERROR_DIVISION_BY_ZERO) {
            bread_value_release(&left);
            bread_value_release(&right);
            return 0; // Wrong error type
        }
        
        const char* message = bread_error_get_message();
        if (!message || strlen(message) == 0) {
            bread_value_release(&left);
            bread_value_release(&right);
            return 0; // No error message
        }
        
        bread_value_release(&left);
        bread_value_release(&right);
        return 1; // Proper error handling
    } else {
        BreadValue left, right, result;
        bread_value_set_int(&left, data->dividend);
        bread_value_set_int(&right, data->divisor);
        
        int success = bread_binary_op('/', &left, &right, &result);
        
        bread_value_release(&left);
        bread_value_release(&right);
        if (success) {
            bread_value_release(&result);
        }
        
        // Should not have set an error for valid division
        return !bread_error_has_error();
    }
}


int property_bounds_error_reporting(void* test_data) {
    BoundsTestData* data = (BoundsTestData*)test_data;
    if (!data) return 0;
    
    // Clear any previous errors
    bread_error_clear();
    
    // Create a string from the test data
    BreadString* test_string = bread_string_new(data->array_data);
    if (!test_string) return 0;
    
    // Test string indexing bounds checking
    char result = bread_string_get_char(test_string, (size_t)data->access_index);
    
    if (data->access_index < 0 || data->access_index >= data->array_length) {
        // Out of bounds access should set an error
        if (!bread_error_has_error()) {
            bread_string_release(test_string);
            return 0; // Should have set an error
        }
        
        BreadErrorType error_type = bread_error_get_type();
        if (error_type != BREAD_ERROR_INDEX_OUT_OF_BOUNDS && 
            error_type != BREAD_ERROR_RUNTIME_ERROR) {
            bread_string_release(test_string);
            return 0; // Wrong error type
        }
        
        // Should return null character for out of bounds
        if (result != '\0') {
            bread_string_release(test_string);
            return 0; // Should return null char
        }
    } else {
        // Valid access should not set an error
        if (bread_error_has_error()) {
            bread_string_release(test_string);
            return 0; // Should not have error for valid access
        }
        
        // Should return the correct character
        if (result != data->array_data[data->access_index]) {
            bread_string_release(test_string);
            return 0; // Wrong character returned
        }
    }
    
    bread_string_release(test_string);
    return 1;
}

int property_compile_time_error_detection(void* test_data) {
    VariableTestData* data = (VariableTestData*)test_data;
    if (!data) return 0;
    bread_error_clear();
    init_variables();
    
    if (data->should_exist) {
        BreadValue test_value;
        bread_value_set_int(&test_value, 42);
        bread_var_decl(data->variable_name, TYPE_INT, 0, &test_value);
        bread_value_release(&test_value);
    }
    
    BreadValue result;
    int success = bread_var_load(data->variable_name, &result);
    
    if (data->should_exist) {
        if (!success) {
            cleanup_variables();
            return 0; // Should have succeeded
        }
        
        if (bread_error_has_error()) {
            bread_value_release(&result);
            cleanup_variables();
            return 0; // Should not have error
        }
        
        bread_value_release(&result);
    } else {
        if (success) {
            bread_value_release(&result);
            cleanup_variables();
            return 0; // Should have failed
        }
        
        if (!bread_error_has_error()) {
            cleanup_variables();
            return 0; // Should have set an error
        }
        
        BreadErrorType error_type = bread_error_get_type();
        if (error_type != BREAD_ERROR_UNDEFINED_VARIABLE) {
            cleanup_variables();
            return 0; // Wrong error type
        }
        
        const char* message = bread_error_get_message();
        if (!message || strstr(message, data->variable_name) == NULL) {
            cleanup_variables();
            return 0; // Error message should mention variable name
        }
    }
    
    cleanup_variables();
    return 1;
}

int main() {
    printf("Running Error Handling Property Tests...\n");
    bread_error_init();
    int all_passed = 1;
    PBTResult result1 = pbt_run_property(
        "Runtime error reporting for division by zero",
        generate_division_test_data,
        property_runtime_error_reporting,
        cleanup_division_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 12, 
                     "Runtime error reporting", result1);
    
    if (result1.failed > 0) all_passed = 0;
    PBTResult result2 = pbt_run_property(
        "Bounds checking error reporting",
        generate_bounds_test_data,
        property_bounds_error_reporting,
        cleanup_bounds_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 12, 
                     "Bounds checking error reporting", result2);
    
    if (result2.failed > 0) all_passed = 0;
    PBTResult result3 = pbt_run_property(
        "Compile-time error detection for undefined variables",
        generate_variable_test_data,
        property_compile_time_error_detection,
        cleanup_variable_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 13, 
                     "Compile-time error detection", result3);
    
    if (result3.failed > 0) all_passed = 0;
    bread_error_cleanup();
    
    printf("\nError Handling Property Tests %s\n", 
           all_passed ? "PASSED" : "FAILED");
    
    return all_passed ? 0 : 1;
}