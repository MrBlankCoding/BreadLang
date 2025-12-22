#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_memory.h"
#include "compiler/ast/ast_types.h"
#include "compiler/ast/ast_expr_parser.h"
#include "compiler/ast/ast_stmt_parser.h"

#define MAX_TOKEN_LEN 1024

static void skip_whitespace(const char** code);
static char* dup_range(const char* start, const char* end);

ASTStmtList* parse_block(const char** code) {
    ASTStmtList* list = ast_stmt_list_new();
    if (!list) return NULL;

    while (**code && **code != '}') {
        skip_whitespace(code);
        if (**code == '}') break;
        ASTStmt* st = parse_stmt(code);
        if (!st) {
            ast_free_stmt_list(list);
            return NULL;
        }
        ast_stmt_list_add(list, st);
    }

    return list;
}

ASTStmt* parse_stmt(const char** code) {
    skip_whitespace(code);

    if (strncmp(*code, "func ", 5) == 0 || strncmp(*code, "fn ", 3) == 0) {
        int is_short = strncmp(*code, "fn ", 3) == 0;
        *code += is_short ? 3 : 5;
        skip_whitespace(code);

        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* fn_name = dup_range(start, *code);
        if (!fn_name) return NULL;

        skip_whitespace(code);
        if (**code != '(') {
            free(fn_name);
            return NULL;
        }
        (*code)++;

        int param_cap = 0;
        int param_count = 0;
        char** param_names = NULL;
        TypeDescriptor** param_type_descs = NULL;
        ASTExpr** param_defaults = NULL;

        skip_whitespace(code);
        if (**code != ')') {
            while (**code) {
                skip_whitespace(code);
                const char* pstart = *code;
                while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                if (*code == pstart) {
                    free(fn_name);
                    return NULL;
                }
                char* p_name = dup_range(pstart, *code);
                if (!p_name) {
                    free(fn_name);
                    return NULL;
                }

                skip_whitespace(code);
                if (**code != ':') {
                    free(p_name);
                    free(fn_name);
                    return NULL;
                }
                (*code)++;

                TypeDescriptor* p_type_desc = parse_type_descriptor(code);
                if (!p_type_desc) {
                    free(p_name);
                    free(fn_name);
                    return NULL;
                }

                // Check for default value
                ASTExpr* default_expr = NULL;
                skip_whitespace(code);
                if (**code == '=') {
                    (*code)++;
                    skip_whitespace(code);
                    default_expr = parse_expression_str_as_ast(code);
                    if (!default_expr) {
                        free(p_name);
                        free(fn_name);
                        type_descriptor_free(p_type_desc);
                        for (int i = 0; i < param_count; i++) {
                            free(param_names[i]);
                            if (param_defaults[i]) ast_free_expr(param_defaults[i]);
                            if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
                        }
                        free(param_names);
                        free(param_type_descs);
                        free(param_defaults);
                        return NULL;
                    }
                }

                if (param_count >= param_cap) {
                    int new_cap = param_cap == 0 ? 4 : param_cap * 2;
                    char** new_names = malloc(sizeof(char*) * (size_t)new_cap);
                    TypeDescriptor** new_types = malloc(sizeof(TypeDescriptor*) * (size_t)new_cap);
                    ASTExpr** new_defaults = malloc(sizeof(ASTExpr*) * (size_t)new_cap);
                    if (!new_names || !new_types || !new_defaults) {
                        free(new_names);
                        free(new_types);
                        free(new_defaults);
                        free(p_name);
                        free(fn_name);
                        type_descriptor_free(p_type_desc);
                        for (int i = 0; i < param_count; i++) free(param_names[i]);
                        free(param_names);
                        for (int i = 0; i < param_count; i++) {
                            if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
                        }
                        free(param_type_descs);
                        free(param_defaults);
                        return NULL;
                    }
                    if (param_count > 0) {
                        memcpy(new_names, param_names, sizeof(char*) * (size_t)param_count);
                        memcpy(new_types, param_type_descs, sizeof(TypeDescriptor*) * (size_t)param_count);
                        memcpy(new_defaults, param_defaults, sizeof(ASTExpr*) * (size_t)param_count);
                    }
                    free(param_names);
                    free(param_type_descs);
                    free(param_defaults);
                    param_names = new_names;
                    param_type_descs = new_types;
                    param_defaults = new_defaults;
                    param_cap = new_cap;
                }
                param_names[param_count] = p_name;
                param_type_descs[param_count] = p_type_desc;
                param_defaults[param_count] = default_expr;
                param_count++;

                skip_whitespace(code);
                if (**code == ',') {
                    (*code)++;
                    continue;
                }
                break;
            }
        }

        skip_whitespace(code);
        if (**code != ')') {
            free(fn_name);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            return NULL;
        }
        (*code)++;

        VarType ret_type = TYPE_INT;
        TypeDescriptor* ret_type_desc = type_descriptor_create_primitive(TYPE_INT);
        skip_whitespace(code);
        if (**code == '-' && *(*code + 1) == '>') {
            *code += 2;
            type_descriptor_free(ret_type_desc);
            ret_type_desc = parse_type_descriptor(code);
            if (!ret_type_desc) {
                free(fn_name);
                for (int i = 0; i < param_count; i++) {
                    free(param_names[i]);
                    if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                    if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
                }
                free(param_names);
                free(param_type_descs);
                free(param_defaults);
                return NULL;
            }
            ret_type = ret_type_desc->base_type;
        }

        if (**code == '-' && *(*code + 1) == '>') {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            return NULL;
        }

        skip_whitespace(code);
        if (**code != '{') {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            return NULL;
        }
        (*code)++;

        ASTStmtList* body = parse_block(code);
        if (!body || **code != '}') {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            if (body) ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_FUNC_DECL);
        if (!s) {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.func_decl.name = fn_name;
        s->as.func_decl.param_count = param_count;
        s->as.func_decl.param_names = param_names;
        s->as.func_decl.param_type_descs = param_type_descs;
        s->as.func_decl.param_defaults = param_defaults;
        s->as.func_decl.return_type = ret_type;
        s->as.func_decl.return_type_desc = ret_type_desc;
        s->as.func_decl.body = body;
        return s;
    }

    if (strncmp(*code, "return", 6) == 0 && !isalnum((unsigned char)*(*code + 6))) {
        *code += 6;
        skip_whitespace(code);
        ASTExpr* e = parse_expression_str_as_ast(code);
        if (!e) return NULL;
        ASTStmt* s = ast_stmt_new(AST_STMT_RETURN);
        if (!s) {
            ast_free_expr(e);
            return NULL;
        }
        s->as.ret.expr = e;

        const char* lookahead = *code;
        while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\r') {
            lookahead++;
        }
        if (*lookahead == '\n' || *lookahead == '\0' || *lookahead == '}' || *lookahead == ';') {
            *code = lookahead;
            if (**code == '\n' || **code == ';') (*code)++;
        }

        return s;
    }

    if (strncmp(*code, "let ", 4) == 0 || strncmp(*code, "var ", 4) == 0 || strncmp(*code, "const ", 6) == 0) {
        int is_const = strncmp(*code, "const ", 6) == 0;
        if (strncmp(*code, "const ", 6) == 0) *code += 6;
        else *code += 4;
        skip_whitespace(code);

        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* var_name = dup_range(start, *code);
        if (!var_name) return NULL;

        skip_whitespace(code);

        TypeDescriptor* type_desc = NULL;
        if (**code == ':') {
            (*code)++;
            type_desc = parse_type_descriptor(code);
            if (!type_desc) {
                free(var_name);
                return NULL;
            }
        } else {
            type_desc = type_descriptor_create_primitive(TYPE_INT);
            if (!type_desc) {
                free(var_name);
                return NULL;
            }
        }

        VarType type = type_desc->base_type;

        skip_whitespace(code);
        if (**code != '=') {
            free(var_name);
            return NULL;
        }
        (*code)++;
        skip_whitespace(code);

        ASTExpr* init = parse_expression_str_as_ast(code);
        if (!init) {
            free(var_name);
            return NULL;
        }

        ASTStmt* s = ast_stmt_new(AST_STMT_VAR_DECL);
        if (!s) {
            free(var_name);
            type_descriptor_free(type_desc);
            ast_free_expr(init);
            return NULL;
        }
        s->as.var_decl.var_name = var_name;
        s->as.var_decl.type = type;
        s->as.var_decl.type_desc = type_desc;
        s->as.var_decl.init = init;
        s->as.var_decl.is_const = is_const;
        return s;
    }

    if (strncmp(*code, "print(", 6) == 0) {
        *code += 6;
        ASTExpr* e = parse_expression_str_as_ast(code);
        if (**code == ')') (*code)++;

        const char* lookahead = *code;
        while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\r') {
            lookahead++;
        }
        if (*lookahead == '\n' || *lookahead == '\0' || *lookahead == '}' || *lookahead == ';') {
            *code = lookahead;
            if (**code == '\n' || **code == ';') (*code)++;
            ASTStmt* s = ast_stmt_new(AST_STMT_PRINT);
            if (!s) {
                ast_free_expr(e);
                return NULL;
            }
            s->as.print.expr = e;
            return s;
        }

        ast_free_expr(e);
        return NULL;
    }

    if (strncmp(*code, "if ", 3) == 0) {
        *code += 3;
        skip_whitespace(code);
        ASTExpr* cond = parse_expression_str_as_ast(code);
        if (!cond) return NULL;
        skip_whitespace(code);
        if (**code != '{') {
            ast_free_expr(cond);
            return NULL;
        }
        (*code)++;
        ASTStmtList* then_branch = parse_block(code);
        if (**code != '}') {
            ast_free_expr(cond);
            ast_free_stmt_list(then_branch);
            return NULL;
        }
        (*code)++;

        ASTStmtList* else_branch = NULL;
        skip_whitespace(code);
        if (strncmp(*code, "else ", 5) == 0) {
            *code += 5;
            skip_whitespace(code);
            if (**code != '{') {
                ast_free_expr(cond);
                ast_free_stmt_list(then_branch);
                return NULL;
            }
            (*code)++;
            else_branch = parse_block(code);
            if (**code != '}') {
                ast_free_expr(cond);
                ast_free_stmt_list(then_branch);
                ast_free_stmt_list(else_branch);
                return NULL;
            }
            (*code)++;
        }

        ASTStmt* s = ast_stmt_new(AST_STMT_IF);
        if (!s) {
            ast_free_expr(cond);
            ast_free_stmt_list(then_branch);
            if (else_branch) ast_free_stmt_list(else_branch);
            return NULL;
        }
        s->as.if_stmt.condition = cond;
        s->as.if_stmt.then_branch = then_branch;
        s->as.if_stmt.else_branch = else_branch;
        return s;
    }

    if (strncmp(*code, "while ", 6) == 0) {
        *code += 6;
        skip_whitespace(code);
        ASTExpr* cond = parse_expression_str_as_ast(code);
        if (!cond) return NULL;
        skip_whitespace(code);
        if (**code != '{') {
            ast_free_expr(cond);
            return NULL;
        }
        (*code)++;
        ASTStmtList* body = parse_block(code);
        if (**code != '}') {
            ast_free_expr(cond);
            ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_WHILE);
        if (!s) {
            ast_free_expr(cond);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.while_stmt.condition = cond;
        s->as.while_stmt.body = body;
        return s;
    }

    if (strncmp(*code, "for ", 4) == 0) {
        *code += 4;
        skip_whitespace(code);
        const char* start = *code;
        while (**code && **code != ' ' && **code != '\t') (*code)++;
        if (*code == start) return NULL;
        char* var_name = dup_range(start, *code);
        if (!var_name) return NULL;

        skip_whitespace(code);
        if (strncmp(*code, "in ", 3) != 0) {
            free(var_name);
            return NULL;
        }
        *code += 3;
        skip_whitespace(code);
        ASTExpr* range_expr = parse_expression_str_as_ast(code);
        if (!range_expr) {
            free(var_name);
            return NULL;
        }
        skip_whitespace(code);
        if (**code != '{') {
            free(var_name);
            ast_free_expr(range_expr);
            return NULL;
        }
        (*code)++;
        ASTStmtList* body = parse_block(code);
        if (**code != '}') {
            free(var_name);
            ast_free_expr(range_expr);
            ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_FOR_IN);
        if (!s) {
            free(var_name);
            ast_free_expr(range_expr);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.for_in_stmt.var_name = var_name;
        s->as.for_in_stmt.iterable = range_expr;
        s->as.for_in_stmt.body = body;
        return s;
    }

    if (strncmp(*code, "break", 5) == 0 && !isalnum((unsigned char)*(*code + 5))) {
        *code += 5;
        return ast_stmt_new(AST_STMT_BREAK);
    }

    if (strncmp(*code, "continue", 8) == 0 && !isalnum((unsigned char)*(*code + 8))) {
        *code += 8;
        return ast_stmt_new(AST_STMT_CONTINUE);
    }

    const char* start = *code;
    const char* scan = *code;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    int in_string = 0;
    int escape = 0;
    const char* eq_pos = NULL;

    while (*scan) {
        char c = *scan;
        if (in_string) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') in_string = 0;
            scan++;
            continue;
        }
        if (c == '"') { in_string = 1; scan++; continue; }
        if (c == '(') paren++;
        else if (c == ')') { if (paren > 0) paren--; }
        else if (c == '[') bracket++;
        else if (c == ']') { if (bracket > 0) bracket--; }
        else if (c == '{') brace++;
        else if (c == '}') {
            if (brace == 0) break;
            brace--;
        }

        if (paren == 0 && bracket == 0 && brace == 0) {
            if (c == '=') {
                eq_pos = scan;
                break;
            }
            if (c == '\n' || c == ';') {
                break;
            }
        }

        scan++;
    }

    if (eq_pos) {
        *code = eq_pos;
        size_t name_len = (size_t)(eq_pos - start);
        while (name_len > 0 && isspace((unsigned char)start[name_len - 1])) name_len--;
        while (name_len > 0 && isspace((unsigned char)*start)) { start++; name_len--; }

        char* var_name = dup_range(start, start + name_len);
        if (!var_name) return NULL;

        ASTExpr* lhs_index_target = NULL;
        ASTExpr* lhs_index_expr = NULL;
        if (strchr(var_name, '[')) {
            const char* lhs_src = var_name;
            const char* lhs_ptr = lhs_src;
            ASTExpr* lhs = parse_expression_str_as_ast(&lhs_ptr);
            if (lhs && lhs->kind == AST_EXPR_INDEX) {
                lhs_index_target = lhs->as.index.target;
                lhs_index_expr = lhs->as.index.index;
                lhs->as.index.target = NULL;
                lhs->as.index.index = NULL;
                ast_free_expr(lhs);
            } else if (lhs) {
                ast_free_expr(lhs);
            }
        }

        (*code)++;
        skip_whitespace(code);
        ASTExpr* rhs = parse_expression_str_as_ast(code);
        if (!rhs) {
            ast_free_expr(lhs_index_target);
            ast_free_expr(lhs_index_expr);
            free(var_name);
            return NULL;
        }

        if (lhs_index_target && lhs_index_expr) {
            ASTStmt* s = ast_stmt_new(AST_STMT_INDEX_ASSIGN);
            if (!s) {
                ast_free_expr(lhs_index_target);
                ast_free_expr(lhs_index_expr);
                ast_free_expr(rhs);
                free(var_name);
                return NULL;
            }
            s->as.index_assign.target = lhs_index_target;
            s->as.index_assign.index = lhs_index_expr;
            s->as.index_assign.value = rhs;
            free(var_name);
            return s;
        }

        ASTStmt* s = ast_stmt_new(AST_STMT_VAR_ASSIGN);
        if (!s) {
            free(var_name);
            ast_free_expr(rhs);
            return NULL;
        }
        s->as.var_assign.var_name = var_name;
        s->as.var_assign.value = rhs;
        return s;
    }

    ASTExpr* expr = parse_expression_str_as_ast(code);
    if (!expr) return NULL;

    ASTStmt* s = ast_stmt_new(AST_STMT_EXPR);
    if (!s) {
        ast_free_expr(expr);
        return NULL;
    }
    s->as.expr.expr = expr;
    return s;
}

// Utility functions
static char* dup_range(const char* start, const char* end) {
    size_t len = (size_t)(end - start);
    char* s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static void skip_whitespace(const char** code) {
    while (**code && isspace((unsigned char)**code)) (*code)++;
}