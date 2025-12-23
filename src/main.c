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
#include "codegen/codegen_runtime_bridge.h"

#define MAX_FILE_SIZE 1048576  // 1MB max file size
#define VERSION "1.0.0"

static char* normalize_source(const char* src, size_t len, size_t* out_len) {
    if (!src) return NULL;

    size_t i = 0;
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
        i = 3;
    }

    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;

    size_t j = 0;
    while (i < len) {
        unsigned char c = (unsigned char)src[i];

        // Normalize line endings
        if (c == '\r') {
            if (i + 1 < len && src[i + 1] == '\n') i++;
            out[j++] = '\n';
            i++;
            continue;
        }

        // Unicode whitespace normalization (UTF-8 sequences)
        // NBSP U+00A0 -> space
        if (c == 0xC2 && i + 1 < len && (unsigned char)src[i + 1] == 0xA0) {
            out[j++] = ' ';
            i += 2;
            continue;
        }

        // E2 80 xx: various spaces and separators
        if (c == 0xE2 && i + 2 < len && (unsigned char)src[i + 1] == 0x80) {
            unsigned char c2 = (unsigned char)src[i + 2];
            // U+2000..U+200A (en quad..hair space), U+202F (narrow no-break space)
            // U+200B..U+200D (zero-width space/non-joiner/joiner)
            if ((c2 >= 0x80 && c2 <= 0x8A) || (c2 >= 0x8B && c2 <= 0x8D) || c2 == 0xAF) {
                out[j++] = ' ';
                i += 3;
                continue;
            }
            // U+2028 (line separator) / U+2029 (paragraph separator)
            if (c2 == 0xA8 || c2 == 0xA9) {
                out[j++] = '\n';
                i += 3;
                continue;
            }
        }

        // Word joiner U+2060 (E2 81 A0) -> space
        if (c == 0xE2 && i + 2 < len && (unsigned char)src[i + 1] == 0x81 && (unsigned char)src[i + 2] == 0xA0) {
            out[j++] = ' ';
            i += 3;
            continue;
        }

        out[j++] = (char)c;
        i++;
    }

    out[j] = '\0';
    if (out_len) *out_len = j;
    return out;
}

static void print_usage(const char* prog) {
    printf("BreadLang v%s - A modern programming language with LLVM JIT compilation\n\n", VERSION);
    printf("Usage: %s [options] <file>\n", prog);
    printf("Options:\n");
    printf("  -h, --help            Show this help message\n");
    printf("  --emit-llvm           Emit LLVM IR to a .ll file\n");
    printf("  --emit-obj            Emit an object file\n");
    printf("  --emit-exe            Emit a native executable (default)\n");
    printf("  --jit                 Execute using JIT compilation\n");
    printf("  -o <file>             Output path for emit operations\n");
    printf("  --verbose             Enable verbose output\n");
    printf("\nMakefile Integration:\n");
    printf("  make run FILE=program.bread              # JIT execution\n");
    printf("  make compile-exe FILE=program.bread      # Create executable\n");
    printf("  make compile-llvm FILE=program.bread     # Emit LLVM IR\n");
    printf("  make compile-obj FILE=program.bread      # Emit object file\n");
    printf("\nExamples:\n");
    printf("  %s --jit program.bread                   # JIT execution\n", prog);
    printf("  %s --emit-exe -o myapp program.bread     # Create executable\n", prog);
    printf("  make run FILE=program.bread              # JIT via Makefile\n");
    printf("  make compile-exe FILE=program.bread OUT=myapp  # Compile via Makefile\n");
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
    int jit_exec = 0;
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
         if (strcmp(argv[i], "--jit") == 0) {
             jit_exec = 1;
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

    size_t normalized_len = 0;
    char* normalized = normalize_source(code, bytes_read, &normalized_len);
    if (!normalized) {
        fprintf(stderr, "Error: Out of memory\n");
        free(code);
        return 1;
    }
    free(code);
    code = normalized;
    bytes_read = normalized_len;
    
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
    if (!emit_llvm && !emit_obj && !emit_exe && !jit_exec) {
        emit_exe = 1;
    }
    if (jit_exec) {
        if (verbose) {
            printf("Executing with JIT compilation...\n");
        }
        result = bread_llvm_jit_exec(program);
    } else if (emit_llvm) {
        const char* dst = out_path ? out_path : "out.ll";
        if (!bread_llvm_emit_ll(program, dst)) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: failed to emit LLVM IR\n");
            if (bread_error_has_error()) {
                bread_error_print_current();
            }
            result = 1;
        }
    } else if (emit_obj) {
        const char* dst = out_path ? out_path : "out.o";
        if (!bread_llvm_emit_obj(program, dst)) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: failed to emit object file\n");
            if (bread_error_has_error()) {
                bread_error_print_current();
            }
            result = 1;
        }
    } else if (emit_exe) {
        const char* dst = out_path ? out_path : "a.out";
        if (!bread_llvm_emit_exe(program, dst)) {
            fprintf(stderr, "\n");
            fprintf(stderr, "error: failed to emit executable\n");
            if (bread_error_has_error()) {
                bread_error_print_current();
            }
            result = 1;
        }
    }
    
    ast_free_stmt_list(program);
    free(code);
    
    // Cleanup codegen runtime bridge resources
    cg_cleanup_class_registry();
    cg_cleanup_jit_engine();
    
    bread_error_cleanup();
    bread_builtin_cleanup();
    bread_string_intern_cleanup();
    bread_memory_cleanup();
    cleanup_functions();
    cleanup_variables();
    return result;
}
