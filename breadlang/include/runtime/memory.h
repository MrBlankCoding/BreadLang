#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include "runtime/runtime.h"

// For debugging
typedef struct {
    size_t total_allocations;
    size_t total_deallocations;
    size_t current_objects;
    size_t peak_objects;
    size_t bytes_allocated;
    size_t bytes_freed;
} BreadMemoryStats;
typedef struct BreadObjectNode {
    void* object;
    BreadObjKind kind;
    int marked;
    struct BreadObjectNode* next;
} BreadObjectNode;

typedef struct {
    BreadObjectNode* all_objects;
    BreadMemoryStats stats;
    int cycle_collection_enabled;
    int cycle_collection_threshold;  
    int allocations_since_gc;
    int debug_mode;
} BreadMemoryManager;

void bread_memory_init(void);
void bread_memory_cleanup(void);
void* bread_memory_alloc(size_t size, BreadObjKind kind);
void* bread_memory_realloc(void* ptr, size_t new_size);
void bread_memory_free(void* ptr);
void bread_memory_track_object(void* object, BreadObjKind kind);
void bread_memory_untrack_object(void* object);
void bread_object_retain(void* object);
void bread_object_release(void* object);
uint32_t bread_object_get_refcount(void* object);
// cycle detect
void bread_memory_collect_cycles(void);
void bread_memory_mark_reachable(void* root);
void bread_memory_sweep_unreachable(void);
void bread_memory_set_cycle_collection_threshold(int threshold);
int bread_memory_get_cycle_collection_threshold(void);
BreadMemoryStats bread_memory_get_stats(void);
void bread_memory_print_stats(void);
int bread_memory_check_leaks(void);
void bread_memory_print_leak_report(void);
void bread_memory_enable_debug_mode(int enable);

// exit and clean. Could be improved
void bread_memory_cleanup_all(void);
void bread_memory_cleanup_on_error(void);

#endif