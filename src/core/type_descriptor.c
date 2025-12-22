#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/var.h"

TypeDescriptor* type_descriptor_create_primitive(VarType type) {
    if (type == TYPE_ARRAY || type == TYPE_DICT || type == TYPE_OPTIONAL) {
        return NULL;
    }
    
    TypeDescriptor* desc = malloc(sizeof(TypeDescriptor));
    if (!desc) return NULL;
    
    desc->base_type = type;
    return desc;
}

TypeDescriptor* type_descriptor_create_array(TypeDescriptor* element_type) {
    if (!element_type) return NULL;
    
    TypeDescriptor* desc = malloc(sizeof(TypeDescriptor));
    if (!desc) return NULL;
    
    desc->base_type = TYPE_ARRAY;
    desc->params.array.element_type = element_type;
    return desc;
}

TypeDescriptor* type_descriptor_create_dict(TypeDescriptor* key_type, TypeDescriptor* value_type) {
    if (!key_type || !value_type) return NULL;
    
    TypeDescriptor* desc = malloc(sizeof(TypeDescriptor));
    if (!desc) return NULL;
    
    desc->base_type = TYPE_DICT;
    desc->params.dict.key_type = key_type;
    desc->params.dict.value_type = value_type;
    return desc;
}

TypeDescriptor* type_descriptor_create_optional(TypeDescriptor* wrapped_type) {
    if (!wrapped_type) return NULL;
    
    TypeDescriptor* desc = malloc(sizeof(TypeDescriptor));
    if (!desc) return NULL;
    
    desc->base_type = TYPE_OPTIONAL;
    desc->params.optional.wrapped_type = wrapped_type;
    return desc;
}

void type_descriptor_free(TypeDescriptor* desc) {
    if (!desc) return;
    
    switch (desc->base_type) {
        case TYPE_ARRAY:
            type_descriptor_free(desc->params.array.element_type);
            break;
        case TYPE_DICT:
            type_descriptor_free(desc->params.dict.key_type);
            type_descriptor_free(desc->params.dict.value_type);
            break;
        case TYPE_OPTIONAL:
            type_descriptor_free(desc->params.optional.wrapped_type);
            break;
        default:
            // Primitive types have no nested descriptors
            break;
    }
    
    free(desc);
}

TypeDescriptor* type_descriptor_clone(const TypeDescriptor* desc) {
    if (!desc) return NULL;

    switch (desc->base_type) {
        case TYPE_ARRAY: {
            TypeDescriptor* elem = type_descriptor_clone(desc->params.array.element_type);
            if (!elem) return NULL;
            TypeDescriptor* out = type_descriptor_create_array(elem);
            if (!out) {
                type_descriptor_free(elem);
                return NULL;
            }
            return out;
        }
        case TYPE_DICT: {
            TypeDescriptor* key = type_descriptor_clone(desc->params.dict.key_type);
            if (!key) return NULL;
            TypeDescriptor* value = type_descriptor_clone(desc->params.dict.value_type);
            if (!value) {
                type_descriptor_free(key);
                return NULL;
            }
            TypeDescriptor* out = type_descriptor_create_dict(key, value);
            if (!out) {
                type_descriptor_free(key);
                type_descriptor_free(value);
                return NULL;
            }
            return out;
        }
        case TYPE_OPTIONAL: {
            TypeDescriptor* wrapped = type_descriptor_clone(desc->params.optional.wrapped_type);
            if (!wrapped) return NULL;
            TypeDescriptor* out = type_descriptor_create_optional(wrapped);
            if (!out) {
                type_descriptor_free(wrapped);
                return NULL;
            }
            return out;
        }
        default:
            return type_descriptor_create_primitive(desc->base_type);
    }
}

int type_descriptor_equals(const TypeDescriptor* a, const TypeDescriptor* b) {
    if (!a || !b) return a == b;
    if (a->base_type != b->base_type) return 0;
    
    switch (a->base_type) {
        case TYPE_ARRAY:
            return type_descriptor_equals(a->params.array.element_type, 
                                        b->params.array.element_type);
        case TYPE_DICT:
            return type_descriptor_equals(a->params.dict.key_type, 
                                        b->params.dict.key_type) &&
                   type_descriptor_equals(a->params.dict.value_type, 
                                        b->params.dict.value_type);
        case TYPE_OPTIONAL:
            return type_descriptor_equals(a->params.optional.wrapped_type, 
                                        b->params.optional.wrapped_type);
        default:
            return 1;
    }
}

int type_descriptor_compatible(const TypeDescriptor* from, const TypeDescriptor* to) {
    if (!from || !to) return 0;
    
    // Exact match is always compatible
    if (type_descriptor_equals(from, to)) return 1;
    
    // Special case: empty array [nil] can be assigned to any array type
    if (from->base_type == TYPE_ARRAY && to->base_type == TYPE_ARRAY &&
        from->params.array.element_type && from->params.array.element_type->base_type == TYPE_NIL) {
        return 1;
    }
    
    // Special case: empty dict {nil:nil} can be assigned to any dict type
    if (from->base_type == TYPE_DICT && to->base_type == TYPE_DICT &&
        from->params.dict.key_type && from->params.dict.key_type->base_type == TYPE_NIL &&
        from->params.dict.value_type && from->params.dict.value_type->base_type == TYPE_NIL) {
        return 1;
    }
    
    // nil can be assigned to any optional type
    if (from->base_type == TYPE_NIL && to->base_type == TYPE_OPTIONAL) {
        return 1;
    }
    
    // Implicit wrapping: T -> T? (non-optional to optional)
    if (to->base_type == TYPE_OPTIONAL) {
        return type_descriptor_equals(from, to->params.optional.wrapped_type);
    }
    
    // NO implicit numeric coercion for Phase 3 enforcement
    // Int, Double, Float are distinct types - no automatic conversion
    
    return 0;
}

const char* type_descriptor_to_string(const TypeDescriptor* desc, char* buffer, size_t buffer_size) {
    if (!desc || !buffer || buffer_size == 0) return "";
    
    switch (desc->base_type) {
        case TYPE_INT:
            snprintf(buffer, buffer_size, "Int");
            break;
        case TYPE_DOUBLE:
            snprintf(buffer, buffer_size, "Double");
            break;
        case TYPE_BOOL:
            snprintf(buffer, buffer_size, "Bool");
            break;
        case TYPE_STRING:
            snprintf(buffer, buffer_size, "String");
            break;
        case TYPE_ARRAY: {
            char element_buf[256];
            type_descriptor_to_string(desc->params.array.element_type, element_buf, sizeof(element_buf));
            snprintf(buffer, buffer_size, "[%s]", element_buf);
            break;
        }
        case TYPE_DICT: {
            char key_buf[128], value_buf[128];
            type_descriptor_to_string(desc->params.dict.key_type, key_buf, sizeof(key_buf));
            type_descriptor_to_string(desc->params.dict.value_type, value_buf, sizeof(value_buf));
            snprintf(buffer, buffer_size, "{%s:%s}", key_buf, value_buf);
            break;
        }
        case TYPE_OPTIONAL: {
            char wrapped_buf[256];
            type_descriptor_to_string(desc->params.optional.wrapped_type, wrapped_buf, sizeof(wrapped_buf));
            snprintf(buffer, buffer_size, "%s?", wrapped_buf);
            break;
        }
        case TYPE_NIL:
            snprintf(buffer, buffer_size, "nil");
            break;
        default:
            snprintf(buffer, buffer_size, "Unknown");
            break;
    }
    
    return buffer;
}