#ifndef VALUE_H
#define VALUE_H

#include "core/forward_decls.h"
#include "core/var.h"
#include "runtime/runtime.h"
#include "compiler/expr.h"

struct BreadArray {
    BreadObjHeader header;
    int count;
    int capacity;
    VarType element_type;  // Type constraint for array elements
    BreadValue* items;
};

typedef struct {
    BreadString* key;
    BreadValue value;
} BreadDictEntry;

struct BreadDict {
    BreadObjHeader header;
    int count;
    int capacity;
    BreadDictEntry* entries;
};

struct BreadOptional {
    BreadObjHeader header;
    int is_some;
    BreadValue value;
};

BreadValue bread_value_from_expr_result(ExprResult r);
ExprResult bread_expr_result_from_value(BreadValue v);

void bread_value_release(BreadValue* v);
BreadValue bread_value_clone(BreadValue v);

BreadArray* bread_array_new(void);
BreadArray* bread_array_new_typed(VarType element_type);
void bread_array_retain(BreadArray* a);
void bread_array_release(BreadArray* a);
int bread_array_append(BreadArray* a, BreadValue v);
int bread_array_set(BreadArray* a, int idx, BreadValue v);
BreadValue* bread_array_get(BreadArray* a, int idx);
int bread_array_length(BreadArray* a);

BreadDict* bread_dict_new(void);
void bread_dict_retain(BreadDict* d);
void bread_dict_release(BreadDict* d);
int bread_dict_set(BreadDict* d, const char* key, BreadValue v);
BreadValue* bread_dict_get(BreadDict* d, const char* key);

BreadOptional* bread_optional_new_none(void);
BreadOptional* bread_optional_new_some(BreadValue v);
void bread_optional_retain(BreadOptional* o);
void bread_optional_release(BreadOptional* o);

int bread_value_get_int(BreadValue* v);
double bread_value_get_double(BreadValue* v);
int bread_value_get_bool(BreadValue* v);
int bread_value_get_type(BreadValue* v);

#endif
