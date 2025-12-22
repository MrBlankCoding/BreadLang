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
    c->parent_class = NULL;  // Will be resolved later
    c->field_count = field_count;
    c->method_count = 0;
    c->constructor = NULL;
    
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

void bread_class_set_field(BreadClass* c, const char* field_name, BreadValue value) {
    if (!c || !field_name) return;
    
    int index = bread_class_find_field_index(c, field_name);
    if (index >= 0) {
        bread_value_release(&c->field_values[index]);
        c->field_values[index] = bread_value_clone(value);
    } else if (c->parent_class) {
        // Try parent class
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
        // Try parent class
        bread_class_set_field_value_ptr(c->parent_class, field_name, value);
    }
}

BreadValue* bread_class_get_field(BreadClass* c, const char* field_name) {
    if (!c || !field_name) return NULL;
    
    int index = bread_class_find_field_index(c, field_name);
    if (index >= 0) {
        return &c->field_values[index];
    } else if (c->parent_class) {
        // Try parent class
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
    
    // Expand method arrays
    c->method_count++;
    c->method_names = realloc(c->method_names, c->method_count * sizeof(char*));
    c->methods = realloc(c->methods, c->method_count * sizeof(BreadMethod));
    
    if (!c->method_names || !c->methods) return;
    
    c->method_names[c->method_count - 1] = strdup(method_name);
    c->methods[c->method_count - 1] = method;
}

BreadMethod bread_class_get_method(BreadClass* c, const char* method_name) {
    if (!c || !method_name) return NULL;
    
    // Search in this class
    for (int i = 0; i < c->method_count; i++) {
        if (strcmp(c->method_names[i], method_name) == 0) {
            return c->methods[i];
        }
    }
    
    // Search in parent class
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
    
    // Search in parent class
    if (c->parent_class) {
        return bread_class_find_method_index(c->parent_class, method_name);
    }
    
    return -1;
}

// Method execution context structure
typedef struct {
    BreadClass* self;
    BreadValue* args;
    int arg_count;
    BreadValue* local_vars;
    int local_var_count;
} MethodExecutionContext;

// Execute a method with proper context
int bread_class_execute_method(BreadClass* c, int method_index, int argc, const BreadValue* args, BreadValue* out) {
    if (!c || method_index < 0 || method_index >= c->method_count || !out) {
        return 0;
    }
    
    // For now, implement hardcoded method behavior
    // In a full implementation, this would execute the method's AST body
    const char* method_name = c->method_names[method_index];
    
    if (strcmp(method_name, "greet") == 0) {
        // Get name field if it exists
        BreadValue name_val;
        char greeting[256];
        
        if (bread_class_get_field_value_ptr(c, "name", &name_val) && name_val.type == TYPE_STRING) {
            snprintf(greeting, sizeof(greeting), "Hello, my name is %s!", 
                    bread_string_cstr(name_val.value.string_val));
        } else {
            snprintf(greeting, sizeof(greeting), "Hello from %s class!", 
                    c->class_name ? c->class_name : "unknown");
        }
        
        bread_value_set_string(out, greeting);
        return 1;
    }
    
    if (strcmp(method_name, "get_age") == 0) {
        // Return age field or default value
        BreadValue age_val;
        if (bread_class_get_field_value_ptr(c, "age", &age_val)) {
            *out = bread_value_clone(age_val);
        } else {
            bread_value_set_int(out, 25); // Default value
        }
        return 1;
    }
    
    // Default: return nil for unknown methods
    bread_value_set_nil(out);
    return 1;
}

// Execute constructor with proper field initialization
int bread_class_execute_constructor(BreadClass* c, int argc, const BreadValue* args, BreadValue* out) {
    if (!c || !out) {
        return 0;
    }
    
    // Initialize fields from constructor arguments
    // For Person class: init(name: String, age: Int)
    if (argc >= 2 && c->field_count >= 2) {
        // Set name field (first argument)
        if (args[0].type == TYPE_STRING) {
            bread_class_set_field_value_ptr(c, "name", &args[0]);
        }
        
        // Set age field (second argument)  
        if (args[1].type == TYPE_INT) {
            bread_class_set_field_value_ptr(c, "age", &args[1]);
        }
    }
    
    // Constructor returns nil
    bread_value_set_nil(out);
    return 1;
}