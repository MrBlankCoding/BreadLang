#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "runtime/memory.h"
#include "runtime/error.h"
#include "core/value.h"

#define BREAD_DEFAULT_GC_THRESHOLD 1000
#define BREAD_GC_GROWTH_FACTOR 2
#define BREAD_MAX_STACK_DEPTH 8192
#define BREAD_LEAK_REPORT_LIMIT 100
#define BREAD_POOL_BLOCK_SIZE 64
#define BREAD_POOL_BLOCKS_PER_CHUNK 256

typedef struct BreadMemoryChunk {
    uint8_t blocks[BREAD_POOL_BLOCKS_PER_CHUNK][BREAD_POOL_BLOCK_SIZE];
    uint64_t free_bitmap[BREAD_POOL_BLOCKS_PER_CHUNK / 64];
    struct BreadMemoryChunk* next;
    size_t free_count;
} BreadMemoryChunk;


static BreadMemoryManager g_mem = {0};
static int g_mem_initialized = 0;

static inline void bread_memory_ensure_init(void) {
    if (g_mem_initialized) return;

    memset(&g_mem, 0, sizeof(g_mem));
    g_mem.cycle_collection_enabled = 0;
    g_mem.gc_threshold = BREAD_DEFAULT_GC_THRESHOLD;
    g_mem.bytes_threshold = 1024 * 1024; /* 1MB default */
    g_mem.auto_gc_enabled = 1;
    g_mem.debug_mode = 0;

    g_mem_initialized = 1;
}

static BreadObjectNode* bread_memory_find_node(const void* object) {
    if (!object) return NULL;
    
    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        if (n->object == object) return n;
    }
    return NULL;
}

static inline int bread_memory_should_trigger_gc(void) {
    if (!g_mem.auto_gc_enabled || !g_mem.cycle_collection_enabled) {
        return 0;
    }
    
    return (g_mem.allocations_since_gc >= g_mem.gc_threshold) ||
           (g_mem.bytes_since_gc >= g_mem.bytes_threshold);
}

static BreadMemoryChunk* bread_pool_alloc_chunk(void) {
    BreadMemoryChunk* chunk = calloc(1, sizeof(BreadMemoryChunk));
    if (!chunk) return NULL;
    
    // mark all blocks as free
    memset(chunk->free_bitmap, 0xFF, sizeof(chunk->free_bitmap));
    chunk->free_count = BREAD_POOL_BLOCKS_PER_CHUNK;
    chunk->next = NULL;
    
    return chunk;
}

static void* bread_pool_alloc(BreadMemoryPool* pool) {
    if (!pool->chunks || pool->chunks->free_count == 0) {
        BreadMemoryChunk* new_chunk = bread_pool_alloc_chunk();
        if (!new_chunk) return NULL;
        
        new_chunk->next = pool->chunks;
        pool->chunks = new_chunk;
        pool->total_chunks++;
    }
    
    BreadMemoryChunk* chunk = pool->chunks;
    
    // find the first tree block
    for (size_t i = 0; i < BREAD_POOL_BLOCKS_PER_CHUNK / 64; i++) {
        if (chunk->free_bitmap[i] == 0) continue;
        uint64_t bitmap = chunk->free_bitmap[i];
        int bit = __builtin_ctzll(bitmap);
        
        chunk->free_bitmap[i] &= ~(1ULL << bit);
        chunk->free_count--;
        
        size_t block_idx = i * 64 + bit;
        return &chunk->blocks[block_idx][0];
    }
    
    return NULL; // SHOULD NEVER HAPPEN
}

static int bread_pool_free(BreadMemoryPool* pool, void* ptr) {
    if (!ptr || !pool->chunks) return 0;
    
    for (BreadMemoryChunk* chunk = pool->chunks; chunk; chunk = chunk->next) {
        uintptr_t chunk_start = (uintptr_t)&chunk->blocks[0][0];
        uintptr_t chunk_end = chunk_start + sizeof(chunk->blocks);
        uintptr_t ptr_addr = (uintptr_t)ptr;
        
        if (ptr_addr >= chunk_start && ptr_addr < chunk_end) {
            size_t offset = ptr_addr - chunk_start;
            size_t block_idx = offset / BREAD_POOL_BLOCK_SIZE;
            
            if (block_idx >= BREAD_POOL_BLOCKS_PER_CHUNK) return 0;
            
            size_t bitmap_idx = block_idx / 64;
            size_t bit_idx = block_idx % 64;
            
            chunk->free_bitmap[bitmap_idx] |= (1ULL << bit_idx);
            chunk->free_count++;
            
            return 1;
        }
    }
    
    return 0;
}

static void bread_pool_cleanup(BreadMemoryPool* pool) {
    BreadMemoryChunk* chunk = pool->chunks;
    while (chunk) {
        BreadMemoryChunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
    pool->chunks = NULL;
    pool->total_chunks = 0;
}

static void bread_memory_track_object_internal(void* object, size_t size, BreadObjKind kind) {
    if (!object) return;
    
    BreadObjectNode* node = malloc(sizeof(BreadObjectNode));
    if (!node) {
        // tracking failed but obj allocated. no good. 
        fprintf(stderr, "CRITICAL: Failed to track object %p\n", object);
        return;
    }

    node->object = object;
    node->size = size;
    node->kind = kind;
    node->marked = 0;
    node->next = g_mem.all_objects;
    g_mem.all_objects = node;

    g_mem.stats.total_allocations++;
    g_mem.stats.current_objects++;
    
    if (g_mem.stats.current_objects > g_mem.stats.peak_objects) {
        g_mem.stats.peak_objects = g_mem.stats.current_objects;
    }
}

static void bread_memory_untrack_object_internal(void* object) {
    if (!object) return;

    BreadObjectNode** cur = &g_mem.all_objects;
    while (*cur) {
        if ((*cur)->object == object) {
            BreadObjectNode* dead = *cur;
            *cur = dead->next;

            g_mem.stats.bytes_freed += dead->size;
            g_mem.stats.total_deallocations++;
            
            if (g_mem.stats.current_objects > 0) {
                g_mem.stats.current_objects--;
            }

            free(dead);
            return;
        }
        cur = &(*cur)->next;
    }
    
    if (g_mem.debug_mode) {
        fprintf(stderr, "Warning: Untracking unknown object %p\n", object);
    }
}

void bread_memory_init(void) {
    bread_memory_ensure_init();
}

void bread_memory_cleanup(void) {
    if (!g_mem_initialized) return;

    if (g_mem.debug_mode) {
        bread_memory_print_stats();
        bread_memory_print_leak_report();
    }

    // free all
    BreadObjectNode* n = g_mem.all_objects;
    while (n) {
        BreadObjectNode* next = n->next;
        
        if (g_mem.debug_mode && n->object) {
            BreadObjHeader* hdr = (BreadObjHeader*)n->object;
            if (hdr->refcount > 0) {
                fprintf(stderr, "Cleanup: freeing object %p with refcount %u\n", 
                        n->object, hdr->refcount);
            }
        }
        
        free(n);
        n = next;
    }

    g_mem.all_objects = NULL;
    
    // cleanup pool
    bread_pool_cleanup(&g_mem.small_pool);
    free(g_mem.gc_roots);
    g_mem.gc_roots = NULL;
    g_mem.gc_root_count = 0;
    g_mem.gc_root_capacity = 0;
    
    g_mem_initialized = 0;
}

void bread_memory_track_object(void* object, size_t size, BreadObjKind kind) {
    bread_memory_ensure_init();
    bread_memory_track_object_internal(object, size, kind);
}

void bread_memory_untrack_object(void* object) {
    if (!g_mem_initialized) return;
    bread_memory_untrack_object_internal(object);
}

void* bread_memory_alloc(size_t size, BreadObjKind kind) {
    bread_memory_ensure_init();
    
    void* ptr = NULL;
    if (size <= BREAD_POOL_BLOCK_SIZE) {
        ptr = bread_pool_alloc(&g_mem.small_pool);
        if (ptr) {
            memset(ptr, 0, size);
        }
    }
    
    if (!ptr) {
        ptr = malloc(size);
        if (!ptr) {
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
            return NULL;
        }
        memset(ptr, 0, size);
    }

    BreadObjHeader* hdr = (BreadObjHeader*)ptr;
    hdr->kind = (uint32_t)kind;
    hdr->refcount = 1;
    g_mem.stats.bytes_allocated += size;
    g_mem.allocations_since_gc++;
    g_mem.bytes_since_gc += size;
    bread_memory_track_object_internal(ptr, size, kind);

    if (bread_memory_should_trigger_gc()) {
        bread_memory_collect_cycles();
    }
    
    return ptr;
}

void* bread_memory_realloc(void* ptr, size_t new_size) {
    bread_memory_ensure_init();

    if (!ptr) {
        return bread_memory_alloc(new_size, BREAD_OBJ_STRING);
    }

    BreadObjectNode* node = bread_memory_find_node(ptr);
    size_t old_size = node ? node->size : 0;
    int was_pooled = bread_pool_free(&g_mem.small_pool, ptr);
    void* new_ptr = NULL;
    
    if (new_size <= BREAD_POOL_BLOCK_SIZE) {
        new_ptr = bread_pool_alloc(&g_mem.small_pool);
        if (new_ptr && ptr) {
            memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
            if (!was_pooled) {
                free(ptr);
            }
        }
    }
    
    if (!new_ptr) {
        if (was_pooled) {
            // pool to heap
            new_ptr = malloc(new_size);
            if (new_ptr && ptr) {
                memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
            }
        } else {
            new_ptr = realloc(ptr, new_size);
        }
    }
    
    if (!new_ptr) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
        return NULL;
    }

    // zero new memory
    if (new_size > old_size) {
        memset((char*)new_ptr + old_size, 0, new_size - old_size);
        g_mem.stats.bytes_allocated += (new_size - old_size);
        g_mem.bytes_since_gc += (new_size - old_size);
    } else if (old_size > new_size) {
        g_mem.stats.bytes_freed += (old_size - new_size);
    }

    if (node) {
        node->object = new_ptr;
        node->size = new_size;
    }

    return new_ptr;
}

void bread_memory_free(void* ptr) {
    if (!ptr) return;
    
    bread_memory_untrack_object_internal(ptr);
    
    // try pool first
    if (!bread_pool_free(&g_mem.small_pool, ptr)) {
        free(ptr);
    }
}

void bread_object_retain(void* object) {
    if (!object) return;

    BreadObjHeader* hdr = (BreadObjHeader*)object;
    
    if (hdr->refcount == UINT32_MAX) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Refcount overflow");
        return;
    }

    hdr->refcount++;
}

void bread_object_release(void* object) {
    if (!object) return;

    BreadObjHeader* hdr = (BreadObjHeader*)object;
    
    if (hdr->refcount == 0) {
        if (g_mem.debug_mode) {
            fprintf(stderr, "ERROR: Double free detected on object %p\n", object);
        }
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Double free detected");
        return;
    }

    hdr->refcount--;
    
    if (hdr->refcount == 0) {
        bread_memory_free(object);
    }
}

uint32_t bread_object_get_refcount(void* object) {
    if (!object) return 0;
    return ((BreadObjHeader*)object)->refcount;
}

int bread_memory_add_root(void* root) {
    bread_memory_ensure_init();
    
    if (!root) return 0;
    
    if (g_mem.gc_root_count >= g_mem.gc_root_capacity) {
        size_t new_capacity = g_mem.gc_root_capacity == 0 ? 16 : g_mem.gc_root_capacity * 2;
        void** new_roots = realloc(g_mem.gc_roots, new_capacity * sizeof(void*));
        
        if (!new_roots) return 0;
        
        g_mem.gc_roots = new_roots;
        g_mem.gc_root_capacity = new_capacity;
    }
    
    g_mem.gc_roots[g_mem.gc_root_count++] = root;
    return 1;
}

void bread_memory_remove_root(void* root) {
    if (!g_mem_initialized || !root) return;
    
    for (size_t i = 0; i < g_mem.gc_root_count; i++) {
        if (g_mem.gc_roots[i] == root) {
            g_mem.gc_roots[i] = g_mem.gc_roots[g_mem.gc_root_count - 1];
            g_mem.gc_root_count--;
            return;
        }
    }
}

static void bread_memory_mark_value(BreadValue* v, void** stack, int* top, int max_depth) {
    if (!v || *top >= max_depth) return;
    
    void* child = NULL;
    
    switch (v->type) {
        case TYPE_STRING:   child = v->value.string_val;   break;
        case TYPE_ARRAY:    child = v->value.array_val;    break;
        case TYPE_DICT:     child = v->value.dict_val;     break;
        case TYPE_OPTIONAL: child = v->value.optional_val; break;
        case TYPE_STRUCT:   child = v->value.struct_val;   break;
        case TYPE_CLASS:    child = v->value.class_val;    break;
        default: return;
    }
    
    if (child) {
        BreadObjectNode* cn = bread_memory_find_node(child);
        if (cn && !cn->marked) {
            stack[(*top)++] = cn;
        }
    }
}

void bread_memory_mark_reachable(void* root) {
    bread_memory_ensure_init();
    if (!root) return;

    BreadObjectNode* start = bread_memory_find_node(root);
    if (!start || start->marked) return;
    void** stack = malloc(BREAD_MAX_STACK_DEPTH * sizeof(void*));
    if (!stack) {
        fprintf(stderr, "ERROR: Failed to allocate GC stack\n");
        return;
    }
    
    int top = 0;
    stack[top++] = start;

    while (top > 0) {
        BreadObjectNode* cur = (BreadObjectNode*)stack[--top];
        
        if (!cur || cur->marked) continue;
        cur->marked = 1;

        BreadObjHeader* hdr = (BreadObjHeader*)cur->object;
        if (!hdr) continue;

        switch ((BreadObjKind)hdr->kind) {
            case BREAD_OBJ_ARRAY: {
                BreadArray* a = (BreadArray*)cur->object;
                if (a && a->items) {
                    for (int i = 0; i < a->count; i++) {
                        bread_memory_mark_value(&a->items[i], (void**)stack, &top, BREAD_MAX_STACK_DEPTH);
                    }
                }
                break;
            }
            
            case BREAD_OBJ_DICT: {
                BreadDict* d = (BreadDict*)cur->object;
                if (d && d->entries) {
                    for (int i = 0; i < d->capacity; i++) {
                        if (d->entries[i].is_occupied && !d->entries[i].is_deleted) {
                            bread_memory_mark_value(&d->entries[i].key, (void**)stack, &top, BREAD_MAX_STACK_DEPTH);
                            bread_memory_mark_value(&d->entries[i].value, (void**)stack, &top, BREAD_MAX_STACK_DEPTH);
                        }
                    }
                }
                break;
            }
            
            case BREAD_OBJ_OPTIONAL: {
                BreadOptional* o = (BreadOptional*)cur->object;
                if (o && o->is_some) {
                    bread_memory_mark_value(&o->value, (void**)stack, &top, BREAD_MAX_STACK_DEPTH);
                }
                break;
            }
            
            case BREAD_OBJ_STRUCT: {
                BreadStruct* s = (BreadStruct*)cur->object;
                if (s && s->field_values) {
                    for (int i = 0; i < s->field_count; i++) {
                        bread_memory_mark_value(&s->field_values[i], (void**)stack, &top, BREAD_MAX_STACK_DEPTH);
                    }
                }
                break;
            }
            
            case BREAD_OBJ_CLASS: {
                BreadClass* c = (BreadClass*)cur->object;
                if (c && c->field_values) {
                    for (int i = 0; i < c->field_count; i++) {
                        bread_memory_mark_value(&c->field_values[i], (void**)stack, &top, BREAD_MAX_STACK_DEPTH);
                    }
                }
                break;
            }
            
            case BREAD_OBJ_STRING:
            case BREAD_OBJ_UNKNOWN:
            default:
                break;
        }
    }
    
    free(stack);
}


void bread_memory_collect_cycles(void) {
    bread_memory_ensure_init();

    if (!g_mem.cycle_collection_enabled) {
        return;
    }
    
    size_t objects_before = g_mem.stats.current_objects;
    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        n->marked = 0;
    }

    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        BreadObjHeader* hdr = (BreadObjHeader*)n->object;
        if (hdr && hdr->refcount > 0) {
            bread_memory_mark_reachable(n->object);
        }
    }
    
    for (size_t i = 0; i < g_mem.gc_root_count; i++) {
        bread_memory_mark_reachable(g_mem.gc_roots[i]);
    }

    bread_memory_sweep_unreachable();
    
    g_mem.allocations_since_gc = 0;
    g_mem.bytes_since_gc = 0;
    
    size_t objects_freed = objects_before > g_mem.stats.current_objects ? 
                          objects_before - g_mem.stats.current_objects : 0;
    
    if (objects_freed < g_mem.gc_threshold / 10) {
        g_mem.gc_threshold = (g_mem.gc_threshold * 3) / 2;
    }
    
    if (g_mem.debug_mode) {
        fprintf(stderr, "GC: freed %zu objects, %zu remaining, threshold now %zu\n",
                objects_freed, g_mem.stats.current_objects, g_mem.gc_threshold);
    }
}

void bread_memory_sweep_unreachable(void) {
    bread_memory_ensure_init();
    
    BreadObjectNode** cur = &g_mem.all_objects;
    
    while (*cur) {
        BreadObjectNode* node = *cur;
        
        if (!node->marked) {
            BreadObjHeader* hdr = (BreadObjHeader*)node->object;
            
            if (hdr && hdr->refcount == 0) {
                *cur = node->next;
                
                if (!bread_pool_free(&g_mem.small_pool, node->object)) {
                    free(node->object);
                }
                
                g_mem.stats.bytes_freed += node->size;
                g_mem.stats.total_deallocations++;
                
                if (g_mem.stats.current_objects > 0) {
                    g_mem.stats.current_objects--;
                }
                
                free(node);
                continue;
            }
        }
        
        cur = &(*cur)->next;
    }
}

BreadMemoryStats bread_memory_get_stats(void) {
    bread_memory_ensure_init();
    return g_mem.stats;
}

void bread_memory_print_stats(void) {
    if (!g_mem_initialized) return;

    fprintf(stderr,
        "\n=== Bread Memory Statistics ===\n"
        "Allocations:     %zu\n"
        "Deallocations:   %zu\n"
        "Current objects: %zu\n"
        "Peak objects:    %zu\n"
        "Bytes allocated: %zu\n"
        "Bytes freed:     %zu\n"
        "Net memory:      %zu bytes\n"
        "Pool chunks:     %zu\n"
        "GC threshold:    %zu\n"
        "================================\n",
        g_mem.stats.total_allocations,
        g_mem.stats.total_deallocations,
        g_mem.stats.current_objects,
        g_mem.stats.peak_objects,
        g_mem.stats.bytes_allocated,
        g_mem.stats.bytes_freed,
        g_mem.stats.bytes_allocated > g_mem.stats.bytes_freed ?
            g_mem.stats.bytes_allocated - g_mem.stats.bytes_freed : 0,
        g_mem.small_pool.total_chunks,
        g_mem.gc_threshold);
}

int bread_memory_check_leaks(void) {
    return g_mem_initialized && g_mem.stats.current_objects != 0;
}

void bread_memory_print_leak_report(void) {
    if (!bread_memory_check_leaks()) {
        if (g_mem.debug_mode) {
            fprintf(stderr, "No memory leaks detected.\n");
        }
        return;
    }

    fprintf(stderr,
        "\n!!! MEMORY LEAK DETECTED !!!\n"
        "Leaked objects: %zu\n\n",
        g_mem.stats.current_objects);

    if (!g_mem.debug_mode) {
        fprintf(stderr, "Enable debug mode for detailed leak report.\n");
        return;
    }

    size_t shown = 0;
    const char* kind_names[] = {
        "UNKNOWN", "STRING", "ARRAY", "DICT", 
        "OPTIONAL", "STRUCT", "CLASS"
    };
    
    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        BreadObjHeader* hdr = (BreadObjHeader*)n->object;
        const char* kind_name = (n->kind < sizeof(kind_names)/sizeof(kind_names[0])) ?
                               kind_names[n->kind] : "INVALID";
        
        fprintf(stderr,
            "  [%zu] %p: kind=%s size=%zu refcount=%u marked=%d\n",
            shown + 1, n->object, kind_name, n->size,
            hdr ? hdr->refcount : 0, n->marked);
        
        if (++shown >= BREAD_LEAK_REPORT_LIMIT) {
            fprintf(stderr, "  ... (%zu more objects not shown)\n",
                    g_mem.stats.current_objects - shown);
            break;
        }
    }
    
    fprintf(stderr, "\n");
}

void bread_memory_enable_debug_mode(int enable) {
    bread_memory_ensure_init();
    g_mem.debug_mode = enable ? 1 : 0;
    
    if (enable) {
        fprintf(stderr, "Bread memory debug mode enabled\n");
    }
}

void bread_memory_set_cycle_collection_threshold(int threshold) {
    bread_memory_ensure_init();
    
    if (threshold <= 0) {
        g_mem.cycle_collection_enabled = 0;
        g_mem.gc_threshold = 0;
        return;
    }
    
    g_mem.cycle_collection_enabled = 1;
    g_mem.gc_threshold = threshold;
}

int bread_memory_get_cycle_collection_threshold(void) {
    bread_memory_ensure_init();
    return (int)g_mem.gc_threshold;
}

void bread_memory_enable_auto_gc(int enable) {
    bread_memory_ensure_init();
    g_mem.auto_gc_enabled = enable ? 1 : 0;
}

void bread_memory_set_bytes_threshold(size_t bytes) {
    bread_memory_ensure_init();
    g_mem.bytes_threshold = bytes;
}

void bread_memory_cleanup_all(void) {
    bread_memory_cleanup();
    bread_string_intern_cleanup();
}

void bread_memory_cleanup_on_error(void) {
    if (g_mem.debug_mode) {
        bread_memory_print_leak_report();
    }
    bread_memory_cleanup();
}
