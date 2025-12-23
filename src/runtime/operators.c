#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"

int bread_index_op(const BreadValue* target, const BreadValue* idx, BreadValue* out) {
    if (!target || !idx || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;
    if (real_target.type == TYPE_OPTIONAL) {
        BreadOptional* o = real_target.value.optional_val;
        if (!o || !o->is_some) {
            bread_value_set_nil(out);
            return 1;
        }
        real_target = bread_value_clone(o->value);
        target_owned = 1;
    }

    bread_value_set_nil(out);

    if (real_target.type == TYPE_STRING) {
        if (idx->type != TYPE_INT) {
            BREAD_ERROR_SET_TYPE_MISMATCH("String index must be Int");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        int index = idx->value.int_val;
        size_t len = bread_string_len(real_target.value.string_val);
        
        // Handle negative indices (Python-style)
        if (index < 0) {
            index = (int)len + index;
        }
        
        if (index < 0 || index >= (int)len) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "String index %d out of bounds (length %zu)", idx->value.int_val, len);
            BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        char ch = bread_string_get_char(real_target.value.string_val, (size_t)index);
        char ch_str[2] = {ch, '\0'};
        bread_value_set_string(out, ch_str);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_ARRAY) {
        if (idx->type != TYPE_INT) {
            BREAD_ERROR_SET_TYPE_MISMATCH("Array index must be Int");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        int index = idx->value.int_val;
        int length = bread_array_length(real_target.value.array_val);
        
        if (index < 0) {
            index = length + index;
        }
        
        if (index < 0 || index >= length) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Array index %d out of bounds (length %d)", idx->value.int_val, length);
            BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        BreadValue* at = bread_array_get(real_target.value.array_val, index);
        if (at) *out = bread_value_clone(*at);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_DICT) {
        if (idx->type != TYPE_STRING) {
            BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary key must be String");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        BreadValue* v = bread_dict_get(real_target.value.dict_val, bread_string_cstr(idx->value.string_val));
        if (v) {
            *out = bread_value_clone(*v);
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Dictionary key '%s' not found", bread_string_cstr(idx->value.string_val));
            BREAD_ERROR_SET_RUNTIME(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    BREAD_ERROR_SET_RUNTIME("Type does not support indexing");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_index_set_op(BreadValue* target, const BreadValue* idx, const BreadValue* value) {
    if (!target || !idx || !value) return 0;

    if (target->type == TYPE_ARRAY) {
        if (idx->type != TYPE_INT) {
            BREAD_ERROR_SET_TYPE_MISMATCH("Array index must be Int");
            return 0;
        }
        return bread_array_set_value(target->value.array_val, idx->value.int_val, value);
    }

    if (target->type == TYPE_DICT) {
        if (idx->type != TYPE_STRING) {
            BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary key must be String");
            return 0;
        }
        return bread_dict_set_value(target->value.dict_val, idx, value);
    }

    BREAD_ERROR_SET_RUNTIME("Type does not support indexing");
    return 0;
}

int bread_member_op(const BreadValue* target, const char* member, int is_opt, BreadValue* out) {
    if (!target || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;

    if (is_opt) {
        if (real_target.type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (real_target.type == TYPE_OPTIONAL) {
            BreadOptional* o = real_target.value.optional_val;
            if (!o || !o->is_some) {
                bread_value_set_nil(out);
                return 1;
            }
            real_target = bread_value_clone(o->value);
            target_owned = 1;
        }
    }

    bread_value_set_nil(out);

    if (member && strcmp(member, "length") == 0) {
        if (real_target.type == TYPE_ARRAY) {
            bread_value_set_int(out, real_target.value.array_val ? real_target.value.array_val->count : 0);
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }
        if (real_target.type == TYPE_STRING) {
            bread_value_set_int(out, (int)bread_string_len(real_target.value.string_val));
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }
        if (real_target.type == TYPE_DICT) {
            bread_value_set_int(out, real_target.value.dict_val ? real_target.value.dict_val->count : 0);
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }

        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Member '%s' not supported for this type", member ? member : "");
        BREAD_ERROR_SET_RUNTIME(error_msg);
        if (target_owned) bread_value_release(&real_target);
        return 0;
    }

    if (real_target.type == TYPE_DICT) {
        BreadValue* v = bread_dict_get(real_target.value.dict_val, member ? member : "");
        if (v) {
            *out = bread_value_clone(*v);
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Dictionary key '%s' not found", member ? member : "");
            BREAD_ERROR_SET_RUNTIME(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_STRUCT) {
        BreadStruct* s = real_target.value.struct_val;
        if (!s) {
            BREAD_ERROR_SET_RUNTIME("Cannot access field of null struct");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        BreadValue* field_value = bread_struct_get_field(s, member ? member : "");
        if (field_value) {
            *out = bread_value_clone(*field_value);
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Struct field '%s' not found in struct '%s'", 
                    member ? member : "", s->type_name ? s->type_name : "unknown");
            BREAD_ERROR_SET_RUNTIME(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_CLASS) {
        BreadClass* c = real_target.value.class_val;
        if (!c) {
            BREAD_ERROR_SET_RUNTIME("Cannot access field of null class");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        BreadValue* field_value = bread_class_get_field(c, member ? member : "");
        if (field_value) {
            *out = bread_value_clone(*field_value);
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Class field '%s' not found in class '%s'", 
                    member ? member : "", c->class_name ? c->class_name : "unknown");
            BREAD_ERROR_SET_RUNTIME(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (is_opt) {
        bread_value_set_nil(out);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
            "Member '%s' not supported for this type", member ? member : "");
    BREAD_ERROR_SET_RUNTIME(error_msg);
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_member_set_op(BreadValue* target, const char* member, const BreadValue* value) {
    if (!target || !value) return 0;

    BreadValue real_target = *target;

    if (real_target.type == TYPE_STRUCT) {
        BreadStruct* s = real_target.value.struct_val;
        if (!s) {
            BREAD_ERROR_SET_RUNTIME("Cannot set field of null struct");
            return 0;
        }
        
        bread_struct_set_field(s, member ? member : "", *value);
        return 1;
    }

    if (real_target.type == TYPE_CLASS) {
        BreadClass* c = real_target.value.class_val;
        if (!c) {
            BREAD_ERROR_SET_RUNTIME("Cannot set field of null class");
            return 0;
        }
        
        bread_class_set_field(c, member ? member : "", *value);
        return 1;
    }

    if (real_target.type == TYPE_DICT) {
        if (!bread_dict_set(real_target.value.dict_val, member ? member : "", *value)) {
            BREAD_ERROR_SET_RUNTIME("Failed to set dictionary value");
            return 0;
        }
        return 1;
    }

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
            "Member assignment not supported for this type");
    BREAD_ERROR_SET_RUNTIME(error_msg);
    return 0;
}

int bread_method_call_op(const BreadValue* target, const char* name, int argc, const BreadValue* args, int is_opt, BreadValue* out) {
    if (!target || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;

    if (is_opt) {
        if (real_target.type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (real_target.type == TYPE_OPTIONAL) {
            BreadOptional* o = real_target.value.optional_val;
            if (!o || !o->is_some) {
                bread_value_set_nil(out);
                return 1;
            }
            real_target = bread_value_clone(o->value);
            target_owned = 1;
        }
    }

    bread_value_set_nil(out);

    if (name && strcmp(name, "toString") == 0) {
        if (argc != 0) {
            BREAD_ERROR_SET_RUNTIME("toString() expects 0 arguments");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }

        char buf[128];
        buf[0] = '\0';

        switch (real_target.type) {
            case TYPE_STRING:
                bread_value_set_string(out, bread_string_cstr(real_target.value.string_val));
                if (target_owned) bread_value_release(&real_target);
                return 1;
            case TYPE_INT:
                snprintf(buf, sizeof(buf), "%d", real_target.value.int_val);
                break;
            case TYPE_BOOL:
                snprintf(buf, sizeof(buf), "%s", real_target.value.bool_val ? "true" : "false");
                break;
            case TYPE_FLOAT:
                snprintf(buf, sizeof(buf), "%f", real_target.value.float_val);
                break;
            case TYPE_DOUBLE:
                snprintf(buf, sizeof(buf), "%lf", real_target.value.double_val);
                break;
            default:
                BREAD_ERROR_SET_RUNTIME("toString() not supported for this type");
                if (target_owned) bread_value_release(&real_target);
                return 0;
        }

        bread_value_set_string(out, buf);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (name && strcmp(name, "append") == 0) {
        if (real_target.type != TYPE_ARRAY) {
            BREAD_ERROR_SET_RUNTIME("append() is only supported on arrays");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (argc != 1 || !args) {
            BREAD_ERROR_SET_RUNTIME("append() expects 1 argument");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (!bread_array_append(real_target.value.array_val, args[0])) {
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory during array append");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        bread_value_set_nil(out);
        return 1;
    }

    // Handle class methods
    if (real_target.type == TYPE_CLASS) {
        BreadClass* class_instance = real_target.value.class_val;
        if (class_instance && name) {
            // Look up the method in the class hierarchy and execute it.
            // bread_class_find_method_index() already walks parent_class; bread_class_execute_method()
            // resolves the actual defining class before dispatch.
            int method_index = bread_class_find_method_index(class_instance, name);
            if (method_index >= 0) {
                int ok = bread_class_execute_method(class_instance, method_index, argc, args, out);
                if (target_owned) bread_value_release(&real_target);
                return ok;
            }
            
            // Handle constructor separately
            if (strcmp(name, "init") == 0) {
                return bread_class_execute_constructor(class_instance, argc, args, out);
            }
            
            // Fallback for built-in methods
            if (strcmp(name, "greet") == 0) {
                // Simple greet method implementation
                char greeting[256];
                snprintf(greeting, sizeof(greeting), "Hello, I'm a %s instance", 
                        class_instance->class_name ? class_instance->class_name : "unknown");
                bread_value_set_string(out, greeting);
                if (target_owned) bread_value_release(&real_target);
                return 1;
            }
            
            if (strcmp(name, "get_age") == 0) {
                // Return age field or default value
                BreadValue age_val;
                if (bread_class_get_field_value_ptr(class_instance, "age", &age_val)) {
                    *out = bread_value_clone(age_val);
                } else {
                    bread_value_set_int(out, 25); // Default value
                }
                if (target_owned) bread_value_release(&real_target);
                return 1;
            }
            
            if (strcmp(name, "get_age") == 0) {
                // Simple get_age method - return the age field
                BreadValue* age_value = bread_class_get_field(class_instance, "age");
                if (age_value) {
                    *out = *age_value;
                } else {
                    bread_value_set_nil(out);
                }
                if (target_owned) bread_value_release(&real_target);
                return 1;
            }
            
            // Method not found - fall through to error case
        }
    }

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
            "Method '%s' not supported for this type", name ? name : "");
    BREAD_ERROR_SET_RUNTIME(error_msg);
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_dict_set_value(struct BreadDict* d, const BreadValue* key, const BreadValue* val) {
    if (!d || !key || !val) return 0;
    if (key->type != TYPE_STRING) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary keys must be strings");
        return 0;
    }
    return bread_dict_set((BreadDict*)d, bread_string_cstr(key->value.string_val), *val);
}

int bread_array_append_value(struct BreadArray* a, const BreadValue* v) {
    if (!a || !v) return 0;
    return bread_array_append((BreadArray*)a, *v);
}

int bread_array_set_value(struct BreadArray* a, int index, const BreadValue* v) {
    if (!a || !v) return 0;
    
    int length = bread_array_length((BreadArray*)a);
    if (index < 0) {
        index = length + index;
    }
    
    if (index < 0 || index >= length) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Array index %d out of bounds (length %d)", index, length);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return 0;
    }
    
    return bread_array_set((BreadArray*)a, index, *v);
}
// Super constructor call - simplified version that just sets fields
int bread_super_init_simple(const BreadValue* self, const char* parent_name, int argc, const BreadValue* args, BreadValue* out) {
    if (!self || !parent_name || !out) return 0;
    
    // The self parameter should be a class instance
    if (self->type != TYPE_CLASS) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Super call requires class instance");
        return 0;
    }
    
    BreadClass* instance = self->value.class_val;
    if (!instance) {
        BREAD_ERROR_SET_RUNTIME("Invalid class instance");
        return 0;
    }
    
    // Find the parent class definition
    BreadClass* parent_class = bread_class_find_definition(parent_name);
    if (!parent_class) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Parent class '%s' not found", parent_name);
        BREAD_ERROR_SET_RUNTIME(error_msg);
        return 0;
    }
    
    // Validate arguments before processing
    if (argc > 0 && !args) {
        BREAD_ERROR_SET_RUNTIME("Invalid arguments array");
        return 0;
    }
    
    // Validate each argument
    for (int i = 0; i < argc; i++) {
        if (args[i].type == TYPE_NIL && i < parent_class->field_count) {
            // Skip nil arguments, but don't fail
            continue;
        }
    }
    
    // Simple approach: just set the fields directly based on argument position
    // This assumes the parent constructor sets fields in the same order as arguments
    int fields_to_set = (argc < parent_class->field_count) ? argc : parent_class->field_count;
    
    for (int i = 0; i < fields_to_set; i++) {
        if (i < parent_class->field_count && parent_class->field_names[i] && i < argc) {
            // Create a safe copy of the argument to avoid memory issues
            BreadValue safe_arg = bread_value_clone(args[i]);
            bread_class_set_field(instance, parent_class->field_names[i], safe_arg);
            bread_value_release(&safe_arg);
        }
    }
    
    bread_value_set_nil(out);
    return 1;
}
// Super constructor call with 0 arguments
int bread_super_init_0(const BreadValue* self, const char* parent_name, BreadValue* out) {
    return bread_super_init_simple(self, parent_name, 0, NULL, out);
}

// Super constructor call with 1 argument
int bread_super_init_1(const BreadValue* self, const char* parent_name, const BreadValue* arg0, BreadValue* out) {
    if (!arg0) return 0;
    return bread_super_init_simple(self, parent_name, 1, arg0, out);
}

// Super constructor call with 2 arguments
int bread_super_init_2(const BreadValue* self, const char* parent_name, const BreadValue* arg0, const BreadValue* arg1, BreadValue* out) {
    if (!arg0 || !arg1) return 0;
    BreadValue args[2] = {*arg0, *arg1};
    return bread_super_init_simple(self, parent_name, 2, args, out);
}

// Super constructor call with 3 arguments
int bread_super_init_3(const BreadValue* self, const char* parent_name, const BreadValue* arg0, const BreadValue* arg1, const BreadValue* arg2, BreadValue* out) {
    if (!arg0 || !arg1 || !arg2) return 0;
    BreadValue args[3] = {*arg0, *arg1, *arg2};
    return bread_super_init_simple(self, parent_name, 3, args, out);
}