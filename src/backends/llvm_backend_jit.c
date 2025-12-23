#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/llvm_backend_jit.h"
#include "backends/llvm_backend_codegen.h"
#include "runtime/error.h"
#include "codegen/codegen_runtime_bridge.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

#include "core/function.h"
#include "codegen/codegen.h"
#include "../codegen/codegen_internal.h"

int bread_llvm_jit_exec(const ASTStmtList* program) {
    if (!program) return 1;
    
    // Check for compilation errors before proceeding
    if (bread_error_has_compilation_errors()) {
        return 1;
    }

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    LLVMModuleRef mod = NULL;
    Cg cg;
    if (!bread_llvm_build_module_from_program(program, &mod, &cg)) return 1;
    if (!bread_llvm_verify_module(mod)) {
        LLVMDisposeModule(mod);
        return 1;
    }

    LLVMExecutionEngineRef engine;
    char* err = NULL;
    if (LLVMCreateExecutionEngineForModule(&engine, mod, &err) != 0) {
        fprintf(stderr, "Error creating execution engine: %s\n", err);
        LLVMDisposeMessage(err);
        return 1;
    }

    // Set the JIT module and engine for the runtime bridge
    cg_set_jit_module(mod, engine);
    
    // Now connect all classes to runtime with JIT engine available
    if (!cg_connect_all_classes_to_runtime(&cg)) {
        fprintf(stderr, "Warning: Failed to connect classes to runtime, using AST execution only\n");
    }

    uint64_t main_ptr = LLVMGetFunctionAddress(engine, "main");
    if (!main_ptr) {
        fprintf(stderr, "Error: Could not get function address for 'main'\n");
        LLVMDisposeExecutionEngine(engine);
        LLVMDisposeModule(mod);
        return 1;
    }

    int (*main_jit)() = (int (*)())main_ptr;
    int result = main_jit();

    LLVMDisposeExecutionEngine(engine);
    return result;
}

int bread_llvm_jit_function(Function* fn) {
    if (!fn || !fn->body) return 0;

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    LLVMModuleRef mod = LLVMModuleCreateWithName("jit_function_module");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    Cg cg;
    cg_init(&cg, mod, builder);

    // DECLARE ALL FUNCTIONS AT ONCE
    int fn_count = get_function_count();
    for (int i = 0; i < fn_count; i++) {
        const Function* other_fn = get_function_at(i);
        if (!other_fn) continue;

        // skip if compiling
        if (strcmp(other_fn->name, fn->name) == 0) continue;

        CgFunction* cg_fn = (CgFunction*)malloc(sizeof(CgFunction));
        cg_fn->name = strdup(other_fn->name);
        cg_fn->body = NULL; // Declaration only
        cg_fn->param_count = other_fn->param_count;
        cg_fn->param_names = other_fn->param_names;
        cg_fn->scope = NULL;
        cg_fn->ret_slot = NULL;
        cg_fn->next = cg.functions;
        cg.functions = cg_fn;

        int param_total = other_fn->param_count + 1;
        LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * (size_t)param_total);
        param_types[0] = cg.value_ptr_type;
        for (int k = 0; k < other_fn->param_count; k++) {
            param_types[k+1] = cg.value_ptr_type;
        }
        LLVMTypeRef fn_type = LLVMFunctionType(cg.void_ty, param_types, (unsigned)param_total, 0);
        free(param_types);

        cg_fn->type = fn_type;
        cg_fn->fn = LLVMAddFunction(cg.mod, other_fn->name, fn_type);
    }

    CgFunction* target_fn = (CgFunction*)malloc(sizeof(CgFunction));
    target_fn->name = strdup(fn->name);
    target_fn->body = (ASTStmtList*)fn->body;
    target_fn->param_count = fn->param_count;
    target_fn->param_names = fn->param_names;
    target_fn->scope = NULL; // Will be created in loop below
    target_fn->ret_slot = NULL;
    target_fn->next = cg.functions;
    cg.functions = target_fn;

    int param_total = fn->param_count + 1;
    LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * (size_t)param_total);
    param_types[0] = cg.value_ptr_type;
    for (int k = 0; k < fn->param_count; k++) {
        param_types[k+1] = cg.value_ptr_type;
    }
    LLVMTypeRef fn_type = LLVMFunctionType(cg.void_ty, param_types, (unsigned)param_total, 0);
    free(param_types);
    target_fn->type = fn_type;
    target_fn->fn = LLVMAddFunction(cg.mod, fn->name, fn_type);

    // Generate Wrapper: void wrapper(BreadValue* ret, BreadValue** args)
    // args is BreadValue**, so it points to array of pointers to BreadValue.
    LLVMTypeRef wrapper_params[] = { cg.i8_ptr, LLVMPointerType(cg.i8_ptr, 0) };
    LLVMTypeRef wrapper_ty = LLVMFunctionType(cg.void_ty, wrapper_params, 2, 0);
    LLVMValueRef wrapper_fn = LLVMAddFunction(mod, "jit_wrapper", wrapper_ty);
    LLVMBasicBlockRef wrapper_entry = LLVMAppendBasicBlock(wrapper_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, wrapper_entry);

    LLVMValueRef ret_arg = LLVMGetParam(wrapper_fn, 0); // i8
    LLVMValueRef args_arg = LLVMGetParam(wrapper_fn, 1); // i8

    LLVMValueRef* call_args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * (size_t)param_total);
    call_args[0] = LLVMBuildBitCast(builder, ret_arg, cg.value_ptr_type, "");
    
    for (int i = 0; i < fn->param_count; i++) {
        LLVMValueRef idx = LLVMConstInt(cg.i32, i, 0);
        LLVMValueRef ptr_to_arg_ptr = LLVMBuildGEP2(builder, cg.i8_ptr, args_arg, &idx, 1, "");
        LLVMValueRef arg_ptr = LLVMBuildLoad2(builder, cg.i8_ptr, ptr_to_arg_ptr, "");
        call_args[i + 1] = LLVMBuildBitCast(builder, arg_ptr, cg.value_ptr_type, "");
    }

    LLVMBuildCall2(builder, fn_type, target_fn->fn, call_args, (unsigned)param_total, "");
    free(call_args);
    LLVMBuildRetVoid(builder);
    
    LLVMBasicBlockRef fn_entry = LLVMAppendBasicBlock(target_fn->fn, "entry");
    LLVMPositionBuilderAtEnd(builder, fn_entry);

    target_fn->ret_slot = LLVMGetParam(target_fn->fn, 0);
    target_fn->scope = (CgScope*)malloc(sizeof(CgScope));
    target_fn->scope->vars = NULL;
    target_fn->scope->parent = NULL;

    // Push a new scope when entering the JIT function
    (void)LLVMBuildCall2(builder, cg.ty_push_scope, cg.fn_push_scope, NULL, 0, "");

    LLVMValueRef val_size = cg_value_size(&cg);

    for (int i = 0; i < target_fn->param_count; i++) {
        LLVMValueRef param_val = LLVMGetParam(target_fn->fn, (unsigned)(i + 1));
        LLVMValueRef alloca = cg_alloc_value(&cg, target_fn->param_names[i]);
        cg_scope_add_var(target_fn->scope, target_fn->param_names[i], alloca);

        LLVMValueRef copy_args[] = {
            LLVMBuildBitCast(builder, param_val, cg.i8_ptr, ""),
            LLVMBuildBitCast(builder, alloca, cg.i8_ptr, "")
        };
        (void)LLVMBuildCall2(builder, cg.ty_value_copy, cg.fn_value_copy, copy_args, 2, "");
    }

    if (!cg_build_stmt_list(&cg, target_fn, val_size, target_fn->body)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }
    
    // ONLY IF THERE IS NO TERMINATOR, ADD A RETURN
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
        // Pop the scope before returning from the JIT function
        (void)LLVMBuildCall2(builder, cg.ty_pop_scope, cg.fn_pop_scope, NULL, 0, "");
        LLVMBuildRetVoid(builder);
    }

    if (!bread_llvm_verify_module(mod)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    LLVMDisposeBuilder(builder);

    // JIT Execution
    LLVMExecutionEngineRef engine;
    char* err = NULL;
    if (LLVMCreateExecutionEngineForModule(&engine, mod, &err) != 0) {
        fprintf(stderr, "Error creating execution engine: %s\n", err);
        LLVMDisposeMessage(err);
        return 0;
    }

    uint64_t wrapper_ptr = LLVMGetFunctionAddress(engine, "jit_wrapper");
    if (!wrapper_ptr) {
        fprintf(stderr, "Error: Could not get function address for 'jit_wrapper'\n");
        LLVMDisposeExecutionEngine(engine);
        // Owned by engine
        return 0;
    }

    fn->jit_engine = engine;
    fn->jit_fn = (void (*)(void*, void**))wrapper_ptr;
    fn->is_jitted = 1;

    printf("JIT compiled function '%s'\n", fn->name);

    return 1;
}