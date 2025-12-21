#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/llvm_backend.h"
#include "compiler/type_stability.h"
#include "compiler/escape_analysis.h"
#include "compiler/optimization.h"
#include "codegen/optimized_codegen.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Support.h>

#include <limits.h>

#include "core/var.h"
#include "core/function.h"
#include "codegen/codegen.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#elif defined(__linux__)
#endif

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
    cg->value_type = LLVMArrayType(cg->i8, 128);
    cg->value_ptr_type = LLVMPointerType(cg->value_type, 0);

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
    cg->ty_member_op = LLVMFunctionType(cg->i32, (LLVMTypeRef[]){cg->i8_ptr, cg->i8_ptr, cg->i32, cg->i8_ptr}, 4, 0);
    cg->fn_member_op = cg_declare_fn(cg, "bread_member_op", cg->ty_member_op);
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
    
    cg_define_functions(cg);
}

static int bread_llvm_build_module_from_program(const ASTStmtList* program, LLVMModuleRef* out_mod) {
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

    cg_init(&cg, mod, builder);

    main_ty = LLVMFunctionType(cg.i32, NULL, 0, 0);
    main_fn = LLVMAddFunction(mod, "main", main_ty);
    entry = LLVMAppendBasicBlock(main_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_init_variables, NULL, 0, "");
    (void)LLVMBuildCall2(builder, LLVMFunctionType(cg.void_ty, NULL, 0, 0), cg.fn_init_functions, NULL, 0, "");

    val_size = cg_value_size(&cg);

    if (!cg_build_stmt_list(&cg, NULL, val_size, (ASTStmtList*)program)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 0;
    }

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
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
                LLVMBuildRetVoid(builder);
            }
        }
    }
    
    LLVMPositionBuilderAtEnd(builder, main_block);
    // LLVM optimization passes disabled (legacy pass manager removed)

    LLVMDisposeBuilder(builder);
    *out_mod = mod;
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

    LLVMModuleRef mod = NULL;
    if (!bread_llvm_build_module_from_program(program, &mod)) return 0;
    if (!bread_llvm_verify_module(mod)) {
        LLVMDisposeModule(mod);
        return 0;
    }

    char* ir_text = LLVMPrintModuleToString(mod);
    int ok = write_text_file(out_path, ir_text ? ir_text : "");
    if (!ok) {
        printf("Error: Could not write LLVM IR to '%s'\n", out_path);
    }
    if (ir_text) LLVMDisposeMessage(ir_text);
    LLVMDisposeModule(mod);
    return ok;
}

int bread_llvm_emit_obj(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;

    LLVMModuleRef mod = NULL;
    if (!bread_llvm_build_module_from_program(program, &mod)) return 0;

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

    char root_dir[PATH_MAX];
    memset(root_dir, 0, sizeof(root_dir));
    if (!bread_get_exe_dir(root_dir, sizeof(root_dir))) {
        printf("Error: could not determine BreadLang install directory for linking\n");
        return 0;
    }

    char rt_path[PATH_MAX];
    char print_path[PATH_MAX];
    char value_path[PATH_MAX];
    char var_path[PATH_MAX];
    char function_path[PATH_MAX];
    char ast_path[PATH_MAX];
    char expr_path[PATH_MAX];
    char expr_ops_path[PATH_MAX];
    char inc_path[PATH_MAX];
    snprintf(rt_path, sizeof(rt_path), "%s/src/runtime/runtime.c", root_dir);
    snprintf(print_path, sizeof(print_path), "%s/src/runtime/print.c", root_dir);
    snprintf(value_path, sizeof(value_path), "%s/src/core/value.c", root_dir);
    snprintf(var_path, sizeof(var_path), "%s/src/core/var.c", root_dir);
    snprintf(function_path, sizeof(function_path), "%s/src/core/function.c", root_dir);
    snprintf(ast_path, sizeof(ast_path), "%s/src/compiler/ast.c", root_dir);
    snprintf(expr_path, sizeof(expr_path), "%s/src/compiler/expr.c", root_dir);
    snprintf(expr_ops_path, sizeof(expr_ops_path), "%s/src/compiler/expr_ops.c", root_dir);
    snprintf(inc_path, sizeof(inc_path), "%s/include", root_dir);

    size_t cap = strlen(obj_path) + strlen(out_path) + strlen(rt_path) + strlen(print_path) + strlen(value_path) + strlen(var_path) + strlen(function_path) + strlen(ast_path) + strlen(expr_path) + strlen(expr_ops_path) + strlen(inc_path) + 192;
    char* cmd = (char*)malloc(cap);
    if (!cmd) return 0;

    snprintf(
        cmd,
        cap,
        "clang -std=c11 -I'%s' -o '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s' -lm",
        inc_path,
        out_path,
        obj_path,
        rt_path,
        print_path,
        value_path,
        var_path,
        function_path,
        ast_path,
        expr_path,
        expr_ops_path);
    int rc = system(cmd);
    free(cmd);
    return rc == 0;
}

int bread_llvm_emit_exe(const ASTStmtList* program, const char* out_path) {
    if (!program || !out_path) return 0;

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

    free(obj_path);
    return ok;
}

int bread_llvm_jit_exec(const ASTStmtList* program) {
    if (!program) return 1;

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    LLVMModuleRef mod = NULL;
    if (!bread_llvm_build_module_from_program(program, &mod)) return 1;
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

    // Now compile the target function body
    // Reuse logic from bread_llvm_build_module_from_program
    
    LLVMBasicBlockRef fn_entry = LLVMAppendBasicBlock(target_fn->fn, "entry");
    LLVMPositionBuilderAtEnd(builder, fn_entry);

    target_fn->ret_slot = LLVMGetParam(target_fn->fn, 0);
    target_fn->scope = (CgScope*)malloc(sizeof(CgScope));
    target_fn->scope->vars = NULL;
    target_fn->scope->parent = NULL;

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
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) == NULL) {
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
