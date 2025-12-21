#ifndef COMPILER_H
#define COMPILER_H

#include "compiler/ast.h"
#include "vm/bytecode.h"

int compile_program(ASTStmtList* program, BytecodeChunk* out_chunk);

#endif
