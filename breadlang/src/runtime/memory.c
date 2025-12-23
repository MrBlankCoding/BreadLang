#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/memory.h"
#include "runtime/error.h"

static BreadMemoryManager g_mem = {0};
static int g_mem_initialized = 0;

static void bread_memory_ensure_init(void) {
    if (g_mem_initialized) return;
    memset(&g_mem, 0, sizeof(g_mem));
    g_mem.cycle_collection_enabled = 0;
    g_mem.cycle_collection_threshold = 0;
    g_mem.allocations_since_gc = 0;
    g_mem.debug_mode = 0;
    g_mem_initialized = 1;
}

void bread_memory_init(void) {
    bread_memory_ensure_init();
}

void bread_memory_cleanup(void) {
    if (!g_mem_initialized) return;

    // Best-effort leak report (do not free objects here; individual releases own lifetime).
    if (g_mem.debug_mode) {
        bread_memory_print_stats();
        bread_memory_print_leak_report();
    }
}

void bread_memory_track_object(void* object, BreadObjKind kind) {
    if (!object) return;
    bread_memory_ensure_init();

    BreadObjectNode* node = (BreadObjectNode*)malloc(sizeof(BreadObjectNode));
    if (!node) return;
    node->object = object;
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

void bread_memory_untrack_object(void* object) {
    if (!object || !g_mem_initialized) return;

    BreadObjectNode** cur = &g_mem.all_objects;
    while (*cur) {
        if ((*cur)->object == object) {
            BreadObjectNode* dead = *cur;
            *cur = dead->next;
            free(dead);
            if (g_mem.stats.current_objects > 0) g_mem.stats.current_objects--;
            g_mem.stats.total_deallocations++;
            return;
        }
        cur = &(*cur)->next;
    }
}

void* bread_memory_alloc(size_t size, BreadObjKind kind) {
    bread_memory_ensure_init();

    void* ptr = malloc(size);
    if (!ptr) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
        return NULL;
    }

    memset(ptr, 0, size);

    // All heap objects begin with BreadObjHeader.
    BreadObjHeader* hdr = (BreadObjHeader*)ptr;
    hdr->kind = (uint32_t)kind;
    hdr->refcount = 1;

    g_mem.stats.bytes_allocated += size;
    bread_memory_track_object(ptr, kind);

    g_mem.allocations_since_gc++;
    return ptr;
}

void* bread_memory_realloc(void* ptr, size_t new_size) {
    bread_memory_ensure_init();

    if (!ptr) {
        // Kind unknown; default to string.
        return bread_memory_alloc(new_size, BREAD_OBJ_STRING);
    }

    // Preserve tracking node pointer identity by updating stored object pointer if needed.
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
        return NULL;
    }

    if (new_ptr != ptr) {
        BreadObjectNode* node = g_mem.all_objects;
        while (node) {
            if (node->object == ptr) {
                node->object = new_ptr;
                break;
            }
            node = node->next;
        }
    }

    return new_ptr;
}

void bread_memory_free(void* ptr) {
    if (!ptr) return;

    bread_memory_untrack_object(ptr);
    free(ptr);
}

void bread_object_retain(void* object) {
    if (!object) return;
    BreadObjHeader* hdr = (BreadObjHeader*)object;
    hdr->refcount++;
}

void bread_object_release(void* object) {
    if (!object) return;
    BreadObjHeader* hdr = (BreadObjHeader*)object;
    if (hdr->refcount == 0) return;

    hdr->refcount--;
    if (hdr->refcount == 0) {
        // For most object kinds, specialized *_release functions should be used.
        // This raw free is safe for BreadString and acts as a fallback.
        bread_memory_free(object);
    }
}

uint32_t bread_object_get_refcount(void* object) {
    if (!object) return 0;
    BreadObjHeader* hdr = (BreadObjHeader*)object;
    return hdr->refcount;
}

void bread_memory_collect_cycles(void) {
    // Not implemented (optional feature).
}

void bread_memory_mark_reachable(void* root) {
    (void)root;
}

void bread_memory_sweep_unreachable(void) {
}

void bread_memory_set_cycle_collection_threshold(int threshold) {
    bread_memory_ensure_init();
    g_mem.cycle_collection_threshold = threshold;
}

int bread_memory_get_cycle_collection_threshold(void) {
    if (!g_mem_initialized) return 0;
    return g_mem.cycle_collection_threshold;
}

BreadMemoryStats bread_memory_get_stats(void) {
    bread_memory_ensure_init();
    return g_mem.stats;
}

void bread_memory_print_stats(void) {
    if (!g_mem_initialized) return;
    fprintf(stderr,
            "BreadMemoryStats: allocs=%zu frees=%zu current=%zu peak=%zu bytes_alloc=%zu bytes_free=%zu\n",
            g_mem.stats.total_allocations,
            g_mem.stats.total_deallocations,
            g_mem.stats.current_objects,
            g_mem.stats.peak_objects,
            g_mem.stats.bytes_allocated,
            g_mem.stats.bytes_freed);
}

int bread_memory_check_leaks(void) {
    if (!g_mem_initialized) return 0;
    return g_mem.stats.current_objects != 0;
}

void bread_memory_print_leak_report(void) {
    if (!g_mem_initialized) return;
    if (!bread_memory_check_leaks()) return;

    fprintf(stderr, "BreadLang: potential leaks detected (%zu objects still tracked)\n",
            g_mem.stats.current_objects);

    if (!g_mem.debug_mode) return;

    size_t shown = 0;
    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        fprintf(stderr, "  leak: object=%p kind=%u\n", n->object, (unsigned)n->kind);
        if (++shown >= 50) {
            fprintf(stderr, "  ... (truncated)\n");
            break;
        }
    }
}

void bread_memory_enable_debug_mode(int enable) {
    bread_memory_ensure_init();
    g_mem.debug_mode = enable ? 1 : 0;
}

void bread_memory_cleanup_all(void) {
    bread_memory_cleanup();
}

void bread_memory_cleanup_on_error(void) {
    bread_memory_cleanup();
}
