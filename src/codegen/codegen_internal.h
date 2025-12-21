#ifndef BREAD_CODEGEN_INTERNAL_H
#define BREAD_CODEGEN_INTERNAL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen/codegen.h"

#include "runtime/builtins.h"
#include "compiler/analysis/type_stability.h"

LLVMValueRef cg_alloc_value(Cg* cg, const char* name);
LLVMValueRef cg_value_to_i8_ptr(Cg* cg, LLVMValueRef value_ptr);
void cg_copy_value_into(Cg* cg, LLVMValueRef dst, LLVMValueRef src);
LLVMValueRef cg_clone_value(Cg* cg, LLVMValueRef src, const char* name);
LLVMValueRef cg_get_string_global(Cg* cg, const char* s);
CgScope* cg_scope_new(CgScope* parent);

#endif
