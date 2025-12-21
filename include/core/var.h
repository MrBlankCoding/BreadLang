#ifndef VAR_H
#define VAR_H

#include "core/forward_decls.h"

typedef enum {
    TYPE_STRING,
    TYPE_INT,
    TYPE_BOOL,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_ARRAY,
    TYPE_DICT,
    TYPE_OPTIONAL,
    TYPE_NIL
} VarType;

typedef union {
    BreadString* string_val;
    int int_val;
    int bool_val;
    float float_val;
    double double_val;
    BreadArray* array_val;
    BreadDict* dict_val;
    BreadOptional* optional_val;
} VarValue;

typedef struct {
    char* name;
    VarType type;
    VarValue value;
    int is_const;
} Variable;

struct ExprResult;

void init_variables();
void push_scope();
void pop_scope();
void execute_variable_declaration(char* line);
void execute_variable_assignment(char* line);
int bread_init_variable_from_expr_result(const char* name, const struct ExprResult* value);
int bread_assign_variable_from_expr_result(const char* name, const struct ExprResult* value);
int declare_variable_raw(const char* name, VarType type, VarValue value, int is_const);
Variable* get_variable(const char* name);
void cleanup_variables();

#endif