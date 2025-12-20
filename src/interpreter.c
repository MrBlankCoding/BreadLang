#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/print.h"
#include "../include/var.h"
#include "../include/ast.h"
#include "../include/function.h"
#include "../include/semantic.h"
#include "../include/compiler.h"
#include "../include/vm.h"
#include "../include/llvm_backend.h"

#define MAX_FILE_SIZE 65536

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
    int trace = 0;
    int use_ast = 0;
    int emit_llvm = 0;
    const char* filename = NULL;
    const char* out_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-ast") == 0) {
            dump_ast = 1;
            continue;
        }
        if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
            continue;
        }
        if (strcmp(argv[i], "--use-ast") == 0) {
            use_ast = 1;
            continue;
        }
         if (strcmp(argv[i], "--emit-llvm") == 0) {
             emit_llvm = 1;
             continue;
         }
         if (strcmp(argv[i], "-o") == 0) {
             if (i + 1 >= argc) {
                 printf("Usage: %s [--dump-ast] [--trace] [--use-ast] [--emit-llvm] [-o out.ll] <filename>\n", argv[0]);
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

        printf("Usage: %s [--dump-ast] [--trace] [--use-ast] [--emit-llvm] [-o out.ll] <filename>\n", argv[0]);
        return 1;
    }

    if (!filename) {
        printf("Usage: %s [--dump-ast] [--trace] [--use-ast] [--emit-llvm] [-o out.ll] <filename>\n", argv[0]);
        return 1;
    }

    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Could not open file '%s'\n", filename);
        return 1;
    }
    
    // Read entire file
    char* code = malloc(MAX_FILE_SIZE);
    if (!code) {
        printf("Error: Out of memory\n");
        fclose(file);
        return 1;
    }
    size_t bytes_read = fread(code, 1, MAX_FILE_SIZE - 1, file);
    code[bytes_read] = '\0';
    fclose(file);
    
    init_variables();
    init_functions();

    bread_set_trace(trace);
    ast_runtime_init();
     
    ASTStmtList* program = ast_parse_program(code);
    if (program) {
        if (dump_ast) {
            ast_dump_stmt_list(program, stdout);
        } else {
            if (semantic_analyze(program)) {
                if (emit_llvm) {
                    const char* dst = out_path ? out_path : "out.ll";
                    (void)bread_llvm_emit_ll(program, dst);
                } else if (use_ast) {
                    (void)ast_execute_stmt_list(program, NULL);
                } else {
                    BytecodeChunk chunk;
                    bc_chunk_init(&chunk);
                    if (compile_program(program, &chunk)) {
                        VM vm;
                        vm_init(&vm);
                        (void)vm_run(&vm, &chunk, 0, NULL);
                        vm_free(&vm);
                    }
                    bc_chunk_free(&chunk);
                }
            }
        }
        ast_free_stmt_list(program);
    }
     
    free(code);
    ast_runtime_cleanup();
    cleanup_functions();
    cleanup_variables();
    return 0;
}