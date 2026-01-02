#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/llvm_backend_emit.h"
#include "backends/llvm_backend_codegen.h"
#include "backends/llvm_backend_utils.h"
#include "runtime/error.h"
#include "codegen/codegen_runtime_bridge.h"
#include "../codegen/codegen_internal.h"

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

static int bread_llvm_configure_target(LLVMModuleRef mod, LLVMTargetMachineRef tm) {
    char* triple = LLVMGetDefaultTargetTriple();
    if (!triple) return 0;

    LLVMSetTarget(mod, triple);

    LLVMTargetDataRef data = LLVMCreateTargetDataLayout(tm);
    char* layout = LLVMCopyStringRepOfTargetData(data);

    if (layout) {
        LLVMSetDataLayout(mod, layout);
        LLVMDisposeMessage(layout);
    }

    LLVMDisposeTargetData(data);
    LLVMDisposeMessage(triple);
    return 1;
}

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;
    if (bread_error_has_compilation_errors()) return 0;

    LLVMModuleRef mod = NULL;
    if (!bread_llvm_build_module_from_program(program, &mod, NULL)) {
        BREAD_ERROR_SET_COMPILE_ERROR("Failed to build LLVM module");
        return 0;
    }

    if (!bread_llvm_verify_module(mod)) {
        BREAD_ERROR_SET_COMPILE_ERROR("LLVM module verification failed");
        LLVMDisposeModule(mod);
        return 0;
    }

    char* ir = LLVMPrintModuleToString(mod);
    int ok = write_text_file(out_path, ir ? ir : "");

    if (!ok) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Could not write LLVM IR to '%s'", out_path);
        BREAD_ERROR_SET_COMPILE_ERROR(buf);
    }

    if (ir) LLVMDisposeMessage(ir);
    LLVMDisposeModule(mod);
    return ok;
}

int bread_llvm_emit_obj(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;
    if (bread_error_has_compilation_errors()) return 0;

    LLVMModuleRef mod = NULL;
    Cg cg = {0};

    if (!bread_llvm_build_module_from_program(program, &mod, &cg)) {
        BREAD_ERROR_SET_COMPILE_ERROR("Failed to build LLVM module");
        return 0;
    }

    bread_llvm_generate_class_runtime_init(&cg, mod);

    LLVMTargetMachineRef tm = bread_llvm_create_native_target_machine();
    if (!tm) {
        LLVMDisposeModule(mod);
        return 0;
    }

    if (!bread_llvm_configure_target(mod, tm)) {
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeModule(mod);
        return 0;
    }

    if (!bread_llvm_verify_module(mod)) {
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeModule(mod);
        return 0;
    }

    char* err = NULL;
    if (LLVMTargetMachineEmitToFile(tm, mod, (char*)out_path,
                                   LLVMObjectFile, &err)) {
        if (err) {
            fprintf(stderr, "LLVM emit error: %s\n", err);
            LLVMDisposeMessage(err);
        }
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeModule(mod);
        return 0;
    }

    LLVMDisposeTargetMachine(tm);
    LLVMDisposeModule(mod);
    return 1;
}

int bread_llvm_generate_class_runtime_init(Cg* cg, LLVMModuleRef mod) {
    if (!cg || !cg->classes) return 1;

    LLVMTypeRef init_fn_ty = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    LLVMValueRef init_fn =
        LLVMAddFunction(mod, "bread_runtime_init_classes", init_fn_ty);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(init_fn, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);

    // Declare once
    LLVMTypeRef ty_class_create =
        LLVMFunctionType(cg->i8_ptr,
            (LLVMTypeRef[]){
                cg->i8_ptr, cg->i8_ptr,
                cg->i32, LLVMPointerType(cg->i8_ptr, 0),
                cg->i32, LLVMPointerType(cg->i8_ptr, 0)
            }, 6, 0);

    LLVMTypeRef ty_register =
        LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);

    LLVMTypeRef ty_set_compiled_ctor =
        LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);

    LLVMTypeRef ty_set_compiled_method_by_name =
        LLVMFunctionType(
            cg->void_ty,
            (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr},
            3,
            0);

    LLVMTypeRef ty_resolve_inheritance =
        LLVMFunctionType(cg->void_ty, NULL, 0, 0);

    LLVMValueRef fn_class_create =
        cg_declare_fn(cg, "bread_class_create_instance", ty_class_create);
    LLVMValueRef fn_register =
        cg_declare_fn(cg, "bread_class_register_definition", ty_register);
    LLVMValueRef fn_set_compiled_ctor =
        cg_declare_fn(cg, "bread_class_set_compiled_constructor", ty_set_compiled_ctor);
    LLVMValueRef fn_set_compiled_method_by_name =
        cg_declare_fn(cg, "bread_class_set_compiled_method_by_name", ty_set_compiled_method_by_name);
    LLVMValueRef fn_resolve_inheritance =
        cg_declare_fn(cg, "bread_class_resolve_inheritance", ty_resolve_inheritance);

    for (CgClass* cls = cg->classes; cls; cls = cls->next) {
        LLVMValueRef class_name =
            LLVMBuildBitCast(builder,
                cg_get_string_global(cg, cls->name),
                cg->i8_ptr, "");

        LLVMValueRef parent_name =
            cls->parent_name
                ? LLVMBuildBitCast(builder,
                      cg_get_string_global(cg, cls->parent_name),
                      cg->i8_ptr, "")
                : LLVMConstNull(cg->i8_ptr);

        char** field_names = NULL;
        int field_count = 0;
        cg_collect_all_fields(cg, cls, &field_names, &field_count);

        LLVMValueRef fields_ptr =
            LLVMConstNull(LLVMPointerType(cg->i8_ptr, 0));

        if (field_count > 0) {
            LLVMTypeRef arr_ty = LLVMArrayType(cg->i8_ptr, field_count);
            LLVMValueRef arr = LLVMBuildAlloca(builder, arr_ty, "fields");

            for (int i = 0; i < field_count; i++) {
                LLVMValueRef slot =
                    LLVMBuildGEP2(builder, arr_ty, arr,
                        (LLVMValueRef[]){
                            LLVMConstInt(cg->i32, 0, 0),
                            LLVMConstInt(cg->i32, i, 0)
                        }, 2, "");

                LLVMBuildStore(builder,
                    LLVMBuildBitCast(builder,
                        cg_get_string_global(cg, field_names[i]),
                        cg->i8_ptr, ""),
                    slot);
                free(field_names[i]);
            }
            free(field_names);

            fields_ptr = LLVMBuildBitCast(
                builder,
                LLVMBuildGEP2(builder, arr_ty, arr,
                    (LLVMValueRef[]){
                        LLVMConstInt(cg->i32, 0, 0),
                        LLVMConstInt(cg->i32, 0, 0)
                    }, 2, ""),
                LLVMPointerType(cg->i8_ptr, 0),
                "");
        }

        LLVMValueRef methods_ptr =
            LLVMConstNull(LLVMPointerType(cg->i8_ptr, 0));

        if (cls->method_count > 0 && cls->method_names) {
            LLVMTypeRef marr_ty = LLVMArrayType(cg->i8_ptr, cls->method_count);
            LLVMValueRef marr = LLVMBuildAlloca(builder, marr_ty, "methods");

            for (int i = 0; i < cls->method_count; i++) {
                if (!cls->method_names[i]) continue;
                LLVMValueRef slot =
                    LLVMBuildGEP2(builder, marr_ty, marr,
                        (LLVMValueRef[]){
                            LLVMConstInt(cg->i32, 0, 0),
                            LLVMConstInt(cg->i32, i, 0)
                        }, 2, "");

                LLVMBuildStore(builder,
                    LLVMBuildBitCast(builder,
                        cg_get_string_global(cg, cls->method_names[i]),
                        cg->i8_ptr, ""),
                    slot);
            }

            methods_ptr = LLVMBuildBitCast(
                builder,
                LLVMBuildGEP2(builder, marr_ty, marr,
                    (LLVMValueRef[]){
                        LLVMConstInt(cg->i32, 0, 0),
                        LLVMConstInt(cg->i32, 0, 0)
                    }, 2, ""),
                LLVMPointerType(cg->i8_ptr, 0),
                "");
        }

        LLVMValueRef args[] = {
            class_name,
            parent_name,
            LLVMConstInt(cg->i32, field_count, 0),
            fields_ptr,
            LLVMConstInt(cg->i32, cls->method_count, 0),
            methods_ptr
        };

        LLVMValueRef runtime_class =
            LLVMBuildCall2(builder, ty_class_create,
                           fn_class_create, args, 6, "");

        LLVMBuildCall2(builder, ty_register,
                       fn_register,
                       (LLVMValueRef[]){runtime_class}, 1, "");

        if (cls->constructor) {
            char ctor_name_buf[256];
            snprintf(ctor_name_buf, sizeof(ctor_name_buf), "%s_init", cls->name);
            LLVMValueRef ctor_fn = LLVMGetNamedFunction(mod, ctor_name_buf);
            if (ctor_fn) {
                LLVMValueRef ctor_ptr = LLVMBuildBitCast(builder, ctor_fn, cg->i8_ptr, "");
                LLVMBuildCall2(builder, ty_set_compiled_ctor, fn_set_compiled_ctor,
                               (LLVMValueRef[]){runtime_class, ctor_ptr}, 2, "");
            }
        }

        for (int i = 0; i < cls->method_count; i++) {
            if (!cls->method_names || !cls->method_names[i]) continue;
            if (strcmp(cls->method_names[i], "init") == 0) continue;

            char method_fn_buf[256];
            snprintf(method_fn_buf, sizeof(method_fn_buf), "%s_%s", cls->name, cls->method_names[i]);
            LLVMValueRef method_fn = LLVMGetNamedFunction(mod, method_fn_buf);
            if (!method_fn) continue;

            LLVMValueRef method_name_ptr =
                LLVMBuildBitCast(builder, cg_get_string_global(cg, cls->method_names[i]), cg->i8_ptr, "");
            LLVMValueRef method_ptr = LLVMBuildBitCast(builder, method_fn, cg->i8_ptr, "");
            LLVMBuildCall2(builder, ty_set_compiled_method_by_name, fn_set_compiled_method_by_name,
                           (LLVMValueRef[]){runtime_class, method_name_ptr, method_ptr}, 3, "");
        }
    }

    LLVMBuildCall2(builder, ty_resolve_inheritance, fn_resolve_inheritance, NULL, 0, "");

    LLVMBuildRetVoid(builder);
    LLVMDisposeBuilder(builder);

    // inject call into main
    LLVMValueRef main_fn = LLVMGetNamedFunction(mod, "main");
    if (main_fn) {
        LLVMBasicBlockRef bb = LLVMGetEntryBasicBlock(main_fn);
        LLVMValueRef first = LLVMGetFirstInstruction(bb);
        if (first) {
            LLVMBuilderRef b = LLVMCreateBuilder();
            LLVMPositionBuilderBefore(b, first);
            LLVMBuildCall2(b, init_fn_ty, init_fn, NULL, 0, "");
            LLVMDisposeBuilder(b);
        }
    }

    return 1;
}
