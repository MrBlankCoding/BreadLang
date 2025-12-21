#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_types.h"
#include "core/var.h"

#define MAX_TOKEN_LEN 1024

static void skip_whitespace(const char** code);

int parse_type_string(const char** code, char* out_buf, size_t out_sz) {
    if (!out_buf || out_sz == 0) return 0;
    skip_whitespace(code);
    const char* start = *code;
    if (*start == '\0') return 0;

    int bracket_depth = 0;
    while (**code) {
        char c = **code;
        if (c == '[') bracket_depth++;
        else if (c == ']') bracket_depth--;

        if (bracket_depth == 0) {
            if (c == ',' || c == ')' || c == '{' || c == '}' || isspace((unsigned char)c)) {
                break;
            }
            if (c == '-' && *(*code + 1) == '>') {
                break;
            }
        }

        (*code)++;
        if (bracket_depth < 0) break;
    }

    const char* end = *code;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= out_sz) return 0;
    memcpy(out_buf, start, len);
    out_buf[len] = '\0';
    return 1;
}

int parse_type_token(const char** code, VarType* out_type) {
    skip_whitespace(code);
    char tmp[MAX_TOKEN_LEN];
    if (!parse_type_string(code, tmp, sizeof(tmp))) return 0;

    if (strcmp(tmp, "Int") == 0) *out_type = TYPE_INT;
    else if (strcmp(tmp, "String") == 0) *out_type = TYPE_STRING;
    else if (strcmp(tmp, "Bool") == 0) *out_type = TYPE_BOOL;
    else if (strcmp(tmp, "Float") == 0) *out_type = TYPE_FLOAT;
    else if (strcmp(tmp, "Double") == 0) *out_type = TYPE_DOUBLE;
    else if (tmp[0] == '[') {
        int depth = 0;
        const char* end = strrchr(tmp, ']');
        if (!end) return 0;
        int is_dict = 0;
        for (const char* p = tmp + 1; p < end; p++) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == ':' && depth == 0) {
                is_dict = 1;
                break;
            }
        }
        *out_type = is_dict ? TYPE_DICT : TYPE_ARRAY;
    } else {
        size_t tlen = strlen(tmp);
        if (tlen > 0 && tmp[tlen - 1] == '?') *out_type = TYPE_OPTIONAL;
        else return 0;
    }

    return 1;
}

static void skip_whitespace(const char** code) {
    while (**code && isspace((unsigned char)**code)) (*code)++;
}