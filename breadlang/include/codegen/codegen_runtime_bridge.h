#ifndef CODEGEN_RUNTIME_BRIDGE_H
#define CODEGEN_RUNTIME_BRIDGE_H

#include "codegen/codegen.h"
#include "core/value.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
// Bridge between LLVM codegen and runtime class objects

void cg_set_jit_module(LLVMModuleRef module, LLVMExecutionEngineRef engine);
int cg_connect_class_to_runtime(Cg* cg, CgClass* cg_class, BreadClass* runtime_class);
int cg_connect_all_classes_to_runtime(Cg* cg);
int cg_execute_llvm_method(LLVMValueRef llvm_fn, BreadClass* self, int argc, const BreadValue* args, BreadValue* out);
int cg_execute_compiled_method(BreadCompiledMethod compiled_fn, BreadClass* self, int argc, const BreadValue* args, BreadValue* out);
BreadMethod cg_create_runtime_method_wrapper(LLVMValueRef llvm_fn);
BreadClass* cg_get_runtime_class(const char* class_name);
int cg_is_jit_available();
void cg_cleanup_jit_engine();
void cg_cleanup_class_registry();

#endif // CODEGEN_RUNTIME_BRIDGE_H