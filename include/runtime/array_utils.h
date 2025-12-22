#ifndef ARRAY_UTILS_H
#define ARRAY_UTILS_H

#include "runtime/runtime.h"

struct BreadArray* bread_range_create(int start, int end, int step);
struct BreadArray* bread_range(int n);
int bread_array_get_value(struct BreadArray* a, int idx, BreadValue* out);
int bread_value_array_get(BreadValue* array_val, int idx, BreadValue* out);
int bread_value_array_length(BreadValue* array_val);
struct BreadArray* bread_value_dict_keys(BreadValue* dict_val);
int bread_value_dict_keys_as_value(BreadValue* dict_val, BreadValue* out);

#endif