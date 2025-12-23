#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/llvm_backend.h"
#include "runtime/error.h"
#include "runtime/runtime.h"  // For BreadValue definition
#include "compiler/analysis/type_stability.h"
#include "compiler/analysis/escape_analysis.h"
#include "compiler/optimization/optimization.h"
#include "codegen/optimized_codegen.h"
#include "codegen/codegen_runtime_bridge.h"  // For class connection
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Support.h>

#include <limits.h>
#include <unistd.h>

#include "core/var.h"
#include "core/function.h"
#include "codegen/codegen.h"
#include "../codegen/codegen_internal.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#elif defined(__linux__)
#endif

static int g_use_compact_print = 0;

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

static int bread_is_project_root_dir(const char* dir) {
    if (!dir) return 0;
    char src_dir[PATH_MAX];
    char inc_dir[PATH_MAX];
    char marker[PATH_MAX];
    snprintf(src_dir, sizeof(src_dir), "%s/src", dir);
    snprintf(inc_dir, sizeof(inc_dir), "%s/include", dir);
    snprintf(marker, sizeof(marker), "%s/src/runtime/runtime.c", dir);
    if (access(src_dir, F_OK) != 0) return 0;
    if (access(inc_dir, F_OK) != 0) return 0;
    if (access(marker, R_OK) != 0) return 0;
    return 1;
}

static int bread_find_project_root_from_exe_dir(const char* exe_dir, char* out_root, size_t cap) {
    if (!exe_dir || !out_root || cap == 0) return 0;

    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    snprintf(candidate, sizeof(candidate), "%s/..", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    snprintf(candidate, sizeof(candidate), "%s/../..", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    snprintf(candidate, sizeof(candidate), "%s/../../..", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    return 0;
}

static int bread_get_exe_dir(char* out_dir, size_t cap) {
    if (!out_dir || cap == 0) return 0;

#if defined(__APPLE__)
    uint32_t size = (uint32_t)cap;
    if (_NSGetExecutablePath(out_dir, &size) != 0) {
        return 0;
    }
    out_dir[cap - 1] = '\0';
    char* d = dirname(out_dir);
    if (!d) return 0;
    if (d != out_dir) {
        size_t n = strlen(d);
        if (n + 1 > cap) return 0;
        memmove(out_dir, d, n + 1);
    }
    return 1;
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", out_dir, cap - 1);
    if (n <= 0) return 0;
    out_dir[n] = '\0';
    char* d = dirname(out_dir);
    if (!d) return 0;
    if (d != out_dir) {
        size_t dn = strlen(d);
        if (dn + 1 > cap) return 0;
        memmove(out_dir, d, dn + 1);
    }
    return 1;
#else
    (void)out_dir;
    (void)cap;
    return 0;
#endif
}

static void cg_init(Cg* cg, LLVMModuleRef mod, LLVMBuilderRef builder) {
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
    
    // Use actual BreadValue size instead of hardcoded 128 bytes
    cg->value_type = LLVMArrayType(cg->i8, sizeof(BreadValue));
    cg->value_ptr_type = LLVMPointerType(cg->value_type, 0);
    
    // Initialize semantic analysis fields
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
    cg->fn_print = cg_declare_fn(cg, g_use_compact_print ? "bread_print_compact" : "bread_print", cg->ty_print);
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
    
    // String operations
    cg->ty_string_create = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0);
    cg->fn_string_create = cg_declare_fn(cg, "bread_string_create", cg->ty_string_create);
    cg->ty_string_concat = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr}, 2, 0);
    cg->fn_string_concat = cg_declare_fn(cg, "bread_string_concat", cg->ty_string_concat);
    cg->ty_string_get_char = LLVMFunctionType(cg->i8, (LLVMTypeRef[]){cg->i8_ptr, cg->i64}, 2, 0);
    cg->fn_string_get_char = cg_declare_fn(cg, "bread_string_get_char", cg->ty_string_get_char);
    
    // Array operations
    cg->ty_array_create = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i32, cg->i64}, 2, 0);
    cg->fn_array_create = cg_declare_fn(cg, "bread_array_create", cg->ty_array_create);
    cg->ty_array_get = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i32, cg->i8_ptr}, 3, 0);
    cg->fn_array_get = cg_declare_fn(cg, "bread_value_array_get", cg->ty_array_get);
    cg->ty_array_set = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i64, cg->i8_ptr}, 3, 0);
    cg->fn_array_set = cg_declare_fn(cg, "bread_array_set", cg->ty_array_set);
    cg->ty_array_length = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_array_length = cg_declare_fn(cg, "bread_value_array_length", cg->ty_array_length);
    
    // Range operations
    cg->ty_range_create = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i32, cg->i32, cg->i32}, 3, 0);
    cg->fn_range_create = cg_declare_fn(cg, "bread_range_create", cg->ty_range_create);
    cg->ty_range_simple = LLVMFunctionType(cg->i8_ptr, (LLVMTypeRef[]){cg->i32}, 1, 0);
    cg->fn_range_simple = cg_declare_fn(cg, "bread_range", cg->ty_range_simple);
    
    // Value getter functions
    cg->ty_value_get_int = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_get_int = cg_declare_fn(cg, "bread_value_get_int", cg->ty_value_get_int);
    
    cg->ty_value_get_double = LLVMFunctionType(cg->f64, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_get_double = cg_declare_fn(cg, "bread_value_get_double", cg->ty_value_get_double);
    
    cg->ty_value_get_bool = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr}, 1, 0);
    cg->fn_value_get_bool = cg_declare_fn(cg, "bread_value_get_bool", cg->ty_value_get_bool);
    
    // Boxing/unboxing functions
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

static int bread_llvm_build_module_from_program(const ASTStmtList* program, LLVMModuleRef* out_mod, Cg* out_cg) {
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

    // Run Stage 5 optimization analysis passes
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

    for (CgFunction* f = cg.functions; f; f = f->next) {
        if (f->body && LLVMCountBasicBlocks(f->fn) == 0) {
            LLVMBasicBlockRef fn_entry = LLVMAppendBasicBlock(f->fn, "entry");
            LLVMPositionBuilderAtEnd(builder, fn_entry);

            f->ret_slot = LLVMGetParam(f->fn, 0);

            // Push a new scope when entering the function
            // (void)LLVMBuildCall2(builder, cg.ty_push_scope, cg.fn_push_scope, NULL, 0, "");

            for (int i = 0; i < f->param_count; i++) {
                LLVMValueRef param_val = LLVMGetParam(f->fn, (unsigned)(i + 1));
                LLVMValueRef alloca = LLVMBuildAlloca(builder, cg.value_type, f->param_names[i]);
                cg_scope_add_var(f->scope, f->param_names[i], alloca);

                LLVMValueRef copy_args[] = {
                    LLVMBuildBitCast(builder, param_val, cg.i8_ptr, ""),
                    LLVMBuildBitCast(builder, alloca, cg.i8_ptr, "")
                };
                (void)LLVMBuildCall2(builder, cg.ty_value_copy, cg.fn_value_copy, copy_args, 2, "");
            }

            if (!cg_build_stmt_list(&cg, f, val_size, f->body)) {
                LLVMDisposeBuilder(builder);
                LLVMDisposeModule(mod);
                return 0;
            }
            
            // Only pop scope and add return if there's no terminator (explicit return)
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                // Pop the scope before returning from the function
                // (void)LLVMBuildCall2(builder, cg.ty_pop_scope, cg.fn_pop_scope, NULL, 0, "");
                LLVMBuildRetVoid(builder);
            }
        }
    }
    
    // Generate method bodies for classes
    for (CgClass* c = cg.classes; c; c = c->next) {
        // Generate constructor body
        if (c->constructor && c->constructor_function) {
            LLVMValueRef constructor_fn = c->constructor_function;
            
            if (constructor_fn && LLVMCountBasicBlocks(constructor_fn) == 0) {
                LLVMBasicBlockRef constructor_entry = LLVMAppendBasicBlock(constructor_fn, "entry");
                LLVMPositionBuilderAtEnd(builder, constructor_entry);
                
                // Create a temporary function context for the constructor
                CgFunction temp_fn;
                temp_fn.ret_slot = LLVMGetParam(constructor_fn, 0);
                temp_fn.scope = cg_scope_new(NULL);
                
                // Add self parameter to scope
                LLVMValueRef self_param = LLVMGetParam(constructor_fn, 1);
                LLVMValueRef self_alloca = LLVMBuildAlloca(builder, cg.value_type, "self");
                cg_scope_add_var(temp_fn.scope, "self", self_alloca);
                
                LLVMValueRef self_copy_args[] = {
                    LLVMBuildBitCast(builder, self_param, cg.i8_ptr, ""),
                    LLVMBuildBitCast(builder, self_alloca, cg.i8_ptr, "")
                };
                (void)LLVMBuildCall2(builder, cg.ty_value_copy, cg.fn_value_copy, self_copy_args, 2, "");
                
                // Add constructor parameters to scope
                for (int i = 0; i < c->constructor->param_count; i++) {
                    LLVMValueRef param_val = LLVMGetParam(constructor_fn, (unsigned)(i + 2)); // +2 for ret_slot and self
                    LLVMValueRef alloca = LLVMBuildAlloca(builder, cg.value_type, c->constructor->param_names[i]);
                    cg_scope_add_var(temp_fn.scope, c->constructor->param_names[i], alloca);
                    
                    LLVMValueRef copy_args[] = {
                        LLVMBuildBitCast(builder, param_val, cg.i8_ptr, ""),
                        LLVMBuildBitCast(builder, alloca, cg.i8_ptr, "")
                    };
                    (void)LLVMBuildCall2(builder, cg.ty_value_copy, cg.fn_value_copy, copy_args, 2, "");
                }
                
                // Generate constructor body
                if (!cg_build_stmt_list(&cg, &temp_fn, val_size, c->constructor->body)) {
                    LLVMDisposeBuilder(builder);
                    LLVMDisposeModule(mod);
                    return 0;
                }
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                    LLVMBuildRetVoid(builder);
                }
            }
        }
        
        // Generate other method bodies
        for (int i = 0; i < c->method_count; i++) {
            ASTStmtFuncDecl* method = c->methods[i];
            if (!method || strcmp(method->name, "init") == 0) continue; // Skip constructor
            
            LLVMValueRef method_fn = c->method_functions[i];
            if (method_fn && LLVMCountBasicBlocks(method_fn) == 0) {
                LLVMBasicBlockRef method_entry = LLVMAppendBasicBlock(method_fn, "entry");
                LLVMPositionBuilderAtEnd(builder, method_entry);
                
                // Create a temporary function context for the method
                CgFunction temp_fn;
                temp_fn.ret_slot = LLVMGetParam(method_fn, 0);
                temp_fn.scope = cg_scope_new(NULL);
                
                // Add self parameter to scope
                LLVMValueRef self_param = LLVMGetParam(method_fn, 1);
                LLVMValueRef self_alloca = LLVMBuildAlloca(builder, cg.value_type, "self");
                cg_scope_add_var(temp_fn.scope, "self", self_alloca);
                
                LLVMValueRef self_copy_args[] = {
                    LLVMBuildBitCast(builder, self_param, cg.i8_ptr, ""),
                    LLVMBuildBitCast(builder, self_alloca, cg.i8_ptr, "")
                };
                (void)LLVMBuildCall2(builder, cg.ty_value_copy, cg.fn_value_copy, self_copy_args, 2, "");
                
                // Add method parameters to scope
                for (int j = 0; j < method->param_count; j++) {
                    LLVMValueRef param_val = LLVMGetParam(method_fn, (unsigned)(j + 2)); // +2 for ret_slot and self
                    LLVMValueRef alloca = LLVMBuildAlloca(builder, cg.value_type, method->param_names[j]);
                    cg_scope_add_var(temp_fn.scope, method->param_names[j], alloca);
                    
                    LLVMValueRef copy_args[] = {
                        LLVMBuildBitCast(builder, param_val, cg.i8_ptr, ""),
                        LLVMBuildBitCast(builder, alloca, cg.i8_ptr, "")
                    };
                    (void)LLVMBuildCall2(builder, cg.ty_value_copy, cg.fn_value_copy, copy_args, 2, "");
                }
                
                // Generate method body
                if (!cg_build_stmt_list(&cg, &temp_fn, val_size, method->body)) {
                    LLVMDisposeBuilder(builder);
                    LLVMDisposeModule(mod);
                    return 0;
                }
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                    LLVMBuildRetVoid(builder);
                }
            }
        }
    }
    
    LLVMPositionBuilderAtEnd(builder, main_block);
    
    // Note: Class connection to runtime moved to after execution engine creation
    // This ensures JIT function pointers are available when connecting classes

    LLVMDisposeBuilder(builder);
    *out_mod = mod;
    
    // Copy Cg context to output parameter
    if (out_cg) {
        *out_cg = cg;
    }
    return 1;
}

static int bread_llvm_verify_module(LLVMModuleRef mod) {
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

static LLVMTargetMachineRef bread_llvm_create_native_target_machine(void) {
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

int bread_llvm_emit_ll(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;
    
    // Check for compilation errors before proceeding
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
    if (!bread_llvm_build_module_from_program(program, &mod, NULL)) {
        if (!bread_error_has_error()) {
            BREAD_ERROR_SET_COMPILE_ERROR("Failed to build LLVM module from program");
        }
        return 0;
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

static int bread_llvm_link_executable_with_clang(const char* obj_path, const char* out_path) {
    if (!obj_path || !out_path) return 0;

    char exe_dir[PATH_MAX];
    memset(exe_dir, 0, sizeof(exe_dir));
    if (!bread_get_exe_dir(exe_dir, sizeof(exe_dir))) {
        printf("Error: could not determine BreadLang install directory for linking\n");
        return 0;
    }

    char root_dir[PATH_MAX];
    memset(root_dir, 0, sizeof(root_dir));
    if (!bread_find_project_root_from_exe_dir(exe_dir, root_dir, sizeof(root_dir))) {
        printf("Error: could not determine BreadLang project root directory for linking\n");
        return 0;
    }

    char rt_path[PATH_MAX];
    char memory_path[PATH_MAX];
    char print_path[PATH_MAX];
    char string_ops_path[PATH_MAX];
    char operators_path[PATH_MAX];
    char array_utils_path[PATH_MAX];
    char value_ops_path[PATH_MAX];
    char builtins_path[PATH_MAX];
    char error_path[PATH_MAX];
    char value_core_path[PATH_MAX];
    char value_array_path[PATH_MAX];
    char value_dict_path[PATH_MAX];
    char value_optional_path[PATH_MAX];
    char value_struct_path[PATH_MAX];
    char value_class_path[PATH_MAX];
    char var_path[PATH_MAX];
    char function_path[PATH_MAX];
    char type_descriptor_path[PATH_MAX];
    char ast_path[PATH_MAX];
    char expr_path[PATH_MAX];
    char expr_ops_path[PATH_MAX];
    char ast_memory_path[PATH_MAX];
    char ast_types_path[PATH_MAX];
    char ast_expr_parser_path[PATH_MAX];
    char ast_stmt_parser_path[PATH_MAX];
    char ast_dump_path[PATH_MAX];
    char inc_path[PATH_MAX];
    // Core runtime and utilities
    snprintf(rt_path, sizeof(rt_path), "%s/src/runtime/runtime.c", root_dir);
    snprintf(memory_path, sizeof(memory_path), "%s/src/runtime/memory.c", root_dir);
    snprintf(print_path, sizeof(print_path), "%s/src/runtime/print.c", root_dir);
    snprintf(string_ops_path, sizeof(string_ops_path), "%s/src/runtime/string_ops.c", root_dir);
    snprintf(operators_path, sizeof(operators_path), "%s/src/runtime/operators.c", root_dir);
    snprintf(array_utils_path, sizeof(array_utils_path), "%s/src/runtime/array_utils.c", root_dir);
    snprintf(value_ops_path, sizeof(value_ops_path), "%s/src/runtime/value_ops.c", root_dir);
    snprintf(builtins_path, sizeof(builtins_path), "%s/src/runtime/builtins.c", root_dir);
    
    // Error handling
    snprintf(error_path, sizeof(error_path), "%s/src/runtime/error.c", root_dir);
    
    // Core types and variables
    snprintf(value_core_path, sizeof(value_core_path), "%s/src/core/value_core.c", root_dir);
    snprintf(value_array_path, sizeof(value_array_path), "%s/src/core/value_array.c", root_dir);
    snprintf(value_dict_path, sizeof(value_dict_path), "%s/src/core/value_dict.c", root_dir);
    snprintf(value_optional_path, sizeof(value_optional_path), "%s/src/core/value_optional.c", root_dir);
    snprintf(value_struct_path, sizeof(value_struct_path), "%s/src/core/value_struct.c", root_dir);
    snprintf(value_class_path, sizeof(value_class_path), "%s/src/core/value_class.c", root_dir);
    snprintf(var_path, sizeof(var_path), "%s/src/core/var.c", root_dir);
    snprintf(function_path, sizeof(function_path), "%s/src/core/function.c", root_dir);
    snprintf(type_descriptor_path, sizeof(type_descriptor_path), "%s/src/core/type_descriptor.c", root_dir);
    
    // AST and parser
    snprintf(ast_path, sizeof(ast_path), "%s/src/compiler/ast/ast.c", root_dir);
    snprintf(expr_path, sizeof(expr_path), "%s/src/compiler/parser/expr.c", root_dir);
    snprintf(expr_ops_path, sizeof(expr_ops_path), "%s/src/compiler/parser/expr_ops.c", root_dir);
    
    snprintf(ast_memory_path, sizeof(ast_memory_path), "%s/src/compiler/ast/ast_memory.c", root_dir);
    snprintf(ast_types_path, sizeof(ast_types_path), "%s/src/compiler/ast/ast_types.c", root_dir);
    snprintf(ast_expr_parser_path, sizeof(ast_expr_parser_path), "%s/src/compiler/ast/ast_expr_parser.c", root_dir);
    snprintf(ast_stmt_parser_path, sizeof(ast_stmt_parser_path), "%s/src/compiler/ast/ast_stmt_parser.c", root_dir);
    
    // AST dump
    snprintf(ast_dump_path, sizeof(ast_dump_path), "%s/src/compiler/ast/ast_dump.c", root_dir);
    
    // Include path
    snprintf(inc_path, sizeof(inc_path), "%s/include", root_dir);

    // Calculate required buffer size for the command
    size_t cap = strlen(obj_path) + strlen(out_path) + 
                strlen(rt_path) + strlen(memory_path) + strlen(print_path) + 
                strlen(string_ops_path) + strlen(operators_path) + 
                strlen(array_utils_path) + strlen(value_ops_path) + 
                strlen(builtins_path) + strlen(error_path) + 
                strlen(value_core_path) + strlen(value_array_path) + strlen(value_dict_path) + 
                strlen(value_optional_path) + strlen(value_struct_path) + strlen(value_class_path) + 
                strlen(var_path) + 
                strlen(function_path) + strlen(type_descriptor_path) + strlen(ast_path) + 
                strlen(expr_path) + strlen(expr_ops_path) + 
                strlen(ast_memory_path) + strlen(ast_types_path) + 
                strlen(ast_expr_parser_path) + strlen(ast_stmt_parser_path) + 
                strlen(ast_dump_path) + strlen(inc_path) + 
                2048;  // Extra space for flags and formatting
    char* cmd = (char*)malloc(cap);
    if (!cmd) return 0;

    // Build the command with all required source files and flags
    snprintf(
        cmd,
        cap,
        "clang -std=c11 -I'%s' -o '%s' "
        "'%s' "  // obj_path
        "'%s' '%s' '%s' "  // rt_path, memory_path, print_path
        "'%s' '%s' '%s' "  // string_ops_path, operators_path, array_utils_path
        "'%s' '%s' "  // value_ops_path, builtins_path
        "'%s' '%s' '%s' "  // error_path, value_core_path, value_array_path
        "'%s' '%s' '%s' "  // value_dict_path, value_optional_path, value_struct_path
        "'%s' '%s' '%s' "  // value_class_path, var_path, function_path
        "'%s' '%s' '%s' "  // type_descriptor_path, ast_path, expr_path
        "'%s' '%s' '%s' "  // expr_ops_path, ast_memory_path, ast_types_path
        "'%s' '%s' '%s' "  // ast_expr_parser_path, ast_stmt_parser_path, ast_dump_path
        " -lm -fPIC -O2 -g",  // linker flags
        inc_path,          // Include path
        out_path,          // Output file
        obj_path,          // Input object file
        rt_path, memory_path, print_path,  // Runtime
        string_ops_path, operators_path, array_utils_path,  // Runtime utilities
        value_ops_path, builtins_path,  // Core runtime
        error_path, value_core_path, value_array_path,  // Error handling and core types
        value_dict_path, value_optional_path, value_struct_path,  // More core types
        value_class_path, var_path, function_path,  // Classes, vars, and functions
        type_descriptor_path, ast_path, expr_path,  // Types, AST, and parser
        expr_ops_path, ast_memory_path, ast_types_path,  // Parser ops and AST components
        ast_expr_parser_path, ast_stmt_parser_path, ast_dump_path  // AST parsers and dump
    );
    
    // Print the command for debugging
    if (getenv("BREAD_DEBUG_LINK")) {
        printf("Linking command: %s\n", cmd);
    }
    int rc = system(cmd);
    free(cmd);
    return rc == 0;
}

int bread_llvm_emit_exe(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;

    g_use_compact_print = 1;

    size_t obj_len = strlen(out_path) + 3;
    char* obj_path = (char*)malloc(obj_len);
    if (!obj_path) return 0;
    snprintf(obj_path, obj_len, "%s.o", out_path);

    int ok = bread_llvm_emit_obj(program, obj_path);
    if (!ok) {
        free(obj_path);
        return 0;
    }

    ok = bread_llvm_link_executable_with_clang(obj_path, out_path);
    if (!ok) {
        printf("Error: clang failed to link executable\n");
    }

    g_use_compact_print = 0;

    free(obj_path);
    return ok;
}

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

    // DECLARE ALL FUNCTIONS AT ONCE YOUNG BLUD?
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

    // Define the function we are JITting
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

    LLVMValueRef ret_arg = LLVMGetParam(wrapper_fn, 0); // BreadValue* (casted to i8*)
    LLVMValueRef args_arg = LLVMGetParam(wrapper_fn, 1); // BreadValue** (casted to i8**)

    // Prepare arguments for target function call
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
        LLVMValueRef alloca = LLVMBuildAlloca(builder, cg.value_type, target_fn->param_names[i]);
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
    
    // Only pop scope and add return if there's no terminator (explicit return)
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
        // Module is owned by engine now
        return 0;
    }

    fn->jit_engine = engine;
    fn->jit_fn = (void (*)(void*, void**))wrapper_ptr;
    fn->is_jitted = 1;

    printf("JIT compiled function '%s'\n", fn->name);

    return 1;
}
