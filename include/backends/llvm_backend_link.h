#ifndef LLVM_BACKEND_LINK_H
#define LLVM_BACKEND_LINK_H

#include "compiler/ast/ast.h"

int bread_llvm_link_executable_with_clang(const char* obj_path, const char* out_path);
int bread_llvm_emit_exe(const ASTStmtList* program, const char* out_path);

#endif