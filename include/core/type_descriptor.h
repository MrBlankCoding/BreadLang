#ifndef TYPE_DESCRIPTOR_H
#define TYPE_DESCRIPTOR_H

#include <stddef.h>

#include "core/var.h"

TypeDescriptor* type_descriptor_create_primitive(VarType type);
TypeDescriptor* type_descriptor_create_array(TypeDescriptor* element_type);
TypeDescriptor* type_descriptor_create_dict(TypeDescriptor* key_type, TypeDescriptor* value_type);
TypeDescriptor* type_descriptor_create_optional(TypeDescriptor* wrapped_type);
TypeDescriptor* type_descriptor_create_struct(const char* name, int field_count, char** field_names, TypeDescriptor** field_types);
void type_descriptor_free(TypeDescriptor* desc);
TypeDescriptor* type_descriptor_clone(const TypeDescriptor* desc);
int type_descriptor_equals(const TypeDescriptor* a, const TypeDescriptor* b);
int type_descriptor_compatible(const TypeDescriptor* from, const TypeDescriptor* to);
const char* type_descriptor_to_string(const TypeDescriptor* desc, char* buffer, size_t buffer_size);

#endif
