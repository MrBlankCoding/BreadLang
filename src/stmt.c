#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/stmt.h"
#include "../include/var.h"
#include "../include/print.h"
#include "../include/function.h"
#include "../include/value.h"

#define MAX_TOKEN_LEN 1024

// Forward declarations
static Stmt* parse_statement(const char** code);
static StmtList* parse_block(const char** code);
static char* parse_expression_str(const char** code);
static void skip_whitespace(const char** code);

static int parse_type_token(const char** code, VarType* out_type);

static int parse_type_string(const char** code, char* out_buf, size_t out_sz);

char* trim_stmt(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Helper to create statement list
StmtList* create_stmt_list() {
    StmtList* list = malloc(sizeof(StmtList));
    list->head = NULL;
    list->tail = NULL;
    return list;
}

void add_stmt(StmtList* list, Stmt* stmt) {
    stmt->next = NULL;
    if (list->tail) {
        list->tail->next = stmt;
        list->tail = stmt;
    } else {
        list->head = stmt;
        list->tail = stmt;
    }
}

// Parse entire code into statement list
StmtList* parse_statements(const char* code) {
    StmtList* list = create_stmt_list();
    const char* ptr = code;
    while (*ptr) {
        skip_whitespace(&ptr);

        if (*ptr == '\0') {
            break;
        }

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

        Stmt* stmt = parse_statement(&ptr);
        if (stmt) {
            add_stmt(list, stmt);
        } else {
            free_stmt_list(list);
            return NULL;
        }

        skip_whitespace(&ptr);
        if (*ptr == ';') {
            ptr++;
        }
    }
    return list;
}

// Parse a single statement
static Stmt* parse_statement(const char** code) {
    skip_whitespace(code);

    // Function declaration: func name(a: Int, b: Int) -> Int { ... }
    if (strncmp(*code, "func ", 5) == 0) {
        *code += 5;
        skip_whitespace(code);

        const char* start = *code;
        while (**code && (isalnum(**code) || **code == '_')) (*code)++;
        if (*code == start) {
            return NULL;
        }
        char* fn_name = malloc((size_t)(*code - start) + 1);
        strncpy(fn_name, start, (size_t)(*code - start));
        fn_name[*code - start] = '\0';

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
                while (**code && (isalnum(**code) || **code == '_')) (*code)++;
                if (*code == pstart) {
                    free(fn_name);
                    return NULL;
                }
                char* p_name = malloc((size_t)(*code - pstart) + 1);
                strncpy(p_name, pstart, (size_t)(*code - pstart));
                p_name[*code - pstart] = '\0';

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
        StmtList* body = parse_block(code);
        if (!body || **code != '}') {
            free(fn_name);
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_names);
            free(param_types);
            if (body) free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_FUNC_DECL;
        stmt->data.func_decl.name = fn_name;
        stmt->data.func_decl.param_count = param_count;
        stmt->data.func_decl.param_names = param_names;
        stmt->data.func_decl.param_types = param_types;
        stmt->data.func_decl.return_type = ret_type;
        stmt->data.func_decl.body = body;
        return stmt;
    }

    // Return statement
    if (strncmp(*code, "return", 6) == 0 && !isalnum(*(*code + 6))) {
        *code += 6;
        skip_whitespace(code);
        char* expr_str = parse_expression_str(code);
        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_RETURN;
        stmt->data.ret.expr_str = expr_str;
        return stmt;
    }

    // Variable declaration: let/const var: Type = expr
    if (strncmp(*code, "let ", 4) == 0 || strncmp(*code, "const ", 6) == 0) {
        int is_const = strncmp(*code, "const ", 6) == 0;
        *code += is_const ? 6 : 4;
        skip_whitespace(code);

        // Parse variable name
        const char* start = *code;
        while (**code && (isalnum(**code) || **code == '_')) (*code)++;
        size_t name_len = *code - start;
        char* var_name = malloc(name_len + 1);
        strncpy(var_name, start, name_len);
        var_name[name_len] = '\0';

        skip_whitespace(code);
        if (**code != ':') {
            free(var_name);
            return NULL;
        }
        (*code)++;
        skip_whitespace(code);

        // Parse type (supports Int, [Int], [String: String], Int?, etc)
        char type_buf[MAX_TOKEN_LEN];
        if (!parse_type_string(code, type_buf, sizeof(type_buf))) {
            free(var_name);
            return NULL;
        }

        VarType type;
        if (strcmp(type_buf, "Int") == 0) type = TYPE_INT;
        else if (strcmp(type_buf, "String") == 0) type = TYPE_STRING;
        else if (strcmp(type_buf, "Bool") == 0) type = TYPE_BOOL;
        else if (strcmp(type_buf, "Float") == 0) type = TYPE_FLOAT;
        else if (strcmp(type_buf, "Double") == 0) type = TYPE_DOUBLE;
        else if (type_buf[0] == '[') {
            // [T] or [K:V]
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
            size_t tlen = strlen(type_buf);
            if (tlen > 0 && type_buf[tlen - 1] == '?') {
                type = TYPE_OPTIONAL;
            } else {
                free(var_name);
                return NULL;
            }
        }

        skip_whitespace(code);
        if (**code != '=') {
            free(var_name);
            return NULL;
        }
        (*code)++;
        skip_whitespace(code);

        // Parse expression until end of line or }
        char* expr_str = parse_expression_str(code);

        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_VAR_DECL;
        stmt->data.var_decl.var_name = var_name;
        stmt->data.var_decl.type = type;
        stmt->data.var_decl.type_str = strdup(type_buf);
        stmt->data.var_decl.expr_str = expr_str;
        stmt->data.var_decl.is_const = is_const;
        return stmt;
    }

    // Print statement
    if (strncmp(*code, "print(", 6) == 0) {
        *code += 6;
        char* expr_str = parse_expression_str(code);
        if (**code == ')') (*code)++;

        // Peek ahead for a statement boundary without consuming into the next statement.
        // IMPORTANT: don't skip newlines here, otherwise we may skip past the actual
        // statement terminator and land on the next statement.
        const char* lookahead = *code;
        while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\r') {
            lookahead++;
        }
        if (*lookahead == '\n' || *lookahead == '\0' || *lookahead == '}' || *lookahead == ';') {
            *code = lookahead;
            if (**code == '\n' || **code == ';') {
                (*code)++;
            }
            Stmt* stmt = malloc(sizeof(Stmt));
            stmt->type = STMT_PRINT;
            stmt->data.print.expr_str = expr_str;
            return stmt;
        }

        free(expr_str);
        return NULL;
    }

    // If statement
    if (strncmp(*code, "if ", 3) == 0) {
        *code += 3;
        skip_whitespace(code);
        char* condition = parse_expression_str(code);
        skip_whitespace(code);
        if (**code != '{') {
            free(condition);
            return NULL;
        }
        (*code)++;
        StmtList* then_branch = parse_block(code);
        if (**code != '}') {
            free(condition);
            free_stmt_list(then_branch);
            return NULL;
        }
        (*code)++;

        StmtList* elif_branches = NULL;
        StmtList* else_branch = NULL;

        skip_whitespace(code);
        while (strncmp(*code, "elif ", 5) == 0) {
            if (!elif_branches) elif_branches = create_stmt_list();
            *code += 5;
            skip_whitespace(code);
            char* elif_condition = parse_expression_str(code);
            skip_whitespace(code);
            if (**code != '{') {
                free(condition);
                free(elif_condition);
                free_stmt_list(then_branch);
                free_stmt_list(elif_branches);
                return NULL;
            }
            (*code)++;
            StmtList* elif_block = parse_block(code);
            if (**code != '}') {
                free(condition);
                free(elif_condition);
                free_stmt_list(then_branch);
                free_stmt_list(elif_branches);
                free_stmt_list(elif_block);
                return NULL;
            }
            (*code)++;
            // For simplicity, store elif as separate statements in the list
            // Actually, need to modify StmtIf to handle multiple elif
            // For now, skip elif
            free(elif_condition);
            free_stmt_list(elif_block);
            skip_whitespace(code);
        }

        if (strncmp(*code, "else ", 5) == 0) {
            *code += 5;
            skip_whitespace(code);
            if (**code != '{') {
                free(condition);
                free_stmt_list(then_branch);
                if (elif_branches) free_stmt_list(elif_branches);
                return NULL;
            }
            (*code)++;
            else_branch = parse_block(code);
            if (**code != '}') {
                free(condition);
                free_stmt_list(then_branch);
                if (elif_branches) free_stmt_list(elif_branches);
                free_stmt_list(else_branch);
                return NULL;
            }
            (*code)++;
        }

        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_IF;
        stmt->data.if_stmt.condition_str = condition;
        stmt->data.if_stmt.then_branch = then_branch;
        stmt->data.if_stmt.elif_branches = elif_branches;
        stmt->data.if_stmt.else_branch = else_branch;
        return stmt;
    }

    // While statement
    if (strncmp(*code, "while ", 6) == 0) {
        *code += 6;
        skip_whitespace(code);
        char* condition = parse_expression_str(code);
        skip_whitespace(code);
        if (**code != '{') {
            free(condition);
            return NULL;
        }
        (*code)++;
        StmtList* body = parse_block(code);
        if (**code != '}') {
            free(condition);
            free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_WHILE;
        stmt->data.while_stmt.condition_str = condition;
        stmt->data.while_stmt.body = body;
        return stmt;
    }

    // For statement
    if (strncmp(*code, "for ", 4) == 0) {
        *code += 4;
        skip_whitespace(code);
        const char* start = *code;
        while (**code && **code != ' ' && **code != '\t') (*code)++;
        size_t var_len = *code - start;
        char* var_name = malloc(var_len + 1);
        strncpy(var_name, start, var_len);
        var_name[var_len] = '\0';

        skip_whitespace(code);
        if (strncmp(*code, "in ", 3) != 0) {
            free(var_name);
            return NULL;
        }
        *code += 3;
        skip_whitespace(code);
        char* range_expr = parse_expression_str(code);
        skip_whitespace(code);
        if (**code != '{') {
            free(var_name);
            free(range_expr);
            return NULL;
        }
        (*code)++;
        StmtList* body = parse_block(code);
        if (**code != '}') {
            free(var_name);
            free(range_expr);
            free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_FOR;
        stmt->data.for_stmt.var_name = var_name;
        stmt->data.for_stmt.range_expr_str = range_expr;
        stmt->data.for_stmt.body = body;
        return stmt;
    }

    // Break
    if (strncmp(*code, "break", 5) == 0 && !isalnum(*(*code + 5))) {
        *code += 5;
        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_BREAK;
        return stmt;
    }

    // Continue
    if (strncmp(*code, "continue", 8) == 0 && !isalnum(*(*code + 8))) {
        *code += 8;
        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_CONTINUE;
        return stmt;
    }

    // Variable assignment
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
        char* var_name = malloc(name_len + 1);
        strncpy(var_name, start, name_len);
        var_name[name_len] = '\0';
        (*code)++; // consume '='
        skip_whitespace(code);
        char* expr_str = parse_expression_str(code);

        Stmt* stmt = malloc(sizeof(Stmt));
        stmt->type = STMT_VAR_ASSIGN;
        stmt->data.var_assign.var_name = var_name;
        stmt->data.var_assign.expr_str = expr_str;
        return stmt;
    }

    // Expression statement (evaluate for side effects)
    char* expr_str = parse_expression_str(code);
    if (!expr_str) return NULL;
    Stmt* stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXPR;
    stmt->data.expr.expr_str = expr_str;
    return stmt;

    // If nothing matched, error
    return NULL;
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
        // [T] or [K:V]
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

static int parse_type_string(const char** code, char* out_buf, size_t out_sz) {
    if (!out_buf || out_sz == 0) return 0;
    skip_whitespace(code);
    const char* start = *code;
    if (*start == '\0') return 0;

    int bracket_depth = 0;
    while (**code) {
        char c = **code;
        if (c == '[') {
            bracket_depth++;
        } else if (c == ']') {
            bracket_depth--;
        }

        if (bracket_depth == 0) {
            if (c == ',' || c == ')' || c == '{' || c == '}' || isspace((unsigned char)c)) {
                break;
            }
            // function return arrow handling: stop before '-'
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

// Parse a block of statements until }
static StmtList* parse_block(const char** code) {
    StmtList* list = create_stmt_list();
    while (**code && **code != '}') {
        skip_whitespace(code);
        if (**code == '}') break;
        Stmt* stmt = parse_statement(code);
        if (stmt) {
            add_stmt(list, stmt);
        } else {
            // Error
            free_stmt_list(list);
            return NULL;
        }
    }
    return list;
}

// Parse expression string until end of statement
static char* parse_expression_str(const char** code) {
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
    size_t len = *code - start;
    char* expr = malloc(len + 1);
    strncpy(expr, start, len);
    expr[len] = '\0';
    // Trim trailing whitespace
    char* end = expr + len - 1;
    while (end > expr && isspace(*end)) *end-- = '\0';
    return expr;
}

static void skip_whitespace(const char** code) {
    while (**code && isspace(**code)) (*code)++;
}

// Execute statements
ExecSignal execute_statements(StmtList* stmts, ExprResult* out_return) {
    Stmt* current = stmts->head;
    while (current) {
        switch (current->type) {
            case STMT_VAR_DECL: {
                char line[1024];
                snprintf(line, sizeof(line), "%s %s: %s = %s",
                         current->data.var_decl.is_const ? "const" : "let",
                         current->data.var_decl.var_name,
                         current->data.var_decl.type_str ? current->data.var_decl.type_str : "Int",
                         current->data.var_decl.expr_str);
                execute_variable_declaration(line);
                break;
            }
            case STMT_VAR_ASSIGN: {
                char line[1024];
                snprintf(line, sizeof(line), "%s = %s", current->data.var_assign.var_name, current->data.var_assign.expr_str);
                execute_variable_assignment(line);
                break;
            }
            case STMT_PRINT: {
                char line[1024];
                snprintf(line, sizeof(line), "print(%s)", current->data.print.expr_str);
                execute_print(line);
                break;
            }
            case STMT_EXPR: {
                ExprResult v = evaluate_expression(current->data.expr.expr_str);
                if (!v.is_error) {
                    BreadValue tmp = bread_value_from_expr_result(v);
                    bread_value_release(&tmp);
                }
                break;
            }
            case STMT_IF: {
                ExprResult cond = evaluate_expression(current->data.if_stmt.condition_str);
                if (!cond.is_error && cond.type == TYPE_BOOL && cond.value.bool_val) {
                    ExecSignal sig = execute_statements(current->data.if_stmt.then_branch, out_return);
                    if (sig != EXEC_SIGNAL_NONE) return sig;
                } else if (current->data.if_stmt.else_branch) {
                    ExecSignal sig = execute_statements(current->data.if_stmt.else_branch, out_return);
                    if (sig != EXEC_SIGNAL_NONE) return sig;
                }
                break;
            }
            case STMT_WHILE: {
                while (1) {
                    ExprResult cond = evaluate_expression(current->data.while_stmt.condition_str);
                    if (cond.is_error || cond.type != TYPE_BOOL || !cond.value.bool_val) break;
                    ExecSignal sig = execute_statements(current->data.while_stmt.body, out_return);
                    if (sig == EXEC_SIGNAL_BREAK) break;
                    if (sig == EXEC_SIGNAL_CONTINUE) continue;
                    if (sig == EXEC_SIGNAL_RETURN) return sig;
                }
                break;
            }
            case STMT_FOR: {
                // For simplicity, assume range(10) format
                if (strncmp(current->data.for_stmt.range_expr_str, "range(", 6) == 0) {
                    char* end = strchr(current->data.for_stmt.range_expr_str + 6, ')');
                    if (end) {
                        *end = '\0';
                        int limit = atoi(current->data.for_stmt.range_expr_str + 6);
                        if (!get_variable(current->data.for_stmt.var_name)) {
                            char decl_line[1024];
                            snprintf(decl_line, sizeof(decl_line), "let %s: Int = 0", current->data.for_stmt.var_name);
                            execute_variable_declaration(decl_line);
                        }
                        for (int i = 0; i < limit; i++) {
                            // Set loop variable
                            char assign_line[1024];
                            snprintf(assign_line, sizeof(assign_line), "%s = %d", current->data.for_stmt.var_name, i);
                            execute_variable_assignment(assign_line);
                            ExecSignal sig = execute_statements(current->data.for_stmt.body, out_return);
                            if (sig == EXEC_SIGNAL_BREAK) break;
                            if (sig == EXEC_SIGNAL_CONTINUE) continue;
                            if (sig == EXEC_SIGNAL_RETURN) return sig;
                        }
                    }
                }
                break;
            }
            case STMT_BREAK:
                return EXEC_SIGNAL_BREAK;
            case STMT_CONTINUE:
                return EXEC_SIGNAL_CONTINUE;
            case STMT_FUNC_DECL: {
                Function fn;
                memset(&fn, 0, sizeof(fn));
                fn.name = current->data.func_decl.name;
                fn.param_count = current->data.func_decl.param_count;
                fn.param_names = current->data.func_decl.param_names;
                fn.param_types = current->data.func_decl.param_types;
                fn.return_type = current->data.func_decl.return_type;
                fn.body = current->data.func_decl.body;
                (void)register_function(&fn);
                break;
            }
            case STMT_RETURN: {
                if (!out_return) {
                    printf("Error: 'return' used outside of function\n");
                    return EXEC_SIGNAL_RETURN;
                }
                ExprResult v = evaluate_expression(current->data.ret.expr_str);
                if (v.is_error) {
                    *out_return = v;
                    return EXEC_SIGNAL_RETURN;
                }
                *out_return = v;
                return EXEC_SIGNAL_RETURN;
            }
            default:
                break;
        }
        current = current->next;
    }

    return EXEC_SIGNAL_NONE;
}

void free_stmt_list(StmtList* stmts) {
    Stmt* current = stmts->head;
    while (current) {
        Stmt* next = current->next;
        // Free strings
        switch (current->type) {
            case STMT_VAR_DECL:
                free(current->data.var_decl.var_name);
                free(current->data.var_decl.type_str);
                free(current->data.var_decl.expr_str);
                break;
            case STMT_VAR_ASSIGN:
                free(current->data.var_assign.var_name);
                free(current->data.var_assign.expr_str);
                break;
            case STMT_PRINT:
                free(current->data.print.expr_str);
                break;
            case STMT_EXPR:
                free(current->data.expr.expr_str);
                break;
            case STMT_IF:
                free(current->data.if_stmt.condition_str);
                free_stmt_list(current->data.if_stmt.then_branch);
                if (current->data.if_stmt.elif_branches) free_stmt_list(current->data.if_stmt.elif_branches);
                if (current->data.if_stmt.else_branch) free_stmt_list(current->data.if_stmt.else_branch);
                break;
            case STMT_WHILE:
                free(current->data.while_stmt.condition_str);
                free_stmt_list(current->data.while_stmt.body);
                break;
            case STMT_FOR:
                free(current->data.for_stmt.var_name);
                free(current->data.for_stmt.range_expr_str);
                free_stmt_list(current->data.for_stmt.body);
                break;
            case STMT_FUNC_DECL:
                free(current->data.func_decl.name);
                for (int i = 0; i < current->data.func_decl.param_count; i++) {
                    free(current->data.func_decl.param_names[i]);
                }
                free(current->data.func_decl.param_names);
                free(current->data.func_decl.param_types);
                free_stmt_list(current->data.func_decl.body);
                break;
            case STMT_RETURN:
                free(current->data.ret.expr_str);
                break;
            default:
                break;
        }
        free(current);
        current = next;
    }
    free(stmts);
}