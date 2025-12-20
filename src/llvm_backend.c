#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/llvm_backend.h"
#include "../include/bread_ir.h"

#ifndef BREAD_HAVE_LLVM
int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path) {
    (void)program;
    (void)out_path;
    printf("Error: LLVM backend not enabled (rebuild with llvm-config flags)\n");
    return 0;
}
#else
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>

static int write_text_file(const char* path, const char* data) {
    if (!path || !data) return 0;
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    size_t n = strlen(data);
    if (n > 0) {
        if (fwrite(data, 1, n, f) != n) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

static LLVMValueRef get_printf(LLVMModuleRef mod, LLVMTypeRef i32, LLVMTypeRef i8_ptr, LLVMTypeRef* out_fn_ty) {
    LLVMTypeRef arg_tys[1];
    arg_tys[0] = i8_ptr;
    LLVMTypeRef fn_ty = LLVMFunctionType(i32, arg_tys, 1, 1);
    if (out_fn_ty) *out_fn_ty = fn_ty;

    LLVMValueRef fn = LLVMGetNamedFunction(mod, "printf");
    if (!fn) fn = LLVMAddFunction(mod, "printf", fn_ty);
    return fn;
}

static LLVMValueRef get_puts(LLVMModuleRef mod, LLVMTypeRef i32, LLVMTypeRef i8_ptr, LLVMTypeRef* out_fn_ty) {
    LLVMTypeRef arg_tys[1];
    arg_tys[0] = i8_ptr;
    LLVMTypeRef fn_ty = LLVMFunctionType(i32, arg_tys, 1, 0);
    if (out_fn_ty) *out_fn_ty = fn_ty;

    LLVMValueRef fn = LLVMGetNamedFunction(mod, "puts");
    if (!fn) fn = LLVMAddFunction(mod, "puts", fn_ty);
    return fn;
}

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;

    BreadIRProgram ir;
    if (!bread_ir_lower_program(program, &ir)) return 0;

    LLVMModuleRef mod = LLVMModuleCreateWithName("bread_module");
    LLVMBuilderRef builder = LLVMCreateBuilder();

    LLVMTypeRef i32 = LLVMInt32Type();
    LLVMTypeRef i8 = LLVMInt8Type();
    LLVMTypeRef i8_ptr = LLVMPointerType(i8, 0);

    LLVMTypeRef main_ty = LLVMFunctionType(i32, NULL, 0, 0);
    LLVMValueRef main_fn = LLVMAddFunction(mod, "main", main_ty);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    LLVMTypeRef printf_ty = NULL;
    LLVMTypeRef puts_ty = NULL;
    LLVMValueRef printf_fn = get_printf(mod, i32, i8_ptr, &printf_ty);
    LLVMValueRef puts_fn = get_puts(mod, i32, i8_ptr, &puts_ty);

    for (size_t i = 0; i < ir.count; i++) {
        BreadIRInst inst = ir.insts[i];
        if (inst.op == BREAD_IR_OP_PRINT) {
            if (inst.a.kind == BREAD_IR_VAL_STRING) {
                LLVMValueRef s = LLVMBuildGlobalStringPtr(builder, inst.a.as.string_val ? inst.a.as.string_val : "", "str");
                LLVMValueRef args[1];
                args[0] = s;
                (void)LLVMBuildCall2(builder, puts_ty, puts_fn, args, 1, "");
                continue;
            }

            if (inst.a.kind == BREAD_IR_VAL_BOOL) {
                const char* txt = inst.a.as.bool_val ? "true" : "false";
                LLVMValueRef s = LLVMBuildGlobalStringPtr(builder, txt, "bool_str");
                LLVMValueRef args[1];
                args[0] = s;
                (void)LLVMBuildCall2(builder, puts_ty, puts_fn, args, 1, "");
                continue;
            }

            if (inst.a.kind == BREAD_IR_VAL_INT) {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%d\\n", "fmt_int");
                LLVMValueRef v = LLVMConstInt(i32, (unsigned long long)(unsigned int)inst.a.as.int_val, 1);
                LLVMValueRef args[2];
                args[0] = fmt;
                args[1] = v;
                (void)LLVMBuildCall2(builder, printf_ty, printf_fn, args, 2, "");
                continue;
            }

            if (inst.a.kind == BREAD_IR_VAL_DOUBLE) {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%lf\\n", "fmt_double");
                LLVMValueRef v = LLVMConstReal(LLVMDoubleType(), inst.a.as.double_val);
                LLVMValueRef args[2];
                args[0] = fmt;
                args[1] = v;
                (void)LLVMBuildCall2(builder, printf_ty, printf_fn, args, 2, "");
                continue;
            }

            bread_ir_program_free(&ir);
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(mod);
            printf("Error: LLVM backend: unsupported print value\n");
            return 0;
        }

        if (inst.op == BREAD_IR_OP_RET) {
            LLVMValueRef zero = LLVMConstInt(i32, 0, 0);
            (void)LLVMBuildRet(builder, zero);
            break;
        }
    }

    if (LLVMGetBasicBlockTerminator(entry) == NULL) {
        LLVMValueRef zero = LLVMConstInt(i32, 0, 0);
        (void)LLVMBuildRet(builder, zero);
    }

    char* err = NULL;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &err)) {
        if (err) {
            printf("Error: LLVM module verification failed: %s\n", err);
            LLVMDisposeMessage(err);
        }
        bread_ir_program_free(&ir);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    char* ir_text = LLVMPrintModuleToString(mod);
    int ok = write_text_file(out_path, ir_text ? ir_text : "");
    if (!ok) {
        printf("Error: Could not write LLVM IR to '%s'\n", out_path);
    }

    if (ir_text) LLVMDisposeMessage(ir_text);

    bread_ir_program_free(&ir);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(mod);

    return ok;
}
#endif
