#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "framework/pbt_framework.h"
#include "../../include/core/var.h"
#include "../../include/core/value.h"
#include "../../include/runtime/runtime.h"

typedef struct {
    VarType type;
    VarValue value;
    char* var_name;
} TypeTestData;

void* generate_type_test_data(PBTGenerator* gen) {
    TypeTestData* data = malloc(sizeof(TypeTestData));
    if (!data) return NULL;
    
    VarType types[] = {TYPE_STRING, TYPE_INT, TYPE_BOOL, TYPE_FLOAT, TYPE_ARRAY};
    int type_count = sizeof(types) / sizeof(types[0]);
    data->type = types[pbt_random_int(gen, 0, type_count)];
    
    int name_len = pbt_random_int(gen, 1, 10) + 1; // 1-10 characters
    data->var_name = malloc(name_len + 1);
    if (!data->var_name) {
        free(data);
        return NULL;
    }
    
    data->var_name[0] = 'a' + (pbt_random_uint32(gen) % 26);
    for (int i = 1; i < name_len; i++) {
        if (pbt_random_uint32(gen) % 2) {
            data->var_name[i] = 'a' + (pbt_random_uint32(gen) % 26);
        } else {
            data->var_name[i] = '0' + (pbt_random_uint32(gen) % 10);
        }
    }
    data->var_name[name_len] = '\0';
    memset(&data->value, 0, sizeof(VarValue));
    
    switch (data->type) {
        case TYPE_STRING:
            data->value.string_val = bread_string_new("test_string");
            break;
        case TYPE_INT:
            data->value.int_val = pbt_random_int(gen, -1000, 1000);
            break;
        case TYPE_BOOL:
            data->value.bool_val = pbt_random_int(gen, 0, 2);
            break;
        case TYPE_FLOAT:
            data->value.float_val = (float)pbt_random_int(gen, -1000, 1000) / 10.0f;
            break;
        case TYPE_ARRAY:
            data->value.array_val = bread_array_new();
            break;
        default:
            free(data->var_name);
            free(data);
            return NULL;
    }
    
    return data;
}

void cleanup_type_test_data(void* test_data) {
    TypeTestData* data = (TypeTestData*)test_data;
    if (!data) return;
    switch (data->type) {
        case TYPE_STRING:
            if (data->value.string_val) {
                bread_string_release(data->value.string_val);
            }
            break;
        case TYPE_ARRAY:
            if (data->value.array_val) {
                bread_array_release(data->value.array_val);
            }
            break;
        default:
            break;
    }
    
    if (data->var_name) {
        free(data->var_name);
    }
    free(data);
}

int property_type_consistency(void* test_data) {
    TypeTestData* data = (TypeTestData*)test_data;
    if (!data || !data->var_name) return 0;
    if (strlen(data->var_name) == 0) return 1;
    
    init_variables();
    push_scope();
    
    int declare_result = declare_variable_raw(data->var_name, data->type, data->value, 0);
    if (!declare_result) {
        pop_scope();
        cleanup_variables();
        return 0; // Declaration should succeed for valid types
    }
    
    Variable* var = get_variable(data->var_name);
    if (!var) {
        pop_scope();
        cleanup_variables();
        return 0;
    }
    
    if (var->type != data->type) {
        pop_scope();
        cleanup_variables();
        return 0; // Type should match
    }
    
    int value_correct = 0;
    switch (data->type) {
        case TYPE_STRING:
            value_correct = (var->value.string_val != NULL);
            break;
        case TYPE_INT:
            value_correct = (var->value.int_val == data->value.int_val);
            break;
        case TYPE_BOOL:
            value_correct = (var->value.bool_val == data->value.bool_val);
            break;
        case TYPE_FLOAT:
            // Allow small floating point differences
            value_correct = (fabsf(var->value.float_val - data->value.float_val) < 0.001f);
            break;
        case TYPE_ARRAY:
            value_correct = (var->value.array_val != NULL);
            break;
        default:
            value_correct = 0;
    }
    
    pop_scope();
    cleanup_variables();
    
    return value_correct;
}

int run_type_system_tests() {
    printf("Running Type System Property Tests\n");
    printf("==================================\n\n");
    
    PBTResult result1 = pbt_run_property(
        "Type consistency for enhanced types",
        generate_type_test_data,
        property_type_consistency,
        cleanup_type_test_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 1, 
                     "Type consistency for enhanced types", result1);
    
    int all_passed = (result1.failed == 0);
    
    pbt_free_result(&result1);
    
    return all_passed;
}

int main() {
    return run_type_system_tests() ? 0 : 1;
}