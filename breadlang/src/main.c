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
#include "core/module.h"
#include "backends/llvm_backend.h"
#include "codegen/codegen_runtime_bridge.h"

#define MAX_FILE_SIZE 1048576  // 1MB max file size
#define VERSION "1.0.0"

typedef enum {
    MODE_EMIT_EXE,
    MODE_EMIT_LLVM,
    MODE_EMIT_OBJ,
    MODE_JIT_EXEC
} CompilationMode;

typedef struct {
    CompilationMode mode;
    const char* input_file;
    const char* output_file;
    int verbose;
} CompilerConfig;

static char* normalize_source(const char* src, size_t len, size_t* out_len) {
    if (!src || !out_len) {
        return NULL;
    }

    size_t i = 0;
    
    if (len >= 3 && (unsigned char)src[0] == 0xEF && 
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
        i = 3;
    }

    char* out = (char*)malloc(len + 1);
    if (!out) {
        return NULL;
    }

    size_t j = 0;
    while (i < len) {
        unsigned char c = (unsigned char)src[i];

        if (c == '\r') {
            if (i + 1 < len && src[i + 1] == '\n') {
                i++; 
            }
            out[j++] = '\n';
            i++;
            continue;
        }

        if (c == 0xC2 && i + 1 < len && (unsigned char)src[i + 1] == 0xA0) {
            out[j++] = ' ';
            i += 2;
            continue;
        }

        if (c == 0xE2 && i + 2 < len && (unsigned char)src[i + 1] == 0x80) {
            unsigned char c2 = (unsigned char)src[i + 2];
            if ((c2 >= 0x80 && c2 <= 0x8A) || (c2 >= 0x8B && c2 <= 0x8D) || c2 == 0xAF) {
                out[j++] = ' ';
                i += 3;
                continue;
            }
            
            if (c2 == 0xA8 || c2 == 0xA9) {
                out[j++] = '\n';
                i += 3;
                continue;
            }
        }

        if (c == 0xE2 && i + 2 < len && 
            (unsigned char)src[i + 1] == 0x81 && 
            (unsigned char)src[i + 2] == 0xA0) {
            out[j++] = ' ';
            i += 3;
            continue;
        }

        out[j++] = (char)c;
        i++;
    }

    out[j] = '\0';
    *out_len = j;
    return out;
}

static void print_usage(const char* prog) {
    printf("BreadLang v%s\n\n", VERSION);
    printf("Usage: %s [options] <file>\n", prog);
    printf("\nOptions:\n");
    printf("  -h, --help            Show this help message\n");
    printf("  --emit-llvm           Emit LLVM IR to a .ll file\n");
    printf("  --emit-obj            Emit an object file (.o)\n");
    printf("  --emit-exe            Emit a native executable (default)\n");
    printf("  --jit                 Execute using JIT compilation\n");
    printf("  -o <file>             Output path for emit operations\n");
    printf("  --verbose             Enable verbose output\n");
}

static int parse_arguments(int argc, char* argv[], CompilerConfig* config) {
    // Initialize defaults
    config->mode = MODE_EMIT_EXE;
    config->input_file = NULL;
    config->output_file = NULL;
    config->verbose = 0;
    
    int mode_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1; // Signal to print help
        }
        
        if (strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
            continue;
        }
        
        if (strcmp(argv[i], "--emit-llvm") == 0) {
            config->mode = MODE_EMIT_LLVM;
            mode_count++;
            continue;
        }
        
        if (strcmp(argv[i], "--emit-obj") == 0) {
            config->mode = MODE_EMIT_OBJ;
            mode_count++;
            continue;
        }
        
        if (strcmp(argv[i], "--emit-exe") == 0) {
            config->mode = MODE_EMIT_EXE;
            mode_count++;
            continue;
        }
        
        if (strcmp(argv[i], "--jit") == 0) {
            config->mode = MODE_JIT_EXEC;
            mode_count++;
            continue;
        }
        
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an output file argument\n");
                return 1;
            }
            config->output_file = argv[++i];
            continue;
        }
        
        if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return 1;
        }
        
        if (!config->input_file) {
            config->input_file = argv[i];
            continue;
        }

        fprintf(stderr, "Error: Too many arguments\n");
        return 1;
    }

    if (!config->input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    if (mode_count > 1) {
        fprintf(stderr, "Error: Multiple compilation modes specified\n");
        return 1;
    }

    return 0;
}

static char* read_source_file(const char* filename, size_t* out_len, int verbose) {
    if (access(filename, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot read file '%s'\n", filename);
        return NULL;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Could not determine file size\n");
        fclose(file);
        return NULL;
    }
    
    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "Error: Could not determine file size\n");
        fclose(file);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Could not seek to file start\n");
        fclose(file);
        return NULL;
    }
    
    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "Error: File '%s' is too large (max %d bytes)\n", 
                filename, MAX_FILE_SIZE);
        fclose(file);
        return NULL;
    }
    
    char* code = malloc(file_size + 1);
    if (!code) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(code, 1, file_size, file);
    code[bytes_read] = '\0';
    fclose(file);
    
    if (verbose) {
        printf("Read %zu bytes from %s\n", bytes_read, filename);
    }

    size_t normalized_len = 0;
    char* normalized = normalize_source(code, bytes_read, &normalized_len);
    free(code);
    
    if (!normalized) {
        fprintf(stderr, "Error: Failed to normalize source\n");
        return NULL;
    }
    
    if (out_len) {
        *out_len = normalized_len;
    }
    
    return normalized;
}

static void init_runtime(int verbose) {
    if (verbose) {
        printf("Initializing runtime systems...\n");
    }
    
    init_variables();
    init_functions();
    bread_memory_init();
    bread_string_intern_init();
    bread_builtin_init();
    bread_error_init();
}

static void cleanup_runtime(void) {
    cg_cleanup_class_registry();
    cg_cleanup_jit_engine();
    bread_error_cleanup();
    bread_builtin_cleanup();
    bread_string_intern_cleanup();
    bread_memory_cleanup();
    cleanup_functions();
    cleanup_variables();
}

static const char* get_default_output(CompilationMode mode) {
    switch (mode) {
        case MODE_EMIT_LLVM: return "out.ll";
        case MODE_EMIT_OBJ:  return "out.o";
        case MODE_EMIT_EXE:  return "a.out";
        default:             return NULL;
    }
}

static int compile_or_execute(ASTStmtList* program, const CompilerConfig* config) {
    int result = 0;
    
    switch (config->mode) {
        case MODE_JIT_EXEC:
            if (config->verbose) {
                printf("Executing with JIT compilation...\n");
            }
            result = bread_llvm_jit_exec(program);
            printf("\n=== Memory Statistics ===\n");
            bread_memory_print_stats();
            if (bread_memory_check_leaks()) {
                printf("\n=== Memory Leaks Detected ===\n");
                bread_memory_print_leak_report();
            }
            break;
            
        case MODE_EMIT_LLVM: {
            const char* dst = config->output_file ? config->output_file : get_default_output(config->mode);
            if (config->verbose) {
                printf("Emitting LLVM IR to %s...\n", dst);
            }
            if (!bread_llvm_emit_ll(program, dst)) {
                fprintf(stderr, "Error: Failed to emit LLVM IR\n");
                result = 1;
            }
            break;
        }
        
        case MODE_EMIT_OBJ: {
            const char* dst = config->output_file ? config->output_file : get_default_output(config->mode);
            if (config->verbose) {
                printf("Emitting object file to %s...\n", dst);
            }
            if (!bread_llvm_emit_obj(program, dst)) {
                fprintf(stderr, "Error: Failed to emit object file\n");
                result = 1;
            }
            break;
        }
        
        case MODE_EMIT_EXE: {
            const char* dst = config->output_file ? config->output_file : get_default_output(config->mode);
            if (config->verbose) {
                printf("Emitting executable to %s...\n", dst);
            }
            if (!bread_llvm_emit_exe(program, dst)) {
                fprintf(stderr, "Error: Failed to emit executable\n");
                result = 1;
            }
            break;
        }
    }
    
    if (result != 0 && bread_error_has_error()) {
        bread_error_print_current();
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    CompilerConfig config;
    int parse_result = parse_arguments(argc, argv, &config);
    if (parse_result == -1) {
        print_usage(argv[0]);
        return 0;
    }
    if (parse_result != 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (config.verbose) {
        printf("BreadLang v%s starting...\n", VERSION);
        printf("Processing file: %s\n", config.input_file);
    }

    size_t source_len = 0;
    char* source = read_source_file(config.input_file, &source_len, config.verbose);
    if (!source) {
        return 1;
    }
    
    init_runtime(config.verbose);
    module_system_init();

     if (config.input_file) {
         char* path_copy = strdup(config.input_file);
         if (path_copy) {
             char* dir = dirname(path_copy);
             if (dir) {
                 module_add_search_path(dir);
             }
             free(path_copy);
         }
     }
    
    if (config.verbose) {
        printf("Parsing program...\n");
    }
    
    ASTStmtList* program = ast_parse_program(config.input_file ? config.input_file : "<string>", source);
    if (!program || bread_error_has_compilation_errors()) {
        fprintf(stderr, "\nError: Could not compile due to previous error(s)\n");
        if (bread_error_has_error()) {
            bread_error_print_current();
        }
        
        if (program) {
            ast_free_stmt_list(program);
        }
        free(source);
        cleanup_runtime();
        return 1;
    }

     if (!module_preprocess_program(program, config.input_file)) {
         fprintf(stderr, "\nError: Could not process imports/exports: %s\n", module_get_error());
         ast_free_stmt_list(program);
         free(source);
         module_system_cleanup();
         cleanup_runtime();
         return 1;
     }
    
    int result = compile_or_execute(program, &config);
    ast_free_stmt_list(program);
    free(source);
    module_system_cleanup();
    cleanup_runtime();
    
    return result;
}