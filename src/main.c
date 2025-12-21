#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "runtime/print.h"
#include "runtime/runtime.h"
#include "runtime/memory.h"
#include "runtime/error.h"
#include "core/var.h"
#include "compiler/ast/ast.h"
#include "core/function.h"
#include "compiler/analysis/semantic.h"
#include "backends/llvm_backend.h"

#define MAX_FILE_SIZE 65536

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <file>\n", prog);
    printf("Options:\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -c, --eval <code>     Execute BreadLang code from command line\n");
    printf("  --dump-ast            Print the parsed AST\n");
    printf("  --emit-llvm           Emit LLVM IR to a .ll file\n");
    printf("  --emit-obj            Emit an object file\n");
    printf("  --emit-exe            Emit a native executable\n");
    printf("  -o <file>             Output path for emit operations\n");
    printf("Examples:\n");
    printf("  %s -c 'print(1 + 2)'\n", prog);
    printf("  %s --emit-llvm -o out.ll program.bread\n", prog);
}

char* trim_main(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

int main(int argc, char* argv[]) {
    int dump_ast = 0;
    int emit_llvm = 0;
    int emit_obj = 0;
    int emit_exe = 0;
    const char* filename = NULL;
    const char* out_path = NULL;
    const char* inline_code = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--dump-ast") == 0) {
            dump_ast = 1;
            continue;
        }
         if (strcmp(argv[i], "--emit-llvm") == 0) {
             emit_llvm = 1;
             continue;
         }
         if (strcmp(argv[i], "--emit-obj") == 0) {
             emit_obj = 1;
             continue;
         }
         if (strcmp(argv[i], "--emit-exe") == 0) {
             emit_exe = 1;
             continue;
         }
         if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--eval") == 0) {
             if (i + 1 >= argc) {
                 print_usage(argv[0]);
                 return 1;
             }
             inline_code = argv[i + 1];
             i++;
             continue;
         }
         if (strcmp(argv[i], "-o") == 0) {
             if (i + 1 >= argc) {
                print_usage(argv[0]);
                 return 1;
             }
             out_path = argv[i + 1];
             i++;
             continue;
         }
        if (!filename) {
            filename = argv[i];
            continue;
        }

        print_usage(argv[0]);
        return 1;
    }

    if (!filename && !inline_code) {
        print_usage(argv[0]);
        return 1;
    }

    char* code = NULL;
    if (inline_code) {
        size_t n = strlen(inline_code);
        code = malloc(n + 1);
        if (!code) {
            printf("Error: Out of memory\n");
            return 1;
        }
        memcpy(code, inline_code, n + 1);
    } else {
        FILE* file = fopen(filename, "r");
        if (!file) {
            printf("Error: Could not open file '%s'\n", filename);
            return 1;
        }
        code = malloc(MAX_FILE_SIZE);
        if (!code) {
            printf("Error: Out of memory\n");
            fclose(file);
            return 1;
        }
        size_t bytes_read = fread(code, 1, MAX_FILE_SIZE - 1, file);
        code[bytes_read] = '\0';
        fclose(file);
    }
    
    init_variables();
    init_functions();
    bread_memory_init();
    bread_string_intern_init();
    bread_builtin_init();
    bread_error_init();
     
    ASTStmtList* program = ast_parse_program(code);
    
    // Check for parsing errors
    if (!program) {
        fprintf(stderr, "\n");
        fprintf(stderr, "error: could not compile due to previous error(s)\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        free(code);
        bread_error_cleanup();
        bread_builtin_cleanup();
        bread_string_intern_cleanup();
        bread_memory_cleanup();
        cleanup_functions();
        cleanup_variables();
        return 1;
    }
    
    // Check for compilation errors after parsing
    if (bread_error_has_compilation_errors()) {
        fprintf(stderr, "\n");
        fprintf(stderr, "error: could not compile due to previous error(s)\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        ast_free_stmt_list(program);
        free(code);
        bread_error_cleanup();
        bread_builtin_cleanup();
        bread_string_intern_cleanup();
        bread_memory_cleanup();
        cleanup_functions();
        cleanup_variables();
        return 1;
    }
    
    if (dump_ast) {
        ast_dump_stmt_list(program, stdout);
        ast_free_stmt_list(program);
        free(code);
        bread_error_cleanup();
        bread_builtin_cleanup();
        bread_string_intern_cleanup();
        bread_memory_cleanup();
        cleanup_functions();
        cleanup_variables();
        return 0;
    }
    
    // Perform semantic analysis
    if (!semantic_analyze(program)) {
        fprintf(stderr, "\n");
        fprintf(stderr, "error: could not compile due to previous error(s)\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        ast_free_stmt_list(program);
        free(code);
        bread_error_cleanup();
        bread_builtin_cleanup();
        bread_string_intern_cleanup();
        bread_memory_cleanup();
        cleanup_functions();
        cleanup_variables();
        return 1;
    }
    
    // Check for compilation errors after semantic analysis
    if (bread_error_has_compilation_errors()) {
        fprintf(stderr, "\n");
        fprintf(stderr, "error: could not compile due to previous error(s)\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        ast_free_stmt_list(program);
        free(code);
        bread_error_cleanup();
        bread_builtin_cleanup();
        bread_string_intern_cleanup();
        bread_memory_cleanup();
        cleanup_functions();
        cleanup_variables();
        return 1;
    }
    
    // Code generation and execution
    int result = 0;
    if (emit_llvm) {
        const char* dst = out_path ? out_path : "out.ll";
        if (!bread_llvm_emit_ll(program, dst)) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: failed to emit LLVM IR\n");
            result = 1;
        }
    } else if (emit_obj) {
        const char* dst = out_path ? out_path : "out.o";
        if (!bread_llvm_emit_obj(program, dst)) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: failed to emit object file\n");
            result = 1;
        }
    } else if (emit_exe) {
        const char* dst = out_path ? out_path : "a.out";
        if (!bread_llvm_emit_exe(program, dst)) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: failed to emit executable\n");
            result = 1;
        }
    } else {
        // Default execution: LLVM JIT
        if (bread_llvm_jit_exec(program) != 0) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: execution failed\n");
            result = 1;
        }
    }
    
    ast_free_stmt_list(program);
    free(code);
    bread_error_cleanup();
    bread_builtin_cleanup();
    bread_string_intern_cleanup();
    bread_memory_cleanup();
    cleanup_functions();
    cleanup_variables();
    return result;
}
