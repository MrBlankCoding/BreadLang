#ifndef PBT_FRAMEWORK_H
#define PBT_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

// Property-Based Testing Framework for BreadLang
// Minimum 100 iterations per property test as specified in design

#define PBT_MIN_ITERATIONS 100
#define PBT_MAX_SHRINK_ATTEMPTS 50

typedef struct {
    int passed;
    int failed;
    int total;
    char* failure_message;
    char* counterexample;
} PBTResult;

typedef struct {
    uint32_t seed;
    int iteration;
} PBTGenerator;

typedef void* (*pbt_generator_fn)(PBTGenerator* gen);
typedef void (*pbt_cleanup_fn)(void* data);
typedef int (*pbt_property_fn)(void* data);

PBTResult pbt_run_property(const char* property_name, 
                          pbt_generator_fn generator,
                          pbt_property_fn property,
                          pbt_cleanup_fn cleanup,
                          int iterations);

void pbt_init_generator(PBTGenerator* gen, uint32_t seed);
uint32_t pbt_random_uint32(PBTGenerator* gen);
int pbt_random_int(PBTGenerator* gen, int min, int max);
char* pbt_random_string(PBTGenerator* gen, int max_length);
void pbt_free_result(PBTResult* result);
void pbt_report_result(const char* feature, int property_num, const char* property_text, PBTResult result);

#endif