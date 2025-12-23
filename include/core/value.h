#ifndef VALUE_H
#define VALUE_H

#include "core/forward_decls.h"
#include "core/var.h"
#include "runtime/runtime.h"
#include "compiler/parser/expr.h"

struct BreadArray {
    BreadObjHeader header;
    int count;
    int capacity;
    VarType element_type; 
    BreadValue* items;
};

typedef struct {
    BreadValue key;        
    BreadValue value;      
    int is_occupied;       
    int is_deleted;        
} BreadDictEntry;

struct BreadDict {
    BreadObjHeader header;
    int count;
    int capacity;
    VarType key_type;      
    VarType value_type;    
    BreadDictEntry* entries;
};

struct BreadOptional {
    BreadObjHeader header;
    int is_some;
    BreadValue value;
};

struct BreadStruct {
    BreadObjHeader header;
    char* type_name;
    int field_count;
    char** field_names;
    BreadValue* field_values;
};

// Forward declaration for method function pointer
typedef struct BreadValue (*BreadMethod)(struct BreadClass* self, struct BreadValue* args, int arg_count);

// LLVM function pointer type for compiled methods
typedef void (*BreadCompiledMethod)(void);

struct BreadClass {
    BreadObjHeader header;
    char* class_name;
    char* parent_name;  // NULL if no inheritance
    struct BreadClass* parent_class;  // NULL if no inheritance
    int field_count;
    char** field_names;
    BreadValue* field_values;
    int method_count;
    char** method_names;
    BreadMethod* methods;
    BreadMethod constructor;  // NULL if no explicit constructor
    
    // LLVM compiled function support
    BreadCompiledMethod* compiled_methods;  // Array of compiled LLVM function pointers
    BreadCompiledMethod compiled_constructor;  // Compiled constructor function
};

BreadValue bread_value_from_expr_result(ExprResult r);
ExprResult bread_expr_result_from_value(BreadValue v);

void bread_value_release(BreadValue* v);
BreadValue bread_value_clone(BreadValue v);

BreadArray* bread_array_new(void);
BreadArray* bread_array_new_typed(VarType element_type);
BreadArray* bread_array_new_with_capacity(int capacity, VarType element_type);
BreadArray* bread_array_from_literal(BreadValue* elements, int count);

BreadStruct* bread_struct_new(const char* type_name, int field_count, char** field_names);
void bread_struct_set_field(BreadStruct* s, const char* field_name, BreadValue value);
void bread_struct_set_field_value_ptr(BreadStruct* s, const char* field_name, const BreadValue* value);
BreadValue* bread_struct_get_field(BreadStruct* s, const char* field_name);
int bread_struct_find_field_index(BreadStruct* s, const char* field_name);
void bread_struct_retain(BreadStruct* s);
void bread_struct_release(BreadStruct* s);

BreadClass* bread_class_new(const char* class_name, const char* parent_name, int field_count, char** field_names);
BreadClass* bread_class_new_with_methods(const char* class_name, const char* parent_name, 
                                        int field_count, char** field_names,
                                        int method_count, char** method_names);
void bread_class_register_definition(BreadClass* class_def);
BreadClass* bread_class_find_definition(const char* class_name);
void bread_class_resolve_inheritance(void);
BreadClass* bread_class_create_instance(const char* class_name, const char* parent_name, 
                                       int field_count, char** field_names,
                                       int method_count, char** method_names);
void bread_class_set_field(BreadClass* c, const char* field_name, BreadValue value);
void bread_class_set_field_value_ptr(BreadClass* c, const char* field_name, const BreadValue* value);
BreadValue* bread_class_get_field(BreadClass* c, const char* field_name);
int bread_class_get_field_value_ptr(BreadClass* c, const char* field_name, BreadValue* out);
int bread_class_find_field_index(BreadClass* c, const char* field_name);
int bread_class_find_method_index(BreadClass* c, const char* method_name);
void bread_class_add_method(BreadClass* c, const char* method_name, BreadMethod method);
BreadMethod bread_class_get_method(BreadClass* c, const char* method_name);
BreadValue bread_class_call_method(BreadClass* c, const char* method_name, BreadValue* args, int arg_count);
int bread_class_execute_method(BreadClass* c, int method_index, int argc, const BreadValue* args, BreadValue* out);
int bread_class_execute_method_direct(BreadClass* defining_class, int method_index, BreadClass* instance, int argc, const BreadValue* args, BreadValue* out);
int bread_class_execute_constructor(BreadClass* c, int argc, const BreadValue* args, BreadValue* out);
void bread_class_set_compiled_method(BreadClass* c, int method_index, BreadCompiledMethod compiled_fn);
void bread_class_set_compiled_constructor(BreadClass* c, BreadCompiledMethod compiled_fn);
void bread_class_retain(BreadClass* c);
void bread_class_release(BreadClass* c);
void bread_struct_release(BreadStruct* s);

// Initialize the value system
void value_init(void);
BreadArray* bread_array_repeating(BreadValue value, int count);
void bread_array_retain(BreadArray* a);
void bread_array_release(BreadArray* a);
int bread_array_append(BreadArray* a, BreadValue v);
int bread_array_insert(BreadArray* array, BreadValue value, int index);
BreadValue bread_array_remove_at(BreadArray* array, int index);
int bread_array_contains(BreadArray* array, BreadValue value);
int bread_array_index_of(BreadArray* array, BreadValue value);
int bread_array_set(BreadArray* a, int idx, BreadValue v);
BreadValue* bread_array_get(BreadArray* a, int idx);
BreadValue* bread_array_get_safe(BreadArray* array, int index);
int bread_array_set_safe(BreadArray* array, int index, BreadValue value);
int bread_array_negative_index(BreadArray* array, int index);
int bread_array_length(BreadArray* a);
BreadDict* bread_dict_new(void);
BreadDict* bread_dict_new_typed(VarType key_type, VarType value_type);
BreadDict* bread_dict_new_with_capacity(int capacity, VarType key_type, VarType value_type);
BreadDict* bread_dict_from_literal(BreadDictEntry* entries, int count);
void bread_dict_retain(BreadDict* d);
void bread_dict_release(BreadDict* d);
uint32_t bread_dict_hash_key(BreadValue key);
int bread_dict_find_slot(BreadDict* dict, BreadValue key);
void bread_dict_resize(BreadDict* dict, int new_capacity);
int bread_dict_set(BreadDict* d, const char* key, BreadValue v);
BreadValue* bread_dict_get(BreadDict* d, const char* key);
BreadValue* bread_dict_get_safe(BreadDict* dict, BreadValue key);
BreadValue bread_dict_get_with_default(BreadDict* dict, BreadValue key, BreadValue default_val);
int bread_dict_set_safe(BreadDict* dict, BreadValue key, BreadValue value);
int bread_dict_count(BreadDict* dict);
BreadArray* bread_dict_keys(BreadDict* dict);
BreadArray* bread_dict_values(BreadDict* dict);
int bread_dict_contains_key(BreadDict* dict, BreadValue key);
BreadValue bread_dict_remove(BreadDict* dict, BreadValue key);
void bread_dict_clear(BreadDict* dict);
BreadOptional* bread_optional_new_none(void);
BreadOptional* bread_optional_new_some(BreadValue v);
void bread_optional_retain(BreadOptional* o);
void bread_optional_release(BreadOptional* o);
int bread_value_get_int(BreadValue* v);
double bread_value_get_double(BreadValue* v);
int bread_value_get_bool(BreadValue* v);
int bread_value_get_type(BreadValue* v);
BreadValue bread_box_int(int value);
BreadValue bread_box_double(double value);
BreadValue bread_box_bool(int value);
int bread_unbox_int(const BreadValue* v);
double bread_unbox_double(const BreadValue* v);
int bread_unbox_bool(const BreadValue* v);

#endif
