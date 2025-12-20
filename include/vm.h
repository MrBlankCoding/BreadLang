#ifndef VM_H
#define VM_H

#include "../include/bytecode.h"
#include "../include/expr.h"

typedef struct {
    BreadValue stack[1024];
    int stack_top;

    const BytecodeChunk* chunk;
    const uint8_t* ip;

    int had_error;
} VM;

void vm_init(VM* vm);
void vm_free(VM* vm);

int vm_run(VM* vm, const BytecodeChunk* chunk, int is_function, ExprResult* out_return);

#endif
