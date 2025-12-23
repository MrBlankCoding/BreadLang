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

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;
    
    // comp err check
    if (bread_error_has_compilation_errors()) {
        return 0;
    }

    LLVMModuleRef mod = NULL;
    if (!bread_llvm_build_module_from_program(program, &mod, NULL)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Failed to build LLVM module from program");
        }
        return 0;
    }
    
    if (!bread_llvm_verify_module(mod)) {
        BREAD_ERROR_SET_COMPILE_ERROR("LLVM module verification failed");
        LLVMDisposeModule(mod);
        return 0;
    }

    char* ir_text = LLVMPrintModuleToString(mod);
    int ok = write_text_file(out_path, ir_text ? ir_text : "");
    if (!ok) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not write LLVM IR to '%s'", out_path);
        BREAD_ERROR_SET_COMPILE_ERROR(error_msg);
    }
    if (ir_text) LLVMDisposeMessage(ir_text);
    LLVMDisposeModule(mod);
    return ok;
}

int bread_llvm_emit_obj(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;
    
    if (bread_error_has_compilation_errors()) {
        return 0;
    }

    LLVMModuleRef mod = NULL;
    Cg cg;  // cg context
    if (!bread_llvm_build_module_from_program(program, &mod, &cg)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Failed to build LLVM module from program");
        }
        return 0;
    }

    if (!bread_llvm_generate_class_runtime_init(&cg, mod)) {
        printf("Warning: Failed to generate class runtime initialization\n");
    }

    LLVMTargetMachineRef tm = bread_llvm_create_native_target_machine();
    if (!tm) {
        LLVMDisposeModule(mod);
        return 0;
    }

    char* triple = LLVMGetDefaultTargetTriple();
    if (!triple) {
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeModule(mod);
        return 0;
    }

    LLVMSetTarget(mod, triple);
    LLVMTargetDataRef data = LLVMCreateTargetDataLayout(tm);
    char* layout = LLVMCopyStringRepOfTargetData(data);
    if (layout) {
        LLVMSetDataLayout(mod, layout);
        LLVMDisposeMessage(layout);
    }
    LLVMDisposeTargetData(data);
    LLVMDisposeMessage(triple);

    if (!bread_llvm_verify_module(mod)) {
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeModule(mod);
        return 0;
    }

    char* err = NULL;
    if (LLVMTargetMachineEmitToFile(tm, mod, (char*)out_path, LLVMObjectFile, &err) != 0) {
        if (err) {
            printf("Error: could not emit object file: %s\n", err);
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
    if (!cg->classes) return 1; // no classes. Fun project you got there
    
    // runtime for registering classes
    LLVMTypeRef init_fn_type = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    LLVMValueRef init_fn = LLVMAddFunction(mod, "bread_runtime_init_classes", init_fn_type);
    
    LLVMBasicBlockRef init_bb = LLVMAppendBasicBlock(init_fn, "entry");
    LLVMBuilderRef init_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(init_builder, init_bb);
    
    for (CgClass* cg_class = cg->classes; cg_class; cg_class = cg_class->next) {
        LLVMValueRef class_name_str = cg_get_string_global(cg, cg_class->name);
        LLVMValueRef class_name_ptr = LLVMBuildBitCast(init_builder, class_name_str, cg->i8_ptr, "");
        
        LLVMValueRef parent_name_ptr = LLVMConstNull(cg->i8_ptr);
        if (cg_class->parent_name) {
            LLVMValueRef parent_name_str = cg_get_string_global(cg, cg_class->parent_name);
            parent_name_ptr = LLVMBuildBitCast(init_builder, parent_name_str, cg->i8_ptr, "");
        }
        
        // make sure to grab all the fields
        char** all_field_names;
        int total_field_count;
        
        if (cg_collect_all_fields(cg, cg_class, &all_field_names, &total_field_count)) {
            // array with the names
            LLVMTypeRef i8_ptr_ptr = LLVMPointerType(cg->i8_ptr, 0);
            LLVMValueRef field_names_ptr = LLVMConstNull(i8_ptr_ptr);
            
            if (total_field_count > 0) {
                LLVMTypeRef field_names_arr_ty = LLVMArrayType(cg->i8_ptr, (unsigned)total_field_count);
                LLVMValueRef field_names_arr = LLVMBuildAlloca(init_builder, field_names_arr_ty, "field_names");
                
                for (int i = 0; i < total_field_count; i++) {
                    LLVMValueRef field_name_str = cg_get_string_global(cg, all_field_names[i]);
                    LLVMValueRef field_name_ptr = LLVMBuildBitCast(init_builder, field_name_str, cg->i8_ptr, "");
                    LLVMValueRef field_slot = LLVMBuildGEP2(
                        init_builder,
                        field_names_arr_ty,
                        field_names_arr,
                        (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, i, 0)},
                        2,
                        "field_name_slot");
                    LLVMBuildStore(init_builder, field_name_ptr, field_slot);
                }

                LLVMValueRef first = LLVMBuildGEP2(
                    init_builder,
                    field_names_arr_ty,
                    field_names_arr,
                    (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, 0, 0)},
                    2,
                    "field_names_first");
                field_names_ptr = LLVMBuildBitCast(init_builder, first, i8_ptr_ptr, "");
            }
            
            LLVMValueRef method_names_ptr = LLVMConstNull(i8_ptr_ptr);
            if (cg_class->method_count > 0) {
                LLVMTypeRef method_names_arr_ty = LLVMArrayType(cg->i8_ptr, (unsigned)cg_class->method_count);
                LLVMValueRef method_names_arr = LLVMBuildAlloca(init_builder, method_names_arr_ty, "method_names");
                
                for (int i = 0; i < cg_class->method_count; i++) {
                    LLVMValueRef method_name_str = cg_get_string_global(cg, cg_class->method_names[i]);
                    LLVMValueRef method_name_ptr = LLVMBuildBitCast(init_builder, method_name_str, cg->i8_ptr, "");
                    LLVMValueRef method_slot = LLVMBuildGEP2(
                        init_builder,
                        method_names_arr_ty,
                        method_names_arr,
                        (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, i, 0)},
                        2,
                        "method_name_slot");
                    LLVMBuildStore(init_builder, method_name_ptr, method_slot);
                }

                LLVMValueRef first = LLVMBuildGEP2(
                    init_builder,
                    method_names_arr_ty,
                    method_names_arr,
                    (LLVMValueRef[]){LLVMConstInt(cg->i32, 0, 0), LLVMConstInt(cg->i32, 0, 0)},
                    2,
                    "method_names_first");
                method_names_ptr = LLVMBuildBitCast(init_builder, first, i8_ptr_ptr, "");
            }
            
            LLVMTypeRef ty_class_create = LLVMFunctionType(
                cg->i8_ptr,  // Returns BreadClass*
                (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, i8_ptr_ptr, cg->i32, i8_ptr_ptr},
                6,
                0
            );
            LLVMValueRef fn_class_create = cg_declare_fn(cg, "bread_class_create_instance", ty_class_create);
            
            LLVMValueRef create_args[] = {
                class_name_ptr,
                parent_name_ptr,
                LLVMConstInt(cg->i32, total_field_count, 0),
                field_names_ptr,
                LLVMConstInt(cg->i32, cg_class->method_count, 0),
                method_names_ptr
            };
            
            LLVMValueRef runtime_class = LLVMBuildCall2(init_builder, ty_class_create, fn_class_create, create_args, 6, "");
            LLVMTypeRef ty_register_class = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
            LLVMValueRef fn_register_class = cg_declare_fn(cg, "bread_class_register_definition", ty_register_class);
            LLVMBuildCall2(init_builder, ty_register_class, fn_register_class, (LLVMValueRef[]){runtime_class}, 1, "");
            
            // connect to runtime class
            if (cg_class->method_count > 0) {
                for (int i = 0; i < cg_class->method_count; i++) {
                    if (cg_class->method_functions && cg_class->method_functions[i]) {
                        LLVMValueRef method_fn = cg_class->method_functions[i];
                        LLVMValueRef method_ptr = LLVMBuildBitCast(init_builder, method_fn, cg->i8_ptr, "method_ptr");
                        LLVMTypeRef ty_set_method = LLVMFunctionType(
                            cg->void_ty,
                            (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i8_ptr},  // class*, method_index, function_ptr
                            3,
                            0
                        );
                        LLVMValueRef fn_set_method = cg_declare_fn(cg, "bread_class_set_compiled_method", ty_set_method);
                        
                        LLVMValueRef set_method_args[] = {
                            runtime_class,
                            LLVMConstInt(cg->i32, i, 0),
                            method_ptr
                        };
                        LLVMBuildCall2(init_builder, ty_set_method, fn_set_method, set_method_args, 3, "");
                        
                        printf("Connected method '%s::%s' to runtime\n", cg_class->name, cg_class->method_names[i]);
                    }
                }
            }

            if (cg_class->constructor_function) {
                LLVMValueRef constructor_ptr = LLVMBuildBitCast(init_builder, cg_class->constructor_function, cg->i8_ptr, "constructor_ptr");
                
                LLVMTypeRef ty_set_constructor = LLVMFunctionType(
                    cg->void_ty,
                    (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr},  // class*, function_ptr
                    2,
                    0
                );
                LLVMValueRef fn_set_constructor = cg_declare_fn(cg, "bread_class_set_compiled_constructor", ty_set_constructor);
                
                LLVMValueRef set_constructor_args[] = {
                    runtime_class,
                    constructor_ptr
                };
                LLVMBuildCall2(init_builder, ty_set_constructor, fn_set_constructor, set_constructor_args, 2, "");
                
                printf("Connected constructor for class '%s'\n", cg_class->name);
            }
            
            for (int i = 0; i < total_field_count; i++) {
                free(all_field_names[i]);
            }
            free(all_field_names);
            
            printf("Generated class registration code for '%s'\n", cg_class->name);
        }
    }
    
    // could be buggy
    LLVMTypeRef ty_resolve_inheritance = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    LLVMValueRef fn_resolve_inheritance = cg_declare_fn(cg, "bread_class_resolve_inheritance", ty_resolve_inheritance);
    LLVMBuildCall2(init_builder, ty_resolve_inheritance, fn_resolve_inheritance, NULL, 0, "");
    
    LLVMBuildRetVoid(init_builder);
    LLVMDisposeBuilder(init_builder);
    
    // modify main to call init
    LLVMValueRef main_fn = LLVMGetNamedFunction(mod, "main");
    if (main_fn) {
        LLVMBasicBlockRef main_entry = LLVMGetEntryBasicBlock(main_fn);
        if (main_entry) {
            LLVMValueRef first_instr = LLVMGetFirstInstruction(main_entry);
            if (first_instr) {
                LLVMBuilderRef main_builder = LLVMCreateBuilder();
                LLVMPositionBuilderBefore(main_builder, first_instr);
                LLVMBuildCall2(main_builder, init_fn_type, init_fn, NULL, 0, "");
                
                LLVMDisposeBuilder(main_builder);
            }
        }
    }
    
    printf("Generated runtime class initialization for executable mode\n");
    return 1;
}