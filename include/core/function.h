#ifndef FUNCTION_H
#define FUNCTION_H

#include "compiler/expr.h"

typedef struct {
    char* name;
    int param_count;
    char** param_names;
    VarType* param_types;
    VarType return_type;
    void* body;
    int body_is_ast;

    // JIT fields
    int hot_count;
    int is_jitted;
    void (*jit_fn)(void*, void**); // Takes return slot and array of argument pointers
    void* jit_engine; // LLVMExecutionEngineRef
} Function;

void init_functions();
void cleanup_functions();
int register_function(const Function* fn);
const Function* get_function(const char* name);
int get_function_count();
const Function* get_function_at(int index);
int type_compatible(VarType expected, VarType actual);
VarValue coerce_value(VarType target, ExprResult val);

ExprResult call_function(const char* name, int arg_count, const char** arg_exprs);
ExprResult call_function_values(const char* name, int arg_count, ExprResult* arg_vals);

#endif
