#ifndef BREAD_IR_H
#define BREAD_IR_H

#include <stddef.h>

#include "../include/ast.h"

typedef enum {
    BREAD_IR_OP_NOP = 0,
    BREAD_IR_OP_PRINT = 1,
    BREAD_IR_OP_RET = 2
} BreadIROp;

typedef enum {
    BREAD_IR_VAL_NONE = 0,
    BREAD_IR_VAL_INT = 1,
    BREAD_IR_VAL_DOUBLE = 2,
    BREAD_IR_VAL_BOOL = 3,
    BREAD_IR_VAL_STRING = 4
} BreadIRValKind;

typedef struct {
    BreadIRValKind kind;
    union {
        int int_val;
        double double_val;
        int bool_val;
        const char* string_val;
    } as;
} BreadIRValue;

typedef struct {
    BreadIROp op;
    BreadIRValue a;
} BreadIRInst;

typedef struct {
    BreadIRInst* insts;
    size_t count;
    size_t capacity;
} BreadIRProgram;

void bread_ir_program_init(BreadIRProgram* p);
void bread_ir_program_free(BreadIRProgram* p);

int bread_ir_lower_program(const ASTStmtList* program, BreadIRProgram* out);

#endif
