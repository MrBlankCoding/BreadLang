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

// Unboxed types
typedef enum {
    UNBOXED_NONE = 0,    // Defauly is boxed lmaooooooo
    UNBOXED_INT = 1,     // i32
    UNBOXED_BOOL = 2,    // i1 :0
    UNBOXED_DOUBLE = 3   // DOUBLEEEEE
} UnboxedType;

static inline int var_type_can_unbox(VarType type) {
    return type == TYPE_INT || type == TYPE_BOOL || type == TYPE_DOUBLE;
}

static inline UnboxedType var_type_to_unboxed(VarType type) {
    switch (type) {
        case TYPE_INT: return UNBOXED_INT;
        case TYPE_BOOL: return UNBOXED_BOOL;
        case TYPE_DOUBLE: return UNBOXED_DOUBLE;
        default: return UNBOXED_NONE;
    }
}

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