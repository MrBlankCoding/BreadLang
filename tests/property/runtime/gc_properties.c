#include "../framework/pbt_framework.h"
#include "runtime/memory.h"
#include "core/value.h"
#include <stdio.h>
#include <stdlib.h>

// Property: Garbage collection should not crash with cyclic references
static bool test_gc_cyclic_references(void) {
    bread_memory_init();
    
    // Create circular reference
    BreadValue* arr1 = bread_value_create_array();
    BreadValue* arr2 = bread_value_create_array();
    
    if (!arr1 || !arr2) return false;
    
    bread_value_array_append(arr1, arr2);
    bread_value_array_append(arr2, arr1);
    
    // Should handle cleanup without crashing
    bread_value_free(arr1);
    bread_value_free(arr2);
    
    bread_memory_cleanup();
    return true;
}

// Property: Memory allocation should be consistent
static bool test_memory_allocation_consistency(void) {
    bread_memory_init();
    
    void* ptrs[100];
    
    // Allocate memory
    for (int i = 0; i < 100; i++) {
        ptrs[i] = bread_malloc(100);
        if (!ptrs[i]) return false;
    }
    
    // Free memory
    for (int i = 0; i < 100; i++) {
        bread_free(ptrs[i]);
    }
    
    bread_memory_cleanup();
    return true;
}

// Property: Reference counting should work correctly
static bool test_reference_counting(void) {
    bread_memory_init();
    
    BreadValue* val = bread_value_create_int(42);
    if (!val) return false;
    
    // Increment reference count
    bread_value_retain(val);
    bread_value_retain(val);
    
    // Decrement reference count
    bread_value_release(val);
    bread_value_release(val);
    
    // Final release should free
    bread_value_release(val);
    
    bread_memory_cleanup();
    return true;
}

int main(void) {
    pbt_init("Garbage Collection Properties");
    
    pbt_property("GC handles cyclic references", test_gc_cyclic_references);
    pbt_property("Memory allocation consistency", test_memory_allocation_consistency);
    pbt_property("Reference counting correctness", test_reference_counting);
    
    return pbt_run();
}