#ifndef VALUE_OPS_H
#define VALUE_OPS_H

#include "runtime/runtime.h"

int bread_add(const BreadValue* left, const BreadValue* right, BreadValue* out);
int bread_eq(const BreadValue* left, const BreadValue* right, int* out_bool);
int bread_binary_op(char op, const BreadValue* left, const BreadValue* right, BreadValue* out);
int bread_unary_not(const BreadValue* in, BreadValue* out);
void bread_print(const BreadValue* v);
void bread_print_compact(const BreadValue* v);
void bread_value_set_nil(BreadValue* out);
void bread_value_set_bool(BreadValue* out, int v);
void bread_value_set_int(BreadValue* out, int v);
void bread_value_set_float(BreadValue* out, float v);
void bread_value_set_double(BreadValue* out, double v);
void bread_value_set_string(BreadValue* out, const char* cstr);
void bread_value_set_array(BreadValue* out, struct BreadArray* a);
void bread_value_set_dict(BreadValue* out, struct BreadDict* d);
void bread_value_set_optional(BreadValue* out, struct BreadOptional* o);
size_t bread_value_size(void);
void bread_value_copy(const BreadValue* in, BreadValue* out);
void bread_value_release_value(BreadValue* v);
int bread_is_truthy(const BreadValue* v);
int bread_coerce_value(VarType target, const BreadValue* in, BreadValue* out);

#endif