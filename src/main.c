#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <libgen.h>
#include "runtime/print.h"
#include "runtime/runtime.h"
#include "runtime/memory.h"
#include "runtime/error.h"
#include "core/var.h"
#include "compiler/ast/ast.h"
#include "core/function.h"
#include "backends/llvm_backend.h"

#define MAX_FILE_SIZE 1048576  // 1MB max file size
#define VERSION "1.0.0"

static void print_usage(const char* prog) {
    printf("BreadLang v%s - A modern programming language with LLVM JIT compilation\n\n", VERSION);
    printf("Usage: %s [options] <file>\n", prog);
    printf("Options:\n");
    printf("  -h, --help            Show this help message\n");
    printf("  --emit-llvm           Emit LLVM IR to a .ll file\n");
    printf("  --emit-obj            Emit an object file\n");
    printf("  --emit-exe            Emit a native executable (default)\n");
    printf("  -o <file>             Output path for emit operations\n");
    printf("  --verbose             Enable verbose output\n");
    printf("\nExamples:\n");
    printf("  %s -o myapp program.bread                # Build a native executable\n", prog);
    printf("  %s --emit-llvm -o out.ll program.bread   # Emit LLVM IR\n", prog);
    printf("  %s --emit-exe -o myapp program.bread     # Create standalone executable\n", prog);
    printf("\nFor more information, visit: https://github.com/breadlang/breadlang\n");
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
    int emit_llvm = 0;
    int emit_obj = 0;
    int emit_exe = 0;
    int verbose = 0;
    const char* filename = NULL;
    const char* out_path = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
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
         if (strcmp(argv[i], "-o") == 0) {
             if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an output file argument\n");
                print_usage(argv[0]);
                 return 1;
             }
             out_path = argv[i + 1];
             i++;
             continue;
         }
        if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        if (!filename) {
            filename = argv[i];
            continue;
        }

        fprintf(stderr, "Error: Too many arguments\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!filename) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    if (verbose) {
        printf("BreadLang v%s starting...\n", VERSION);
        printf("Processing file: %s\n", filename);
    }

    // Check if file exists and is readable
    if (access(filename, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot read file '%s'\n", filename);
        return 1;
    }
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return 1;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "Error: File '%s' is too large (max %d bytes)\n", filename, MAX_FILE_SIZE);
        fclose(file);
        return 1;
    }
    
    char* code = malloc(file_size + 1);
    if (!code) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(file);
        return 1;
    }
    
    size_t bytes_read = fread(code, 1, file_size, file);
    code[bytes_read] = '\0';
    fclose(file);
    
    if (verbose) {
        printf("Read %zu bytes from %s\n", bytes_read, filename);
    }
    
    // Initialize runtime systems
    if (verbose) {
        printf("Initializing runtime systems...\n");
    }
    
    init_variables();
    init_functions();
    bread_memory_init();
    bread_string_intern_init();
    bread_builtin_init();
    bread_error_init();
     
    // Parse the program
    if (verbose) {
        printf("Parsing program...\n");
    }
    
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
    
    // Code generation and execution
    int result = 0;
    if (!emit_llvm && !emit_obj && !emit_exe) {
        emit_exe = 1;
    }
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
