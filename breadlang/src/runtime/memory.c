#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

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

static BreadObjectNode* bread_memory_find_node(void* object) {
    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        if (n->object == object) return n;
    }
    return NULL;
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

    /* Free tracking nodes (objects should already be freed) */
    BreadObjectNode* n = g_mem.all_objects;
    while (n) {
        BreadObjectNode* next = n->next;
        free(n);
        n = next;
    }

    g_mem.all_objects = NULL;
}

void bread_memory_track_object(void* object, size_t size, BreadObjKind kind) {
    if (!object) return;
    bread_memory_ensure_init();

    BreadObjectNode* node = malloc(sizeof(BreadObjectNode));
    if (!node) return;

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

void bread_memory_untrack_object(void* object) {
    if (!object || !g_mem_initialized) return;

    BreadObjectNode** cur = &g_mem.all_objects;
    while (*cur) {
        if ((*cur)->object == object) {
            BreadObjectNode* dead = *cur;
            *cur = dead->next;

            g_mem.stats.bytes_freed += dead->size;
            g_mem.stats.total_deallocations++;
            if (g_mem.stats.current_objects > 0)
                g_mem.stats.current_objects--;

            free(dead);
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

    BreadObjHeader* hdr = (BreadObjHeader*)ptr;
    hdr->kind = (uint32_t)kind;
    hdr->refcount = 1;

    g_mem.stats.bytes_allocated += size;
    g_mem.allocations_since_gc++;

    bread_memory_track_object(ptr, size, kind);
    return ptr;
}

void* bread_memory_realloc(void* ptr, size_t new_size) {
    bread_memory_ensure_init();

    if (!ptr) {
        return bread_memory_alloc(new_size, BREAD_OBJ_STRING);
    }

    BreadObjectNode* node = bread_memory_find_node(ptr);
    if (!node) {
        /* Untracked pointer: treat as raw realloc */
        return realloc(ptr, new_size);
    }

    size_t old_size = node->size;
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
        return NULL;
    }

    if (new_size > old_size) {
        memset((char*)new_ptr + old_size, 0, new_size - old_size);
        g_mem.stats.bytes_allocated += (new_size - old_size);
    } else {
        g_mem.stats.bytes_freed += (old_size - new_size);
    }

    node->object = new_ptr;
    node->size = new_size;

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

void bread_memory_collect_cycles(void) {
#ifdef BREAD_DEBUG
    fprintf(stderr, "Cycle collection not implemented\n");
#endif
}

void bread_memory_mark_reachable(void* root) {
    (void)root;
}

void bread_memory_sweep_unreachable(void) {
}

BreadMemoryStats bread_memory_get_stats(void) {
    bread_memory_ensure_init();
    return g_mem.stats;
}

void bread_memory_print_stats(void) {
    if (!g_mem_initialized) return;

    fprintf(stderr,
        "BreadMemoryStats:\n"
        "  allocs=%zu frees=%zu\n"
        "  current=%zu peak=%zu\n"
        "  bytes_alloc=%zu bytes_free=%zu\n",
        g_mem.stats.total_allocations,
        g_mem.stats.total_deallocations,
        g_mem.stats.current_objects,
        g_mem.stats.peak_objects,
        g_mem.stats.bytes_allocated,
        g_mem.stats.bytes_freed);
}

int bread_memory_check_leaks(void) {
    return g_mem_initialized && g_mem.stats.current_objects != 0;
}

void bread_memory_print_leak_report(void) {
    if (!bread_memory_check_leaks()) return;

    fprintf(stderr,
        "BreadLang: potential leaks detected (%zu objects)\n",
        g_mem.stats.current_objects);

    if (!g_mem.debug_mode) return;

    size_t shown = 0;
    for (BreadObjectNode* n = g_mem.all_objects; n; n = n->next) {
        fprintf(stderr,
            "  leak: %p kind=%u size=%zu\n",
            n->object, (unsigned)n->kind, n->size);
        if (++shown >= 50) {
            fprintf(stderr, "  ... truncated\n");
            break;
        }
    }
}

void bread_memory_enable_debug_mode(int enable) {
    bread_memory_ensure_init();
    g_mem.debug_mode = enable ? 1 : 0;
}
