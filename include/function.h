#ifndef FUNCTION_H
#define FUNCTION_H

#include "../include/stmt.h"
#include "../include/expr.h"

typedef struct {
    char* name;
    int param_count;
    char** param_names;
    VarType* param_types;
    VarType return_type;
    StmtList* body;
} Function;

void init_functions();
void cleanup_functions();
int register_function(const Function* fn);
const Function* get_function(const char* name);
ExprResult call_function(const char* name, int arg_count, const char** arg_exprs);

#endif
