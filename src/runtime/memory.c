#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "runtime/memory.h"
#include "runtime/error.h"
#include "core/value.h"

static BreadMemoryManager g_memory_manager = {0};

void bread_memory_init(void) {
    memset(&g_memory_manager, 0, sizeof(BreadMemoryManager));
    g_memory_manager.cycle_collection_enabled = 1;
    g_memory_manager.cycle_collection_threshold = 100;  // Collect after 100 allocations
}

void bread_memory_cleanup(void) {
    bread_memory_cleanup_all();
    
    #ifdef DEBUG
    bread_memory_print_stats();
    if (bread_memory_check_leaks()) {
        fprintf(stderr, "Warning: Memory leaks detected!\n");
    }
    #endif
}

void* bread_memory_alloc(size_t size, BreadObjKind kind) {
    void* ptr = malloc(size);
    if (!ptr) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Failed to allocate memory");
        return NULL;
    }
    
    BreadObjHeader* header = (BreadObjHeader*)ptr;
    header->kind = kind;
    header->refcount = 1;
    bread_memory_track_object(ptr, kind);
    g_memory_manager.stats.total_allocations++;
    g_memory_manager.stats.current_objects++;
    g_memory_manager.stats.bytes_allocated += size;
    
    if (g_memory_manager.stats.current_objects > g_memory_manager.stats.peak_objects) {
        g_memory_manager.stats.peak_objects = g_memory_manager.stats.current_objects;
    }
    
    g_memory_manager.allocations_since_gc++;
    if (g_memory_manager.cycle_collection_enabled && 
        g_memory_manager.allocations_since_gc >= g_memory_manager.cycle_collection_threshold &&
        g_memory_manager.stats.current_objects > 50) {  // Only collect if we have enough objects
        bread_memory_collect_cycles();
        g_memory_manager.allocations_since_gc = 0;
    }
    
    return ptr;
}

void* bread_memory_realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return bread_memory_alloc(new_size, BREAD_OBJ_STRING);  // Default to string for realloc
    }
    
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Failed to reallocate memory");
        return NULL;
    }
    
    if (new_ptr != ptr) {
        BreadObjHeader* header = (BreadObjHeader*)ptr;
        bread_memory_untrack_object(ptr);
        bread_memory_track_object(new_ptr, header->kind);
    }
    
    return new_ptr;
}

void bread_memory_free(void* ptr) {
    if (!ptr) return;
    
    bread_memory_untrack_object(ptr);
    g_memory_manager.stats.total_deallocations++;
    g_memory_manager.stats.current_objects--;
    
    free(ptr);
}

void bread_memory_track_object(void* object, BreadObjKind kind) {
    if (!object) return;
    
    BreadObjectNode* node = malloc(sizeof(BreadObjectNode));
    if (!node) return;  // Fail silently to avoid infinite recursion
    
    node->object = object;
    node->kind = kind;
    node->marked = 0;
    node->next = g_memory_manager.all_objects;
    g_memory_manager.all_objects = node;
}

void bread_memory_untrack_object(void* object) {
    if (!object) return;
    
    BreadObjectNode** current = &g_memory_manager.all_objects;
    while (*current) {
        if ((*current)->object == object) {
            BreadObjectNode* to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            return;
        }
        current = &(*current)->next;
    }
}

void bread_object_retain(void* object) {
    if (!object) return;
    
    BreadObjHeader* header = (BreadObjHeader*)object;
    header->refcount++;
}

void bread_object_release(void* object) {
    if (!object) return;
    
    BreadObjHeader* header = (BreadObjHeader*)object;
    if (header->refcount == 0) return;  // Already freed
    
    header->refcount--;
    if (header->refcount == 0) {
        // Immediate deallocation when reference count reaches zero
        // Don't call the specific release functions to avoid recursion
        switch (header->kind) {
            case BREAD_OBJ_STRING:
                bread_memory_free(object);
                break;
            case BREAD_OBJ_ARRAY: {
                BreadArray* array = (BreadArray*)object;
                if (array->items) {
                    for (int i = 0; i < array->count; i++) {
                        bread_value_release(&array->items[i]);
                    }
                    free(array->items);
                }
                bread_memory_free(array);
                break;
            }
            case BREAD_OBJ_DICT: {
                BreadDict* dict = (BreadDict*)object;
                if (dict->entries) {
                    for (int i = 0; i < dict->capacity; i++) {
                        if (dict->entries[i].key) {
                            bread_object_release(dict->entries[i].key);
                            bread_value_release(&dict->entries[i].value);
                        }
                    }
                    free(dict->entries);
                }
                bread_memory_free(dict);
                break;
            }
            case BREAD_OBJ_OPTIONAL: {
                BreadOptional* optional = (BreadOptional*)object;
                if (optional->is_some) {
                    bread_value_release(&optional->value);
                }
                bread_memory_free(optional);
                break;
            }
            default:
                bread_memory_free(object);
                break;
        }
    }
}

uint32_t bread_object_get_refcount(void* object) {
    if (!object) return 0;
    
    BreadObjHeader* header = (BreadObjHeader*)object;
    return header->refcount;
}

void bread_memory_collect_cycles(void) {
    if (!g_memory_manager.cycle_collection_enabled) return;
    BreadObjectNode* node = g_memory_manager.all_objects;
    while (node) {
        node->marked = 0;
        node = node->next;
    }
    
    // Mark reachable objects from stack variables and global roots
    // This is a simplified implementation - in a real system, you'd
    // traverse from actual root objects (stack variables, globals, etc.)
    node = g_memory_manager.all_objects;
    while (node) {
        BreadObjHeader* header = (BreadObjHeader*)node->object;
        if (header->refcount > 0) {
            bread_memory_mark_reachable(node->object);
        }
        node = node->next;
    }
    
    bread_memory_sweep_unreachable();
}

void bread_memory_set_cycle_collection_threshold(int threshold) {
    if (threshold > 0) {
        g_memory_manager.cycle_collection_threshold = threshold;
    }
}

int bread_memory_get_cycle_collection_threshold(void) {
    return g_memory_manager.cycle_collection_threshold;
}

void bread_memory_mark_reachable(void* root) {
    if (!root) return;
    BreadObjectNode* node = g_memory_manager.all_objects;
    while (node) {
        if (node->object == root) {
            if (node->marked) return;  // Already marked
            node->marked = 1;
            
            BreadObjHeader* header = (BreadObjHeader*)root;
            switch (header->kind) {
                case BREAD_OBJ_ARRAY: {
                    BreadArray* array = (BreadArray*)root;
                    for (int i = 0; i < array->count; i++) {
                        BreadValue* item = &array->items[i];
                        switch (item->type) {
                            case TYPE_STRING:
                                bread_memory_mark_reachable(item->value.string_val);
                                break;
                            case TYPE_ARRAY:
                                bread_memory_mark_reachable(item->value.array_val);
                                break;
                            case TYPE_DICT:
                                bread_memory_mark_reachable(item->value.dict_val);
                                break;
                            case TYPE_OPTIONAL:
                                bread_memory_mark_reachable(item->value.optional_val);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                }
                case BREAD_OBJ_DICT: {
                    BreadDict* dict = (BreadDict*)root;
                    for (int i = 0; i < dict->count; i++) {
                        if (dict->entries[i].key) {
                            bread_memory_mark_reachable(dict->entries[i].key);
                            BreadValue* value = &dict->entries[i].value;
                            switch (value->type) {
                                case TYPE_STRING:
                                    bread_memory_mark_reachable(value->value.string_val);
                                    break;
                                case TYPE_ARRAY:
                                    bread_memory_mark_reachable(value->value.array_val);
                                    break;
                                case TYPE_DICT:
                                    bread_memory_mark_reachable(value->value.dict_val);
                                    break;
                                case TYPE_OPTIONAL:
                                    bread_memory_mark_reachable(value->value.optional_val);
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                    break;
                }
                case BREAD_OBJ_OPTIONAL: {
                    BreadOptional* optional = (BreadOptional*)root;
                    if (optional->is_some) {
                        BreadValue* value = &optional->value;
                        switch (value->type) {
                            case TYPE_STRING:
                                bread_memory_mark_reachable(value->value.string_val);
                                break;
                            case TYPE_ARRAY:
                                bread_memory_mark_reachable(value->value.array_val);
                                break;
                            case TYPE_DICT:
                                bread_memory_mark_reachable(value->value.dict_val);
                                break;
                            case TYPE_OPTIONAL:
                                bread_memory_mark_reachable(value->value.optional_val);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            return;
        }
        node = node->next;
    }
}

void bread_memory_sweep_unreachable(void) {
    BreadObjectNode* node = g_memory_manager.all_objects;
    while (node) {
        BreadObjHeader* header = (BreadObjHeader*)node->object;
        if (!node->marked && header->refcount > 0) {
            header->refcount = 1;
            bread_object_release(node->object);
        }
        
        node = node->next;
    }
}

BreadMemoryStats bread_memory_get_stats(void) {
    return g_memory_manager.stats;
}

void bread_memory_print_stats(void) {
    BreadMemoryStats stats = g_memory_manager.stats;
    printf("Memory Statistics:\n");
    printf("  Total allocations: %zu\n", stats.total_allocations);
    printf("  Total deallocations: %zu\n", stats.total_deallocations);
    printf("  Current objects: %zu\n", stats.current_objects);
    printf("  Peak objects: %zu\n", stats.peak_objects);
    printf("  Bytes allocated: %zu\n", stats.bytes_allocated);
    printf("  Bytes freed: %zu\n", stats.bytes_freed);
}

int bread_memory_check_leaks(void) {
    return g_memory_manager.stats.current_objects > 0;
}

void bread_memory_cleanup_all(void) {
    int old_cycle_enabled = g_memory_manager.cycle_collection_enabled;
    g_memory_manager.cycle_collection_enabled = 0;
    
    BreadObjectNode* node = g_memory_manager.all_objects;
    while (node) {
        BreadObjectNode* next = node->next;
        BreadObjHeader* header = (BreadObjHeader*)node->object;
        if (header->refcount > 0) {
            header->refcount = 1;
            bread_object_release(node->object);
        }
        
        free(node);
        node = next;
    }
    
    g_memory_manager.all_objects = NULL;
    g_memory_manager.stats.current_objects = 0;
    g_memory_manager.cycle_collection_enabled = old_cycle_enabled;
}
void bread_memory_print_leak_report(void) {
    if (g_memory_manager.stats.current_objects == 0) {
        printf("No memory leaks detected.\n");
        return;
    }
    
    printf("Memory leak report:\n");
    printf("  Leaked objects: %zu\n", g_memory_manager.stats.current_objects);
    
    if (g_memory_manager.debug_mode) {
        printf("  Leaked object details:\n");
        BreadObjectNode* node = g_memory_manager.all_objects;
        int count = 0;
        while (node && count < 10) {  // Limit to first 10 for readability
            const char* type_name = "Unknown";
            switch (node->kind) {
                case BREAD_OBJ_STRING: type_name = "String"; break;
                case BREAD_OBJ_ARRAY: type_name = "Array"; break;
                case BREAD_OBJ_DICT: type_name = "Dict"; break;
                case BREAD_OBJ_OPTIONAL: type_name = "Optional"; break;
            }
            
            BreadObjHeader* header = (BreadObjHeader*)node->object;
            printf("    %s object at %p (refcount: %u)\n", 
                   type_name, node->object, header->refcount);
            
            node = node->next;
            count++;
        }
        
        if (g_memory_manager.stats.current_objects > 10) {
            printf("    ... and %zu more objects\n", 
                   g_memory_manager.stats.current_objects - 10);
        }
    }
}

void bread_memory_enable_debug_mode(int enable) {
    g_memory_manager.debug_mode = enable ? 1 : 0;
}

void bread_memory_cleanup_on_error(void) {
    if (g_memory_manager.cycle_collection_enabled) {
        bread_memory_collect_cycles();
    }
    
    if (g_memory_manager.debug_mode && bread_memory_check_leaks()) {
        fprintf(stderr, "Warning: Memory leaks detected during error cleanup:\n");
        bread_memory_print_leak_report();
    }
    
    bread_memory_cleanup_all();
}