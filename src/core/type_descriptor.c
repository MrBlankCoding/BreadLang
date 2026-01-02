#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/var.h"

// Forward declaration
TypeDescriptor* type_descriptor_clone(const TypeDescriptor* desc);

TypeDescriptor* type_descriptor_create_primitive(VarType type) {
    if (type == TYPE_ARRAY || type == TYPE_DICT || type == TYPE_OPTIONAL || type == TYPE_STRUCT || type == TYPE_CLASS) {
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

TypeDescriptor* type_descriptor_create_struct(const char* name, int field_count, char** field_names, TypeDescriptor** field_types) {
    if (!name || field_count < 0 || (field_count > 0 && (!field_names || !field_types))) {
        return NULL;
    }
    
    TypeDescriptor* desc = malloc(sizeof(TypeDescriptor));
    if (!desc) return NULL;
    
    desc->base_type = TYPE_STRUCT;
    desc->params.struct_type.name = strdup(name);
    desc->params.struct_type.field_count = field_count;
    
    if (field_count > 0) {
        desc->params.struct_type.field_names = malloc(field_count * sizeof(char*));
        desc->params.struct_type.field_types = malloc(field_count * sizeof(TypeDescriptor*));
        
        if (!desc->params.struct_type.field_names || !desc->params.struct_type.field_types) {
            free(desc->params.struct_type.name);
            free(desc->params.struct_type.field_names);
            free(desc->params.struct_type.field_types);
            free(desc);
            return NULL;
        }
        
        for (int i = 0; i < field_count; i++) {
            desc->params.struct_type.field_names[i] = strdup(field_names[i]);
            desc->params.struct_type.field_types[i] = type_descriptor_clone(field_types[i]);
        }
    } else {
        desc->params.struct_type.field_names = NULL;
        desc->params.struct_type.field_types = NULL;
    }
    
    return desc;
}

TypeDescriptor* type_descriptor_create_class(const char* name, const char* parent_name, int field_count, char** field_names, TypeDescriptor** field_types) {
    if (!name || field_count < 0 || (field_count > 0 && (!field_names || !field_types))) {
        return NULL;
    }
    
    TypeDescriptor* desc = malloc(sizeof(TypeDescriptor));
    if (!desc) return NULL;
    
    desc->base_type = TYPE_CLASS;
    desc->params.class_type.name = strdup(name);
    desc->params.class_type.parent_name = parent_name ? strdup(parent_name) : NULL;
    desc->params.class_type.field_count = field_count;
    desc->params.class_type.method_count = 0;
    desc->params.class_type.method_names = NULL;
    desc->params.class_type.method_signatures = NULL;
    
    if (field_count > 0) {
        desc->params.class_type.field_names = malloc(field_count * sizeof(char*));
        desc->params.class_type.field_types = malloc(field_count * sizeof(TypeDescriptor*));
        
        if (!desc->params.class_type.field_names || !desc->params.class_type.field_types) {
            free(desc->params.class_type.name);
            free(desc->params.class_type.parent_name);
            free(desc->params.class_type.field_names);
            free(desc->params.class_type.field_types);
            free(desc);
            return NULL;
        }
        
        for (int i = 0; i < field_count; i++) {
            desc->params.class_type.field_names[i] = strdup(field_names[i]);
            desc->params.class_type.field_types[i] = type_descriptor_clone(field_types[i]);
        }
    } else {
        desc->params.class_type.field_names = NULL;
        desc->params.class_type.field_types = NULL;
    }
    
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
        case TYPE_STRUCT:
            free(desc->params.struct_type.name);
            if (desc->params.struct_type.field_names) {
                for (int i = 0; i < desc->params.struct_type.field_count; i++) {
                    free(desc->params.struct_type.field_names[i]);
                }
                free(desc->params.struct_type.field_names);
            }
            if (desc->params.struct_type.field_types) {
                for (int i = 0; i < desc->params.struct_type.field_count; i++) {
                    type_descriptor_free(desc->params.struct_type.field_types[i]);
                }
                free(desc->params.struct_type.field_types);
            }
            break;
        case TYPE_CLASS:
            free(desc->params.class_type.name);
            free(desc->params.class_type.parent_name);
            if (desc->params.class_type.field_names) {
                for (int i = 0; i < desc->params.class_type.field_count; i++) {
                    free(desc->params.class_type.field_names[i]);
                }
                free(desc->params.class_type.field_names);
            }
            if (desc->params.class_type.field_types) {
                for (int i = 0; i < desc->params.class_type.field_count; i++) {
                    type_descriptor_free(desc->params.class_type.field_types[i]);
                }
                free(desc->params.class_type.field_types);
            }
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
        case TYPE_STRUCT: {
            TypeDescriptor** field_types = NULL;
            if (desc->params.struct_type.field_count > 0) {
                field_types = malloc(desc->params.struct_type.field_count * sizeof(TypeDescriptor*));
                if (!field_types) return NULL;
                
                for (int i = 0; i < desc->params.struct_type.field_count; i++) {
                    field_types[i] = type_descriptor_clone(desc->params.struct_type.field_types[i]);
                    if (!field_types[i]) {
                        for (int j = 0; j < i; j++) {
                            type_descriptor_free(field_types[j]);
                        }
                        free(field_types);
                        return NULL;
                    }
                }
            }
            
            TypeDescriptor* out = type_descriptor_create_struct(
                desc->params.struct_type.name,
                desc->params.struct_type.field_count,
                desc->params.struct_type.field_names,
                field_types
            );
            
            if (field_types) {
                for (int i = 0; i < desc->params.struct_type.field_count; i++) {
                    type_descriptor_free(field_types[i]);
                }
                free(field_types);
            }
            
            return out;
        }
        case TYPE_CLASS: {
            TypeDescriptor** field_types = NULL;
            if (desc->params.class_type.field_count > 0) {
                field_types = malloc(desc->params.class_type.field_count * sizeof(TypeDescriptor*));
                if (!field_types) return NULL;
                
                for (int i = 0; i < desc->params.class_type.field_count; i++) {
                    field_types[i] = type_descriptor_clone(desc->params.class_type.field_types[i]);
                    if (!field_types[i]) {
                        for (int j = 0; j < i; j++) {
                            type_descriptor_free(field_types[j]);
                        }
                        free(field_types);
                        return NULL;
                    }
                }
            }
            
            TypeDescriptor* out = type_descriptor_create_class(
                desc->params.class_type.name,
                desc->params.class_type.parent_name,
                desc->params.class_type.field_count,
                desc->params.class_type.field_names,
                field_types
            );
            
            if (field_types) {
                for (int i = 0; i < desc->params.class_type.field_count; i++) {
                    type_descriptor_free(field_types[i]);
                }
                free(field_types);
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
        case TYPE_STRUCT:
            if (strcmp(a->params.struct_type.name, b->params.struct_type.name) != 0) return 0;
            if (a->params.struct_type.field_count != b->params.struct_type.field_count) return 0;
            for (int i = 0; i < a->params.struct_type.field_count; i++) {
                if (strcmp(a->params.struct_type.field_names[i], b->params.struct_type.field_names[i]) != 0) return 0;
                if (!type_descriptor_equals(a->params.struct_type.field_types[i], b->params.struct_type.field_types[i])) return 0;
            }
            return 1;
        case TYPE_CLASS:
            if (strcmp(a->params.class_type.name, b->params.class_type.name) != 0) return 0;
            // For classes, we only check the name - inheritance and methods are handled separately
            return 1;
        default:
            return 1;
    }
}

int type_descriptor_compatible(const TypeDescriptor* from, const TypeDescriptor* to) {
    if (!from || !to) return 0;
    
    // Exact match is always compatible
    if (type_descriptor_equals(from, to)) return 1;

    // Allow nominal interoperability between structs and classes by name.
    // The type parser represents unknown user-defined nominal types as TYPE_STRUCT,
    // but later stages may resolve them as TYPE_CLASS.
    if (from->base_type == TYPE_STRUCT && to->base_type == TYPE_CLASS) {
        if (from->params.struct_type.name && to->params.class_type.name &&
            strcmp(from->params.struct_type.name, to->params.class_type.name) == 0) {
            return 1;
        }
    }
    if (from->base_type == TYPE_CLASS && to->base_type == TYPE_STRUCT) {
        if (from->params.class_type.name && to->params.struct_type.name &&
            strcmp(from->params.class_type.name, to->params.struct_type.name) == 0) {
            return 1;
        }
    }

    // Structs are nominally typed by name. Field metadata may be absent or differ
    // between declared types and inferred literals; treat same-name structs as compatible.
    if (from->base_type == TYPE_STRUCT && to->base_type == TYPE_STRUCT) {
        if (from->params.struct_type.name && to->params.struct_type.name &&
            strcmp(from->params.struct_type.name, to->params.struct_type.name) == 0) {
            return 1;
        }
    }
    
    // Classes are nominally typed by name. Field metadata may be absent or differ
    // between declared types and inferred literals; treat same-name classes as compatible.
    if (from->base_type == TYPE_CLASS && to->base_type == TYPE_CLASS) {
        if (from->params.class_type.name && to->params.class_type.name &&
            strcmp(from->params.class_type.name, to->params.class_type.name) == 0) {
            return 1;
        }
    }
    
    // Special case: empty array [nil] can be assigned to any array type
    if (from->base_type == TYPE_ARRAY && to->base_type == TYPE_ARRAY &&
        from->params.array.element_type && from->params.array.element_type->base_type == TYPE_NIL) {
        return 1;
    }
    
    // Array compatibility: check element type compatibility
    if (from->base_type == TYPE_ARRAY && to->base_type == TYPE_ARRAY) {
        return type_descriptor_compatible(from->params.array.element_type, to->params.array.element_type);
    }

    // Special case: empty dict {nil:nil} can be assigned to any dict type
    if (from->base_type == TYPE_DICT && to->base_type == TYPE_DICT &&
        from->params.dict.key_type && from->params.dict.key_type->base_type == TYPE_NIL &&
        from->params.dict.value_type && from->params.dict.value_type->base_type == TYPE_NIL) {
        return 1;
    }
    
    // Dictionary compatibility: check key and value type compatibility
    if (from->base_type == TYPE_DICT && to->base_type == TYPE_DICT) {
        return type_descriptor_compatible(from->params.dict.key_type, to->params.dict.key_type) &&
               type_descriptor_compatible(from->params.dict.value_type, to->params.dict.value_type);
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
        case TYPE_STRUCT:
            snprintf(buffer, buffer_size, "%s", desc->params.struct_type.name);
            break;
        case TYPE_CLASS:
            snprintf(buffer, buffer_size, "%s", desc->params.class_type.name);
            break;
        case TYPE_NIL:
            snprintf(buffer, buffer_size, "nil");
            break;
        default:
            snprintf(buffer, buffer_size, "Unknown");
            break;
    }
    
    return buffer;
}