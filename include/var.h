#ifndef VAR_H
#define VAR_H

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

typedef struct BreadArray BreadArray;
typedef struct BreadDict BreadDict;
typedef struct BreadOptional BreadOptional;

typedef union {
    char* string_val;
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

void init_variables();
void push_scope();
void pop_scope();
void execute_variable_declaration(char* line);
void execute_variable_assignment(char* line);
int declare_variable_raw(const char* name, VarType type, VarValue value, int is_const);
Variable* get_variable(char* name);
void cleanup_variables();

#endif