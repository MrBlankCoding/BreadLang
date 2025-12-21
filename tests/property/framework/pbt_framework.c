#include "pbt_framework.h"

void pbt_init_generator(PBTGenerator* gen, uint32_t seed) {
    gen->seed = seed;
    gen->iteration = 0;
}

// Simple LCG for reproducible random numbers
uint32_t pbt_random_uint32(PBTGenerator* gen) {
    gen->seed = (gen->seed * 1103515245 + 12345) & 0x7fffffff;
    return gen->seed;
}

int pbt_random_int(PBTGenerator* gen, int min, int max) {
    if (min >= max) return min;
    uint32_t range = max - min;
    return min + (pbt_random_uint32(gen) % range);
}

char* pbt_random_string(PBTGenerator* gen, int max_length) {
    int length = pbt_random_int(gen, 0, max_length);
    char* str = malloc(length + 1);
    if (!str) return NULL;
    
    for (int i = 0; i < length; i++) {
        // Generate printable ASCII characters
        str[i] = (char)(32 + (pbt_random_uint32(gen) % 95));
    }
    str[length] = '\0';
    return str;
}

PBTResult pbt_run_property(const char* property_name,
                          pbt_generator_fn generator,
                          pbt_property_fn property,
                          pbt_cleanup_fn cleanup,
                          int iterations) {
    PBTResult result = {0, 0, 0, NULL, NULL};
    
    if (iterations < PBT_MIN_ITERATIONS) {
        iterations = PBT_MIN_ITERATIONS;
    }
    
    PBTGenerator gen;
    pbt_init_generator(&gen, (uint32_t)time(NULL));
    
    for (int i = 0; i < iterations; i++) {
        gen.iteration = i;
        void* test_data = generator(&gen);
        
        if (!test_data) {
            result.failed++;
            result.total++;
            if (!result.failure_message) {
                result.failure_message = strdup("Generator failed to create test data");
            }
            continue;
        }
        
        int property_holds = property(test_data);
        result.total++;
        
        if (property_holds) {
            result.passed++;
        } else {
            result.failed++;
            if (!result.failure_message) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "Property '%s' failed on iteration %d", property_name, i);
                result.failure_message = strdup(buffer);
                
                // Create a simple counterexample description
                snprintf(buffer, sizeof(buffer), "Iteration %d with seed %u", i, gen.seed);
                result.counterexample = strdup(buffer);
            }
        }
        
        if (cleanup) {
            cleanup(test_data);
        }
        
        if (result.failed > 0) {
            break;
        }
    }
    
    return result;
}

void pbt_free_result(PBTResult* result) {
    if (result->failure_message) {
        free(result->failure_message);
        result->failure_message = NULL;
    }
    if (result->counterexample) {
        free(result->counterexample);
        result->counterexample = NULL;
    }
}

void pbt_report_result(const char* feature, int property_num, const char* property_text, PBTResult result) {
    printf("Feature: %s, Property %d: %s\n", feature, property_num, property_text);
    printf("  Total: %d, Passed: %d, Failed: %d\n", result.total, result.passed, result.failed);
    
    if (result.failed > 0) {
        printf("  FAILED: %s\n", result.failure_message ? result.failure_message : "Unknown failure");
        if (result.counterexample) {
            printf("  Counterexample: %s\n", result.counterexample);
        }
    } else {
        printf("  PASSED\n");
    }
    printf("\n");
}