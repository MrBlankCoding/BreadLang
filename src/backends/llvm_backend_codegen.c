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

void cg_init(Cg* cg, LLVMModuleRef mod, LLVMBuilderRef builder) {
    memset(cg, 0, sizeof(*cg));
    cg->mod = mod;
    cg->builder = builder;
    cg->i1 = LLVMInt1Type();
    cg->i8 = LLVMInt8Type();
    cg->i8_ptr = LLVMPointerType(cg->i8, 0);
    cg->i32 = LLVMInt32Type();
    cg->i64 = LLVMInt64Type();
    cg->f64 = LLVMDoubleType();
    cg->void_ty = LLVMVoidType();
    cg->loop_depth = 0;
    cg->tmp_counter = 0;
    cg->current_loop_end = NULL;
    cg->current_loop_continue = NULL;
    cg->current_loop_scope_base_depth_slot = NULL;
    cg->value_type = LLVMArrayType(cg->i8, sizeof(BreadValue));
    cg->value_ptr_type = LLVMPointerType(cg->value_type, 0);
    
    cg->global_scope = NULL;
    cg->scope_depth = 0;
    cg->had_error = 0;

    cg->ty_bread_value_size = LLVMFunctionType(cg->i64, NULL, 0, 0);
    cg->fn_bread_value_size = cg_declare_fn(cg, "bread_value_size", cg->ty_bread_value_size);
    cg->ty_value_set_nil = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_set_nil = cg_declare_fn(cg, "bread_value_set_nil", cg->ty_value_set_nil);
    cg->ty_value_set_bool = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i32}, 2, 0);
    cg->fn_value_set_bool = cg_declare_fn(cg, "bread_value_set_bool", cg->ty_value_set_bool);
    cg->ty_value_set_int = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i32}, 2, 0);
    cg->fn_value_set_int = cg_declare_fn(cg, "bread_value_set_int", cg->ty_value_set_int);
    cg->ty_value_set_double = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->f64}, 2, 0);
    cg->fn_value_set_double = cg_declare_fn(cg, "bread_value_set_double", cg->ty_value_set_double);
    cg->ty_value_set_string = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_value_set_string = cg_declare_fn(cg, "bread_value_set_string", cg->ty_value_set_string);
    cg->ty_value_set_array = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_value_set_array = cg_declare_fn(cg, "bread_value_set_array", cg->ty_value_set_array);
    cg->ty_value_set_dict = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_value_set_dict = cg_declare_fn(cg, "bread_value_set_dict", cg->ty_value_set_dict);
    cg->ty_value_copy = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_value_copy = cg_declare_fn(cg, "bread_value_copy", cg->ty_value_copy);
    cg->ty_value_release = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_release = cg_declare_fn(cg, "bread_value_release_value", cg->ty_value_release);
    cg->ty_print = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_print = cg_declare_fn(cg, "bread_print", cg->ty_print);
    cg->ty_is_truthy = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_is_truthy = cg_declare_fn(cg, "bread_is_truthy", cg->ty_is_truthy);
    cg->ty_unary_not = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_unary_not = cg_declare_fn(cg, "bread_unary_not", cg->ty_unary_not);
    cg->ty_binary_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8, cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 4, 0);
    cg->fn_binary_op = cg_declare_fn(cg, "bread_binary_op", cg->ty_binary_op);
    cg->ty_index_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0);
    cg->fn_index_op = cg_declare_fn(cg, "bread_index_op", cg->ty_index_op);
    cg->ty_index_set_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0);
    cg->fn_index_set_op = cg_declare_fn(cg, "bread_index_set_op", cg->ty_index_set_op);
    cg->ty_member_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr}, 4, 0);
    cg->fn_member_op = cg_declare_fn(cg, "bread_member_op", cg->ty_member_op);
    cg->ty_member_set_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0);
    cg->fn_member_set_op = cg_declare_fn(cg, "bread_member_set_op", cg->ty_member_set_op);
    cg->ty_method_call_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr, cg->i32, cg->i8_ptr}, 6, 0);
    cg->fn_method_call_op = cg_declare_fn(cg, "bread_method_call_op", cg->ty_method_call_op);
    cg->ty_dict_set_value = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i8_ptr}, 3, 0);
    cg->fn_dict_set_value = cg_declare_fn(cg, "bread_dict_set_value", cg->ty_dict_set_value);
    cg->ty_array_append_value = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_array_append_value = cg_declare_fn(cg, "bread_array_append_value", cg->ty_array_append_value);
    cg->ty_var_decl = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i32, cg->i8_ptr}, 4, 0);
    cg->fn_var_decl = cg_declare_fn(cg, "bread_var_decl", cg->ty_var_decl);
    cg->ty_var_decl_if_missing = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i32, cg->i8_ptr}, 4, 0);
    cg->fn_var_decl_if_missing = cg_declare_fn(cg, "bread_var_decl_if_missing", cg->ty_var_decl_if_missing);
    cg->ty_var_assign = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_var_assign = cg_declare_fn(cg, "bread_var_assign", cg->ty_var_assign);
    cg->ty_var_load = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_var_load = cg_declare_fn(cg, "bread_var_load", cg->ty_var_load);
    cg->ty_push_scope = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_push_scope = cg_declare_fn(cg, "bread_push_scope", cg->ty_push_scope);
    cg->ty_pop_scope = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_pop_scope = cg_declare_fn(cg, "bread_pop_scope", cg->ty_pop_scope);
    cg->ty_can_pop_scope = LLVMFunctionType(cg->i32, NULL, 0, 0);
    cg->fn_can_pop_scope = cg_declare_fn(cg, "bread_can_pop_scope", cg->ty_can_pop_scope);

    cg->ty_scope_depth = LLVMFunctionType(cg->i32, NULL, 0, 0);
    cg->fn_scope_depth = cg_declare_fn(cg, "bread_scope_depth", cg->ty_scope_depth);
    cg->ty_pop_to_scope_depth = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i32}, 1, 0);
    cg->fn_pop_to_scope_depth = cg_declare_fn(cg, "bread_pop_to_scope_depth", cg->ty_pop_to_scope_depth);

    cg->ty_bread_memory_init = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_memory_init = cg_declare_fn(cg, "bread_memory_init", cg->ty_bread_memory_init);
    cg->ty_bread_memory_cleanup = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_memory_cleanup = cg_declare_fn(cg, "bread_memory_cleanup", cg->ty_bread_memory_cleanup);

    cg->ty_bread_string_intern_init = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_string_intern_init = cg_declare_fn(cg, "bread_string_intern_init", cg->ty_bread_string_intern_init);
    cg->ty_bread_string_intern_cleanup = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_string_intern_cleanup = cg_declare_fn(cg, "bread_string_intern_cleanup", cg->ty_bread_string_intern_cleanup);

    cg->ty_bread_builtin_init = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_builtin_init = cg_declare_fn(cg, "bread_builtin_init", cg->ty_bread_builtin_init);
    cg->ty_bread_builtin_cleanup = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_builtin_cleanup = cg_declare_fn(cg, "bread_builtin_cleanup", cg->ty_bread_builtin_cleanup);

    cg->ty_bread_error_init = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_error_init = cg_declare_fn(cg, "bread_error_init", cg->ty_bread_error_init);
    cg->ty_bread_error_cleanup = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_bread_error_cleanup = cg_declare_fn(cg, "bread_error_cleanup", cg->ty_bread_error_cleanup);
    cg->ty_init_variables = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_init_variables = cg_declare_fn(cg, "init_variables", cg->ty_init_variables);
    cg->ty_cleanup_variables = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_cleanup_variables = cg_declare_fn(cg, "cleanup_variables", cg->ty_cleanup_variables);
    cg->ty_init_functions = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_init_functions = cg_declare_fn(cg, "init_functions", cg->ty_init_functions);
    cg->ty_cleanup_functions = LLVMFunctionType(cg->void_ty, NULL, 0, 0);
    cg->fn_cleanup_functions = cg_declare_fn(cg, "cleanup_functions", cg->ty_cleanup_functions);
    cg->ty_array_new = LLVMFunctionType(cg->i8_ptr, NULL, 0, 0);
    cg->fn_array_new = cg_declare_fn(cg, "bread_array_new", cg->ty_array_new);
    cg->ty_array_release = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_array_release = cg_declare_fn(cg, "bread_array_release", cg->ty_array_release);
    cg->ty_dict_new = LLVMFunctionType(cg->i8_ptr, NULL, 0, 0);
    cg->fn_dict_new = cg_declare_fn(cg, "bread_dict_new", cg->ty_dict_new);
    cg->ty_dict_release = LLVMFunctionType(cg->void_ty, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_dict_release = cg_declare_fn(cg, "bread_dict_release", cg->ty_dict_release);
    cg->ty_dict_keys = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_dict_keys = cg_declare_fn(cg, "bread_value_dict_keys_as_value", cg->ty_dict_keys);
    
    //string
    cg->ty_string_create = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0);
    cg->fn_string_create = cg_declare_fn(cg, "bread_string_create", cg->ty_string_create);
    cg->ty_string_concat = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_string_concat = cg_declare_fn(cg, "bread_string_concat", cg->ty_string_concat);
    cg->ty_string_get_char = LLVMFunctionType(cg->i8, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0);
    cg->fn_string_get_char = cg_declare_fn(cg, "bread_string_get_char", cg->ty_string_get_char);
    
    // array
    cg->ty_array_create = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i32, cg->i64}, 2, 0);
    cg->fn_array_create = cg_declare_fn(cg, "bread_array_create", cg->ty_array_create);
    cg->ty_array_get = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i8_ptr}, 3, 0);
    cg->fn_array_get = cg_declare_fn(cg, "bread_value_array_get", cg->ty_array_get);
    cg->ty_array_set = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i64, cg->i8_ptr}, 3, 0);
    cg->fn_array_set = cg_declare_fn(cg, "bread_array_set", cg->ty_array_set);
    cg->ty_array_length = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_array_length = cg_declare_fn(cg, "bread_value_array_length", cg->ty_array_length);
    
    // range
    cg->ty_range_create = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i32, cg->i32, cg->i32}, 3, 0);
    cg->fn_range_create = cg_declare_fn(cg, "bread_range_create", cg->ty_range_create);
    cg->ty_range_simple = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i32}, 1, 0);
    cg->fn_range_simple = cg_declare_fn(cg, "bread_range", cg->ty_range_simple);
    
    // value
    cg->ty_value_get_int = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_get_int = cg_declare_fn(cg, "bread_value_get_int", cg->ty_value_get_int);
    
    cg->ty_value_get_double = LLVMFunctionType(cg->f64, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_get_double = cg_declare_fn(cg, "bread_value_get_double", cg->ty_value_get_double);
    
    cg->ty_value_get_bool = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_get_bool = cg_declare_fn(cg, "bread_value_get_bool", cg->ty_value_get_bool);
    
    // boxing in the ring
    cg->ty_bread_box_int = LLVMFunctionType(cg->value_type, (LLVMTypeRef[]){cg->i32}, 1, 0);
    cg->fn_bread_box_int = cg_declare_fn(cg, "bread_box_int", cg->ty_bread_box_int);
    
    cg->ty_bread_box_double = LLVMFunctionType(cg->value_type, (LLVMTypeRef[]){cg->f64}, 1, 0);
    cg->fn_bread_box_double = cg_declare_fn(cg, "bread_box_double", cg->ty_bread_box_double);
    
    cg->ty_bread_box_bool = LLVMFunctionType(cg->value_type, (LLVMTypeRef[]){cg->i32}, 1, 0);
    cg->fn_bread_box_bool = cg_declare_fn(cg, "bread_box_bool", cg->ty_bread_box_bool);
    
    cg->ty_bread_unbox_int = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_bread_unbox_int = cg_declare_fn(cg, "bread_unbox_int", cg->ty_bread_unbox_int);
    
    cg->ty_bread_unbox_double = LLVMFunctionType(cg->f64, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_bread_unbox_double = cg_declare_fn(cg, "bread_unbox_double", cg->ty_bread_unbox_double);
    
    cg->ty_bread_unbox_bool = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_bread_unbox_bool = cg_declare_fn(cg, "bread_unbox_bool", cg->ty_bread_unbox_bool);
    
    cg_define_functions(cg);
}

int bread_llvm_build_module_from_program(const ASTStmtList* program, LLVMModuleRef* out_mod, Cg* out_cg) {
    LLVMModuleRef mod;
    LLVMBuilderRef builder;
    Cg cg;
    LLVMTypeRef main_ty;
    LLVMValueRef main_fn;
    LLVMBasicBlockRef entry;
    LLVMValueRef val_size;
    
    if (!program || !out_mod) return 0;

    mod = LLVMModuleCreateWithName("bread_module");
    builder = LLVMCreateBuilder();

    cg_init(&cg, mod, builder);
    
    // Integrated semantic analysis - replaces separate semantic analysis pass
    if (!cg_semantic_analyze(&cg, (ASTStmtList*)program)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Semantic analysis failed");
        }
        printf("DEBUG: Semantic analysis failed\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    // Should prob improve this someday
    if (!type_stability_analyze((ASTStmtList*)program)) {
        // Type stability analysis failed - continue anyway
    }
    
    if (!escape_analysis_run((ASTStmtList*)program)) {
        // Escape analysis failed - continue anyway
    }
    
    if (!optimization_analyze((ASTStmtList*)program)) {
        // Optimization analysis failed - continue anyway
    }

    main_ty = LLVMFunctionType(cg.i32, NULL, 0, 0);
    main_fn = LLVMAddFunction(mod, "main", main_ty);
    entry = LLVMAppendBasicBlock(main_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_init_variables, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_init_functions, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_memory_init, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_string_intern_init, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_builtin_init, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_error_init, NULL, 0, "");

    val_size = cg_value_size(&cg);

    if (!cg_build_stmt_list(&cg, NULL, val_size, (ASTStmtList*)program)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Code generation failed");
        }
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_error_cleanup, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_builtin_cleanup, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_string_intern_cleanup, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_bread_memory_cleanup, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_cleanup_functions, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_cleanup_variables, NULL, 0, "");

    LLVMBasicBlockRef final_block = LLVMGetInsertBlock(builder);
    if (LLVMGetBasicBlockTerminator(final_block) == NULL) {
        LLVMBuildRet(builder, LLVMConstInt(cg.i32, 0, 0));
    }

    LLVMBasicBlockRef main_block = LLVMGetInsertBlock(builder);

    // function bodies
    if (!bread_llvm_generate_function_bodies(&cg, builder, val_size)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }
    
    // class bodies
    if (!bread_llvm_generate_class_methods(&cg, builder, val_size)) {
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

int bread_llvm_generate_function_bodies(Cg* cg, LLVMBuilderRef builder, LLVMValueRef val_size) {
    for (CgFunction* f = cg->functions; f; f = f->next) {
        if (f->body && LLVMCountBasicBlocks(f->fn) == 0) {
            LLVMBasicBlockRef fn_entry = LLVMAppendBasicBlock(f->fn, "entry");
            LLVMPositionBuilderAtEnd(builder, fn_entry);

            f->ret_slot = LLVMGetParam(f->fn, 0);

            // Record base runtime scope depth and push a new scope for this function
            LLVMValueRef base_depth = LLVMBuildCall2(builder, cg->ty_scope_depth, cg->fn_scope_depth, NULL, 0, "");
            f->runtime_scope_base_depth_slot = LLVMBuildAlloca(builder, cg->i32, "fn.scope.base");
            LLVMBuildStore(builder, base_depth, f->runtime_scope_base_depth_slot);
            (void)LLVMBuildCall2(builder, cg->ty_push_scope, cg->fn_push_scope, NULL, 0, "");
            for (int i = 0; i < f->param_count; i++) {
                LLVMValueRef param_val = LLVMGetParam(f->fn, (unsigned)(i + 1));
                LLVMValueRef alloca = cg_alloc_value(cg, f->param_names[i]);
                CgVar* param_var = cg_scope_add_var(f->scope, f->param_names[i], alloca);
                
                // Set the type descriptor for the parameter
                if (param_var && f->param_type_descs && f->param_type_descs[i]) {
                    param_var->type_desc = type_descriptor_clone(f->param_type_descs[i]);
                    param_var->type = f->param_type_descs[i]->base_type;
                }

                LLVMValueRef copy_args[] = {
                    LLVMBuildBitCast(builder, param_val, cg->i8_ptr, ""),
                    LLVMBuildBitCast(builder, alloca, cg->i8_ptr, "")
                };
                (void)LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
                
                // Also register the parameter in the runtime scope
                LLVMValueRef name_str = cg_get_string_global(cg, f->param_names[i]);
                LLVMValueRef name_ptr = LLVMBuildBitCast(builder, name_str, cg->i8_ptr, "");
                LLVMValueRef decl_type = LLVMConstInt(cg->i32, TYPE_NIL, 0); // Type will be determined at runtime
                LLVMValueRef decl_const = LLVMConstInt(cg->i32, 0, 0);
                LLVMValueRef decl_args[] = {name_ptr, decl_type, decl_const, cg_value_to_i8_ptr(cg, alloca)};
                (void)LLVMBuildCall2(builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");
            }

            if (!cg_build_stmt_list(cg, f, val_size, f->body)) {
                return 0;
            }
            
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                // Strict cleanup: pop back to base depth before returning
                LLVMValueRef loaded_base = base_depth;
                if (f->runtime_scope_base_depth_slot) {
                    loaded_base = LLVMBuildLoad2(builder, cg->i32, f->runtime_scope_base_depth_slot, "");
                }
                LLVMValueRef pop_args[] = { loaded_base };
                (void)LLVMBuildCall2(builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
                LLVMBuildRetVoid(builder);
            }
        }
    }
    return 1;
}

int bread_llvm_generate_class_methods(Cg* cg, LLVMBuilderRef builder, LLVMValueRef val_size) {
    for (CgClass* c = cg->classes; c; c = c->next) {
        if (c->constructor && c->constructor_function) {
            LLVMValueRef constructor_fn = c->constructor_function;
            
            if (constructor_fn && LLVMCountBasicBlocks(constructor_fn) == 0) {
                LLVMBasicBlockRef constructor_entry = LLVMAppendBasicBlock(constructor_fn, "entry");
                LLVMPositionBuilderAtEnd(builder, constructor_entry);
                
                CgFunction temp_fn;
                temp_fn.ret_slot = LLVMGetParam(constructor_fn, 0);
                temp_fn.scope = cg_scope_new(NULL);
                
                LLVMValueRef self_param = LLVMGetParam(constructor_fn, 1);
                LLVMValueRef self_alloca = cg_alloc_value(cg, "self");
                cg_scope_add_var(temp_fn.scope, "self", self_alloca);
                
                LLVMValueRef self_copy_args[] = {
                    LLVMBuildBitCast(builder, self_param, cg->i8_ptr, ""),
                    LLVMBuildBitCast(builder, self_alloca, cg->i8_ptr, "")
                };
                (void)LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, self_copy_args, 2, "");
                
                for (int i = 0; i < c->constructor->param_count; i++) {
                    LLVMValueRef param_val = LLVMGetParam(constructor_fn, (unsigned)(i + 2)); // +2 for ret_slot and self
                    LLVMValueRef alloca = cg_alloc_value(cg, c->constructor->param_names[i]);
                    cg_scope_add_var(temp_fn.scope, c->constructor->param_names[i], alloca);
                    
                    LLVMValueRef copy_args[] = {
                        LLVMBuildBitCast(builder, param_val, cg->i8_ptr, ""),
                        LLVMBuildBitCast(builder, alloca, cg->i8_ptr, "")
                    };
                    (void)LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
                }
                
                if (!cg_build_stmt_list(cg, &temp_fn, val_size, c->constructor->body)) {
                    return 0;
                }
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                    LLVMBuildRetVoid(builder);
                }
            }
        }
        
        for (int i = 0; i < c->method_count; i++) {
            ASTStmtFuncDecl* method = c->methods[i];
            if (!method || strcmp(method->name, "init") == 0) continue; // Skip constructor
            
            LLVMValueRef method_fn = c->method_functions[i];
            if (method_fn && LLVMCountBasicBlocks(method_fn) == 0) {
                LLVMBasicBlockRef method_entry = LLVMAppendBasicBlock(method_fn, "entry");
                LLVMPositionBuilderAtEnd(builder, method_entry);
                
                CgFunction temp_fn;
                temp_fn.ret_slot = LLVMGetParam(method_fn, 0);
                temp_fn.scope = cg_scope_new(NULL);
                
                LLVMValueRef self_param = LLVMGetParam(method_fn, 1);
                LLVMValueRef self_alloca = cg_alloc_value(cg, "self");
                cg_scope_add_var(temp_fn.scope, "self", self_alloca);
                
                LLVMValueRef self_copy_args[] = {
                    LLVMBuildBitCast(builder, self_param, cg->i8_ptr, ""),
                    LLVMBuildBitCast(builder, self_alloca, cg->i8_ptr, "")
                };
                (void)LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, self_copy_args, 2, "");
                
                for (int j = 0; j < method->param_count; j++) {
                    LLVMValueRef param_val = LLVMGetParam(method_fn, (unsigned)(j + 2)); // +2 for ret_slot and self
                    LLVMValueRef alloca = cg_alloc_value(cg, method->param_names[j]);
                    cg_scope_add_var(temp_fn.scope, method->param_names[j], alloca);
                    
                    LLVMValueRef copy_args[] = {
                        LLVMBuildBitCast(builder, param_val, cg->i8_ptr, ""),
                        LLVMBuildBitCast(builder, alloca, cg->i8_ptr, "")
                    };
                    (void)LLVMBuildCall2(builder, cg->ty_value_copy, cg->fn_value_copy, copy_args, 2, "");
                }
                
                if (!cg_build_stmt_list(cg, &temp_fn, val_size, method->body)) {
                    return 0;
                }
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                    LLVMBuildRetVoid(builder);
                }
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
    if (err) LLVMDisposeMessage(err);
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
        LLVMCodeModelDefault);

    LLVMDisposeMessage(triple);
    return tm;
}