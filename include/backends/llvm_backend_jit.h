#ifndef LLVM_BACKEND_JIT_H
#define LLVM_BACKEND_JIT_H

#include "compiler/ast/ast.h"
#include "core/function.h"

int bread_llvm_jit_exec(const ASTStmtList* program);
int bread_llvm_jit_function(Function* fn);

#endif