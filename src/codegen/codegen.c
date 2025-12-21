#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen/codegen.h"

LLVMValueRef cg_declare_fn(Cg* cg, const char* name, LLVMTypeRef fn_type) {
    LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, name);
    if (fn) {
        return fn;
    }
    return LLVMAddFunction(cg->mod, name, fn_type);
}

int cg_define_functions(Cg* cg) {
    (void)cg;
    return 1;
}

LLVMValueRef cg_value_size(Cg* cg) {
    return LLVMBuildCall2(cg->builder, cg->ty_bread_value_size, cg->fn_bread_value_size, NULL, 0, "");
}