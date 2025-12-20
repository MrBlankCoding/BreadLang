#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../include/ast.h"
#include "../include/expr.h"
#include "../include/print.h"
#include "../include/var.h"
#include "../include/function.h"
#include "../include/value.h"
#include "../include/runtime.h"

#define MAX_TOKEN_LEN 1024

static int g_trace_enabled = 0;

void bread_set_trace(int enabled) {
    g_trace_enabled = enabled ? 1 : 0;
}

int bread_get_trace(void) {
    return g_trace_enabled;
}

static void skip_whitespace(const char** code);
static ASTStmt* parse_stmt(const char** code);
static ASTStmtList* parse_block(const char** code);

static ASTExpr* parse_expression(const char** expr);
static ASTExpr* parse_logical_or(const char** expr);
static ASTExpr* parse_logical_and(const char** expr);
static ASTExpr* parse_comparison(const char** expr);
static ASTExpr* parse_term(const char** expr);
static ASTExpr* parse_factor(const char** expr);
static ASTExpr* parse_unary(const char** expr);
static ASTExpr* parse_primary(const char** expr);
static ASTExpr* parse_postfix(const char** expr, ASTExpr* base);

static int is_ident_start(char c);
static int is_ident_char(char c);

static char* dup_range(const char* start, const char* end) {
    size_t len = (size_t)(end - start);
    char* s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static void* xmalloc(size_t n) {
    void* p = malloc(n);
    if (!p) {
        printf("Error: Out of memory\n");
    }
    return p;
}

static ASTStmtList* ast_stmt_list_new(void) {
    ASTStmtList* list = xmalloc(sizeof(ASTStmtList));
    if (!list) return NULL;
    list->head = NULL;
    list->tail = NULL;
    return list;
}

static void ast_stmt_list_add(ASTStmtList* list, ASTStmt* stmt) {
    if (!list || !stmt) return;
    stmt->next = NULL;
    if (list->tail) {
        list->tail->next = stmt;
        list->tail = stmt;
    } else {
        list->head = stmt;
        list->tail = stmt;
    }
}

static ASTExpr* ast_expr_new(ASTExprKind kind) {
    ASTExpr* e = xmalloc(sizeof(ASTExpr));
    if (!e) return NULL;
    memset(e, 0, sizeof(ASTExpr));
    e->kind = kind;
    e->tag.is_known = 0;
    e->tag.type = TYPE_NIL;
    return e;
}

static ASTStmt* ast_stmt_new(ASTStmtKind kind) {
    ASTStmt* s = xmalloc(sizeof(ASTStmt));
    if (!s) return NULL;
    memset(s, 0, sizeof(ASTStmt));
    s->kind = kind;
    return s;
}

static void ast_free_expr(ASTExpr* e);

static void ast_free_expr_list(ASTExpr** items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        ast_free_expr(items[i]);
    }
    free(items);
}

static void ast_free_expr(ASTExpr* e) {
    if (!e) return;

    switch (e->kind) {
        case AST_EXPR_STRING:
            free(e->as.string_val);
            break;
        case AST_EXPR_VAR:
            free(e->as.var_name);
            break;
        case AST_EXPR_BINARY:
            ast_free_expr(e->as.binary.left);
            ast_free_expr(e->as.binary.right);
            break;
        case AST_EXPR_UNARY:
            ast_free_expr(e->as.unary.operand);
            break;
        case AST_EXPR_CALL:
            free(e->as.call.name);
            ast_free_expr_list(e->as.call.args, e->as.call.arg_count);
            break;
        case AST_EXPR_ARRAY:
            ast_free_expr_list(e->as.array.items, e->as.array.item_count);
            break;
        case AST_EXPR_DICT:
            for (int i = 0; i < e->as.dict.entry_count; i++) {
                ast_free_expr(e->as.dict.entries[i].key);
                ast_free_expr(e->as.dict.entries[i].value);
            }
            free(e->as.dict.entries);
            break;
        case AST_EXPR_INDEX:
            ast_free_expr(e->as.index.target);
            ast_free_expr(e->as.index.index);
            break;
        case AST_EXPR_MEMBER:
            ast_free_expr(e->as.member.target);
            free(e->as.member.member);
            break;
        case AST_EXPR_METHOD_CALL:
            ast_free_expr(e->as.method_call.target);
            free(e->as.method_call.name);
            ast_free_expr_list(e->as.method_call.args, e->as.method_call.arg_count);
            break;
        default:
            break;
    }

    free(e);
}

void ast_free_stmt_list(ASTStmtList* stmts) {
    if (!stmts) return;
    ASTStmt* cur = stmts->head;
    while (cur) {
        ASTStmt* next = cur->next;
        switch (cur->kind) {
            case AST_STMT_VAR_DECL:
                free(cur->as.var_decl.var_name);
                free(cur->as.var_decl.type_str);
                ast_free_expr(cur->as.var_decl.init);
                break;
            case AST_STMT_VAR_ASSIGN:
                free(cur->as.var_assign.var_name);
                ast_free_expr(cur->as.var_assign.value);
                break;
            case AST_STMT_PRINT:
                ast_free_expr(cur->as.print.expr);
                break;
            case AST_STMT_EXPR:
                ast_free_expr(cur->as.expr.expr);
                break;
            case AST_STMT_IF:
                ast_free_expr(cur->as.if_stmt.condition);
                ast_free_stmt_list(cur->as.if_stmt.then_branch);
                if (cur->as.if_stmt.else_branch) ast_free_stmt_list(cur->as.if_stmt.else_branch);
                break;
            case AST_STMT_WHILE:
                ast_free_expr(cur->as.while_stmt.condition);
                ast_free_stmt_list(cur->as.while_stmt.body);
                break;
            case AST_STMT_FOR:
                free(cur->as.for_stmt.var_name);
                ast_free_expr(cur->as.for_stmt.range_expr);
                ast_free_stmt_list(cur->as.for_stmt.body);
                break;
            case AST_STMT_FUNC_DECL:
                free(cur->as.func_decl.name);
                for (int i = 0; i < cur->as.func_decl.param_count; i++) {
                    free(cur->as.func_decl.param_names[i]);
                }
                free(cur->as.func_decl.param_names);
                free(cur->as.func_decl.param_types);
                ast_free_stmt_list(cur->as.func_decl.body);
                break;
            case AST_STMT_RETURN:
                ast_free_expr(cur->as.ret.expr);
                break;
            default:
                break;
        }
        free(cur);
        cur = next;
    }
    free(stmts);
}

static int parse_type_string(const char** code, char* out_buf, size_t out_sz) {
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

static int parse_type_token(const char** code, VarType* out_type) {
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

static ASTExpr* parse_expression_str_as_ast(const char** code) {
    const char* start = *code;
    int paren_count = 0;
    int brace_count = 0;
    int bracket_count = 0;
    int in_string = 0;
    int escape = 0;

    while (**code) {
        if (in_string) {
            if (escape) {
                escape = 0;
            } else if (**code == '\\') {
                escape = 1;
            } else if (**code == '"') {
                in_string = 0;
            }
            (*code)++;
            continue;
        }

        if (**code == '"') {
            in_string = 1;
        } else if (paren_count == 0 && brace_count == 0 && bracket_count == 0 && **code == '{') {
            break;
        } else if (**code == '(') paren_count++;
        else if (**code == ')') {
            if (paren_count == 0) break;
            paren_count--;
        } else if (**code == '{') brace_count++;
        else if (**code == '}') {
            if (brace_count == 0) break;
            brace_count--;
        } else if (**code == '[') bracket_count++;
        else if (**code == ']') {
            if (bracket_count > 0) bracket_count--;
        } else if (paren_count == 0 && brace_count == 0 && bracket_count == 0 && (**code == '\n' || **code == ';' || **code == ',')) {
            break;
        }
        (*code)++;
    }

    const char* end = *code;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    while (start < end && isspace((unsigned char)*start)) start++;

    char* expr_src = dup_range(start, end);
    if (!expr_src) {
        printf("Error: Out of memory\n");
        return NULL;
    }

    const char* p = expr_src;
    ASTExpr* e = parse_expression(&p);
    if (!e) {
        free(expr_src);
        return NULL;
    }
    skip_whitespace(&p);
    if (*p != '\0') {
        ast_free_expr(e);
        free(expr_src);
        printf("Error: Unexpected characters in expression\n");
        return NULL;
    }

    free(expr_src);
    return e;
}

ASTStmtList* ast_parse_program(const char* code) {
    ASTStmtList* list = ast_stmt_list_new();
    if (!list) return NULL;

    const char* ptr = code;
    while (*ptr) {
        skip_whitespace(&ptr);

        if (*ptr == '\0') break;

        if (*ptr == '#') {
            while (*ptr && *ptr != '\n') ptr++;
            continue;
        }

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
            ast_free_stmt_list(list);
            return NULL;
        }
        ast_stmt_list_add(list, st);

        skip_whitespace(&ptr);
        if (*ptr == ';') ptr++;
    }

    return list;
}

static ASTStmt* parse_stmt(const char** code) {
    skip_whitespace(code);

    if (strncmp(*code, "func ", 5) == 0) {
        *code += 5;
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
        VarType* param_types = NULL;

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

                VarType p_type;
                if (!parse_type_token(code, &p_type)) {
                    free(p_name);
                    free(fn_name);
                    return NULL;
                }

                if (param_count >= param_cap) {
                    int new_cap = param_cap == 0 ? 4 : param_cap * 2;
                    char** new_names = malloc(sizeof(char*) * (size_t)new_cap);
                    VarType* new_types = malloc(sizeof(VarType) * (size_t)new_cap);
                    if (!new_names || !new_types) {
                        free(new_names);
                        free(new_types);
                        free(p_name);
                        free(fn_name);
                        for (int i = 0; i < param_count; i++) free(param_names[i]);
                        free(param_names);
                        free(param_types);
                        return NULL;
                    }
                    if (param_count > 0) {
                        memcpy(new_names, param_names, sizeof(char*) * (size_t)param_count);
                        memcpy(new_types, param_types, sizeof(VarType) * (size_t)param_count);
                    }
                    free(param_names);
                    free(param_types);
                    param_names = new_names;
                    param_types = new_types;
                    param_cap = new_cap;
                }
                param_names[param_count] = p_name;
                param_types[param_count] = p_type;
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
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            return NULL;
        }
        (*code)++;

        skip_whitespace(code);
        if (**code != '-' || *(*code + 1) != '>') {
            free(fn_name);
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            return NULL;
        }
        *code += 2;

        VarType ret_type;
        if (!parse_type_token(code, &ret_type)) {
            free(fn_name);
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            return NULL;
        }

        skip_whitespace(code);
        if (**code != '{') {
            free(fn_name);
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            return NULL;
        }
        (*code)++;

        ASTStmtList* body = parse_block(code);
        if (!body || **code != '}') {
            free(fn_name);
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            if (body) ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_FUNC_DECL);
        if (!s) {
            free(fn_name);
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.func_decl.name = fn_name;
        s->as.func_decl.param_count = param_count;
        s->as.func_decl.param_names = param_names;
        s->as.func_decl.param_types = param_types;
        s->as.func_decl.return_type = ret_type;
        s->as.func_decl.body = body;
        return s;
    }

    if (strncmp(*code, "return", 6) == 0 && !isalnum((unsigned char)*(*code + 6))) {
        *code += 6;
        skip_whitespace(code);
        ASTExpr* e = parse_expression_str_as_ast(code);
        ASTStmt* s = ast_stmt_new(AST_STMT_RETURN);
        if (!s) {
            ast_free_expr(e);
            return NULL;
        }
        s->as.ret.expr = e;
        return s;
    }

    if (strncmp(*code, "let ", 4) == 0 || strncmp(*code, "const ", 6) == 0) {
        int is_const = strncmp(*code, "const ", 6) == 0;
        *code += is_const ? 6 : 4;
        skip_whitespace(code);

        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* var_name = dup_range(start, *code);
        if (!var_name) return NULL;

        skip_whitespace(code);
        if (**code != ':') {
            free(var_name);
            return NULL;
        }
        (*code)++;
        skip_whitespace(code);

        char type_buf[MAX_TOKEN_LEN];
        if (!parse_type_string(code, type_buf, sizeof(type_buf))) {
            free(var_name);
            return NULL;
        }

        VarType type;
        size_t tlen = strlen(type_buf);
        if (tlen > 0 && type_buf[tlen - 1] == '?') {
            type = TYPE_OPTIONAL;
        } else if (strcmp(type_buf, "Int") == 0) type = TYPE_INT;
        else if (strcmp(type_buf, "String") == 0) type = TYPE_STRING;
        else if (strcmp(type_buf, "Bool") == 0) type = TYPE_BOOL;
        else if (strcmp(type_buf, "Float") == 0) type = TYPE_FLOAT;
        else if (strcmp(type_buf, "Double") == 0) type = TYPE_DOUBLE;
        else if (type_buf[0] == '[') {
            int depth = 0;
            const char* end = strrchr(type_buf, ']');
            if (!end) {
                free(var_name);
                return NULL;
            }
            int is_dict = 0;
            for (const char* p = type_buf + 1; p < end; p++) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                else if (*p == ':' && depth == 0) {
                    is_dict = 1;
                    break;
                }
            }
            type = is_dict ? TYPE_DICT : TYPE_ARRAY;
        } else {
            free(var_name);
            return NULL;
        }

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
            ast_free_expr(init);
            return NULL;
        }
        s->as.var_decl.var_name = var_name;
        s->as.var_decl.type = type;
        s->as.var_decl.type_str = strdup(type_buf);
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

        ASTStmt* s = ast_stmt_new(AST_STMT_FOR);
        if (!s) {
            free(var_name);
            ast_free_expr(range_expr);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.for_stmt.var_name = var_name;
        s->as.for_stmt.range_expr = range_expr;
        s->as.for_stmt.body = body;
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

        (*code)++;
        skip_whitespace(code);
        ASTExpr* rhs = parse_expression_str_as_ast(code);
        if (!rhs) {
            free(var_name);
            return NULL;
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

static ASTStmtList* parse_block(const char** code) {
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

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void skip_whitespace(const char** code) {
    while (**code && isspace((unsigned char)**code)) (*code)++;
}

static ASTExpr* parse_expression(const char** expr) {
    return parse_logical_or(expr);
}

static ASTExpr* parse_logical_or(const char** expr) {
    ASTExpr* left = parse_logical_and(expr);
    if (!left) return NULL;

    skip_whitespace(expr);
    while (**expr == '|' && *(*expr + 1) == '|') {
        *expr += 2;
        ASTExpr* right = parse_logical_and(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        ASTExpr* out = ast_expr_new(AST_EXPR_BINARY);
        if (!out) {
            ast_free_expr(left);
            ast_free_expr(right);
            return NULL;
        }
        out->as.binary.op = '|';
        out->as.binary.left = left;
        out->as.binary.right = right;
        left = out;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_logical_and(const char** expr) {
    ASTExpr* left = parse_comparison(expr);
    if (!left) return NULL;

    skip_whitespace(expr);
    while (**expr == '&' && *(*expr + 1) == '&') {
        *expr += 2;
        ASTExpr* right = parse_comparison(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        ASTExpr* out = ast_expr_new(AST_EXPR_BINARY);
        if (!out) {
            ast_free_expr(left);
            ast_free_expr(right);
            return NULL;
        }
        out->as.binary.op = '&';
        out->as.binary.left = left;
        out->as.binary.right = right;
        left = out;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_comparison(const char** expr) {
    ASTExpr* left = parse_term(expr);
    if (!left) return NULL;

    skip_whitespace(expr);
    if ((**expr == '=' && *(*expr + 1) == '=') ||
        (**expr == '!' && *(*expr + 1) == '=') ||
        (**expr == '<' && *(*expr + 1) == '=') ||
        (**expr == '>' && *(*expr + 1) == '=')) {
        char op = **expr;
        *expr += 2;
        ASTExpr* right = parse_term(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        ASTExpr* out = ast_expr_new(AST_EXPR_BINARY);
        if (!out) {
            ast_free_expr(left);
            ast_free_expr(right);
            return NULL;
        }
        out->as.binary.op = op;
        out->as.binary.left = left;
        out->as.binary.right = right;
        return out;
    }

    if (**expr == '<' || **expr == '>') {
        char op = **expr;
        (*expr)++;
        ASTExpr* right = parse_term(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        ASTExpr* out = ast_expr_new(AST_EXPR_BINARY);
        if (!out) {
            ast_free_expr(left);
            ast_free_expr(right);
            return NULL;
        }
        out->as.binary.op = op;
        out->as.binary.left = left;
        out->as.binary.right = right;
        return out;
    }

    return left;
}

static ASTExpr* parse_term(const char** expr) {
    ASTExpr* left = parse_factor(expr);
    if (!left) return NULL;

    skip_whitespace(expr);
    while (**expr == '+' || **expr == '-') {
        char op = **expr;
        (*expr)++;
        ASTExpr* right = parse_factor(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        ASTExpr* out = ast_expr_new(AST_EXPR_BINARY);
        if (!out) {
            ast_free_expr(left);
            ast_free_expr(right);
            return NULL;
        }
        out->as.binary.op = op;
        out->as.binary.left = left;
        out->as.binary.right = right;
        left = out;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_factor(const char** expr) {
    ASTExpr* left = parse_unary(expr);
    if (!left) return NULL;

    skip_whitespace(expr);
    while (**expr == '*' || **expr == '/' || **expr == '%') {
        char op = **expr;
        (*expr)++;
        ASTExpr* right = parse_unary(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        ASTExpr* out = ast_expr_new(AST_EXPR_BINARY);
        if (!out) {
            ast_free_expr(left);
            ast_free_expr(right);
            return NULL;
        }
        out->as.binary.op = op;
        out->as.binary.left = left;
        out->as.binary.right = right;
        left = out;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_unary(const char** expr) {
    skip_whitespace(expr);
    if (**expr == '!') {
        (*expr)++;
        ASTExpr* operand = parse_unary(expr);
        if (!operand) return NULL;
        ASTExpr* out = ast_expr_new(AST_EXPR_UNARY);
        if (!out) {
            ast_free_expr(operand);
            return NULL;
        }
        out->as.unary.op = '!';
        out->as.unary.operand = operand;
        return out;
    }

    ASTExpr* prim = parse_primary(expr);
    if (!prim) return NULL;
    return parse_postfix(expr, prim);
}

static ASTExpr* parse_primary(const char** expr) {
    skip_whitespace(expr);

    if (strncmp(*expr, "nil", 3) == 0 && !is_ident_char(*(*expr + 3))) {
        *expr += 3;
        return ast_expr_new(AST_EXPR_NIL);
    }

    if (strncmp(*expr, "true", 4) == 0 && !is_ident_char(*(*expr + 4))) {
        *expr += 4;
        ASTExpr* e = ast_expr_new(AST_EXPR_BOOL);
        if (!e) return NULL;
        e->as.bool_val = 1;
        e->tag.is_known = 1;
        e->tag.type = TYPE_BOOL;
        return e;
    }

    if (strncmp(*expr, "false", 5) == 0 && !is_ident_char(*(*expr + 5))) {
        *expr += 5;
        ASTExpr* e = ast_expr_new(AST_EXPR_BOOL);
        if (!e) return NULL;
        e->as.bool_val = 0;
        e->tag.is_known = 1;
        e->tag.type = TYPE_BOOL;
        return e;
    }

    if (**expr == '(') {
        (*expr)++;
        ASTExpr* inner = parse_expression(expr);
        if (!inner) return NULL;
        skip_whitespace(expr);
        if (**expr != ')') {
            ast_free_expr(inner);
            printf("Error: Missing closing parenthesis\n");
            return NULL;
        }
        (*expr)++;
        return inner;
    }

    if (**expr == '"') {
        (*expr)++;
        const char* start = *expr;
        while (**expr && **expr != '"') {
            if (**expr == '\\' && *(*expr + 1)) {
                (*expr)++;
            }
            (*expr)++;
        }
        if (**expr != '"') {
            printf("Error: Unterminated string literal\n");
            return NULL;
        }
        char* s = dup_range(start, *expr);
        if (!s) return NULL;
        (*expr)++;
        ASTExpr* e = ast_expr_new(AST_EXPR_STRING);
        if (!e) {
            free(s);
            return NULL;
        }
        e->as.string_val = s;
        e->tag.is_known = 1;
        e->tag.type = TYPE_STRING;
        return e;
    }

    const char* start = *expr;
    int has_dot = 0;
    while (**expr && (isdigit((unsigned char)**expr) || **expr == '.')) {
        if (**expr == '.') {
            if (has_dot) break;
            has_dot = 1;
        }
        (*expr)++;
    }

    if (*expr > start) {
        char num_str[MAX_TOKEN_LEN];
        size_t len = (size_t)(*expr - start);
        if (len >= sizeof(num_str)) {
            printf("Error: Number too long\n");
            return NULL;
        }
        memcpy(num_str, start, len);
        num_str[len] = '\0';

        if (has_dot) {
            double val = strtod(num_str, NULL);
            ASTExpr* e = ast_expr_new(AST_EXPR_DOUBLE);
            if (!e) return NULL;
            e->as.double_val = val;
            e->tag.is_known = 1;
            e->tag.type = TYPE_DOUBLE;
            return e;
        }

        int val = atoi(num_str);
        ASTExpr* e = ast_expr_new(AST_EXPR_INT);
        if (!e) return NULL;
        e->as.int_val = val;
        e->tag.is_known = 1;
        e->tag.type = TYPE_INT;
        return e;
    }

    if (**expr == '[') {
        (*expr)++;
        skip_whitespace(expr);

        if (**expr == ']') {
            (*expr)++;
            ASTExpr* e = ast_expr_new(AST_EXPR_ARRAY);
            if (!e) return NULL;
            e->as.array.item_count = 0;
            e->as.array.items = NULL;
            e->tag.is_known = 1;
            e->tag.type = TYPE_ARRAY;
            return e;
        }

        const char* look = *expr;
        int depth = 0;
        int in_string = 0;
        int esc = 0;
        int is_dict = 0;
        while (*look) {
            char c = *look;
            if (in_string) {
                if (esc) esc = 0;
                else if (c == '\\') esc = 1;
                else if (c == '"') in_string = 0;
                look++;
                continue;
            }
            if (c == '"') { in_string = 1; look++; continue; }
            if (c == '[') depth++;
            else if (c == ']') {
                if (depth == 0) break;
                depth--;
            } else if (c == ':' && depth == 0) {
                is_dict = 1;
                break;
            } else if (c == ',' && depth == 0) {
                break;
            }
            look++;
        }

        if (is_dict) {
            ASTExpr* e = ast_expr_new(AST_EXPR_DICT);
            if (!e) return NULL;
            e->tag.is_known = 1;
            e->tag.type = TYPE_DICT;

            int cap = 0;
            int count = 0;
            ASTDictEntry* entries = NULL;

            while (**expr) {
                skip_whitespace(expr);
                ASTExpr* key = parse_expression(expr);
                if (!key) {
                    for (int i = 0; i < count; i++) {
                        ast_free_expr(entries[i].key);
                        ast_free_expr(entries[i].value);
                    }
                    free(entries);
                    ast_free_expr(e);
                    return NULL;
                }

                skip_whitespace(expr);
                if (**expr != ':') {
                    ast_free_expr(key);
                    for (int i = 0; i < count; i++) {
                        ast_free_expr(entries[i].key);
                        ast_free_expr(entries[i].value);
                    }
                    free(entries);
                    ast_free_expr(e);
                    printf("Error: Expected ':' in dictionary literal\n");
                    return NULL;
                }
                (*expr)++;

                ASTExpr* val = parse_expression(expr);
                if (!val) {
                    ast_free_expr(key);
                    for (int i = 0; i < count; i++) {
                        ast_free_expr(entries[i].key);
                        ast_free_expr(entries[i].value);
                    }
                    free(entries);
                    ast_free_expr(e);
                    return NULL;
                }

                if (count >= cap) {
                    int new_cap = cap == 0 ? 4 : cap * 2;
                    ASTDictEntry* new_entries = realloc(entries, sizeof(ASTDictEntry) * (size_t)new_cap);
                    if (!new_entries) {
                        ast_free_expr(key);
                        ast_free_expr(val);
                        for (int i = 0; i < count; i++) {
                            ast_free_expr(entries[i].key);
                            ast_free_expr(entries[i].value);
                        }
                        free(entries);
                        ast_free_expr(e);
                        printf("Error: Out of memory\n");
                        return NULL;
                    }
                    entries = new_entries;
                    cap = new_cap;
                }

                entries[count].key = key;
                entries[count].value = val;
                count++;

                skip_whitespace(expr);
                if (**expr == ',') {
                    (*expr)++;
                    continue;
                }
                break;
            }

            skip_whitespace(expr);
            if (**expr != ']') {
                for (int i = 0; i < count; i++) {
                    ast_free_expr(entries[i].key);
                    ast_free_expr(entries[i].value);
                }
                free(entries);
                ast_free_expr(e);
                printf("Error: Missing closing ']' in dictionary literal\n");
                return NULL;
            }
            (*expr)++;

            e->as.dict.entry_count = count;
            e->as.dict.entries = entries;
            return e;
        }

        ASTExpr* e = ast_expr_new(AST_EXPR_ARRAY);
        if (!e) return NULL;
        e->tag.is_known = 1;
        e->tag.type = TYPE_ARRAY;

        int cap = 0;
        int count = 0;
        ASTExpr** items = NULL;

        while (**expr) {
            ASTExpr* item = parse_expression(expr);
            if (!item) {
                ast_free_expr_list(items, count);
                ast_free_expr(e);
                return NULL;
            }

            if (count >= cap) {
                int new_cap = cap == 0 ? 4 : cap * 2;
                ASTExpr** new_items = realloc(items, sizeof(ASTExpr*) * (size_t)new_cap);
                if (!new_items) {
                    ast_free_expr(item);
                    ast_free_expr_list(items, count);
                    ast_free_expr(e);
                    printf("Error: Out of memory\n");
                    return NULL;
                }
                items = new_items;
                cap = new_cap;
            }
            items[count++] = item;

            skip_whitespace(expr);
            if (**expr == ',') {
                (*expr)++;
                skip_whitespace(expr);
                continue;
            }
            break;
        }

        skip_whitespace(expr);
        if (**expr != ']') {
            ast_free_expr_list(items, count);
            ast_free_expr(e);
            printf("Error: Missing closing ']' in array literal\n");
            return NULL;
        }
        (*expr)++;

        e->as.array.item_count = count;
        e->as.array.items = items;
        return e;
    }

    if (is_ident_start(**expr)) {
        const char* start_id = *expr;
        (*expr)++;
        while (**expr && is_ident_char(**expr)) (*expr)++;
        char* name = dup_range(start_id, *expr);
        if (!name) return NULL;

        const char* after_ident = *expr;
        skip_whitespace(expr);
        if (**expr == '(') {
            (*expr)++;

            int cap = 0;
            int count = 0;
            ASTExpr** args = NULL;

            skip_whitespace(expr);
            if (**expr != ')') {
                while (**expr) {
                    ASTExpr* arg = parse_expression(expr);
                    if (!arg) {
                        ast_free_expr_list(args, count);
                        free(name);
                        return NULL;
                    }

                    if (count >= cap) {
                        int new_cap = cap == 0 ? 4 : cap * 2;
                        ASTExpr** new_args = realloc(args, sizeof(ASTExpr*) * (size_t)new_cap);
                        if (!new_args) {
                            ast_free_expr(arg);
                            ast_free_expr_list(args, count);
                            free(name);
                            printf("Error: Out of memory\n");
                            return NULL;
                        }
                        args = new_args;
                        cap = new_cap;
                    }
                    args[count++] = arg;

                    skip_whitespace(expr);
                    if (**expr == ',') {
                        (*expr)++;
                        skip_whitespace(expr);
                        continue;
                    }
                    break;
                }
            }

            skip_whitespace(expr);
            if (**expr != ')') {
                ast_free_expr_list(args, count);
                free(name);
                printf("Error: Missing closing parenthesis in function call\n");
                return NULL;
            }
            (*expr)++;

            ASTExpr* e = ast_expr_new(AST_EXPR_CALL);
            if (!e) {
                ast_free_expr_list(args, count);
                free(name);
                return NULL;
            }
            e->as.call.name = name;
            e->as.call.arg_count = count;
            e->as.call.args = args;
            return e;
        }

        *expr = after_ident;

        ASTExpr* e = ast_expr_new(AST_EXPR_VAR);
        if (!e) {
            free(name);
            return NULL;
        }
        e->as.var_name = name;
        return e;
    }

    printf("Error: Unexpected character '%c'\n", **expr);
    return NULL;
}

static ASTExpr* parse_postfix(const char** expr, ASTExpr* base) {
    while (1) {
        skip_whitespace(expr);

        if (**expr == '[') {
            (*expr)++;
            ASTExpr* idx = parse_expression(expr);
            if (!idx) {
                ast_free_expr(base);
                return NULL;
            }
            skip_whitespace(expr);
            if (**expr != ']') {
                ast_free_expr(idx);
                ast_free_expr(base);
                printf("Error: Missing closing ']' in indexing\n");
                return NULL;
            }
            (*expr)++;

            ASTExpr* out = ast_expr_new(AST_EXPR_INDEX);
            if (!out) {
                ast_free_expr(idx);
                ast_free_expr(base);
                return NULL;
            }
            out->as.index.target = base;
            out->as.index.index = idx;
            base = out;
            continue;
        }

        int is_optional_chain = 0;
        if (**expr == '?' && *(*expr + 1) == '.') {
            is_optional_chain = 1;
            *expr += 2;
        } else if (**expr == '.') {
            (*expr)++;
        } else {
            break;
        }

        skip_whitespace(expr);
        if (!is_ident_start(**expr)) {
            ast_free_expr(base);
            printf("Error: Expected member name after '.'\n");
            return NULL;
        }

        const char* mstart = *expr;
        (*expr)++;
        while (**expr && is_ident_char(**expr)) (*expr)++;
        char* member = dup_range(mstart, *expr);
        if (!member) {
            ast_free_expr(base);
            return NULL;
        }

        skip_whitespace(expr);
        if (**expr == '(') {
            (*expr)++;

            int cap = 0;
            int count = 0;
            ASTExpr** args = NULL;

            skip_whitespace(expr);
            if (**expr != ')') {
                while (**expr) {
                    ASTExpr* arg = parse_expression(expr);
                    if (!arg) {
                        ast_free_expr_list(args, count);
                        free(member);
                        ast_free_expr(base);
                        return NULL;
                    }

                    if (count >= cap) {
                        int new_cap = cap == 0 ? 4 : cap * 2;
                        ASTExpr** new_args = realloc(args, sizeof(ASTExpr*) * (size_t)new_cap);
                        if (!new_args) {
                            ast_free_expr(arg);
                            ast_free_expr_list(args, count);
                            free(member);
                            ast_free_expr(base);
                            printf("Error: Out of memory\n");
                            return NULL;
                        }
                        args = new_args;
                        cap = new_cap;
                    }
                    args[count++] = arg;

                    skip_whitespace(expr);
                    if (**expr == ',') {
                        (*expr)++;
                        skip_whitespace(expr);
                        continue;
                    }
                    break;
                }
            }

            skip_whitespace(expr);
            if (**expr != ')') {
                ast_free_expr_list(args, count);
                free(member);
                ast_free_expr(base);
                printf("Error: Missing ')' in method call\n");
                return NULL;
            }
            (*expr)++;

            ASTExpr* out = ast_expr_new(AST_EXPR_METHOD_CALL);
            if (!out) {
                ast_free_expr_list(args, count);
                free(member);
                ast_free_expr(base);
                return NULL;
            }
            out->as.method_call.target = base;
            out->as.method_call.name = member;
            out->as.method_call.arg_count = count;
            out->as.method_call.args = args;
            out->as.method_call.is_optional_chain = is_optional_chain;
            base = out;
            continue;
        }

        ASTExpr* out = ast_expr_new(AST_EXPR_MEMBER);
        if (!out) {
            free(member);
            ast_free_expr(base);
            return NULL;
        }
        out->as.member.target = base;
        out->as.member.member = member;
        out->as.member.is_optional_chain = is_optional_chain;
        base = out;
    }

    return base;
}

static ExprResult ast_eval_expr(ASTExpr* e);

static void ast_release_expr_result(ExprResult* r) {
    if (!r || r->is_error) return;
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = r->type;
    v.value = r->value;
    bread_value_release(&v);
    memset(&r->value, 0, sizeof(r->value));
    r->type = TYPE_NIL;
}

static ExprResult ast_error_result(void) {
    ExprResult r;
    memset(&r, 0, sizeof(r));
    r.is_error = 1;
    return r;
}

static ExprResult ast_eval_expr(ASTExpr* e) {
    if (!e) return ast_error_result();

    switch (e->kind) {
        case AST_EXPR_NIL: {
            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_NIL;
            return r;
        }
        case AST_EXPR_BOOL: {
            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_BOOL;
            r.value.bool_val = e->as.bool_val;
            return r;
        }
        case AST_EXPR_INT: {
            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_INT;
            r.value.int_val = e->as.int_val;
            return r;
        }
        case AST_EXPR_DOUBLE: {
            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_DOUBLE;
            r.value.double_val = e->as.double_val;
            return r;
        }
        case AST_EXPR_STRING: {
            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_STRING;
            r.value.string_val = bread_string_new(e->as.string_val ? e->as.string_val : "");
            if (!r.value.string_val) return ast_error_result();
            return r;
        }
        case AST_EXPR_VAR: {
            Variable* var = get_variable(e->as.var_name);
            if (!var) {
                printf("Error: Unknown variable '%s'\n", e->as.var_name ? e->as.var_name : "");
                return ast_error_result();
            }
            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = var->type;
            switch (var->type) {
                case TYPE_STRING:
                    r.value.string_val = var->value.string_val;
                    bread_string_retain(r.value.string_val);
                    break;
                case TYPE_ARRAY:
                    r.value.array_val = var->value.array_val;
                    bread_array_retain(r.value.array_val);
                    break;
                case TYPE_DICT:
                    r.value.dict_val = var->value.dict_val;
                    bread_dict_retain(r.value.dict_val);
                    break;
                case TYPE_OPTIONAL:
                    r.value.optional_val = var->value.optional_val;
                    bread_optional_retain(r.value.optional_val);
                    break;
                default:
                    r.value = var->value;
                    break;
            }
            return r;
        }
        case AST_EXPR_UNARY: {
            ExprResult opnd = ast_eval_expr(e->as.unary.operand);
            if (opnd.is_error) return opnd;
            ExprResult out = evaluate_unary_op(opnd, e->as.unary.op);
            return out;
        }
        case AST_EXPR_BINARY: {
            ExprResult left = ast_eval_expr(e->as.binary.left);
            if (left.is_error) return left;
            ExprResult right = ast_eval_expr(e->as.binary.right);
            if (right.is_error) {
                ast_release_expr_result(&left);
                return right;
            }
            ExprResult out = evaluate_binary_op(left, right, e->as.binary.op);
            return out;
        }
         case AST_EXPR_CALL: {
            ExprResult* arg_vals = NULL;
            if (e->as.call.arg_count > 0) {
                arg_vals = malloc(sizeof(ExprResult) * (size_t)e->as.call.arg_count);
                if (!arg_vals) {
                    printf("Error: Out of memory\n");
                    return ast_error_result();
                }
            }

            for (int i = 0; i < e->as.call.arg_count; i++) {
                arg_vals[i] = ast_eval_expr(e->as.call.args[i]);
                if (arg_vals[i].is_error) {
                    ExprResult err = arg_vals[i];
                    for (int j = 0; j < i; j++) {
                        ast_release_expr_result(&arg_vals[j]);
                    }
                    free(arg_vals);
                    return err;
                }
            }

            if (e->as.call.name && strcmp(e->as.call.name, "range") == 0) {
                if (e->as.call.arg_count != 1) {
                    for (int i = 0; i < e->as.call.arg_count; i++) {
                        ast_release_expr_result(&arg_vals[i]);
                    }
                    free(arg_vals);
                    printf("Error: Function 'range' expected 1 args but got %d\n", e->as.call.arg_count);
                    return ast_error_result();
                }
                if (arg_vals[0].type != TYPE_INT) {
                    ast_release_expr_result(&arg_vals[0]);
                    free(arg_vals);
                    printf("Error: range() expects Int\n");
                    return ast_error_result();
                }
                ExprResult out;
                memset(&out, 0, sizeof(out));
                out.is_error = 0;
                out.type = TYPE_INT;
                out.value.int_val = arg_vals[0].value.int_val;
                ast_release_expr_result(&arg_vals[0]);
                free(arg_vals);
                return out;
            }

            ExprResult out = call_function_values(e->as.call.name, e->as.call.arg_count, arg_vals);

            for (int i = 0; i < e->as.call.arg_count; i++) {
                ast_release_expr_result(&arg_vals[i]);
            }
            free(arg_vals);
            return out;
         }
        case AST_EXPR_ARRAY: {
            BreadArray* a = bread_array_new();
            if (!a) {
                printf("Error: Out of memory\n");
                return ast_error_result();
            }

            for (int i = 0; i < e->as.array.item_count; i++) {
                ExprResult item = ast_eval_expr(e->as.array.items[i]);
                if (item.is_error) {
                    bread_array_release(a);
                    return item;
                }
                BreadValue v = bread_value_from_expr_result(item);
                if (!bread_array_append(a, v)) {
                    BreadValue tmp = bread_value_from_expr_result(item);
                    bread_value_release(&tmp);
                    bread_array_release(a);
                    printf("Error: Out of memory\n");
                    return ast_error_result();
                }
                BreadValue tmp = bread_value_from_expr_result(item);
                bread_value_release(&tmp);
            }

            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_ARRAY;
            r.value.array_val = a;
            return r;
        }
        case AST_EXPR_DICT: {
            BreadDict* d = bread_dict_new();
            if (!d) {
                printf("Error: Out of memory\n");
                return ast_error_result();
            }

            for (int i = 0; i < e->as.dict.entry_count; i++) {
                ExprResult key_r = ast_eval_expr(e->as.dict.entries[i].key);
                if (key_r.is_error) {
                    bread_dict_release(d);
                    return key_r;
                }
                if (key_r.type != TYPE_STRING) {
                    BreadValue tmp = bread_value_from_expr_result(key_r);
                    bread_value_release(&tmp);
                    bread_dict_release(d);
                    printf("Error: Dictionary keys must be strings\n");
                    return ast_error_result();
                }

                ExprResult val_r = ast_eval_expr(e->as.dict.entries[i].value);
                if (val_r.is_error) {
                    BreadValue tmp = bread_value_from_expr_result(key_r);
                    bread_value_release(&tmp);
                    bread_dict_release(d);
                    return val_r;
                }

                BreadValue vv = bread_value_from_expr_result(val_r);
                if (!bread_dict_set(d, bread_string_cstr(key_r.value.string_val), vv)) {
                    BreadValue kt = bread_value_from_expr_result(key_r);
                    bread_value_release(&kt);
                    BreadValue vt = bread_value_from_expr_result(val_r);
                    bread_value_release(&vt);
                    bread_dict_release(d);
                    printf("Error: Out of memory\n");
                    return ast_error_result();
                }

                BreadValue kt = bread_value_from_expr_result(key_r);
                bread_value_release(&kt);
                BreadValue vt = bread_value_from_expr_result(val_r);
                bread_value_release(&vt);
            }

            ExprResult r;
            memset(&r, 0, sizeof(r));
            r.is_error = 0;
            r.type = TYPE_DICT;
            r.value.dict_val = d;
            return r;
        }
        case AST_EXPR_INDEX: {
            ExprResult target = ast_eval_expr(e->as.index.target);
            if (target.is_error) return target;
            ExprResult idx = ast_eval_expr(e->as.index.index);
            if (idx.is_error) {
                ast_release_expr_result(&target);
                return idx;
            }

            ExprResult out;
            memset(&out, 0, sizeof(out));
            out.is_error = 0;
            out.type = TYPE_NIL;

            ExprResult real_target = target;
            int target_owned = 0;

            if (real_target.type == TYPE_OPTIONAL) {
                BreadOptional* o = real_target.value.optional_val;
                if (!o || !o->is_some) {
                    ast_release_expr_result(&idx);
                    ast_release_expr_result(&real_target);
                    return out;
                }
                BreadValue inner = bread_value_clone(o->value);
                ast_release_expr_result(&real_target);
                real_target = bread_expr_result_from_value(inner);
                target_owned = 1;
            }

            if (real_target.type == TYPE_ARRAY) {
                if (idx.type != TYPE_INT) {
                    ast_release_expr_result(&idx);
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    printf("Error: Array index must be Int\n");
                    return ast_error_result();
                }
                BreadValue* at = bread_array_get(real_target.value.array_val, idx.value.int_val);
                if (at) {
                    BreadValue cloned = bread_value_clone(*at);
                    out = bread_expr_result_from_value(cloned);
                }
            } else if (real_target.type == TYPE_DICT) {
                if (idx.type != TYPE_STRING) {
                    ast_release_expr_result(&idx);
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    printf("Error: Dictionary key must be String\n");
                    return ast_error_result();
                }
                BreadValue* v = bread_dict_get(real_target.value.dict_val, bread_string_cstr(idx.value.string_val));
                if (v) {
                    BreadValue cloned = bread_value_clone(*v);
                    out = bread_expr_result_from_value(cloned);
                }
            } else {
                ast_release_expr_result(&idx);
                if (target_owned) ast_release_expr_result(&real_target);
                else ast_release_expr_result(&target);
                printf("Error: Type does not support indexing\n");
                return ast_error_result();
            }

            ast_release_expr_result(&idx);
            if (target_owned) ast_release_expr_result(&real_target);
            else ast_release_expr_result(&target);
            return out;
        }
        case AST_EXPR_MEMBER:
        case AST_EXPR_METHOD_CALL: {
            ASTExpr* target_ast = (e->kind == AST_EXPR_MEMBER) ? e->as.member.target : e->as.method_call.target;
            int is_opt = (e->kind == AST_EXPR_MEMBER) ? e->as.member.is_optional_chain : e->as.method_call.is_optional_chain;
            const char* name = (e->kind == AST_EXPR_MEMBER) ? e->as.member.member : e->as.method_call.name;

            ExprResult target = ast_eval_expr(target_ast);
            if (target.is_error) return target;

            ExprResult real_target = target;
            int target_owned = 0;

            if (is_opt) {
                if (real_target.type == TYPE_NIL) {
                    ast_release_expr_result(&real_target);
                    ExprResult nil_r;
                    memset(&nil_r, 0, sizeof(nil_r));
                    nil_r.is_error = 0;
                    nil_r.type = TYPE_NIL;
                    return nil_r;
                }
                if (real_target.type == TYPE_OPTIONAL) {
                    BreadOptional* o = real_target.value.optional_val;
                    if (!o || !o->is_some) {
                        ast_release_expr_result(&real_target);
                        ExprResult nil_r;
                        memset(&nil_r, 0, sizeof(nil_r));
                        nil_r.is_error = 0;
                        nil_r.type = TYPE_NIL;
                        return nil_r;
                    }
                    BreadValue inner = bread_value_clone(o->value);
                    ast_release_expr_result(&real_target);
                    real_target = bread_expr_result_from_value(inner);
                    target_owned = 1;
                }
            }

            if (e->kind == AST_EXPR_METHOD_CALL && strcmp(name, "append") == 0) {
                if (real_target.type != TYPE_ARRAY) {
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    printf("Error: append() is only supported on arrays\n");
                    return ast_error_result();
                }
                if (e->as.method_call.arg_count != 1) {
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    printf("Error: append() expects 1 argument\n");
                    return ast_error_result();
                }

                ExprResult arg = ast_eval_expr(e->as.method_call.args[0]);
                if (arg.is_error) {
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    return arg;
                }

                BreadValue av = bread_value_from_expr_result(arg);
                if (!bread_array_append(real_target.value.array_val, av)) {
                    BreadValue tmp = bread_value_from_expr_result(arg);
                    bread_value_release(&tmp);
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    printf("Error: Out of memory\n");
                    return ast_error_result();
                }

                BreadValue tmp = bread_value_from_expr_result(arg);
                bread_value_release(&tmp);
                if (target_owned) ast_release_expr_result(&real_target);
                else ast_release_expr_result(&target);

                ExprResult nil_r;
                memset(&nil_r, 0, sizeof(nil_r));
                nil_r.is_error = 0;
                nil_r.type = TYPE_NIL;
                return nil_r;
            }

            if (e->kind == AST_EXPR_MEMBER && strcmp(name, "length") == 0) {
                ExprResult out;
                memset(&out, 0, sizeof(out));
                out.is_error = 0;
                out.type = TYPE_INT;
                if (real_target.type == TYPE_ARRAY) out.value.int_val = real_target.value.array_val ? real_target.value.array_val->count : 0;
                else if (real_target.type == TYPE_DICT) out.value.int_val = real_target.value.dict_val ? real_target.value.dict_val->count : 0;
                else {
                    if (target_owned) ast_release_expr_result(&real_target);
                    else ast_release_expr_result(&target);
                    printf("Error: length is only supported on arrays and dictionaries\n");
                    return ast_error_result();
                }

                if (target_owned) ast_release_expr_result(&real_target);
                else ast_release_expr_result(&target);
                return out;
            }

            if (e->kind == AST_EXPR_MEMBER && real_target.type == TYPE_DICT) {
                BreadValue* v = bread_dict_get(real_target.value.dict_val, name ? name : "");
                ExprResult out;
                memset(&out, 0, sizeof(out));
                out.is_error = 0;
                out.type = TYPE_NIL;
                if (v) {
                    BreadValue cloned = bread_value_clone(*v);
                    out = bread_expr_result_from_value(cloned);
                }
                if (target_owned) ast_release_expr_result(&real_target);
                else ast_release_expr_result(&target);
                return out;
            }

            if (is_opt) {
                if (target_owned) ast_release_expr_result(&real_target);
                else ast_release_expr_result(&target);
                ExprResult nil_r;
                memset(&nil_r, 0, sizeof(nil_r));
                nil_r.is_error = 0;
                nil_r.type = TYPE_NIL;
                return nil_r;
            }

            if (target_owned) ast_release_expr_result(&real_target);
            else ast_release_expr_result(&target);

            printf("Error: Unsupported member access\n");
            return ast_error_result();
        }
        default:
            break;
    }

    return ast_error_result();
}

ASTExecSignal ast_execute_stmt_list(ASTStmtList* stmts, ExprResult* out_return) {
    if (!stmts) return AST_EXEC_SIGNAL_NONE;

    ASTStmt* cur = stmts->head;
    while (cur) {
        if (g_trace_enabled) {
            const char* name = "<stmt>";
            switch (cur->kind) {
                case AST_STMT_VAR_DECL: name = "var_decl"; break;
                case AST_STMT_VAR_ASSIGN: name = "var_assign"; break;
                case AST_STMT_PRINT: name = "print"; break;
                case AST_STMT_EXPR: name = "expr"; break;
                case AST_STMT_IF: name = "if"; break;
                case AST_STMT_WHILE: name = "while"; break;
                case AST_STMT_FOR: name = "for"; break;
                case AST_STMT_BREAK: name = "break"; break;
                case AST_STMT_CONTINUE: name = "continue"; break;
                case AST_STMT_FUNC_DECL: name = "func_decl"; break;
                case AST_STMT_RETURN: name = "return"; break;
                default: break;
            }
            fprintf(stderr, "trace: %s\n", name);
        }

        switch (cur->kind) {
            case AST_STMT_VAR_DECL: {
                ExprResult init = ast_eval_expr(cur->as.var_decl.init);
                if (init.is_error) return AST_EXEC_SIGNAL_NONE;

                VarValue initial;
                memset(&initial, 0, sizeof(initial));
                if (!declare_variable_raw(cur->as.var_decl.var_name, cur->as.var_decl.type, initial, cur->as.var_decl.is_const)) {
                    ast_release_expr_result(&init);
                    return AST_EXEC_SIGNAL_NONE;
                }

                (void)bread_init_variable_from_expr_result(cur->as.var_decl.var_name, &init);

                ast_release_expr_result(&init);
                break;
            }
            case AST_STMT_VAR_ASSIGN: {
                ExprResult rhs = ast_eval_expr(cur->as.var_assign.value);
                if (rhs.is_error) return AST_EXEC_SIGNAL_NONE;

                (void)bread_assign_variable_from_expr_result(cur->as.var_assign.var_name, &rhs);

                 ast_release_expr_result(&rhs);
                 break;
             }
            case AST_STMT_PRINT: {
                ExprResult v = ast_eval_expr(cur->as.print.expr);
                if (!v.is_error) {
                    switch (v.type) {
                        case TYPE_STRING:
                            printf("%s\n", v.value.string_val ? v.value.string_val : "");
                            break;
                        case TYPE_INT:
                            printf("%d\n", v.value.int_val);
                            break;
                        case TYPE_BOOL:
                            printf("%s\n", v.value.bool_val ? "true" : "false");
                            break;
                        case TYPE_FLOAT:
                            printf("%f\n", v.value.float_val);
                            break;
                        case TYPE_DOUBLE:
                            printf("%lf\n", v.value.double_val);
                            break;
                        case TYPE_NIL:
                            printf("nil\n");
                            break;
                        case TYPE_OPTIONAL: {
                            BreadOptional* o = v.value.optional_val;
                            if (!o || !o->is_some) {
                                printf("nil\n");
                            } else {
                                ExprResult inner = bread_expr_result_from_value(bread_value_clone(o->value));
                                switch (inner.type) {
                                    case TYPE_STRING:
                                        printf("%s\n", inner.value.string_val ? inner.value.string_val : "");
                                        break;
                                    case TYPE_INT:
                                        printf("%d\n", inner.value.int_val);
                                        break;
                                    case TYPE_BOOL:
                                        printf("%s\n", inner.value.bool_val ? "true" : "false");
                                        break;
                                    case TYPE_FLOAT:
                                        printf("%f\n", inner.value.float_val);
                                        break;
                                    case TYPE_DOUBLE:
                                        printf("%lf\n", inner.value.double_val);
                                        break;
                                    case TYPE_NIL:
                                        printf("nil\n");
                                        break;
                                    default:
                                        printf("nil\n");
                                        break;
                                }
                                BreadValue tmp = bread_value_from_expr_result(inner);
                                bread_value_release(&tmp);
                            }
                            break;
                        }
                        case TYPE_ARRAY: {
                            BreadArray* a = v.value.array_val;
                            printf("[");
                            int n = a ? a->count : 0;
                            for (int i = 0; i < n; i++) {
                                if (i > 0) printf(", ");
                                BreadValue item = bread_value_clone(a->items[i]);
                                ExprResult inner = bread_expr_result_from_value(item);
                                switch (inner.type) {
                                    case TYPE_STRING:
                                        printf("\"%s\"", inner.value.string_val ? inner.value.string_val : "");
                                        break;
                                    case TYPE_INT:
                                        printf("%d", inner.value.int_val);
                                        break;
                                    case TYPE_BOOL:
                                        printf("%s", inner.value.bool_val ? "true" : "false");
                                        break;
                                    case TYPE_FLOAT:
                                        printf("%f", inner.value.float_val);
                                        break;
                                    case TYPE_DOUBLE:
                                        printf("%lf", inner.value.double_val);
                                        break;
                                    case TYPE_NIL:
                                        printf("nil");
                                        break;
                                    default:
                                        printf("nil");
                                        break;
                                }
                                BreadValue tmp = bread_value_from_expr_result(inner);
                                bread_value_release(&tmp);
                            }
                            printf("]\n");
                            break;
                        }
                        case TYPE_DICT: {
                            BreadDict* d = v.value.dict_val;
                            printf("[");
                            int n = d ? d->count : 0;
                            for (int i = 0; i < n; i++) {
                                if (i > 0) printf(", ");
                                printf("\"%s\": ", d->entries[i].key ? d->entries[i].key : "");
                                BreadValue item = bread_value_clone(d->entries[i].value);
                                ExprResult inner = bread_expr_result_from_value(item);
                                switch (inner.type) {
                                    case TYPE_STRING:
                                        printf("\"%s\"", inner.value.string_val ? inner.value.string_val : "");
                                        break;
                                    case TYPE_INT:
                                        printf("%d", inner.value.int_val);
                                        break;
                                    case TYPE_BOOL:
                                        printf("%s", inner.value.bool_val ? "true" : "false");
                                        break;
                                    case TYPE_FLOAT:
                                        printf("%f", inner.value.float_val);
                                        break;
                                    case TYPE_DOUBLE:
                                        printf("%lf", inner.value.double_val);
                                        break;
                                    case TYPE_NIL:
                                        printf("nil");
                                        break;
                                    default:
                                        printf("nil");
                                        break;
                                }
                                BreadValue tmp = bread_value_from_expr_result(inner);
                                bread_value_release(&tmp);
                            }
                            printf("]\n");
                            break;
                        }
                        default:
                            printf("Error: Unsupported type for print\n");
                            break;
                    }
                    BreadValue tmp = bread_value_from_expr_result(v);
                    bread_value_release(&tmp);
                }
                break;
            }
            case AST_STMT_EXPR: {
                ExprResult v = ast_eval_expr(cur->as.expr.expr);
                if (!v.is_error) {
                    BreadValue tmp = bread_value_from_expr_result(v);
                    bread_value_release(&tmp);
                }
                break;
            }
            case AST_STMT_IF: {
                ExprResult cond = ast_eval_expr(cur->as.if_stmt.condition);
                if (!cond.is_error && cond.type == TYPE_BOOL && cond.value.bool_val) {
                    ASTExecSignal sig = ast_execute_stmt_list(cur->as.if_stmt.then_branch, out_return);
                    BreadValue tmp = bread_value_from_expr_result(cond);
                    bread_value_release(&tmp);
                    if (sig != AST_EXEC_SIGNAL_NONE) return sig;
                } else {
                    BreadValue tmp = bread_value_from_expr_result(cond);
                    bread_value_release(&tmp);
                    if (cur->as.if_stmt.else_branch) {
                        ASTExecSignal sig = ast_execute_stmt_list(cur->as.if_stmt.else_branch, out_return);
                        if (sig != AST_EXEC_SIGNAL_NONE) return sig;
                    }
                }
                break;
            }
            case AST_STMT_WHILE: {
                while (1) {
                    ExprResult cond = ast_eval_expr(cur->as.while_stmt.condition);
                    int ok = (!cond.is_error && cond.type == TYPE_BOOL && cond.value.bool_val);
                    BreadValue tmp = bread_value_from_expr_result(cond);
                    bread_value_release(&tmp);
                    if (!ok) break;

                    ASTExecSignal sig = ast_execute_stmt_list(cur->as.while_stmt.body, out_return);
                    if (sig == AST_EXEC_SIGNAL_BREAK) break;
                    if (sig == AST_EXEC_SIGNAL_CONTINUE) continue;
                    if (sig == AST_EXEC_SIGNAL_RETURN) return sig;
                }
                break;
            }
            case AST_STMT_FOR: {
                if (cur->as.for_stmt.range_expr && cur->as.for_stmt.range_expr->kind == AST_EXPR_CALL &&
                    strcmp(cur->as.for_stmt.range_expr->as.call.name, "range") == 0 &&
                    cur->as.for_stmt.range_expr->as.call.arg_count == 1) {
                    ExprResult lim = ast_eval_expr(cur->as.for_stmt.range_expr->as.call.args[0]);
                    if (!lim.is_error && lim.type == TYPE_INT) {
                        if (!get_variable(cur->as.for_stmt.var_name)) {
                            char decl_line[1024];
                            snprintf(decl_line, sizeof(decl_line), "let %s: Int = 0", cur->as.for_stmt.var_name);
                            execute_variable_declaration(decl_line);
                        }
                        for (int i = 0; i < lim.value.int_val; i++) {
                            char assign_line[1024];
                            snprintf(assign_line, sizeof(assign_line), "%s = %d", cur->as.for_stmt.var_name, i);
                            execute_variable_assignment(assign_line);
                            ASTExecSignal sig = ast_execute_stmt_list(cur->as.for_stmt.body, out_return);
                            if (sig == AST_EXEC_SIGNAL_BREAK) break;
                            if (sig == AST_EXEC_SIGNAL_CONTINUE) continue;
                            if (sig == AST_EXEC_SIGNAL_RETURN) return sig;
                        }
                    }
                    BreadValue tmp = bread_value_from_expr_result(lim);
                    bread_value_release(&tmp);
                }
                break;
            }
            case AST_STMT_BREAK:
                return AST_EXEC_SIGNAL_BREAK;
            case AST_STMT_CONTINUE:
                return AST_EXEC_SIGNAL_CONTINUE;
            case AST_STMT_FUNC_DECL: {
                Function fn;
                memset(&fn, 0, sizeof(fn));
                fn.name = cur->as.func_decl.name;
                fn.param_count = cur->as.func_decl.param_count;
                fn.param_names = cur->as.func_decl.param_names;
                fn.param_types = cur->as.func_decl.param_types;
                fn.return_type = cur->as.func_decl.return_type;
                fn.body = (void*)cur->as.func_decl.body;
                fn.body_is_ast = 1;
                (void)register_function(&fn);
                break;
            }
            case AST_STMT_RETURN: {
                if (!out_return) {
                    printf("Error: 'return' used outside of function\n");
                    return AST_EXEC_SIGNAL_RETURN;
                }
                ExprResult v = ast_eval_expr(cur->as.ret.expr);
                *out_return = v;
                return AST_EXEC_SIGNAL_RETURN;
            }
            default:
                break;
        }

        cur = cur->next;
    }

    return AST_EXEC_SIGNAL_NONE;
}

void ast_runtime_init(void) {
    g_trace_enabled = bread_get_trace();
}

void ast_runtime_cleanup(void) {
    (void)0;
}

static void dump_indent(FILE* out, int indent) {
    if (!out) return;
    for (int i = 0; i < indent; i++) fputc(' ', out);
}

static void ast_dump_expr(const ASTExpr* e, FILE* out);

static void ast_dump_expr(const ASTExpr* e, FILE* out) {
    if (!out) return;
    if (!e) {
        fprintf(out, "<null>");
        return;
    }

    switch (e->kind) {
        case AST_EXPR_NIL:
            fprintf(out, "nil");
            break;
        case AST_EXPR_BOOL:
            fprintf(out, e->as.bool_val ? "true" : "false");
            break;
        case AST_EXPR_INT:
            fprintf(out, "%d", e->as.int_val);
            break;
        case AST_EXPR_DOUBLE:
            fprintf(out, "%lf", e->as.double_val);
            break;
        case AST_EXPR_STRING:
            fprintf(out, "\"%s\"", e->as.string_val ? e->as.string_val : "");
            break;
        case AST_EXPR_VAR:
            fprintf(out, "%s", e->as.var_name ? e->as.var_name : "");
            break;
        case AST_EXPR_BINARY:
            fprintf(out, "(");
            ast_dump_expr(e->as.binary.left, out);
            fprintf(out, " %c ", e->as.binary.op);
            ast_dump_expr(e->as.binary.right, out);
            fprintf(out, ")");
            break;
        case AST_EXPR_UNARY:
            fprintf(out, "(%c", e->as.unary.op);
            ast_dump_expr(e->as.unary.operand, out);
            fprintf(out, ")");
            break;
        case AST_EXPR_CALL:
            fprintf(out, "%s(", e->as.call.name ? e->as.call.name : "");
            for (int i = 0; i < e->as.call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.call.args[i], out);
            }
            fprintf(out, ")");
            break;
        case AST_EXPR_ARRAY:
            fprintf(out, "[");
            for (int i = 0; i < e->as.array.item_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.array.items[i], out);
            }
            fprintf(out, "]");
            break;
        case AST_EXPR_DICT:
            fprintf(out, "[");
            for (int i = 0; i < e->as.dict.entry_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.dict.entries[i].key, out);
                fprintf(out, ": ");
                ast_dump_expr(e->as.dict.entries[i].value, out);
            }
            fprintf(out, "]");
            break;
        case AST_EXPR_INDEX:
            ast_dump_expr(e->as.index.target, out);
            fprintf(out, "[");
            ast_dump_expr(e->as.index.index, out);
            fprintf(out, "]");
            break;
        case AST_EXPR_MEMBER:
            ast_dump_expr(e->as.member.target, out);
            fprintf(out, "%s%s", e->as.member.is_optional_chain ? "?." : ".", e->as.member.member ? e->as.member.member : "");
            break;
        case AST_EXPR_METHOD_CALL:
            ast_dump_expr(e->as.method_call.target, out);
            fprintf(out, "%s%s(", e->as.method_call.is_optional_chain ? "?." : ".", e->as.method_call.name ? e->as.method_call.name : "");
            for (int i = 0; i < e->as.method_call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.method_call.args[i], out);
            }
            fprintf(out, ")");
            break;
        default:
            fprintf(out, "<expr>");
            break;
    }
}

void ast_dump_stmt_list(const ASTStmtList* stmts, FILE* out) {
    if (!out) return;
    if (!stmts) {
        fprintf(out, "<null>\n");
        return;
    }

    const ASTStmt* cur = stmts->head;
    while (cur) {
        switch (cur->kind) {
            case AST_STMT_VAR_DECL:
                fprintf(out, "var_decl name=%s type=%s expr=", cur->as.var_decl.var_name ? cur->as.var_decl.var_name : "",
                        cur->as.var_decl.type_str ? cur->as.var_decl.type_str : "");
                ast_dump_expr(cur->as.var_decl.init, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_VAR_ASSIGN:
                fprintf(out, "var_assign name=%s expr=", cur->as.var_assign.var_name ? cur->as.var_assign.var_name : "");
                ast_dump_expr(cur->as.var_assign.value, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_PRINT:
                fprintf(out, "print expr=");
                ast_dump_expr(cur->as.print.expr, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_EXPR:
                fprintf(out, "expr expr=");
                ast_dump_expr(cur->as.expr.expr, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_IF:
                fprintf(out, "if cond=");
                ast_dump_expr(cur->as.if_stmt.condition, out);
                fprintf(out, "\n");
                dump_indent(out, 0);
                fprintf(out, "then:\n");
                ast_dump_stmt_list(cur->as.if_stmt.then_branch, out);
                if (cur->as.if_stmt.else_branch) {
                    fprintf(out, "else:\n");
                    ast_dump_stmt_list(cur->as.if_stmt.else_branch, out);
                }
                break;
            case AST_STMT_WHILE:
                fprintf(out, "while cond=");
                ast_dump_expr(cur->as.while_stmt.condition, out);
                fprintf(out, "\n");
                fprintf(out, "body:\n");
                ast_dump_stmt_list(cur->as.while_stmt.body, out);
                break;
            case AST_STMT_FOR:
                fprintf(out, "for var=%s range=", cur->as.for_stmt.var_name ? cur->as.for_stmt.var_name : "");
                ast_dump_expr(cur->as.for_stmt.range_expr, out);
                fprintf(out, "\n");
                fprintf(out, "body:\n");
                ast_dump_stmt_list(cur->as.for_stmt.body, out);
                break;
            case AST_STMT_FUNC_DECL:
                fprintf(out, "func_decl name=%s params=%d\n", cur->as.func_decl.name ? cur->as.func_decl.name : "",
                        cur->as.func_decl.param_count);
                fprintf(out, "body:\n");
                ast_dump_stmt_list(cur->as.func_decl.body, out);
                break;
            case AST_STMT_RETURN:
                fprintf(out, "return expr=");
                ast_dump_expr(cur->as.ret.expr, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_BREAK:
                fprintf(out, "break\n");
                break;
            case AST_STMT_CONTINUE:
                fprintf(out, "continue\n");
                break;
            default:
                fprintf(out, "<stmt>\n");
                break;
        }
        cur = cur->next;
    }
}
