#ifndef LLVM_BACKEND_H
#define LLVM_BACKEND_H

#include "../include/ast.h"

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path);

#endif
