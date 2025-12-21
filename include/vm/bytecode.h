#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdint.h>

#include "core/value.h"

typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_DUP,
    OP_POP,

    OP_LOAD_VAR,
    OP_DECL_VAR,
    OP_DECL_VAR_IF_MISSING,
    OP_STORE_VAR,

    OP_PRINT,
    OP_TRACE,

    OP_BINARY,
    OP_NOT,

    OP_CALL,

    OP_ARRAY,
    OP_DICT,
    OP_INDEX,
    OP_MEMBER,
    OP_METHOD_CALL,

    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    OP_FOR_RANGE_INIT,
    OP_FOR_RANGE_CHECK,
    OP_FOR_RANGE_INC,
    OP_FOR_RANGE_END,

    OP_RETURN,
    OP_DEF_FUNC,

    OP_END
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;

    int constants_count;
    int constants_capacity;
    BreadValue* constants;
} BytecodeChunk;

void bc_chunk_init(BytecodeChunk* chunk);
void bc_chunk_free(BytecodeChunk* chunk);

int bc_chunk_write(BytecodeChunk* chunk, uint8_t byte);
int bc_chunk_write_u16(BytecodeChunk* chunk, uint16_t v);

uint16_t bc_chunk_add_constant(BytecodeChunk* chunk, BreadValue v);

#endif
