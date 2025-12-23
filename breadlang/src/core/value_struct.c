#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "runtime/error.h"

BreadStruct* bread_struct_new(const char* type_name, int field_count, char** field_names) {
    if (!type_name || field_count < 0 || (field_count > 0 && !field_names)) {
        return NULL;
    }
    
    BreadStruct* s = (BreadStruct*)bread_memory_alloc(sizeof(BreadStruct), BREAD_OBJ_STRUCT);
    if (!s) return NULL;
    
    s->type_name = strdup(type_name);
    s->field_count = field_count;
    
    if (field_count > 0) {
        s->field_names = malloc(field_count * sizeof(char*));
        s->field_values = malloc(field_count * sizeof(BreadValue));
        
        if (!s->field_names || !s->field_values) {
            free(s->type_name);
            free(s->field_names);
            free(s->field_values);
            bread_memory_free(s);
            return NULL;
        }
        
        for (int i = 0; i < field_count; i++) {
            s->field_names[i] = strdup(field_names[i]);
            memset(&s->field_values[i], 0, sizeof(BreadValue));
            s->field_values[i].type = TYPE_NIL;
        }
    } else {
        s->field_names = NULL;
        s->field_values = NULL;
    }
    
    return s;
}

void bread_struct_set_field(BreadStruct* s, const char* field_name, BreadValue value) {
    if (!s || !field_name) return;
    
    int index = bread_struct_find_field_index(s, field_name);
    if (index >= 0) {
        bread_value_release(&s->field_values[index]);
        s->field_values[index] = bread_value_clone(value);
    }
}

void bread_struct_set_field_value_ptr(BreadStruct* s, const char* field_name, const BreadValue* value) {
    if (!value) return;
    bread_struct_set_field(s, field_name, *value);
}

BreadValue* bread_struct_get_field(BreadStruct* s, const char* field_name) {
    if (!s || !field_name) return NULL;
    
    int index = bread_struct_find_field_index(s, field_name);
    if (index >= 0) {
        return &s->field_values[index];
    }
    
    return NULL;
}

int bread_struct_find_field_index(BreadStruct* s, const char* field_name) {
    if (!s || !field_name) return -1;
    
    for (int i = 0; i < s->field_count; i++) {
        if (strcmp(s->field_names[i], field_name) == 0) {
            return i;
        }
    }
    
    return -1;
}

void bread_struct_retain(BreadStruct* s) {
    bread_object_retain(s);
}

void bread_struct_release(BreadStruct* s) {
    if (!s) return;
    
    BreadObjHeader* header = (BreadObjHeader*)s;
    if (header->refcount == 0) return;
    
    header->refcount--;
    if (header->refcount == 0) {
        free(s->type_name);
        
        if (s->field_names) {
            for (int i = 0; i < s->field_count; i++) {
                free(s->field_names[i]);
            }
            free(s->field_names);
        }
        
        if (s->field_values) {
            for (int i = 0; i < s->field_count; i++) {
                bread_value_release(&s->field_values[i]);
            }
            free(s->field_values);
        }
        
        bread_memory_free(s);
    }
}