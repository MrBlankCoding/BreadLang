#ifndef VALUE_H
#define VALUE_H

#include "../include/expr.h"
#include "../include/var.h"
#include "../include/runtime.h"

typedef struct BreadValue {
    VarType type;
    VarValue value;
} BreadValue;

struct BreadArray {
    BreadObjHeader header;
    int count;
    int capacity;
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
void bread_array_retain(BreadArray* a);
void bread_array_release(BreadArray* a);
int bread_array_append(BreadArray* a, BreadValue v);
BreadValue* bread_array_get(BreadArray* a, int idx);

BreadDict* bread_dict_new(void);
void bread_dict_retain(BreadDict* d);
void bread_dict_release(BreadDict* d);
int bread_dict_set(BreadDict* d, const char* key, BreadValue v);
BreadValue* bread_dict_get(BreadDict* d, const char* key);

BreadOptional* bread_optional_new_none(void);
BreadOptional* bread_optional_new_some(BreadValue v);
void bread_optional_retain(BreadOptional* o);
void bread_optional_release(BreadOptional* o);

#endif
