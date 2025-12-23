#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/value.h"
#include "runtime/memory.h"
#include "runtime/error.h"

BreadClass* bread_class_new(const char* class_name, const char* parent_name, int field_count, char** field_names) {
    if (!class_name || field_count < 0 || (field_count > 0 && !field_names)) {
        return NULL;
    }
    
    BreadClass* c = (BreadClass*)bread_memory_alloc(sizeof(BreadClass), BREAD_OBJ_CLASS);
    if (!c) return NULL;
    
    c->class_name = strdup(class_name);
    c->parent_name = parent_name ? strdup(parent_name) : NULL;
    c->parent_class = NULL;
    c->field_count = field_count;
    c->method_count = 0;
    c->constructor = NULL;
    c->compiled_methods = NULL;
    c->compiled_constructor = NULL;
    
    if (field_count > 0) {
        c->field_names = malloc(field_count * sizeof(char*));
        c->field_values = malloc(field_count * sizeof(BreadValue));
        
        if (!c->field_names || !c->field_values) {
            free(c->class_name);
            free(c->parent_name);
            free(c->field_names);
            free(c->field_values);
            bread_memory_free(c);
            return NULL;
        }
        
        for (int i = 0; i < field_count; i++) {
            c->field_names[i] = strdup(field_names[i]);
            memset(&c->field_values[i], 0, sizeof(BreadValue));
            c->field_values[i].type = TYPE_NIL;
        }
    } else {
        c->field_names = NULL;
        c->field_values = NULL;
    }
    
    c->method_names = NULL;
    c->methods = NULL;
    
    return c;
}

BreadClass* bread_class_new_with_methods(const char* class_name, const char* parent_name, 
                                        int field_count, char** field_names,
                                        int method_count, char** method_names) {
    BreadClass* c = bread_class_new(class_name, parent_name, field_count, field_names);
    if (!c) return NULL;
    
    if (method_count > 0 && method_names) {
        c->method_count = method_count;
        c->method_names = malloc(method_count * sizeof(char*));
        c->compiled_methods = malloc(method_count * sizeof(BreadCompiledMethod));
        
        if (!c->method_names || !c->compiled_methods) {
            bread_class_release(c);
            return NULL;
        }
        
        for (int i = 0; i < method_count; i++) {
            c->method_names[i] = strdup(method_names[i]);
            c->compiled_methods[i] = NULL;
        }
    }
    
    return c;
}

// Global registry of class definitions (templates)
static BreadClass** class_registry = NULL;
static int class_registry_count = 0;
static int class_registry_capacity = 0;

void bread_class_register_definition(BreadClass* class_def) {
    if (!class_def) return;
    
    // Check if already registered
    for (int i = 0; i < class_registry_count; i++) {
        if (class_registry[i] && class_registry[i]->class_name && class_def->class_name &&
            strcmp(class_registry[i]->class_name, class_def->class_name) == 0) {
            bread_class_release(class_registry[i]);
            class_registry[i] = class_def;
            bread_class_retain(class_def);
            return;
        }
    }
    
    // Add new registration
    if (class_registry_count >= class_registry_capacity) {
        int new_capacity = class_registry_capacity == 0 ? 4 : class_registry_capacity * 2;
        BreadClass** new_registry = realloc(class_registry, new_capacity * sizeof(BreadClass*));
        if (!new_registry) return;
        class_registry = new_registry;
        class_registry_capacity = new_capacity;
    }
    
    class_registry[class_registry_count] = class_def;
    bread_class_retain(class_def);
    class_registry_count++;
}

void bread_class_resolve_inheritance(void) {
    for (int i = 0; i < class_registry_count; i++) {
        BreadClass* class = class_registry[i];
        if (class && class->parent_name && !class->parent_class) {
            BreadClass* parent = bread_class_find_definition(class->parent_name);
            if (parent) {
                class->parent_class = parent;
                bread_class_retain(parent);
            }
        }
    }
}

BreadClass* bread_class_find_definition(const char* class_name) {
    for (int i = 0; i < class_registry_count; i++) {
        if (class_registry[i] && class_registry[i]->class_name &&
            strcmp(class_registry[i]->class_name, class_name) == 0) {
            return class_registry[i];
        }
    }
    return NULL;
}

BreadClass* bread_class_create_instance(const char* class_name, const char* parent_name, 
                                       int field_count, char** field_names,
                                       int method_count, char** method_names) {
    BreadClass* class_def = bread_class_find_definition(class_name);
    
    if (class_def) {
        BreadClass* instance = bread_class_new_with_methods(class_name, parent_name, 
                                                           field_count, field_names,
                                                           method_count, method_names);
        if (!instance) return NULL;

        // Inherit resolved parent pointer from the class definition so runtime method lookup
        // can traverse inheritance for instances.
        if (class_def->parent_class) {
            instance->parent_class = class_def->parent_class;
            bread_class_retain(instance->parent_class);
        }
        
        // Copy compiled methods from the class definition
        if (class_def->compiled_methods && instance->compiled_methods) {
            for (int i = 0; i < instance->method_count; i++) {
                if (!instance->method_names || !instance->method_names[i]) continue;
                for (int j = 0; j < class_def->method_count; j++) {
                    if (!class_def->method_names || !class_def->method_names[j]) continue;
                    if (strcmp(instance->method_names[i], class_def->method_names[j]) == 0) {
                        instance->compiled_methods[i] = class_def->compiled_methods[j];
                        break;
                    }
                }
            }
        }
        
        if (class_def->compiled_constructor) {
            instance->compiled_constructor = class_def->compiled_constructor;
        }
        
        return instance;
    }
    
    return bread_class_new_with_methods(class_name, parent_name, field_count, field_names, method_count, method_names);
}

void bread_class_set_field(BreadClass* c, const char* field_name, BreadValue value) {
    if (!c || !field_name) return;
    
    int index = bread_class_find_field_index(c, field_name);
    if (index >= 0) {
        bread_value_release(&c->field_values[index]);
        c->field_values[index] = bread_value_clone(value);
    } else if (c->parent_class) {
        bread_class_set_field(c->parent_class, field_name, value);
    }
}

void bread_class_set_field_value_ptr(BreadClass* c, const char* field_name, const BreadValue* value) {
    if (!c || !field_name || !value) return;
    
    int index = bread_class_find_field_index(c, field_name);
    if (index >= 0) {
        bread_value_release(&c->field_values[index]);
        c->field_values[index] = bread_value_clone(*value);
    } else if (c->parent_class) {
        bread_class_set_field_value_ptr(c->parent_class, field_name, value);
    }
}

BreadValue* bread_class_get_field(BreadClass* c, const char* field_name) {
    if (!c || !field_name) return NULL;
    
    int index = bread_class_find_field_index(c, field_name);
    if (index >= 0) {
        return &c->field_values[index];
    } else if (c->parent_class) {
        return bread_class_get_field(c->parent_class, field_name);
    }
    
    return NULL;
}

int bread_class_find_field_index(BreadClass* c, const char* field_name) {
    if (!c || !field_name) return -1;
    
    for (int i = 0; i < c->field_count; i++) {
        if (strcmp(c->field_names[i], field_name) == 0) {
            return i;
        }
    }
    
    return -1;
}

void bread_class_add_method(BreadClass* c, const char* method_name, BreadMethod method) {
    if (!c || !method_name || !method) return;
    
    c->method_count++;
    c->method_names = realloc(c->method_names, c->method_count * sizeof(char*));
    c->methods = realloc(c->methods, c->method_count * sizeof(BreadMethod));
    c->compiled_methods = realloc(c->compiled_methods, c->method_count * sizeof(BreadCompiledMethod));
    
    if (!c->method_names || !c->methods || !c->compiled_methods) return;
    
    c->method_names[c->method_count - 1] = strdup(method_name);
    c->methods[c->method_count - 1] = method;
    c->compiled_methods[c->method_count - 1] = NULL;
}

void bread_class_set_compiled_method(BreadClass* c, int method_index, BreadCompiledMethod compiled_fn) {
    if (!c || method_index < 0 || method_index >= c->method_count || !c->compiled_methods) return;
    c->compiled_methods[method_index] = compiled_fn;
}

void bread_class_set_compiled_constructor(BreadClass* c, BreadCompiledMethod compiled_fn) {
    if (!c) return;
    c->compiled_constructor = compiled_fn;
}

BreadMethod bread_class_get_method(BreadClass* c, const char* method_name) {
    if (!c || !method_name) return NULL;
    
    for (int i = 0; i < c->method_count; i++) {
        if (strcmp(c->method_names[i], method_name) == 0) {
            return c->methods[i];
        }
    }
    
    if (c->parent_class) {
        return bread_class_get_method(c->parent_class, method_name);
    }
    
    return NULL;
}

BreadValue bread_class_call_method(BreadClass* c, const char* method_name, BreadValue* args, int arg_count) {
    BreadValue result;
    memset(&result, 0, sizeof(result));
    result.type = TYPE_NIL;
    
    if (!c || !method_name) return result;
    
    BreadMethod method = bread_class_get_method(c, method_name);
    if (method) {
        return method(c, args, arg_count);
    }
    
    return result;
}

void bread_class_retain(BreadClass* c) {
    bread_object_retain(c);
}

void bread_class_release(BreadClass* c) {
    if (!c) return;
    
    BreadObjHeader* header = (BreadObjHeader*)c;
    if (header->refcount == 0) return;
    
    header->refcount--;
    if (header->refcount == 0) {
        free(c->class_name);
        free(c->parent_name);
        
        if (c->field_names) {
            for (int i = 0; i < c->field_count; i++) {
                free(c->field_names[i]);
            }
            free(c->field_names);
        }
        
        if (c->field_values) {
            for (int i = 0; i < c->field_count; i++) {
                bread_value_release(&c->field_values[i]);
            }
            free(c->field_values);
        }
        
        if (c->method_names) {
            for (int i = 0; i < c->method_count; i++) {
                free(c->method_names[i]);
            }
            free(c->method_names);
        }
        
        free(c->methods);
        free(c->compiled_methods);
        bread_memory_free(c);
    }
}

int bread_class_get_field_value_ptr(BreadClass* c, const char* field_name, BreadValue* out) {
    if (!c || !field_name || !out) return 0;
    
    BreadValue* field = bread_class_get_field(c, field_name);
    if (field) {
        *out = *field;
        return 1;
    }
    
    return 0;
}

int bread_class_find_method_index(BreadClass* c, const char* method_name) {
    if (!c || !method_name) return -1;
    
    for (int i = 0; i < c->method_count; i++) {
        if (c->method_names[i] && strcmp(c->method_names[i], method_name) == 0) {
            return i;
        }
    }

    return -1;
}

BreadClass* bread_class_find_method_defining_class(BreadClass* c, const char* method_name, int* method_index) {
    if (!c || !method_name || !method_index) return NULL;
    
    for (int i = 0; i < c->method_count; i++) {
        if (c->method_names[i] && strcmp(c->method_names[i], method_name) == 0) {
            *method_index = i;
            return c;
        }
    }
    
    if (c->parent_class) {
        return bread_class_find_method_defining_class(c->parent_class, method_name, method_index);
    }
    
    return NULL;
}

static int bread_class_call_compiled_method(BreadCompiledMethod compiled_fn, BreadClass* instance, 
                                           int argc, const BreadValue* args, BreadValue* out) {
    BreadValue ret_slot;
    bread_value_set_nil(&ret_slot);
    
    BreadValue self_value;
    bread_value_set_class(&self_value, instance);
    
    // Call compiled function based on argument count
    switch (argc) {
        case 0:
            ((void(*)(void*, void*))compiled_fn)(&ret_slot, &self_value);
            break;
        case 1:
            ((void(*)(void*, void*, void*))compiled_fn)(&ret_slot, &self_value, (void*)&args[0]);
            break;
        case 2:
            ((void(*)(void*, void*, void*, void*))compiled_fn)(&ret_slot, &self_value, (void*)&args[0], (void*)&args[1]);
            break;
        case 3:
            ((void(*)(void*, void*, void*, void*, void*))compiled_fn)(&ret_slot, &self_value, (void*)&args[0], (void*)&args[1], (void*)&args[2]);
            break;
        default:
            bread_value_set_nil(out);
            bread_value_release(&ret_slot);
            bread_value_release(&self_value);
            return 1;
    }
    
    *out = ret_slot;
    bread_value_release(&self_value);
    return 1;
}

int bread_class_execute_method_direct(BreadClass* defining_class, int method_index, 
                                     BreadClass* instance, int argc, const BreadValue* args, BreadValue* out) {
    if (!defining_class || !instance || method_index < 0 || method_index >= defining_class->method_count || !out) {
        return 0;
    }
    
    const char* method_name = defining_class->method_names[method_index];
    
    if (strcmp(method_name, "init") == 0) {
        return bread_class_execute_constructor(instance, argc, args, out);
    }
    
    if (defining_class->compiled_methods && defining_class->compiled_methods[method_index]) {
        return bread_class_call_compiled_method(defining_class->compiled_methods[method_index], 
                                               instance, argc, args, out);
    }
    
    fprintf(stderr, "RUNTIME ERROR: Method '%s::%s' not found\n", 
            defining_class->class_name ? defining_class->class_name : "unknown", method_name);
    bread_value_set_nil(out);
    return 0;
}

int bread_class_execute_method(BreadClass* c, int method_index, int argc, const BreadValue* args, BreadValue* out) {
    if (!c || method_index < 0 || !out) {
        return 0;
    }

    return bread_class_execute_method_direct(c, method_index, c, argc, args, out);
}

int bread_class_execute_constructor(BreadClass* c, int argc, const BreadValue* args, BreadValue* out) {
    if (!c || !out) {
        return 0;
    }
    
    if (c->compiled_constructor) {
        return bread_class_call_compiled_method(c->compiled_constructor, c, argc, args, out);
    }
    
    // Default constructor: map arguments to fields by position
    int fields_to_set = (argc < c->field_count) ? argc : c->field_count;
    
    for (int i = 0; i < fields_to_set; i++) {
        if (i < c->field_count && c->field_names[i]) {
            bread_class_set_field_value_ptr(c, c->field_names[i], &args[i]);
        }
    }
    
    bread_value_set_nil(out);
    return 1;
}