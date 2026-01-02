#ifndef BUILTINS_H
#define BUILTINS_H

#include "runtime/runtime.h"

void bread_builtin_init(void);
void bread_builtin_cleanup(void);
int bread_builtin_register(const BuiltinFunction* builtin);
const BuiltinFunction* bread_builtin_lookup(const char* name);
BreadValue bread_builtin_call(const char* name, BreadValue* args, int arg_count);
void bread_builtin_call_out(const char* name, BreadValue* args, int arg_count, BreadValue* out);

BreadValue bread_builtin_len(BreadValue* args, int arg_count);
BreadValue bread_builtin_type(BreadValue* args, int arg_count);
BreadValue bread_builtin_str(BreadValue* args, int arg_count);
BreadValue bread_builtin_int(BreadValue* args, int arg_count);
BreadValue bread_builtin_float(BreadValue* args, int arg_count);
BreadValue bread_builtin_double(BreadValue* args, int arg_count);
BreadValue bread_builtin_input(BreadValue* args, int arg_count);

#endif
