#ifndef LLVM_BACKEND_CODEGEN_H
#define LLVM_BACKEND_CODEGEN_H

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include "compiler/ast/ast.h"
#include "codegen/codegen.h"

void cg_init(Cg* cg, LLVMModuleRef mod, LLVMBuilderRef builder);
int bread_llvm_build_module_from_program(const ASTStmtList* program, LLVMModuleRef* out_mod, Cg* out_cg);
int bread_llvm_generate_function_bodies(Cg* cg, LLVMBuilderRef builder, LLVMValueRef val_size);
int bread_llvm_generate_class_methods(Cg* cg, LLVMBuilderRef builder, LLVMValueRef val_size);
int bread_llvm_verify_module(LLVMModuleRef mod);
LLVMTargetMachineRef bread_llvm_create_native_target_machine(void);

#endif