#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "runtime/error.h"

BreadOptional* bread_optional_new_none(void) {
    BreadOptional* o = (BreadOptional*)bread_memory_alloc(sizeof(BreadOptional), BREAD_OBJ_OPTIONAL);
    if (!o) return NULL;
    o->is_some = 0;
    memset(&o->value, 0, sizeof(BreadValue));
    return o;
}

BreadOptional* bread_optional_new_some(BreadValue v) {
    BreadOptional* o = (BreadOptional*)bread_memory_alloc(sizeof(BreadOptional), BREAD_OBJ_OPTIONAL);
    if (!o) return NULL;
    o->is_some = 1;
    o->value = bread_value_clone(v);
    return o;
}

void bread_optional_retain(BreadOptional* o) {
    bread_object_retain(o);
}

void bread_optional_release(BreadOptional* o) {
    if (!o) return;
    
    BreadObjHeader* header = (BreadObjHeader*)o;
    if (header->refcount == 0) return;  // Already freed
    
    header->refcount--;
    if (header->refcount == 0) {
        if (o->is_some) {
            bread_value_release(&o->value);
        }
        bread_memory_free(o);
    }
}