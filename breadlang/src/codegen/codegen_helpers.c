#include "codegen_internal.h"

 #include <llvm-c/Core.h>
 #include <llvm-c/Target.h>
 #include <llvm-c/TargetMachine.h>

static unsigned fnv1a_hash(const char* s) {
    unsigned h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= (unsigned)(*p);
        h *= 16777619u;
    }
    return h;
}

static unsigned cg_value_alignment(Cg* cg) {
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(cg->mod);
    return LLVMABIAlignmentOfType(td, cg->value_type);
}

LLVMValueRef cg_alloc_value(Cg* cg, const char* name) {
    if (!cg || !cg->builder) return NULL;

    LLVMValueRef alloca =
        LLVMBuildAlloca(cg->builder, cg->value_type, name ? name : "");

    LLVMSetAlignment(alloca, cg_value_alignment(cg));

    /* Initialize to nil to avoid GC seeing garbage */
    LLVMValueRef args[] = {
        cg_value_to_i8_ptr(cg, alloca)
    };

    LLVMBuildCall2(
        cg->builder,
        cg->ty_value_set_nil,
        cg->fn_value_set_nil,
        args,
        1,
        ""
    );

    return alloca;
}

LLVMValueRef cg_value_to_i8_ptr(Cg* cg, LLVMValueRef value_ptr) {
    if (!cg || !value_ptr) return NULL;
    return LLVMBuildBitCast(cg->builder, value_ptr, cg->i8_ptr, "");
}

void cg_copy_value_into(Cg* cg, LLVMValueRef dst, LLVMValueRef src) {
    if (!cg || !dst || !src) return;

    LLVMValueRef args[] = {
        cg_value_to_i8_ptr(cg, src),
        cg_value_to_i8_ptr(cg, dst),
    };

    LLVMBuildCall2(
        cg->builder,
        cg->ty_value_copy,
        cg->fn_value_copy,
        args,
        2,
        ""
    );
}

LLVMValueRef cg_clone_value(Cg* cg, LLVMValueRef src, const char* name) {
    if (!cg || !src) return NULL;

    LLVMValueRef dst = cg_alloc_value(cg, name);
    if (!dst) return NULL;

    cg_copy_value_into(cg, dst, src);
    return dst;
}

CgScope* cg_scope_new(CgScope* parent) {
    CgScope* scope = calloc(1, sizeof(CgScope));
    if (!scope) return NULL;

    scope->parent = parent;
    scope->depth  = parent ? parent->depth + 1 : 0;
    scope->vars   = NULL;

    return scope;
}

LLVMValueRef cg_get_string_global(Cg* cg, const char* s) {
    if (!cg || !cg->mod) return NULL;
    if (!s) s = "";

    unsigned hash = fnv1a_hash(s);

    char gname[64];
    snprintf(gname, sizeof(gname), "__bread_str_%08x", hash);

    LLVMValueRef existing = LLVMGetNamedGlobal(cg->mod, gname);
    if (existing)
        return existing;

    /* Create a constant [N x i8] (LLVMConstString appends the null when dontNullTerminate=0) */
    size_t len = strlen(s);
    LLVMTypeRef arr_ty = LLVMArrayType(cg->i8, (unsigned)(len + 1));
    LLVMValueRef init  = LLVMConstString(s, (unsigned)len, 0);

    LLVMValueRef glob =
        LLVMAddGlobal(cg->mod, arr_ty, gname);

    LLVMSetInitializer(glob, init);
    LLVMSetLinkage(glob, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(glob, 1);
    LLVMSetUnnamedAddress(glob, LLVMGlobalUnnamedAddr);
    LLVMSetAlignment(glob, 1);

    return glob;
}

LLVMValueRef cg_get_string_ptr(Cg* cg, const char* s) {
    if (!cg || !cg->builder) return NULL;

    LLVMValueRef glob = cg_get_string_global(cg, s);
    if (!glob) return NULL;

    LLVMTypeRef arr_ty = LLVMGlobalGetValueType(glob);

    LLVMValueRef idxs[] = {
        LLVMConstInt(cg->i32, 0, 0),
        LLVMConstInt(cg->i32, 0, 0),
    };

    return LLVMBuildInBoundsGEP2(
        cg->builder,
        arr_ty,
        glob,
        idxs,
        2,
        ""
    );
}
