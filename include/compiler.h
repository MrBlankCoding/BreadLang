#ifndef COMPILER_H
#define COMPILER_H

#include "../include/ast.h"
#include "../include/bytecode.h"

int compile_program(ASTStmtList* program, BytecodeChunk* out_chunk);

#endif
