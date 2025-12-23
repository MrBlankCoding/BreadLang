#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "backends/llvm_backend_link.h"
#include "backends/llvm_backend_utils.h"
#include "backends/llvm_backend_emit.h"

int bread_llvm_link_executable_with_clang(const char* obj_path, const char* out_path) {
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
    
    snprintf(rt_path, sizeof(rt_path), "%s/src/runtime/runtime.c", root_dir);
    snprintf(memory_path, sizeof(memory_path), "%s/src/runtime/memory.c", root_dir);
    snprintf(print_path, sizeof(print_path), "%s/src/runtime/print.c", root_dir);
    snprintf(string_ops_path, sizeof(string_ops_path), "%s/src/runtime/string_ops.c", root_dir);
    snprintf(operators_path, sizeof(operators_path), "%s/src/runtime/operators.c", root_dir);
    snprintf(array_utils_path, sizeof(array_utils_path), "%s/src/runtime/array_utils.c", root_dir);
    snprintf(value_ops_path, sizeof(value_ops_path), "%s/src/runtime/value_ops.c", root_dir);
    snprintf(builtins_path, sizeof(builtins_path), "%s/src/runtime/builtins.c", root_dir);
    // Need to improve ERR
    snprintf(error_path, sizeof(error_path), "%s/src/runtime/error.c", root_dir);
    
    snprintf(value_core_path, sizeof(value_core_path), "%s/src/core/value_core.c", root_dir);
    snprintf(value_array_path, sizeof(value_array_path), "%s/src/core/value_array.c", root_dir);
    snprintf(value_dict_path, sizeof(value_dict_path), "%s/src/core/value_dict.c", root_dir);
    snprintf(value_optional_path, sizeof(value_optional_path), "%s/src/core/value_optional.c", root_dir);
    snprintf(value_struct_path, sizeof(value_struct_path), "%s/src/core/value_struct.c", root_dir);
    snprintf(value_class_path, sizeof(value_class_path), "%s/src/core/value_class.c", root_dir);
    snprintf(var_path, sizeof(var_path), "%s/src/core/var.c", root_dir);
    snprintf(function_path, sizeof(function_path), "%s/src/core/function.c", root_dir);
    snprintf(type_descriptor_path, sizeof(type_descriptor_path), "%s/src/core/type_descriptor.c", root_dir);
    
    snprintf(ast_path, sizeof(ast_path), "%s/src/compiler/ast/ast.c", root_dir);
    snprintf(expr_path, sizeof(expr_path), "%s/src/compiler/parser/expr.c", root_dir);
    snprintf(expr_ops_path, sizeof(expr_ops_path), "%s/src/compiler/parser/expr_ops.c", root_dir);
    
    snprintf(ast_memory_path, sizeof(ast_memory_path), "%s/src/compiler/ast/ast_memory.c", root_dir);
    snprintf(ast_types_path, sizeof(ast_types_path), "%s/src/compiler/ast/ast_types.c", root_dir);
    snprintf(ast_expr_parser_path, sizeof(ast_expr_parser_path), "%s/src/compiler/ast/ast_expr_parser.c", root_dir);
    snprintf(ast_stmt_parser_path, sizeof(ast_stmt_parser_path), "%s/src/compiler/ast/ast_stmt_parser.c", root_dir);
    
    snprintf(ast_dump_path, sizeof(ast_dump_path), "%s/src/compiler/ast/ast_dump.c", root_dir);
    snprintf(inc_path, sizeof(inc_path), "%s/include", root_dir);

    // Buffer size for command
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
                2048;  // A little extra space
    char* cmd = (char*)malloc(cap);
    if (!cmd) return 0;

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