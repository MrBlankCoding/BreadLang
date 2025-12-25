#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "backends/llvm_backend_link.h"
#include "backends/llvm_backend_utils.h"
#include "backends/llvm_backend_emit.h"

static int file_exists(const char* path) {
    FILE* f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

static void append_arg(char** cmd,
                       size_t* cap,
                       size_t* len,
                       const char* arg) {
    size_t need = strlen(arg) + 4; // space quotes nil

    if (*len + need >= *cap) {
        *cap *= 2;
        char* tmp = realloc(*cmd, *cap);
        if (!tmp) return;
        *cmd = tmp;
    }

    *len += snprintf(*cmd + *len,
                     *cap - *len,
                     " '%s'",
                     arg);
}

static const char* BREAD_RUNTIME_SOURCES[] = {
    // runtime
    "src/runtime/runtime.c",
    "src/runtime/memory.c",
    "src/runtime/print.c",
    "src/runtime/string_ops.c",
    "src/runtime/operators.c",
    "src/runtime/array_utils.c",
    "src/runtime/value_ops.c",
    "src/runtime/builtins.c",
    "src/runtime/error.c",

    // core
    "src/core/value_core.c",
    "src/core/value_array.c",
    "src/core/value_dict.c",
    "src/core/value_optional.c",
    "src/core/value_struct.c",
    "src/core/value_class.c",
    "src/core/var.c",
    "src/core/function.c",
    "src/core/type_descriptor.c",

    // ast/ compiler
    "src/compiler/ast/ast.c",
    "src/compiler/ast/ast_memory.c",
    "src/compiler/ast/ast_types.c",
    "src/compiler/ast/ast_dump.c",

    // parser
    "src/compiler/parser/expr.c",
    "src/compiler/parser/expr_ops.c",
    "src/compiler/ast/ast_expr_parser.c",
    "src/compiler/ast/ast_stmt_parser.c",
};

int bread_llvm_link_executable_with_clang(const char* obj_path,
                                         const char* out_path) {
    if (!obj_path || !out_path) return 0;

    char exe_dir[PATH_MAX];
    char root_dir[PATH_MAX];

    if (!bread_get_exe_dir(exe_dir, sizeof(exe_dir))) {
        fprintf(stderr,
                "Error: could not determine BreadLang executable directory\n");
        return 0;
    }

    if (!bread_find_project_root_from_exe_dir(
            exe_dir, root_dir, sizeof(root_dir))) {
        fprintf(stderr,
                "Error: could not determine BreadLang project root\n");
        return 0;
    }

    // initial command buffer
    size_t cap = 4096;
    size_t len = 0;
    char* cmd = malloc(cap);
    if (!cmd) return 0;

    len += snprintf(
        cmd + len,
        cap - len,
        "clang -std=c11 -O2 -g -fPIC -lm "
        "-I'%s/breadlang/include' "
        "-o '%s' '%s'",
        root_dir,
        out_path,
        obj_path
    );

    // append runtime sources
    size_t count =
        sizeof(BREAD_RUNTIME_SOURCES) /
        sizeof(BREAD_RUNTIME_SOURCES[0]);

    for (size_t i = 0; i < count; i++) {
        char full_path[PATH_MAX];

        snprintf(full_path,
                 sizeof(full_path),
                 "%s/breadlang/%s",
                 root_dir,
                 BREAD_RUNTIME_SOURCES[i]);

        if (!file_exists(full_path)) {
            fprintf(stderr,
                    "Error: missing runtime file: %s\n",
                    full_path);
            free(cmd);
            return 0;
        }

        append_arg(&cmd, &cap, &len, full_path);
    }

    if (getenv("BREAD_DEBUG_LINK")) {
        printf("BreadLang link command:\n%s\n", cmd);
    }

    int rc = system(cmd);
    free(cmd);

    return rc == 0;
}

// llvm obj to exe
int bread_llvm_emit_exe(const ASTStmtList* program,
                        const char* out_path) {
    if (!program || !out_path) return 0;

    char obj_path[PATH_MAX];
    snprintf(obj_path, sizeof(obj_path), "%s.o", out_path);

    if (!bread_llvm_emit_obj(program, obj_path)) {
        return 0;
    }

    if (!bread_llvm_link_executable_with_clang(obj_path, out_path)) {
        fprintf(stderr,
                "Error: clang failed to link BreadLang executable\n");
        return 0;
    }

    return 1;
}
