#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_memory.h"
#include "compiler/ast/ast_stmt_parser.h"
#include "compiler/ast/ast_dump.h"
#include "runtime/error.h"

static void skip_whitespace(const char** code);

static const char* g_parser_filename = NULL;
static const char* g_parser_source = NULL;

void ast_parser_set_source(const char* filename, const char* source) {
    g_parser_filename = filename;
    g_parser_source = source;
}

SourceLoc ast_parser_get_loc(const char* current) {
    SourceLoc loc = {1, 1, g_parser_filename};
    if (!g_parser_source || !current) return loc;
    
    const char* p = g_parser_source;
    while (p < current && *p) {
        if (*p == '\n') {
            loc.line++;
            loc.column = 1;
        } else {
            loc.column++;
        }
        p++;
    }
    return loc;
}

ASTStmtList* ast_parse_program(const char* filename, const char* code) {
    bread_error_reset_compilation_state();
    
    ast_parser_set_source(filename, code);
    
    ASTStmtList* list = ast_stmt_list_new();
    if (!list) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Failed to allocate AST statement list");
        return NULL;
    }

    const char* ptr = code;
    while (*ptr) {
        skip_whitespace(&ptr);

        if (*ptr == '\0') break;

        if (*ptr == '/' && *(ptr + 1) == '/') {
            while (*ptr && *ptr != '\n') ptr++;
            continue;
        }

        if (*ptr == '\n' || *ptr == ';') {
            ptr++;
            continue;
        }

        ASTStmt* st = parse_stmt(&ptr);
        if (!st) {
            if (!bread_error_has_error()) {
                BREAD_ERROR_SET_PARSE_ERROR("Failed to parse statement");
            }
            ast_free_stmt_list(list);
            return NULL;
        }
        ast_stmt_list_add(list, st);

        skip_whitespace(&ptr);
        if (*ptr == ';') ptr++;
        if (bread_error_has_compilation_errors()) {
            ast_free_stmt_list(list);
            return NULL;
        }
    }

    return list;
}

static void skip_whitespace(const char** code) {
    while (**code) {
        if (isspace((unsigned char)**code)) {
            (*code)++;
        }
        else if (**code == '/' && *(*code + 1) == '/') {
            while (**code && **code != '\n') {
                (*code)++;
            }
        } else {
            break;
        }
    }
}