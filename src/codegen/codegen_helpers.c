#include "codegen_internal.h"

LLVMValueRef cg_alloc_value(Cg* cg, const char* name) {
    LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, cg->value_type, name ? name : "");
    LLVMSetAlignment(alloca, 16);
    // Avoid garbage being freed
    LLVMValueRef args[] = { cg_value_to_i8_ptr(cg, alloca) };
    LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, args, 1, "");
    return alloca;
}

LLVMValueRef cg_value_to_i8_ptr(Cg* cg, LLVMValueRef value_ptr) {
    return LLVMBuildBitCast(cg->builder, value_ptr, cg->i8_ptr, "");
}

void cg_copy_value_into(Cg* cg, LLVMValueRef dst, LLVMValueRef src) {
    LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, src), cg_value_to_i8_ptr(cg, dst)};
    (void)LLVMBuildCall2(cg->builder, cg->ty_value_copy, cg->fn_value_copy, args, 2, "");
}

LLVMValueRef cg_clone_value(Cg* cg, LLVMValueRef src, const char* name) {
    LLVMValueRef dst = cg_alloc_value(cg, name);
    cg_copy_value_into(cg, dst, src);
    return dst;
}

CgScope* cg_scope_new(CgScope* parent) {
    CgScope* scope = (CgScope*)malloc(sizeof(CgScope));
    scope->parent = parent;
    scope->vars = NULL;
    scope->depth = parent ? parent->depth + 1 : 0;
    return scope;
}

LLVMValueRef cg_get_string_global(Cg* cg, const char* s) {
    LLVMValueRef existing = LLVMGetNamedGlobal(cg->mod, s);
    if (existing) {
        return existing;
    }

    LLVMValueRef val = LLVMConstString(s, (unsigned)strlen(s), 0);
    LLVMValueRef glob = LLVMAddGlobal(cg->mod, LLVMTypeOf(val), "");
    LLVMSetInitializer(glob, val);
    LLVMSetLinkage(glob, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(glob, 1);
    LLVMSetUnnamedAddress(glob, LLVMGlobalUnnamedAddr);
    return glob;
}
