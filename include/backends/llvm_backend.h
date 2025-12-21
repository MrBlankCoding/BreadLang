#ifndef LLVM_BACKEND_H
#define LLVM_BACKEND_H

#include "compiler/ast.h"
#include "core/function.h"

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path);
int bread_llvm_emit_obj(const ASTStmtList* program, const char* out_path);
int bread_llvm_emit_exe(const ASTStmtList* program, const char* out_path);
int bread_llvm_jit_exec(const ASTStmtList* program);

// JIT one function. Returns 0 on success, 1 on failure >:(
// The pointer is stored at fn->jit_fn.
int bread_llvm_jit_function(Function* fn);

#endif
