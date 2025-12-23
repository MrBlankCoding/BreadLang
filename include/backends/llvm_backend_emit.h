#ifndef LLVM_BACKEND_EMIT_H
#define LLVM_BACKEND_EMIT_H

#include <llvm-c/Core.h>
#include "compiler/ast/ast.h"
#include "codegen/codegen.h"

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path);
int bread_llvm_emit_obj(const ASTStmtList* program, const char* out_path);
int bread_llvm_generate_class_runtime_init(Cg* cg, LLVMModuleRef mod);

#endif