#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"

// Helper function to unwrap optional values
static int unwrap_optional(const BreadValue* value, BreadValue* unwrapped, int* owned) {
    if (!value || !unwrapped || !owned) return 0;
    
    *unwrapped = *value;
    *owned = 0;
    
    if (value->type == TYPE_OPTIONAL) {
        BreadOptional* o = value->value.optional_val;
        if (!o || !o->is_some) {
            return 0; // None value
        }
        *unwrapped = bread_value_clone(o->value);
        *owned = 1;
    }
    
    return 1;
}

// Helper function for safe cleanup
static void cleanup_if_owned(BreadValue* value, int owned) {
    if (owned && value) {
        bread_value_release(value);
    }
}

int bread_index_op(const BreadValue* target, const BreadValue* idx, BreadValue* out) {
    if (!target || !idx || !out) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in index operation");
        return 0;
    }

    BreadValue real_target;
    int target_owned = 0;
    
    // Handle optional unwrapping
    if (target->type == TYPE_OPTIONAL) {
        if (!unwrap_optional(target, &real_target, &target_owned)) {
            bread_value_set_nil(out);
            return 1;
        }
    } else {
        real_target = *target;
    }

    bread_value_set_nil(out);
    int result = 0;

    switch (real_target.type) {
        case TYPE_STRING: {
            if (idx->type != TYPE_INT) {
                BREAD_ERROR_SET_TYPE_MISMATCH("String index must be Int");
                break;
            }
            
            int index = idx->value.int_val;
            size_t len = bread_string_len(real_target.value.string_val);
            
            // Normalize negative indices
            if (index < 0) {
                index += (int)len;
            }
            
            if (index < 0 || index >= (int)len) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "String index %d out of bounds (length %zu)", 
                        idx->value.int_val, len);
                BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
                break;
            }
            
            char ch = bread_string_get_char(real_target.value.string_val, (size_t)index);
            char ch_str[2] = {ch, '\0'};
            bread_value_set_string(out, ch_str);
            result = 1;
            break;
        }

        case TYPE_ARRAY: {
            if (idx->type != TYPE_INT) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Array index must be Int");
                break;
            }
            
            int index = idx->value.int_val;
            int length = bread_array_length(real_target.value.array_val);
            
            // Normalize negative indices
            if (index < 0) {
                index += length;
            }
            
            if (index < 0 || index >= length) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Array index %d out of bounds (length %d)", 
                        idx->value.int_val, length);
                BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
                break;
            }
            
            BreadValue* at = bread_array_get(real_target.value.array_val, index);
            if (at) {
                *out = bread_value_clone(*at);
                result = 1;
            } else {
                BREAD_ERROR_SET_RUNTIME("Failed to retrieve array element");
            }
            break;
        }

        case TYPE_DICT: {
            if (idx->type != TYPE_STRING) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary key must be String");
                break;
            }
            
            const char* key = bread_string_cstr(idx->value.string_val);
            BreadValue* v = bread_dict_get(real_target.value.dict_val, key);
            
            if (v) {
                *out = bread_value_clone(*v);
                result = 1;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Dictionary key '%s' not found", key);
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
        }

        default:
            BREAD_ERROR_SET_RUNTIME("Type does not support indexing");
            break;
    }

    cleanup_if_owned(&real_target, target_owned);
    return result;
}

int bread_index_set_op(BreadValue* target, const BreadValue* idx, const BreadValue* value) {
    if (!target || !idx || !value) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in index set operation");
        return 0;
    }

    switch (target->type) {
        case TYPE_ARRAY:
            if (idx->type != TYPE_INT) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Array index must be Int");
                return 0;
            }
            return bread_array_set_value(target->value.array_val, idx->value.int_val, value);

        case TYPE_DICT:
            if (idx->type != TYPE_STRING) {
                BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary key must be String");
                return 0;
            }
            return bread_dict_set_value(target->value.dict_val, idx, value);

        default:
            BREAD_ERROR_SET_RUNTIME("Type does not support index assignment");
            return 0;
    }
}

int bread_member_op(const BreadValue* target, const char* member, int is_opt, BreadValue* out) {
    if (!target || !out) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in member operation");
        return 0;
    }

    BreadValue real_target;
    int target_owned = 0;

    // Handle optional chaining
    if (is_opt) {
        if (target->type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (target->type == TYPE_OPTIONAL) {
            if (!unwrap_optional(target, &real_target, &target_owned)) {
                bread_value_set_nil(out);
                return 1;
            }
        } else {
            real_target = *target;
        }
    } else {
        real_target = *target;
    }

    bread_value_set_nil(out);
    int result = 0;

    // Handle special "length" property
    if (member && strcmp(member, "length") == 0) {
        int length = -1;
        
        switch (real_target.type) {
            case TYPE_ARRAY:
                length = real_target.value.array_val ? real_target.value.array_val->count : 0;
                break;
            case TYPE_STRING:
                length = (int)bread_string_len(real_target.value.string_val);
                break;
            case TYPE_DICT:
                length = real_target.value.dict_val ? real_target.value.dict_val->count : 0;
                break;
            default:
                break;
        }
        
        if (length >= 0) {
            bread_value_set_int(out, length);
            result = 1;
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Member 'length' not supported for this type");
            BREAD_ERROR_SET_RUNTIME(error_msg);
        }
        
        cleanup_if_owned(&real_target, target_owned);
        return result;
    }

    // Handle type-specific member access
    switch (real_target.type) {
        case TYPE_DICT: {
            BreadValue* v = bread_dict_get(real_target.value.dict_val, member ? member : "");
            if (v) {
                *out = bread_value_clone(*v);
                result = 1;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Dictionary key '%s' not found", member ? member : "");
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
        }

        case TYPE_STRUCT: {
            BreadStruct* s = real_target.value.struct_val;
            if (!s) {
                BREAD_ERROR_SET_RUNTIME("Cannot access field of null struct");
                break;
            }
            
            BreadValue* field_value = bread_struct_get_field(s, member ? member : "");
            if (field_value) {
                *out = bread_value_clone(*field_value);
                result = 1;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Struct field '%s' not found in struct '%s'", 
                        member ? member : "", s->type_name ? s->type_name : "unknown");
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
        }

        case TYPE_CLASS: {
            BreadClass* c = real_target.value.class_val;
            if (!c) {
                BREAD_ERROR_SET_RUNTIME("Cannot access field of null class");
                break;
            }
            
            BreadValue* field_value = bread_class_get_field(c, member ? member : "");
            if (field_value) {
                *out = bread_value_clone(*field_value);
                result = 1;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Class field '%s' not found in class '%s'", 
                        member ? member : "", c->class_name ? c->class_name : "unknown");
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
        }

        default:
            if (is_opt) {
                bread_value_set_nil(out);
                result = 1;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Member '%s' not supported for this type", member ? member : "");
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
    }

    cleanup_if_owned(&real_target, target_owned);
    return result;
}

int bread_member_set_op(BreadValue* target, const char* member, const BreadValue* value) {
    if (!target || !value) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in member set operation");
        return 0;
    }

    if (!member) {
        BREAD_ERROR_SET_RUNTIME("Null member name");
        return 0;
    }

    switch (target->type) {
        case TYPE_STRUCT: {
            BreadStruct* s = target->value.struct_val;
            if (!s) {
                BREAD_ERROR_SET_RUNTIME("Cannot set field of null struct");
                return 0;
            }
            bread_struct_set_field(s, member, *value);
            return 1;
        }

        case TYPE_CLASS: {
            BreadClass* c = target->value.class_val;
            if (!c) {
                BREAD_ERROR_SET_RUNTIME("Cannot set field of null class");
                return 0;
            }
            bread_class_set_field(c, member, *value);
            return 1;
        }

        case TYPE_DICT:
            if (!bread_dict_set(target->value.dict_val, member, *value)) {
                BREAD_ERROR_SET_RUNTIME("Failed to set dictionary value");
                return 0;
            }
            return 1;

        default:
            BREAD_ERROR_SET_RUNTIME("Member assignment not supported for this type");
            return 0;
    }
}

// Helper for toString conversion
static int convert_to_string(const BreadValue* value, BreadValue* out) {
    char buf[256];
    
    switch (value->type) {
        case TYPE_STRING:
            bread_value_set_string(out, bread_string_cstr(value->value.string_val));
            return 1;
            
        case TYPE_INT:
            snprintf(buf, sizeof(buf), "%d", value->value.int_val);
            break;
            
        case TYPE_BOOL:
            snprintf(buf, sizeof(buf), "%s", value->value.bool_val ? "true" : "false");
            break;
            
        case TYPE_FLOAT:
            snprintf(buf, sizeof(buf), "%g", value->value.float_val);
            break;
            
        case TYPE_DOUBLE:
            snprintf(buf, sizeof(buf), "%g", value->value.double_val);
            break;
            
        case TYPE_NIL:
            snprintf(buf, sizeof(buf), "nil");
            break;
            
        default:
            return 0;
    }
    
    bread_value_set_string(out, buf);
    return 1;
}

int bread_method_call_op(const BreadValue* target, const char* name, int argc, 
                         const BreadValue* args, int is_opt, BreadValue* out) {
    if (!target || !out) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in method call");
        return 0;
    }

    BreadValue real_target;
    int target_owned = 0;

    // Handle optional chaining
    if (is_opt) {
        if (target->type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (target->type == TYPE_OPTIONAL) {
            if (!unwrap_optional(target, &real_target, &target_owned)) {
                bread_value_set_nil(out);
                return 1;
            }
        } else {
            real_target = *target;
        }
    } else {
        real_target = *target;
    }

    bread_value_set_nil(out);
    int result = 0;

    // Built-in methods
    if (name && strcmp(name, "toString") == 0) {
        if (argc != 0) {
            BREAD_ERROR_SET_RUNTIME("toString() expects 0 arguments");
        } else if (convert_to_string(&real_target, out)) {
            result = 1;
        } else {
            BREAD_ERROR_SET_RUNTIME("toString() not supported for this type");
        }
        cleanup_if_owned(&real_target, target_owned);
        return result;
    }

    if (name && strcmp(name, "append") == 0) {
        if (real_target.type != TYPE_ARRAY) {
            BREAD_ERROR_SET_RUNTIME("append() is only supported on arrays");
        } else if (argc != 1 || !args) {
            BREAD_ERROR_SET_RUNTIME("append() expects 1 argument");
        } else if (bread_array_append(real_target.value.array_val, args[0])) {
            bread_value_set_nil(out);
            result = 1;
        } else {
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory during array append");
        }
        cleanup_if_owned(&real_target, target_owned);
        return result;
    }

    // Class methods
    if (real_target.type == TYPE_CLASS) {
        BreadClass* class_instance = real_target.value.class_val;
        if (class_instance && name) {
            // Constructor
            if (strcmp(name, "init") == 0) {
                result = bread_class_execute_constructor(class_instance, argc, args, out);
                cleanup_if_owned(&real_target, target_owned);
                return result;
            }
            
            // Instance methods
            int method_index = bread_class_find_method_index(class_instance, name);
            if (method_index >= 0) {
                result = bread_class_execute_method(class_instance, method_index, argc, args, out);
                cleanup_if_owned(&real_target, target_owned);
                return result;
            }
            
            // Parent class methods
            if (class_instance->parent_class) {
                int parent_method_index = bread_class_find_method_index(
                    class_instance->parent_class, name);
                if (parent_method_index >= 0) {
                    result = bread_class_execute_method(
                        class_instance->parent_class, parent_method_index, argc, args, out);
                    cleanup_if_owned(&real_target, target_owned);
                    return result;
                }
            }
        }
    }

    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
            "Method '%s' not supported for this type", name ? name : "");
    BREAD_ERROR_SET_RUNTIME(error_msg);
    cleanup_if_owned(&real_target, target_owned);
    return 0;
}

int bread_dict_set_value(struct BreadDict* d, const BreadValue* key, const BreadValue* val) {
    if (!d || !key || !val) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in dict set");
        return 0;
    }
    
    if (key->type != TYPE_STRING) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Dictionary keys must be strings");
        return 0;
    }
    
    return bread_dict_set((BreadDict*)d, bread_string_cstr(key->value.string_val), *val);
}

int bread_array_append_value(struct BreadArray* a, const BreadValue* v) {
    if (!a || !v) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in array append");
        return 0;
    }
    return bread_array_append((BreadArray*)a, *v);
}

int bread_array_set_value(struct BreadArray* a, int index, const BreadValue* v) {
    if (!a || !v) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in array set");
        return 0;
    }
    
    int length = bread_array_length((BreadArray*)a);
    
    // Normalize negative indices
    if (index < 0) {
        index += length;
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

// Helper for super initialization
static int bread_super_init_impl(const BreadValue* self, const char* parent_name, 
                                 int argc, const BreadValue* args, BreadValue* out) {
    if (!self || !parent_name || !out) {
        BREAD_ERROR_SET_RUNTIME("Null pointer in super init");
        return 0;
    }
    
    if (self->type != TYPE_CLASS) {
        BREAD_ERROR_SET_TYPE_MISMATCH("Super call requires class instance");
        return 0;
    }
    
    BreadClass* instance = self->value.class_val;
    if (!instance) {
        BREAD_ERROR_SET_RUNTIME("Invalid class instance");
        return 0;
    }
    
    BreadClass* parent_class = bread_class_find_definition(parent_name);
    if (!parent_class) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Parent class '%s' not found", parent_name);
        BREAD_ERROR_SET_RUNTIME(error_msg);
        return 0;
    }
    
    if (argc > 0 && !args) {
        BREAD_ERROR_SET_RUNTIME("Invalid arguments array");
        return 0;
    }
    
    // Set parent fields from arguments
    int fields_to_set = (argc < parent_class->field_count) ? argc : parent_class->field_count;
    
    for (int i = 0; i < fields_to_set; i++) {
        if (parent_class->field_names[i]) {
            BreadValue safe_arg = bread_value_clone(args[i]);
            bread_class_set_field(instance, parent_class->field_names[i], safe_arg);
            bread_value_release(&safe_arg);
        }
    }
    
    bread_value_set_nil(out);
    return 1;
}

int bread_super_init_0(const BreadValue* self, const char* parent_name, BreadValue* out) {
    return bread_super_init_impl(self, parent_name, 0, NULL, out);
}

int bread_super_init_1(const BreadValue* self, const char* parent_name, 
                       const BreadValue* arg0, BreadValue* out) {
    if (!arg0) {
        BREAD_ERROR_SET_RUNTIME("Null argument");
        return 0;
    }
    return bread_super_init_impl(self, parent_name, 1, arg0, out);
}

int bread_super_init_2(const BreadValue* self, const char* parent_name, 
                       const BreadValue* arg0, const BreadValue* arg1, BreadValue* out) {
    if (!arg0 || !arg1) {
        BREAD_ERROR_SET_RUNTIME("Null argument");
        return 0;
    }
    BreadValue args[2] = {*arg0, *arg1};
    return bread_super_init_impl(self, parent_name, 2, args, out);
}

int bread_super_init_3(const BreadValue* self, const char* parent_name, 
                       const BreadValue* arg0, const BreadValue* arg1, 
                       const BreadValue* arg2, BreadValue* out) {
    if (!arg0 || !arg1 || !arg2) {
        BREAD_ERROR_SET_RUNTIME("Null argument");
        return 0;
    }
    BreadValue args[3] = {*arg0, *arg1, *arg2};
    return bread_super_init_impl(self, parent_name, 3, args, out);
}