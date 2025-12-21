#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pbt_framework.h"
#include "../../include/core/var.h"
#include "../../include/core/value.h"
#include "../../include/runtime/runtime.h"

typedef struct {
    char* str1;
    char* str2;
} StringConcatData;

typedef struct {
    char* str;
    int index;
} StringIndexData;

typedef struct {
    char* str1;
    char* str2;
} StringCompareData;

void* generate_concat_test_data(PBTGenerator* gen) {
    StringConcatData* data = malloc(sizeof(StringConcatData));
    if (!data) return NULL;
    
    data->str1 = pbt_random_string(gen, 20);
    data->str2 = pbt_random_string(gen, 20);
    
    if (!data->str1 || !data->str2) {
        if (data->str1) free(data->str1);
        if (data->str2) free(data->str2);
        free(data);
        return NULL;
    }
    
    return data;
}

void* generate_index_test_data(PBTGenerator* gen) {
    StringIndexData* data = malloc(sizeof(StringIndexData));
    if (!data) return NULL;
    
    int min_length = 1;
    int max_length = 20;
    int length = pbt_random_int(gen, min_length, max_length + 1);
    
    data->str = malloc(length + 1);
    if (!data->str) {
        free(data);
        return NULL;
    }

    for (int i = 0; i < length; i++) {
        data->str[i] = 'a' + (pbt_random_uint32(gen) % 26);
    }
    data->str[length] = '\0';
    
    data->index = pbt_random_int(gen, -5, length + 5);
    
    return data;
}

void* generate_compare_test_data(PBTGenerator* gen) {
    StringCompareData* data = malloc(sizeof(StringCompareData));
    if (!data) return NULL;
    if (pbt_random_uint32(gen) % 3 == 0) {
        data->str1 = pbt_random_string(gen, 15);
        if (!data->str1) {
            free(data);
            return NULL;
        }
        data->str2 = strdup(data->str1);
        if (!data->str2) {
            free(data->str1);
            free(data);
            return NULL;
        }
    } else {
        data->str1 = pbt_random_string(gen, 15);
        data->str2 = pbt_random_string(gen, 15);
        
        if (!data->str1 || !data->str2) {
            if (data->str1) free(data->str1);
            if (data->str2) free(data->str2);
            free(data);
            return NULL;
        }
    }
    
    return data;
}

void cleanup_concat_data(void* test_data) {
    StringConcatData* data = (StringConcatData*)test_data;
    if (!data) return;
    if (data->str1) free(data->str1);
    if (data->str2) free(data->str2);
    free(data);
}

void cleanup_index_data(void* test_data) {
    StringIndexData* data = (StringIndexData*)test_data;
    if (!data) return;
    if (data->str) free(data->str);
    free(data);
}

void cleanup_compare_data(void* test_data) {
    StringCompareData* data = (StringCompareData*)test_data;
    if (!data) return;
    if (data->str1) free(data->str1);
    if (data->str2) free(data->str2);
    free(data);
}

int property_string_concatenation(void* test_data) {
    StringConcatData* data = (StringConcatData*)test_data;
    if (!data || !data->str1 || !data->str2) return 0;
    BreadString* bs1 = bread_string_new(data->str1);
    BreadString* bs2 = bread_string_new(data->str2);
    
    if (!bs1 || !bs2) {
        if (bs1) bread_string_release(bs1);
        if (bs2) bread_string_release(bs2);
        return 0;
    }
    
    BreadString* result = bread_string_concat(bs1, bs2);
    if (!result) {
        bread_string_release(bs1);
        bread_string_release(bs2);
        return 0;
    }
    
    const char* result_str = bread_string_cstr(result);
    size_t expected_len = strlen(data->str1) + strlen(data->str2);
    size_t actual_len = bread_string_len(result);
    
    if (actual_len != expected_len) {
        bread_string_release(bs1);
        bread_string_release(bs2);
        bread_string_release(result);
        return 0;
    }
    
    if (strncmp(result_str, data->str1, strlen(data->str1)) != 0) {
        bread_string_release(bs1);
        bread_string_release(bs2);
        bread_string_release(result);
        return 0;
    }
    
    if (strcmp(result_str + strlen(data->str1), data->str2) != 0) {
        bread_string_release(bs1);
        bread_string_release(bs2);
        bread_string_release(result);
        return 0;
    }
    
    bread_string_release(bs1);
    bread_string_release(bs2);
    bread_string_release(result);
    return 1;
}

int property_string_indexing(void* test_data) {
    StringIndexData* data = (StringIndexData*)test_data;
    if (!data || !data->str) return 0;
    
    BreadString* bs = bread_string_new(data->str);
    if (!bs) return 0;
    
    size_t len = bread_string_len(bs);
    int index = data->index;
    
    int adjusted_index = index;
    if (adjusted_index < 0) {
        adjusted_index = (int)len + adjusted_index;
    }
    
    if (adjusted_index >= 0 && adjusted_index < (int)len) {
        char ch = bread_string_get_char(bs, (size_t)adjusted_index);
        char expected = data->str[adjusted_index];
        
        bread_string_release(bs);
        return (ch == expected);
    } else {
        char ch = bread_string_get_char(bs, (size_t)index);
        
        bread_string_release(bs);
        return (ch == '\0');
    }
}

int property_string_comparison(void* test_data) {
    StringCompareData* data = (StringCompareData*)test_data;
    if (!data || !data->str1 || !data->str2) return 0;
    
    BreadString* bs1 = bread_string_new(data->str1);
    BreadString* bs2 = bread_string_new(data->str2);
    
    if (!bs1 || !bs2) {
        if (bs1) bread_string_release(bs1);
        if (bs2) bread_string_release(bs2);
        return 0;
    }
    
    int bread_eq_result = bread_string_eq(bs1, bs2);
    int expected_eq = (strcmp(data->str1, data->str2) == 0);
    
    if ((bread_eq_result != 0) != (expected_eq != 0)) {
        bread_string_release(bs1);
        bread_string_release(bs2);
        return 0;
    }
    
    int bread_cmp_result = bread_string_cmp(bs1, bs2);
    int expected_cmp = strcmp(data->str1, data->str2);
    int bread_sign = (bread_cmp_result > 0) ? 1 : (bread_cmp_result < 0) ? -1 : 0;
    int expected_sign = (expected_cmp > 0) ? 1 : (expected_cmp < 0) ? -1 : 0;
    
    bread_string_release(bs1);
    bread_string_release(bs2);
    
    return (bread_sign == expected_sign);
}

int run_string_tests() {
    printf("Running String Property Tests\n");
    printf("=============================\n\n");
    
    int all_passed = 1;
    
    PBTResult result3 = pbt_run_property(
        "String concatenation consistency",
        generate_concat_test_data,
        property_string_concatenation,
        cleanup_concat_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 3, 
                     "String concatenation consistency", result3);
    
    if (result3.failed > 0) all_passed = 0;
    
    PBTResult result4 = pbt_run_property(
        "String indexing and bounds checking",
        generate_index_test_data,
        property_string_indexing,
        cleanup_index_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 4, 
                     "String indexing and bounds checking", result4);
    
    if (result4.failed > 0) all_passed = 0;
    PBTResult result5 = pbt_run_property(
        "String comparison correctness",
        generate_compare_test_data,
        property_string_comparison,
        cleanup_compare_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 5, 
                     "String comparison correctness", result5);
    
    if (result5.failed > 0) all_passed = 0;
    
    pbt_free_result(&result3);
    pbt_free_result(&result4);
    pbt_free_result(&result5);
    
    return all_passed;
}

int main() {
    return run_string_tests() ? 0 : 1;
}