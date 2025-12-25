#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/llvm_backend_codegen.h"
#include "runtime/error.h"
#include "runtime/runtime.h"
#include "compiler/analysis/type_stability.h"
#include "compiler/analysis/escape_analysis.h"
#include "compiler/optimization/optimization.h"
#include "codegen/optimized_codegen.h"
#include "codegen/codegen_runtime_bridge.h"
#include "core/type_descriptor.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Support.h>

#include "core/var.h"
#include "core/function.h"
#include "codegen/codegen.h"
#include "../codegen/codegen_internal.h"

typedef struct {
    const char* name;
    LLVMTypeRef* type_ref;
    LLVMValueRef* fn_ref;
    LLVMTypeRef return_type;
    LLVMTypeRef* param_types;
    unsigned param_count;
    int variadic;
} FunctionDecl;

static void cg_init_basic_types(Cg* cg) {
    cg->i1 = LLVMInt1Type();
    cg->i8 = LLVMInt8Type();
    cg->i8_ptr = LLVMPointerType(cg->i8, 0);
    cg->i32 = LLVMInt32Type();
    cg->i64 = LLVMInt64Type();
    cg->f64 = LLVMDoubleType();
    cg->void_ty = LLVMVoidType();
    cg->value_type = LLVMArrayType(cg->i8, sizeof(BreadValue));
    cg->value_ptr_type = LLVMPointerType(cg->value_type, 0);
}

static void cg_init_state(Cg* cg) {
    cg->loop_depth = 0;
    cg->tmp_counter = 0;
    cg->current_loop_end = NULL;
    cg->current_loop_continue = NULL;
    cg->current_loop_scope_base_depth_slot = NULL;
    cg->global_scope = NULL;
    cg->scope_depth = 0;
    cg->had_error = 0;
}

static void cg_declare_runtime_functions(Cg* cg) {
    FunctionDecl funcs[] = {
        {"bread_value_size", &cg->ty_bread_value_size, &cg->fn_bread_value_size, cg->i64, NULL, 0, 0},
        {"bread_value_set_nil", &cg->ty_value_set_nil, &cg->fn_value_set_nil, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_value_set_bool", &cg->ty_value_set_bool, &cg->fn_value_set_bool, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i32}, 2, 0},
        {"bread_value_set_int", &cg->ty_value_set_int, &cg->fn_value_set_int, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0},
        {"bread_value_set_double", &cg->ty_value_set_double, &cg->fn_value_set_double, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->f64}, 2, 0},
        {"bread_value_set_string", &cg->ty_value_set_string, &cg->fn_value_set_string, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_value_set_array", &cg->ty_value_set_array, &cg->fn_value_set_array, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_value_set_dict", &cg->ty_value_set_dict, &cg->fn_value_set_dict, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_value_copy", &cg->ty_value_copy, &cg->fn_value_copy, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_value_release_value", &cg->ty_value_release, &cg->fn_value_release, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_print", &cg->ty_print, &cg->fn_print, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_is_truthy", &cg->ty_is_truthy, &cg->fn_is_truthy, cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_unary_not", &cg->ty_unary_not, &cg->fn_unary_not, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_binary_op", &cg->ty_binary_op, &cg->fn_binary_op, cg->i32, (LLVMTypeRef[]){cg->i8, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 4, 0},
        {"bread_index_op", &cg->ty_index_op, &cg->fn_index_op, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0},
        {"bread_index_set_op", &cg->ty_index_set_op, &cg->fn_index_set_op, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0},
        {"bread_member_op", &cg->ty_member_op, &cg->fn_member_op, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr}, 4, 0},
        {"bread_member_set_op", &cg->ty_member_set_op, &cg->fn_member_set_op, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0},
        {"bread_method_call_op", &cg->ty_method_call_op, &cg->fn_method_call_op, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr, cg->i32, cg->i8_ptr}, 6, 0},
        {"bread_dict_set_value", &cg->ty_dict_set_value, &cg->fn_dict_set_value, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0},
        {"bread_array_append_value", &cg->ty_array_append_value, &cg->fn_array_append_value, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_var_decl", &cg->ty_var_decl, &cg->fn_var_decl, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i32, cg->i8_ptr}, 4, 0},
        {"bread_var_decl_if_missing", &cg->ty_var_decl_if_missing, &cg->fn_var_decl_if_missing, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i32, cg->i8_ptr}, 4, 0},
        {"bread_var_assign", &cg->ty_var_assign, &cg->fn_var_assign, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_var_load", &cg->ty_var_load, &cg->fn_var_load, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_push_scope", &cg->ty_push_scope, &cg->fn_push_scope, cg->void_ty, NULL, 0, 0},
        {"bread_pop_scope", &cg->ty_pop_scope, &cg->fn_pop_scope, cg->void_ty, NULL, 0, 0},
        {"bread_can_pop_scope", &cg->ty_can_pop_scope, &cg->fn_can_pop_scope, cg->i32, NULL, 0, 0},
        {"bread_scope_depth", &cg->ty_scope_depth, &cg->fn_scope_depth, cg->i32, NULL, 0, 0},
        {"bread_pop_to_scope_depth", &cg->ty_pop_to_scope_depth, &cg->fn_pop_to_scope_depth, cg->void_ty, (LLVMTypeRef[]){cg->i32}, 1, 0},
        {"bread_memory_init", &cg->ty_bread_memory_init, &cg->fn_bread_memory_init, cg->void_ty, NULL, 0, 0},
        {"bread_memory_cleanup", &cg->ty_bread_memory_cleanup, &cg->fn_bread_memory_cleanup, cg->void_ty, NULL, 0, 0},
        {"bread_string_intern_init", &cg->ty_bread_string_intern_init, &cg->fn_bread_string_intern_init, cg->void_ty, NULL, 0, 0},
        {"bread_string_intern_cleanup", &cg->ty_bread_string_intern_cleanup, &cg->fn_bread_string_intern_cleanup, cg->void_ty, NULL, 0, 0},
        {"bread_builtin_init", &cg->ty_bread_builtin_init, &cg->fn_bread_builtin_init, cg->void_ty, NULL, 0, 0},
        {"bread_builtin_cleanup", &cg->ty_bread_builtin_cleanup, &cg->fn_bread_builtin_cleanup, cg->void_ty, NULL, 0, 0},
        {"bread_error_init", &cg->ty_bread_error_init, &cg->fn_bread_error_init, cg->void_ty, NULL, 0, 0},
        {"bread_error_cleanup", &cg->ty_bread_error_cleanup, &cg->fn_bread_error_cleanup, cg->void_ty, NULL, 0, 0},
        {"init_variables", &cg->ty_init_variables, &cg->fn_init_variables, cg->void_ty, NULL, 0, 0},
        {"cleanup_variables", &cg->ty_cleanup_variables, &cg->fn_cleanup_variables, cg->void_ty, NULL, 0, 0},
        {"init_functions", &cg->ty_init_functions, &cg->fn_init_functions, cg->void_ty, NULL, 0, 0},
        {"cleanup_functions", &cg->ty_cleanup_functions, &cg->fn_cleanup_functions, cg->void_ty, NULL, 0, 0},
        {"bread_array_new", &cg->ty_array_new, &cg->fn_array_new, cg->i8_ptr, NULL, 0, 0},
        {"bread_array_release", &cg->ty_array_release, &cg->fn_array_release, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_dict_new", &cg->ty_dict_new, &cg->fn_dict_new, cg->i8_ptr, NULL, 0, 0},
        {"bread_dict_release", &cg->ty_dict_release, &cg->fn_dict_release, cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_value_dict_keys_as_value", &cg->ty_dict_keys, &cg->fn_dict_keys, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_string_create", &cg->ty_string_create, &cg->fn_string_create, cg->i8_ptr, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0},
        {"bread_string_concat", &cg->ty_string_concat, &cg->fn_string_concat, cg->i8_ptr, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0},
        {"bread_string_get_char", &cg->ty_string_get_char, &cg->fn_string_get_char, cg->i8, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0},
        {"bread_array_create", &cg->ty_array_create, &cg->fn_array_create, cg->i8_ptr, (LLVMTypeRef[]){cg->i32, cg->i64}, 2, 0},
        {"bread_value_array_get", &cg->ty_array_get, &cg->fn_array_get, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i8_ptr}, 3, 0},
        {"bread_array_set", &cg->ty_array_set, &cg->fn_array_set, cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i64, cg->i8_ptr}, 3, 0},
        {"bread_value_array_length", &cg->ty_array_length, &cg->fn_array_length, cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_range_create", &cg->ty_range_create, &cg->fn_range_create, cg->i8_ptr, (LLVMTypeRef[]){cg->i64, cg->i64, cg->i64}, 3, 0},
        {"bread_range", &cg->ty_range_simple, &cg->fn_range_simple, cg->i8_ptr, (LLVMTypeRef[]){cg->i64}, 1, 0},
        {"bread_value_get_int", &cg->ty_value_get_int, &cg->fn_value_get_int, cg->i64, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_value_get_double", &cg->ty_value_get_double, &cg->fn_value_get_double, cg->f64, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_value_get_bool", &cg->ty_value_get_bool, &cg->fn_value_get_bool, cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_box_int", &cg->ty_bread_box_int, &cg->fn_bread_box_int, cg->value_type, (LLVMTypeRef[]){cg->i32}, 1, 0},
        {"bread_box_double", &cg->ty_bread_box_double, &cg->fn_bread_box_double, cg->value_type, (LLVMTypeRef[]){cg->f64}, 1, 0},
        {"bread_box_bool", &cg->ty_bread_box_bool, &cg->fn_bread_box_bool, cg->value_type, (LLVMTypeRef[]){cg->i32}, 1, 0},
        {"bread_unbox_int", &cg->ty_bread_unbox_int, &cg->fn_bread_unbox_int, cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_unbox_double", &cg->ty_bread_unbox_double, &cg->fn_bread_unbox_double, cg->f64, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
        {"bread_unbox_bool", &cg->ty_bread_unbox_bool, &cg->fn_bread_unbox_bool, cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0},
    };

    for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++) {
        FunctionDecl* decl = &funcs[i];
        *decl->type_ref = LLVMFunctionType(decl->return_type, decl->param_types, decl->param_count, decl->variadic);
        *decl->fn_ref = cg_declare_fn(cg, decl->name, *decl->type_ref);
    }
}

void cg_init(Cg* cg, LLVMModuleRef mod, LLVMBuilderRef builder) {
    memset(cg, 0, sizeof(*cg));
    cg->mod = mod;
    cg->builder = builder;
    
    cg_init_basic_types(cg);
    cg_init_state(cg);
    cg_declare_runtime_functions(cg);
    cg_define_functions(cg);
}

static int run_semantic_analysis(Cg* cg, ASTStmtList* program) {
    if (!cg_semantic_analyze(cg, program)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Semantic analysis failed");
        }
        printf("DEBUG: Semantic analysis failed\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        return 0;
    }
    return 1;
}

static void run_optional_analyses(ASTStmtList* program) {
    type_stability_analyze(program);
    escape_analysis_run(program);
    optimization_analyze(program);
}

static void emit_runtime_init_calls(Cg* cg, LLVMBuilderRef builder) {
    struct {
        LLVMValueRef fn;
        LLVMTypeRef ty;
    } init_calls[] = {
        {cg->fn_init_variables, cg->ty_init_variables},
        {cg->fn_init_functions, cg->ty_init_functions},
        {cg->fn_bread_memory_init, cg->ty_bread_memory_init},
        {cg->fn_bread_string_intern_init, cg->ty_bread_string_intern_init},
        {cg->fn_bread_builtin_init, cg->ty_bread_builtin_init},
        {cg->fn_bread_error_init, cg->ty_bread_error_init}
    };

    for (size_t i = 0; i < sizeof(init_calls) / sizeof(init_calls[0]); i++) {
        LLVMBuildCall2(builder, init_calls[i].ty, init_calls[i].fn, NULL, 0, "");
    }
}

static void emit_runtime_cleanup_calls(Cg* cg, LLVMBuilderRef builder) {
    struct {
        LLVMValueRef fn;
        LLVMTypeRef ty;
    } cleanup_calls[] = {
        {cg->fn_bread_error_cleanup, cg->ty_bread_error_cleanup},
        {cg->fn_bread_builtin_cleanup, cg->ty_bread_builtin_cleanup},
        {cg->fn_bread_string_intern_cleanup, cg->ty_bread_string_intern_cleanup},
        {cg->fn_bread_memory_cleanup, cg->ty_bread_memory_cleanup},
        {cg->fn_cleanup_functions, cg->ty_cleanup_functions},
        {cg->fn_cleanup_variables, cg->ty_cleanup_variables}
    };

    for (size_t i = 0; i < sizeof(cleanup_calls) / sizeof(cleanup_calls[0]); i++) {
        LLVMBuildCall2(builder, cleanup_calls[i].ty, cleanup_calls[i].fn, NULL, 0, "");
    }
}

static void ensure_function_return(LLVMBuilderRef builder, LLVMTypeRef i32_type) {
    LLVMBasicBlockRef final_block = LLVMGetInsertBlock(builder);
    if (!LLVMGetBasicBlockTerminator(final_block)) {
        LLVMBuildRet(builder, LLVMConstInt(i32_type, 0, 0));
    }
}

int bread_llvm_build_module_from_program(const ASTStmtList* program, LLVMModuleRef* out_mod, Cg* out_cg) {
    if (!program || !out_mod) return 0;

    LLVMModuleRef mod = LLVMModuleCreateWithName("bread_module");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    Cg cg;

    cg_init(&cg, mod, builder);
    
    if (!run_semantic_analysis(&cg, (ASTStmtList*)program)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    run_optional_analyses((ASTStmtList*)program);

    LLVMTypeRef main_ty = LLVMFunctionType(cg.i32, NULL, 0, 0);
    LLVMValueRef main_fn = LLVMAddFunction(mod, "main", main_ty);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    emit_runtime_init_calls(&cg, builder);

    LLVMValueRef val_size = cg_value_size(&cg);

    if (!cg_build_stmt_list(&cg, NULL, val_size, (ASTStmtList*)program)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Code generation failed");
        }
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    emit_runtime_cleanup_calls(&cg, builder);
    ensure_function_return(builder, cg.i32);

    LLVMBasicBlockRef main_block = LLVMGetInsertBlock(builder);

    if (!bread_llvm_generate_function_bodies(&cg, builder, val_size) ||
        !bread_llvm_generate_class_methods(&cg, builder, val_size)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }
    
    LLVMPositionBuilderAtEnd(builder, main_block);
    LLVMDisposeBuilder(builder);
    
    *out_mod = mod;
    if (out_cg) {
        *out_cg = cg;
    }
    return 1;
}

static void setup_function_parameters(Cg* cg, CgFunction* f, LLVMBuilderRef builder) {
    for (int i = 0; i < f->param_count; i++) {
        LLVMValueRef param_val = LLVMGetParam(f->fn, (unsigned)(i + 1));
        LLVMValueRef alloca = cg_alloc_value(cg, f->param_names[i]);
        CgVar* param_var = cg_scope_add_var(f->scope, f->param_names[i], alloca);
        
        if (param_var && f->param_type_descs && f->param_type_descs[i]) {
            param_var->type_desc = type_descriptor_clone(f->param_type_descs[i]);
            param_var->type = f->param_type_descs[i]->base_type;
        }

        LLVMValueRef copy_args[] = {
            LLVMBuildBitCast(builder, param_val, cg->i8_ptr, ""),
            LLVMBuildBitCast(builder, alloca, cg->i8_ptr, "")
        };
        LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
        
        LLVMValueRef name_str = cg_get_string_global(cg, f->param_names[i]);
        LLVMValueRef name_ptr = LLVMBuildBitCast(builder, name_str, cg->i8_ptr, "");
        LLVMValueRef decl_args[] = {
            name_ptr,
            LLVMConstInt(cg->i32, TYPE_NIL, 0),
            LLVMConstInt(cg->i32, 0, 0),
            cg_value_to_i8_ptr(cg, alloca)
        };
        LLVMBuildCall2(builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");
    }
}

static void setup_function_scope(Cg* cg, CgFunction* f, LLVMBuilderRef builder) {
    LLVMValueRef base_depth = LLVMBuildCall2(builder, cg->ty_scope_depth, cg->fn_scope_depth, NULL, 0, "");
    f->runtime_scope_base_depth_slot = LLVMBuildAlloca(builder, cg->i32, "fn.scope.base");
    LLVMBuildStore(builder, base_depth, f->runtime_scope_base_depth_slot);
    LLVMBuildCall2(builder, cg->ty_push_scope, cg->fn_push_scope, NULL, 0, "");
}

static void cleanup_function_scope(Cg* cg, CgFunction* f, LLVMBuilderRef builder) {
    LLVMValueRef loaded_base = LLVMBuildLoad2(builder, cg->i32, f->runtime_scope_base_depth_slot, "");
    LLVMValueRef min_depth = LLVMConstInt(cg->i32, 1, 0);
    LLVMValueRef safe_depth = LLVMBuildSelect(builder, 
        LLVMBuildICmp(builder, LLVMIntSGE, loaded_base, min_depth, ""), 
        loaded_base, min_depth, "safe_depth");
    LLVMValueRef pop_args[] = {safe_depth};
    LLVMBuildCall2(builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
}

int bread_llvm_generate_function_bodies(Cg* cg, LLVMBuilderRef builder, LLVMValueRef val_size) {
    for (CgFunction* f = cg->functions; f; f = f->next) {
        if (!f->body || LLVMCountBasicBlocks(f->fn) != 0) continue;

        LLVMBasicBlockRef fn_entry = LLVMAppendBasicBlock(f->fn, "entry");
        LLVMPositionBuilderAtEnd(builder, fn_entry);

        f->ret_slot = LLVMGetParam(f->fn, 0);
        setup_function_scope(cg, f, builder);
        setup_function_parameters(cg, f, builder);

        if (!cg_build_stmt_list(cg, f, val_size, f->body)) {
            return 0;
        }
        
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
            cleanup_function_scope(cg, f, builder);
            LLVMBuildRetVoid(builder);
        }
    }
    return 1;
}

static void copy_parameter_value(Cg* cg, LLVMBuilderRef builder, LLVMValueRef param, LLVMValueRef alloca) {
    LLVMValueRef copy_args[] = {
        LLVMBuildBitCast(builder, param, cg->i8_ptr, ""),
        LLVMBuildBitCast(builder, alloca, cg->i8_ptr, "")
    };
    LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
}

static void setup_method_self_parameter(Cg* cg, LLVMBuilderRef builder, LLVMValueRef self_param, CgScope* scope) {
    LLVMValueRef self_alloca = cg_alloc_value(cg, "self");
    cg_scope_add_var(scope, "self", self_alloca);
    copy_parameter_value(cg, builder, self_param, self_alloca);
}

static void setup_method_parameters(Cg* cg, LLVMBuilderRef builder, LLVMValueRef method_fn, 
                                    char* const* param_names, int param_count, CgScope* scope) {
    for (int i = 0; i < param_count; i++) {
        LLVMValueRef param_val = LLVMGetParam(method_fn, (unsigned)(i + 2));
        LLVMValueRef alloca = cg_alloc_value(cg, param_names[i]);
        cg_scope_add_var(scope, param_names[i], alloca);
        copy_parameter_value(cg, builder, param_val, alloca);
    }
}

static int generate_method_body(Cg* cg, LLVMBuilderRef builder, LLVMValueRef method_fn,
                                ASTStmtFuncDecl* method, LLVMValueRef val_size) {
    if (!method || LLVMCountBasicBlocks(method_fn) != 0) {
        return 1;
    }

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(method_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);
    
    CgFunction temp_fn = {0};
    temp_fn.ret_slot = LLVMGetParam(method_fn, 0);
    temp_fn.scope = cg_scope_new(NULL);
    
    LLVMValueRef self_param = LLVMGetParam(method_fn, 1);
    setup_method_self_parameter(cg, builder, self_param, temp_fn.scope);
    setup_method_parameters(cg, builder, method_fn, method->param_names, method->param_count, temp_fn.scope);
    
    if (!cg_build_stmt_list(cg, &temp_fn, val_size, method->body)) {
        return 0;
    }
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
        LLVMBuildRetVoid(builder);
    }
    
    return 1;
}

int bread_llvm_generate_class_methods(Cg* cg, LLVMBuilderRef builder, LLVMValueRef val_size) {
    for (CgClass* c = cg->classes; c; c = c->next) {
        if (c->constructor && c->constructor_function) {
            if (!generate_method_body(cg, builder, c->constructor_function, c->constructor, val_size)) {
                return 0;
            }
        }
        
        for (int i = 0; i < c->method_count; i++) {
            ASTStmtFuncDecl* method = c->methods[i];
            if (!method || strcmp(method->name, "init") == 0) continue;
            
            LLVMValueRef method_fn = c->method_functions[i];
            if (!generate_method_body(cg, builder, method_fn, method, val_size)) {
                return 0;
            }
        }
    }
    return 1;
}

int bread_llvm_verify_module(LLVMModuleRef mod) {
    if (!mod) return 0;
    
    char* err = NULL;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &err)) {
        if (err) {
            printf("Error: LLVM module verification failed: %s\n", err);
            LLVMDisposeMessage(err);
        }
        return 0;
    }
    
    if (err) {
        LLVMDisposeMessage(err);
    }
    return 1;
}

LLVMTargetMachineRef bread_llvm_create_native_target_machine(void) {
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    char* triple = LLVMGetDefaultTargetTriple();
    if (!triple) return NULL;

    LLVMTargetRef target;
    char* err = NULL;
    
    if (LLVMGetTargetFromTriple(triple, &target, &err) != 0) {
        if (err) {
            printf("Error: LLVMGetTargetFromTriple failed: %s\n", err);
            LLVMDisposeMessage(err);
        }
        LLVMDisposeMessage(triple);
        return NULL;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target,
        triple,
        "",
        "",
        LLVMCodeGenLevelNone,
        LLVMRelocDefault,
        LLVMCodeModelDefault
    );

    LLVMDisposeMessage(triple);
    return tm;
}