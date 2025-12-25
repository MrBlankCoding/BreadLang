#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/llvm_backend_jit.h"
#include "backends/llvm_backend_codegen.h"
#include "runtime/error.h"
#include "runtime/runtime.h"
#include "codegen/codegen_runtime_bridge.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

#include "core/function.h"
#include "core/type_descriptor.h"
#include "codegen/codegen.h"
#include "../codegen/codegen_internal.h"

static void jit_map_symbol(LLVMExecutionEngineRef engine, LLVMModuleRef mod, 
                           const char* name, void* addr) {
    if (!engine || !mod || !name || !addr) {
        return;
    }
    
    LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
    if (!fn) {
        return;
    }
    
    LLVMAddGlobalMapping(engine, fn, addr);
}

static void jit_map_runtime_functions(LLVMExecutionEngineRef engine, LLVMModuleRef mod) {
    jit_map_symbol(engine, mod, "bread_var_decl", (void*)&bread_var_decl);
    jit_map_symbol(engine, mod, "bread_var_decl_if_missing", (void*)&bread_var_decl_if_missing);
    jit_map_symbol(engine, mod, "bread_var_assign", (void*)&bread_var_assign);
    jit_map_symbol(engine, mod, "bread_var_load", (void*)&bread_var_load);
    jit_map_symbol(engine, mod, "bread_push_scope", (void*)&bread_push_scope);
    jit_map_symbol(engine, mod, "bread_pop_scope", (void*)&bread_pop_scope);
    jit_map_symbol(engine, mod, "bread_can_pop_scope", (void*)&bread_can_pop_scope);
    jit_map_symbol(engine, mod, "bread_scope_depth", (void*)&bread_scope_depth);
    jit_map_symbol(engine, mod, "bread_pop_to_scope_depth", (void*)&bread_pop_to_scope_depth);
}

static void ensure_llvm_initialized(void) {
    static int initialized = 0;
    if (!initialized) {
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        initialized = 1;
    }
}

static LLVMExecutionEngineRef create_execution_engine(LLVMModuleRef mod) {
    LLVMExecutionEngineRef engine;
    char* err = NULL;
    
    if (LLVMCreateExecutionEngineForModule(&engine, mod, &err) != 0) {
        fprintf(stderr, "Error creating execution engine: %s\n", err);
        LLVMDisposeMessage(err);
        return NULL;
    }
    
    return engine;
}

int bread_llvm_jit_exec(const ASTStmtList* program) {
    if (!program) {
        fprintf(stderr, "Error: NULL program provided to JIT executor\n");
        return 1;
    }
    
    if (bread_error_has_compilation_errors()) {
        return 1;
    }

    ensure_llvm_initialized();
    LLVMModuleRef mod = NULL;
    Cg cg;
    memset(&cg, 0, sizeof(cg));
    
    if (!bread_llvm_build_module_from_program(program, &mod, &cg)) {
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        return 1;
    }
    
    if (!bread_llvm_verify_module(mod)) {
        LLVMDisposeModule(mod);
        return 1;
    }

    LLVMExecutionEngineRef engine = create_execution_engine(mod);
    if (!engine) {
        LLVMDisposeModule(mod);
        return 1;
    }

    jit_map_runtime_functions(engine, mod);
    cg_set_jit_module(mod, engine);
    
    if (!cg_connect_all_classes_to_runtime(&cg)) {
        fprintf(stderr, "Warning: Failed to connect classes to runtime\n");
    }

    uint64_t main_ptr = LLVMGetFunctionAddress(engine, "main");
    if (!main_ptr) {
        fprintf(stderr, "Error: Could not find 'main' function\n");
        LLVMDisposeExecutionEngine(engine);
        return 1;
    }

    int (*main_jit)(void) = (int (*)(void))main_ptr;
    int result = main_jit();

    LLVMDisposeExecutionEngine(engine);
    return result;
}

static LLVMTypeRef* create_param_types(Cg* cg, int param_count) {
    int param_total = param_count + 1;
    LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * (size_t)param_total);
    
    if (!param_types) {
        return NULL;
    }
    
    param_types[0] = cg->value_ptr_type;
    for (int i = 0; i < param_count; i++) {
        param_types[i + 1] = cg->value_ptr_type;
    }
    
    return param_types;
}

static CgFunction* declare_function(Cg* cg, const Function* fn, int is_target) {
    CgFunction* cg_fn = (CgFunction*)calloc(1, sizeof(CgFunction));
    if (!cg_fn) {
        return NULL;
    }
    
    cg_fn->name = strdup(fn->name);
    if (!cg_fn->name) {
        free(cg_fn);
        return NULL;
    }
    
    cg_fn->body = is_target ? (ASTStmtList*)fn->body : NULL;
    cg_fn->param_count = fn->param_count;
    cg_fn->param_names = fn->param_names;
    cg_fn->scope = NULL;
    cg_fn->ret_slot = NULL;
    cg_fn->next = cg->functions;
    cg->functions = cg_fn;

    LLVMTypeRef* param_types = create_param_types(cg, fn->param_count);
    if (!param_types) {
        free(cg_fn->name);
        free(cg_fn);
        return NULL;
    }
    
    int param_total = fn->param_count + 1;
    LLVMTypeRef fn_type = LLVMFunctionType(cg->void_ty, param_types, (unsigned)param_total, 0);
    free(param_types);

    cg_fn->type = fn_type;
    cg_fn->fn = LLVMAddFunction(cg->mod, fn->name, fn_type);
    
    return cg_fn;
}

static LLVMValueRef generate_wrapper(Cg* cg, LLVMBuilderRef builder, 
                                     CgFunction* target_fn, LLVMTypeRef fn_type,
                                     int param_count) {
    // void wrapper(BreadValue* ret, BreadValue** args)
    LLVMTypeRef wrapper_params[] = { cg->i8_ptr, LLVMPointerType(cg->i8_ptr, 0) };
    LLVMTypeRef wrapper_ty = LLVMFunctionType(cg->void_ty, wrapper_params, 2, 0);
    LLVMValueRef wrapper_fn = LLVMAddFunction(cg->mod, "jit_wrapper", wrapper_ty);
    
    LLVMBasicBlockRef wrapper_entry = LLVMAppendBasicBlock(wrapper_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, wrapper_entry);

    LLVMValueRef ret_arg = LLVMGetParam(wrapper_fn, 0);
    LLVMValueRef args_arg = LLVMGetParam(wrapper_fn, 1);

    int param_total = param_count + 1;
    LLVMValueRef* call_args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * (size_t)param_total);
    if (!call_args) {
        return NULL;
    }
    
    call_args[0] = LLVMBuildBitCast(builder, ret_arg, cg->value_ptr_type, "ret_cast");
    
    for (int i = 0; i < param_count; i++) {
        LLVMValueRef idx = LLVMConstInt(cg->i32, (unsigned long long)i, 0);
        LLVMValueRef ptr_to_arg_ptr = LLVMBuildGEP2(builder, cg->i8_ptr, args_arg, &idx, 1, "arg_ptr_ptr");
        LLVMValueRef arg_ptr = LLVMBuildLoad2(builder, cg->i8_ptr, ptr_to_arg_ptr, "arg_ptr");
        call_args[i + 1] = LLVMBuildBitCast(builder, arg_ptr, cg->value_ptr_type, "arg_cast");
    }

    LLVMBuildCall2(builder, fn_type, target_fn->fn, call_args, (unsigned)param_total, "");
    free(call_args);
    LLVMBuildRetVoid(builder);
    
    return wrapper_fn;
}

static int setup_function_parameters(Cg* cg, LLVMBuilderRef builder, CgFunction* target_fn) {
    LLVMValueRef val_size = cg_value_size(cg);
    (void)val_size;

    for (int i = 0; i < target_fn->param_count; i++) {
        LLVMValueRef param_val = LLVMGetParam(target_fn->fn, (unsigned)(i + 1));
        LLVMValueRef alloca = cg_alloc_value(cg, target_fn->param_names[i]);
        CgVar* param_var = cg_scope_add_var(target_fn->scope, target_fn->param_names[i], alloca);
        
        if (param_var && target_fn->param_type_descs && target_fn->param_type_descs[i]) {
            param_var->type_desc = type_descriptor_clone(target_fn->param_type_descs[i]);
            param_var->type = target_fn->param_type_descs[i]->base_type;
        }

        LLVMValueRef copy_args[] = {
            LLVMBuildBitCast(builder, param_val, cg->i8_ptr, "param_src"),
            LLVMBuildBitCast(builder, alloca, cg->i8_ptr, "param_dst")
        };
        (void)LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
    }

    return 1;
}

static void ensure_function_terminator(Cg* cg, LLVMBuilderRef builder, CgFunction* target_fn) {
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) != NULL) {
        return;
    }
    
    LLVMValueRef loaded_base = LLVMBuildLoad2(builder, cg->i32, 
                                               target_fn->runtime_scope_base_depth_slot, 
                                               "base_depth");
    
    LLVMValueRef min_depth = LLVMConstInt(cg->i32, 1, 0);
    LLVMValueRef safe_depth = LLVMBuildSelect(builder, 
        LLVMBuildICmp(builder, LLVMIntSGE, loaded_base, min_depth, "depth_check"), 
        loaded_base, min_depth, "safe_depth");
    
    LLVMValueRef pop_args[] = { safe_depth };
    (void)LLVMBuildCall2(builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, 
                         pop_args, 1, "");
    LLVMBuildRetVoid(builder);
}

int bread_llvm_jit_function(Function* fn) {
    if (!fn || !fn->body) {
        fprintf(stderr, "Error: Invalid function provided to JIT compiler\n");
        return 0;
    }

    ensure_llvm_initialized();
    LLVMModuleRef mod = LLVMModuleCreateWithName("jit_function_module");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    Cg cg;
    cg_init(&cg, mod, builder);

    int fn_count = get_function_count();
    for (int i = 0; i < fn_count; i++) {
        const Function* other_fn = get_function_at(i);
        if (!other_fn || strcmp(other_fn->name, fn->name) == 0) {
            continue;
        }

        if (!declare_function(&cg, other_fn, 0)) {
            fprintf(stderr, "Error: Failed to declare function '%s'\n", other_fn->name);
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(mod);
            return 0;
        }
    }

    CgFunction* target_fn = declare_function(&cg, fn, 1);
    if (!target_fn) {
        fprintf(stderr, "Error: Failed to declare target function '%s'\n", fn->name);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    LLVMValueRef wrapper_fn = generate_wrapper(&cg, builder, target_fn, 
                                                target_fn->type, fn->param_count);
    if (!wrapper_fn) {
        fprintf(stderr, "Error: Failed to generate wrapper function\n");
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }
    
    LLVMBasicBlockRef fn_entry = LLVMAppendBasicBlock(target_fn->fn, "entry");
    LLVMPositionBuilderAtEnd(builder, fn_entry);

    target_fn->ret_slot = LLVMGetParam(target_fn->fn, 0);
    target_fn->scope = cg_scope_new(NULL);

    LLVMValueRef base_depth = LLVMBuildCall2(builder, cg.ty_scope_depth, 
                                             cg.fn_scope_depth, NULL, 0, "scope_depth");
    target_fn->runtime_scope_base_depth_slot = LLVMBuildAlloca(builder, cg.i32, 
                                                                "scope_base_depth");
    LLVMBuildStore(builder, base_depth, target_fn->runtime_scope_base_depth_slot);
    (void)LLVMBuildCall2(builder, cg.ty_push_scope, cg.fn_push_scope, NULL, 0, "");
    if (!setup_function_parameters(&cg, builder, target_fn)) {
        fprintf(stderr, "Error: Failed to setup parameters for function '%s'\n", fn->name);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    LLVMValueRef val_size = cg_value_size(&cg);
    if (!cg_build_stmt_list(&cg, target_fn, val_size, target_fn->body)) {
        fprintf(stderr, "Error: Failed to generate body for function '%s'\n", fn->name);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }
    
    ensure_function_terminator(&cg, builder, target_fn);
    if (!bread_llvm_verify_module(mod)) {
        fprintf(stderr, "Error: Module verification failed\n");
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    LLVMDisposeBuilder(builder);
    LLVMExecutionEngineRef engine = create_execution_engine(mod);
    if (!engine) {
        return 0;
    }

    uint64_t wrapper_ptr = LLVMGetFunctionAddress(engine, "jit_wrapper");
    if (!wrapper_ptr) {
        fprintf(stderr, "Error: Could not find 'jit_wrapper' function\n");
        LLVMDisposeExecutionEngine(engine);
        return 0;
    }

    fn->jit_engine = engine;
    fn->jit_fn = (void (*)(void*, void**))wrapper_ptr;
    fn->is_jitted = 1;

    printf("Successfully JIT compiled function '%s'\n", fn->name);

    return 1;
}