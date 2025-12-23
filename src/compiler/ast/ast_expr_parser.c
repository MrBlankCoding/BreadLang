#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_memory.h"
#include "compiler/ast/ast_expr_parser.h"
#include "runtime/error.h"
#include "core/var.h"

#define MAX_TOKEN_LEN 1024

static void skip_whitespace(const char** code);
static int is_ident_start(char c);
static int is_ident_char(char c);
static char* dup_range(const char* start, const char* end);

static ASTExpr* parse_logical_or(const char** expr);
static ASTExpr* parse_logical_and(const char** expr);
static ASTExpr* parse_comparison(const char** expr);
static ASTExpr* parse_term(const char** expr);
static ASTExpr* parse_factor(const char** expr);
static ASTExpr* parse_unary(const char** expr);
static ASTExpr* parse_primary(const char** expr);
static ASTExpr* parse_postfix(const char** expr, ASTExpr* base);

ASTExpr* parse_expression(const char** expr) {
    return parse_logical_or(expr);
}

ASTExpr* parse_expression_str_as_ast(const char** code) {
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
            // Disambiguate between a statement block opener (e.g. `if cond {`) and
            // an expression literal that uses braces (e.g. `Point{ x: 1 }`, `{ "k": 1 }`, `{}`).
            // Heuristic: if the next non-whitespace token looks like a literal entry (`ident:` or `"...":`)
            // or an immediate closing brace, treat it as part of the expression.
            const char* look = *code + 1;
            while (*look == ' ' || *look == '\t' || *look == '\r') look++;
            if (*look == '}') {
                // `{}` literal
                brace_count++;
            } else if (is_ident_start(*look)) {
                const char* id = look;
                id++;
                while (*id && is_ident_char(*id)) id++;
                while (*id == ' ' || *id == '\t' || *id == '\r') id++;
                if (*id != ':') {
                    break;
                }
                brace_count++;
            } else if (*look == '"') {
                // string key: { "k": v }
                look++;
                while (*look && *look != '"') {
                    if (*look == '\\' && *(look + 1)) look += 2;
                    else look++;
                }
                if (*look != '"') break;
                look++;
                while (*look == ' ' || *look == '\t' || *look == '\r') look++;
                if (*look != ':') break;
                brace_count++;
            } else {
                break;
            }
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
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory parsing expression");
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
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Unexpected characters in expression: '%s'", p);
        BREAD_ERROR_SET_PARSE_ERROR(error_msg);
        return NULL;
    }

    free(expr_src);
    return e;
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
        if (op == '<') op = 'l';
        else if (op == '>') op = 'g';
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
    
    // Handle arithmetic operators
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

    if (**expr == '-') {
        (*expr)++;
        ASTExpr* operand = parse_unary(expr);
        if (!operand) return NULL;
        ASTExpr* out = ast_expr_new(AST_EXPR_UNARY);
        if (!out) {
            ast_free_expr(operand);
            return NULL;
        }
        out->as.unary.op = '-';
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
            BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing parenthesis");
            return NULL;
        }
        (*expr)++;
        return inner;
    }

    if (**expr == '"') {
        (*expr)++;
        
        // Initial buffer size (small to test reallocation)
        size_t capacity = 32;
        char* buffer = malloc(capacity);
        if (!buffer) return NULL;
        size_t length = 0;
        
        while (**expr && **expr != '"') {
            // Ensure we have space for at least 2 more characters (current + null terminator)
            if (length + 2 > capacity) {
                capacity = (capacity == 0) ? 32 : capacity * 2;
                char* new_buffer = realloc(buffer, capacity);
                if (!new_buffer) {
                    free(buffer);
                    BREAD_ERROR_SET_SYNTAX_ERROR("Out of memory while parsing string literal");
                    return NULL;
                }
                buffer = new_buffer;
            }
            
            char c = **expr;
            
            // Handle escape sequences
            if (c == '\\' && *(*expr + 1)) {
                (*expr)++; // Skip backslash
                char escaped = **expr;
                
                switch (escaped) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case '"': c = '"'; break;
                    case '\\': c = '\\'; break;
                    case '/': c = '/'; break;  // JSON-style escaped forward slash
                    case 'u': 
                        // Simple unicode escape (\uXXXX) - just copy as is for now
                        buffer[length++] = '\\';
                        c = 'u';
                        break;
                    default:
                        // Unknown escape sequence, keep both characters
                        buffer[length++] = '\\';
                        c = escaped;
                        break;
                }
            }
            
            buffer[length++] = c;
            (*expr)++;
        }
        
        // Check for unterminated string
        if (**expr != '"') {
            free(buffer);
            BREAD_ERROR_SET_SYNTAX_ERROR("Unterminated string literal");
            return NULL;
        }
        
        // Skip closing quote
        (*expr)++;
        
        // Ensure we have space for null terminator
        if (length + 1 > capacity) {
            char* new_buffer = realloc(buffer, length + 1);
            if (!new_buffer) {
                free(buffer);
                BREAD_ERROR_SET_SYNTAX_ERROR("Out of memory while parsing string literal");
                return NULL;
            }
            buffer = new_buffer;
        }
        buffer[length] = '\0';
        
        ASTExpr* e = ast_expr_new(AST_EXPR_STRING_LITERAL);
        if (!e) {
            free(buffer);
            return NULL;
        }
        e->as.string_literal.value = buffer;
        e->as.string_literal.length = length;
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
            BREAD_ERROR_SET_SYNTAX_ERROR("Number too long");
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
            ASTExpr* e = ast_expr_new(AST_EXPR_ARRAY_LITERAL);
            if (!e) return NULL;
            e->as.array_literal.element_count = 0;
            e->as.array_literal.elements = NULL;
            e->as.array_literal.element_type = TYPE_NIL;
            e->tag.is_known = 1;
            e->tag.type = TYPE_ARRAY;
            return e;
        }

        // Check for empty dictionary syntax [:]
        if (**expr == ':' && *(*expr + 1) == ']') {
            *expr += 2; // Skip ":]"
            ASTExpr* e = ast_expr_new(AST_EXPR_DICT);
            if (!e) return NULL;
            e->as.dict.entry_count = 0;
            e->as.dict.entries = NULL;
            e->tag.is_known = 1;
            e->tag.type = TYPE_DICT;
            return e;
        }

        const char* look = *expr;
        int depth = 0;
        int brace_depth = 0;
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
            } else if (c == '{') brace_depth++;
            else if (c == '}') {
                if (brace_depth > 0) brace_depth--;
            } else if (c == ':' && depth == 0 && brace_depth == 0) {
                is_dict = 1;
                break;
            } else if (c == ',' && depth == 0 && brace_depth == 0) {
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

        ASTExpr* e = ast_expr_new(AST_EXPR_ARRAY_LITERAL);
        if (!e) return NULL;
        e->tag.is_known = 1;
        e->tag.type = TYPE_ARRAY;

        int cap = 0;
        int count = 0;
        ASTExpr** items = NULL;
        VarType element_type = TYPE_NIL; // Will be inferred from first element

        while (**expr) {
            ASTExpr* item = parse_expression(expr);
            if (!item) {
                ast_free_expr_list(items, count);
                ast_free_expr(e);
                return NULL;
            }

            // Type inference: use the type of the first element
            if (count == 0 && item->tag.is_known) {
                element_type = item->tag.type;
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

        e->as.array_literal.element_count = count;
        e->as.array_literal.elements = items;
        e->as.array_literal.element_type = element_type;
        return e;
    }

    if (is_ident_start(**expr)) {
        const char* start_id = *expr;
        (*expr)++;
        while (**expr && is_ident_char(**expr)) (*expr)++;
        char* name = dup_range(start_id, *expr);
        if (!name) return NULL;

        // Check for 'self' keyword
        if (strcmp(name, "self") == 0) {
            free(name);
            ASTExpr* e = ast_expr_new(AST_EXPR_SELF);
            if (!e) return NULL;
            return e;
        }

        // Check for 'super' keyword
        if (strcmp(name, "super") == 0) {
            free(name);
            ASTExpr* e = ast_expr_new(AST_EXPR_SUPER);
            if (!e) return NULL;
            return e;
        }

        const char* after_ident = *expr;
        skip_whitespace(expr);
        
        // Check for struct literal: StructName { field: value, ... }
        if (**expr == '{') {
            (*expr)++;
            
            int cap = 0;
            int count = 0;
            char** field_names = NULL;
            ASTExpr** field_values = NULL;

            skip_whitespace(expr);
            while (**expr && **expr != '}') {
                skip_whitespace(expr);
                if (**expr == '}') break;

                // Parse field name
                if (!is_ident_start(**expr)) {
                    for (int i = 0; i < count; i++) {
                        free(field_names[i]);
                        ast_free_expr(field_values[i]);
                    }
                    free(field_names);
                    free(field_values);
                    free(name);
                    BREAD_ERROR_SET_SYNTAX_ERROR("Expected field name in struct literal");
                    return NULL;
                }

                const char* field_start = *expr;
                (*expr)++;
                while (**expr && is_ident_char(**expr)) (*expr)++;
                char* field_name = dup_range(field_start, *expr);
                if (!field_name) {
                    for (int i = 0; i < count; i++) {
                        free(field_names[i]);
                        ast_free_expr(field_values[i]);
                    }
                    free(field_names);
                    free(field_values);
                    free(name);
                    return NULL;
                }

                skip_whitespace(expr);
                if (**expr != ':') {
                    free(field_name);
                    for (int i = 0; i < count; i++) {
                        free(field_names[i]);
                        ast_free_expr(field_values[i]);
                    }
                    free(field_names);
                    free(field_values);
                    free(name);
                    BREAD_ERROR_SET_SYNTAX_ERROR("Expected ':' after field name in struct literal");
                    return NULL;
                }
                (*expr)++;

                // Parse field value
                ASTExpr* field_value = parse_expression(expr);
                if (!field_value) {
                    free(field_name);
                    for (int i = 0; i < count; i++) {
                        free(field_names[i]);
                        ast_free_expr(field_values[i]);
                    }
                    free(field_names);
                    free(field_values);
                    free(name);
                    return NULL;
                }

                // Expand arrays if needed
                if (count >= cap) {
                    int new_cap = cap == 0 ? 4 : cap * 2;
                    char** new_names = realloc(field_names, sizeof(char*) * (size_t)new_cap);
                    ASTExpr** new_values = realloc(field_values, sizeof(ASTExpr*) * (size_t)new_cap);
                    if (!new_names || !new_values) {
                        free(new_names);
                        free(new_values);
                        free(field_name);
                        ast_free_expr(field_value);
                        for (int i = 0; i < count; i++) {
                            free(field_names[i]);
                            ast_free_expr(field_values[i]);
                        }
                        free(field_names);
                        free(field_values);
                        free(name);
                        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory parsing struct literal");
                        return NULL;
                    }
                    field_names = new_names;
                    field_values = new_values;
                    cap = new_cap;
                }

                field_names[count] = field_name;
                field_values[count] = field_value;
                count++;

                skip_whitespace(expr);
                if (**expr == ',') {
                    (*expr)++;
                    continue;
                }
                break;
            }

            skip_whitespace(expr);
            if (**expr != '}') {
                for (int i = 0; i < count; i++) {
                    free(field_names[i]);
                    ast_free_expr(field_values[i]);
                }
                free(field_names);
                free(field_values);
                free(name);
                BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing '}' in struct literal");
                return NULL;
            }
            (*expr)++;

            // Create struct literal AST node
            ASTExpr* e = ast_expr_new(AST_EXPR_STRUCT_LITERAL);
            if (!e) {
                for (int i = 0; i < count; i++) {
                    free(field_names[i]);
                    ast_free_expr(field_values[i]);
                }
                free(field_names);
                free(field_values);
                free(name);
                return NULL;
            }

            e->as.struct_literal.struct_name = name;
            e->as.struct_literal.field_count = count;
            e->as.struct_literal.field_names = field_names;
            e->as.struct_literal.field_values = field_values;
            e->tag.is_known = 1;
            e->tag.type = TYPE_STRUCT;
            return e;
        }
        
        // Check for function call: name(args...)
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

    char error_msg[64];
    snprintf(error_msg, sizeof(error_msg), "Unexpected character '%c'", **expr);
    BREAD_ERROR_SET_SYNTAX_ERROR(error_msg);
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
            *expr += 1;
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

// Utility functions
static char* dup_range(const char* start, const char* end) {
    size_t len = (size_t)(end - start);
    char* s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
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