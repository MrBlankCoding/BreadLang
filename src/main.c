#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "runtime/print.h"
#include "runtime/runtime.h"
#include "core/var.h"
#include "compiler/ast.h"
#include "core/function.h"
#include "compiler/semantic.h"
#include "backends/llvm_backend.h"

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
    int emit_llvm = 0;
    int emit_obj = 0;
    int emit_exe = 0;
    const char* filename = NULL;
    const char* out_path = NULL;

    for (int i = 1; i < argc; i++) {
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
         if (strcmp(argv[i], "-o") == 0) {
             if (i + 1 >= argc) {
                 printf("Usage: %s [--dump-ast] [--emit-llvm|--emit-obj|--emit-exe] [-o out] <filename>\n", argv[0]);
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

        printf("Usage: %s [--dump-ast] [--emit-llvm|--emit-obj|--emit-exe] [-o out] <filename>\n", argv[0]);
        return 1;
    }

    if (!filename) {
        printf("Usage: %s [--dump-ast] [--emit-llvm|--emit-obj|--emit-exe] [-o out] <filename>\n", argv[0]);
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
    bread_builtin_init();
     
    ASTStmtList* program = ast_parse_program(code);
    if (program) {
        if (dump_ast) {
            ast_dump_stmt_list(program, stdout);
        } else {
            if (semantic_analyze(program)) {
                if (emit_llvm) {
                    const char* dst = out_path ? out_path : "out.ll";
                    if (!bread_llvm_emit_ll(program, dst)) {
                        return 1;
                    }
                } else if (emit_obj) {
                    const char* dst = out_path ? out_path : "out.o";
                    if (!bread_llvm_emit_obj(program, dst)) {
                        return 1;
                    }
                } else if (emit_exe) {
                    const char* dst = out_path ? out_path : "a.out";
                    if (!bread_llvm_emit_exe(program, dst)) {
                        return 1;
                    }
                } else {
                    // Default execution: LLVM JIT
                    if (bread_llvm_jit_exec(program) != 0) {
                        return 1;
                    }
                }
            }
        }
        ast_free_stmt_list(program);
    }
     
    free(code);
    bread_builtin_cleanup();
    cleanup_functions();
    cleanup_variables();
    return 0;
}