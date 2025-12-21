#ifndef LLVM_BACKEND_H
#define LLVM_BACKEND_H

#include "compiler/ast.h"

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path);
int bread_llvm_emit_obj(const ASTStmtList* program, const char* out_path);
int bread_llvm_emit_exe(const ASTStmtList* program, const char* out_path);
int bread_llvm_jit_exec(const ASTStmtList* program);

#endif
