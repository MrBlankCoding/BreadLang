#ifndef OPERATORS_H
#define OPERATORS_H

#include "runtime/runtime.h"

int bread_index_op(const BreadValue* target, const BreadValue* idx, BreadValue* out);
int bread_member_op(const BreadValue* target, const char* member, int is_opt, BreadValue* out);
int bread_method_call_op(const BreadValue* target, const char* name, int argc, const BreadValue* args, int is_opt, BreadValue* out);
int bread_dict_set_value(struct BreadDict* d, const BreadValue* key, const BreadValue* val);
int bread_array_append_value(struct BreadArray* a, const BreadValue* v);
int bread_array_set_value(struct BreadArray* a, int index, const BreadValue* v);

#endif // OPERATORS_H