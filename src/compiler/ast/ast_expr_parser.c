#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_memory.h"
#include "compiler/ast/ast_expr_parser.h"
#include "runtime/error.h"
#include "core/var.h"

#define MAX_TOKEN_LEN 1024
#define INITIAL_STRING_CAPACITY 64
#define INITIAL_ARRAY_CAPACITY 8

static void skip_whitespace(const char** code);
static int is_ident_start(char c);
static int is_ident_char(char c);
static char* dup_range(const char* start, const char* end);

 static int is_expression_brace(const char* look);

static ASTExpr* parse_logical_or(const char** expr);
static ASTExpr* parse_logical_and(const char** expr);
static ASTExpr* parse_comparison(const char** expr);
static ASTExpr* parse_term(const char** expr);
static ASTExpr* parse_factor(const char** expr);
static ASTExpr* parse_unary(const char** expr);
static ASTExpr* parse_primary(const char** expr);
static ASTExpr* parse_postfix(const char** expr, ASTExpr* base);

 static ASTExpr** parse_argument_list(const char** expr, int* out_count);
 static ASTExpr* parse_identifier_expr(const char** expr);
 static ASTExpr* parse_array_or_dict(const char** expr);

static ASTExpr* create_binary_expr(ASTExpr* left, ASTExpr* right, char op, SourceLoc loc) {
    ASTExpr* out = ast_expr_new(AST_EXPR_BINARY, loc);
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

 static ASTExpr* parse_identifier_expr(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
    skip_whitespace(expr);
     if (!is_ident_start(**expr)) return NULL;

     const char* start = *expr;
     (*expr)++;
     while (**expr && is_ident_char(**expr)) (*expr)++;

     char* name = dup_range(start, *expr);
     if (!name) {
         BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory parsing identifier");
         return NULL;
     }

     if (strcmp(name, "self") == 0) {
         free(name);
         return ast_expr_new(AST_EXPR_SELF, loc);
     }
     if (strcmp(name, "super") == 0) {
         free(name);
         return ast_expr_new(AST_EXPR_SUPER, loc);
     }

     skip_whitespace(expr);

     if (**expr == '{') {
         (*expr)++; // consume '{'

         int cap = INITIAL_ARRAY_CAPACITY;
         int count = 0;
         char** field_names = malloc(sizeof(char*) * cap);
         ASTExpr** field_values = malloc(sizeof(ASTExpr*) * cap);
         if (!field_names || !field_values) {
             free(field_names);
             free(field_values);
             free(name);
             BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
             return NULL;
         }

         skip_whitespace(expr);
         while (**expr && **expr != '}') {
             if (!is_ident_start(**expr)) {
                 for (int i = 0; i < count; i++) {
                     free(field_names[i]);
                     ast_free_expr(field_values[i]);
                 }
                 free(field_names);
                 free(field_values);
                 free(name);
                 BREAD_ERROR_SET_SYNTAX_ERROR("Expected field name in literal");
                 return NULL;
             }

             const char* fstart = *expr;
             (*expr)++;
             while (**expr && is_ident_char(**expr)) (*expr)++;
             char* field = dup_range(fstart, *expr);
             if (!field) {
                 for (int i = 0; i < count; i++) {
                     free(field_names[i]);
                     ast_free_expr(field_values[i]);
                 }
                 free(field_names);
                 free(field_values);
                 free(name);
                 BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                 return NULL;
             }

             skip_whitespace(expr);
             if (**expr != ':') {
                 free(field);
                 for (int i = 0; i < count; i++) {
                     free(field_names[i]);
                     ast_free_expr(field_values[i]);
                 }
                 free(field_names);
                 free(field_values);
                 free(name);
                 BREAD_ERROR_SET_SYNTAX_ERROR("Expected ':' after field name in literal");
                 return NULL;
             }
             (*expr)++; // ':'

             ASTExpr* value = parse_expression(expr);
             if (!value) {
                 free(field);
                 for (int i = 0; i < count; i++) {
                     free(field_names[i]);
                     ast_free_expr(field_values[i]);
                 }
                 free(field_names);
                 free(field_values);
                 free(name);
                 return NULL;
             }

             if (count >= cap) {
                 cap *= 2;
                 char** new_names = realloc(field_names, sizeof(char*) * cap);
                 ASTExpr** new_vals = realloc(field_values, sizeof(ASTExpr*) * cap);
                 if (!new_names || !new_vals) {
                     free(field);
                     ast_free_expr(value);
                     for (int i = 0; i < count; i++) {
                         free(field_names[i]);
                         ast_free_expr(field_values[i]);
                     }
                     free(new_names ? new_names : field_names);
                     free(new_vals ? new_vals : field_values);
                     free(name);
                     BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                     return NULL;
                 }
                 field_names = new_names;
                 field_values = new_vals;
             }

             field_names[count] = field;
             field_values[count] = value;
             count++;

             skip_whitespace(expr);
             if (**expr == ',') {
                 (*expr)++;
                 skip_whitespace(expr);
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
             BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing '}' in literal");
             return NULL;
         }
         (*expr)++; // consume '}'

         ASTExpr* out = ast_expr_new(AST_EXPR_STRUCT_LITERAL, loc);
         if (!out) {
             for (int i = 0; i < count; i++) {
                 free(field_names[i]);
                 ast_free_expr(field_values[i]);
             }
             free(field_names);
             free(field_values);
             free(name);
             return NULL;
         }
         out->as.struct_literal.struct_name = name;
         out->as.struct_literal.field_count = count;
         out->as.struct_literal.field_names = field_names;
         out->as.struct_literal.field_values = field_values;
         return out;
     }

     if (**expr == '(') {
         (*expr)++;
         int count;
         ASTExpr** args = parse_argument_list(expr, &count);
         if (!args && count < 0) {
             free(name);
             return NULL;
         }

         ASTExpr* out = ast_expr_new(AST_EXPR_CALL, loc);
         if (!out) {
             ast_free_expr_list(args, count);
             free(name);
             return NULL;
         }
         out->as.call.name = name;
         out->as.call.arg_count = count;
         out->as.call.args = args;
         return out;
     }

     ASTExpr* out = ast_expr_new(AST_EXPR_VAR, loc);
     if (!out) {
         free(name);
         return NULL;
     }
     out->as.var_name = name;
     return out;
 }

 static ASTExpr* parse_array_or_dict(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);     // Expects current char to be '['
     skip_whitespace(expr);
     if (**expr != '[') return NULL;
     (*expr)++; // consume '['
     skip_whitespace(expr);

     // Empty dict literal: [:]
     if (**expr == ':') {
         (*expr)++;
         skip_whitespace(expr);
         if (**expr != ']') {
             BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing ']' in dictionary literal");
             return NULL;
         }
         (*expr)++;
         ASTExpr* out = ast_expr_new(AST_EXPR_DICT, loc);
         if (!out) return NULL;
         out->as.dict.entry_count = 0;
         out->as.dict.entries = NULL;
         return out;
     }

     // Empty array literal: []
     if (**expr == ']') {
         (*expr)++;
         ASTExpr* out = ast_expr_new(AST_EXPR_ARRAY_LITERAL, loc);
         if (!out) return NULL;
         out->as.array_literal.element_count = 0;
         out->as.array_literal.elements = NULL;
         out->as.array_literal.element_type = TYPE_NIL;
         return out;
     }

     int entry_cap = INITIAL_ARRAY_CAPACITY;
     int entry_count = 0;
     ASTDictEntry* entries = NULL;

     int elem_cap = INITIAL_ARRAY_CAPACITY;
     int elem_count = 0;
     ASTExpr** elems = NULL;

     int is_dict = 0;

     while (**expr && **expr != ']') {
         ASTExpr* first = parse_expression(expr);
         if (!first) {
             goto fail;
         }

         skip_whitespace(expr);
         if (!is_dict && **expr == ':') {
             is_dict = 1;
         }

         if (is_dict) {
             if (**expr != ':') {
                 ast_free_expr(first);
                 BREAD_ERROR_SET_SYNTAX_ERROR("Expected ':' in dictionary literal");
                 goto fail;
             }
             (*expr)++;
             ASTExpr* value = parse_expression(expr);
             if (!value) {
                 ast_free_expr(first);
                 goto fail;
             }

             if (!entries) {
                 entries = malloc(sizeof(ASTDictEntry) * entry_cap);
                 if (!entries) {
                     ast_free_expr(first);
                     ast_free_expr(value);
                     BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                     goto fail;
                 }
             }
             if (entry_count >= entry_cap) {
                 entry_cap *= 2;
                 ASTDictEntry* new_entries = realloc(entries, sizeof(ASTDictEntry) * entry_cap);
                 if (!new_entries) {
                     ast_free_expr(first);
                     ast_free_expr(value);
                     BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                     goto fail;
                 }
                 entries = new_entries;
             }
             entries[entry_count].key = first;
             entries[entry_count].value = value;
             entry_count++;
         } else {
             if (!elems) {
                 elems = malloc(sizeof(ASTExpr*) * elem_cap);
                 if (!elems) {
                     ast_free_expr(first);
                     BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                     goto fail;
                 }
             }
             if (elem_count >= elem_cap) {
                 elem_cap *= 2;
                 ASTExpr** new_elems = realloc(elems, sizeof(ASTExpr*) * elem_cap);
                 if (!new_elems) {
                     ast_free_expr(first);
                     BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                     goto fail;
                 }
                 elems = new_elems;
             }
             elems[elem_count++] = first;
         }

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
         BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing ']' in array/dictionary literal");
         goto fail;
     }
     (*expr)++;

     if (is_dict) {
         ASTExpr* out = ast_expr_new(AST_EXPR_DICT, loc);
         if (!out) goto fail;
         out->as.dict.entry_count = entry_count;
         out->as.dict.entries = entries;
         if (elems) {
             for (int i = 0; i < elem_count; i++) ast_free_expr(elems[i]);
             free(elems);
         }
         return out;
     }

     ASTExpr* out = ast_expr_new(AST_EXPR_ARRAY_LITERAL, loc);
     if (!out) goto fail;
     out->as.array_literal.element_count = elem_count;
     out->as.array_literal.elements = elems;
     out->as.array_literal.element_type = TYPE_NIL;
     if (entries) {
         for (int i = 0; i < entry_count; i++) {
             ast_free_expr(entries[i].key);
             ast_free_expr(entries[i].value);
         }
         free(entries);
     }
     return out;

 fail:
     if (entries) {
         for (int i = 0; i < entry_count; i++) {
             ast_free_expr(entries[i].key);
             ast_free_expr(entries[i].value);
         }
         free(entries);
     }
     if (elems) {
         for (int i = 0; i < elem_count; i++) ast_free_expr(elems[i]);
         free(elems);
     }
     return NULL;
 }

static ASTExpr* create_unary_expr(ASTExpr* operand, char op, SourceLoc loc) {
    ASTExpr* out = ast_expr_new(AST_EXPR_UNARY, loc);
    if (!out) {
        ast_free_expr(operand);
        return NULL;
    }
    out->as.unary.op = op;
    out->as.unary.operand = operand;
    return out;
}

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
            if (!is_expression_brace(*code + 1)) {
                break;
            }
            brace_count++;
        } else if (**code == '(') {
            paren_count++;
        } else if (**code == ')') {
            if (paren_count == 0) break;
            paren_count--;
        } else if (**code == '{') {
            brace_count++;
        } else if (**code == '}') {
            if (brace_count == 0) break;
            brace_count--;
        } else if (**code == '[') {
            bracket_count++;
        } else if (**code == ']') {
            if (bracket_count > 0) bracket_count--;
        } else if (paren_count == 0 && brace_count == 0 && bracket_count == 0 && 
                   (**code == '\n' || **code == ';' || **code == ',')) {
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
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Unexpected characters in expression: '%s'", p);
        free(expr_src);
        BREAD_ERROR_SET_PARSE_ERROR(error_msg);
        return NULL;
    }

    free(expr_src);
    return e;
}

static int is_expression_brace(const char* look) {
    while (*look == ' ' || *look == '\t' || *look == '\r') look++;
    if (*look == '}') return 1;
    if (is_ident_start(*look)) {
        const char* id = look;
        id++;
        while (*id && is_ident_char(*id)) id++;
        while (*id == ' ' || *id == '\t' || *id == '\r') id++;
        return (*id == ':');
    }
    
    if (*look == '"') {
        look++;
        while (*look && *look != '"') {
            if (*look == '\\' && *(look + 1)) look += 2;
            else look++;
        }
        if (*look != '"') return 0;
        look++;
        while (*look == ' ' || *look == '\t' || *look == '\r') look++;
        return (*look == ':');
    }
    
    return 0;
}

static ASTExpr* parse_logical_or(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
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
        left = create_binary_expr(left, right, '|', loc);
        if (!left) return NULL;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_logical_and(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
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
        left = create_binary_expr(left, right, '&', loc);
        if (!left) return NULL;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_comparison(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
    ASTExpr* left = parse_term(expr);
    if (!left) return NULL;

    skip_whitespace(expr);
    if ((**expr == '=' && *(*expr + 1) == '=') ||
        (**expr == '!' && *(*expr + 1) == '=') ||
        (**expr == '<' && *(*expr + 1) == '=') ||
        (**expr == '>' && *(*expr + 1) == '=')) {
        char op = **expr;
        if (op == '<') op = 'l';  // <= encoded as 'l'
        else if (op == '>') op = 'g';  // >= encoded as 'g'
        *expr += 2;
        ASTExpr* right = parse_term(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        return create_binary_expr(left, right, op, loc);
    }

    if (**expr == '<' || **expr == '>') {
        char op = **expr;
        (*expr)++;
        ASTExpr* right = parse_term(expr);
        if (!right) {
            ast_free_expr(left);
            return NULL;
        }
        return create_binary_expr(left, right, op, loc);
    }

    return left;
}

static ASTExpr* parse_term(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
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
        left = create_binary_expr(left, right, op, loc);
        if (!left) return NULL;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_factor(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
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
        left = create_binary_expr(left, right, op, loc);
        if (!left) return NULL;
        skip_whitespace(expr);
    }

    return left;
}

static ASTExpr* parse_unary(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
    skip_whitespace(expr);
    
    if (**expr == '!' || **expr == '-') {
        char op = **expr;
        (*expr)++;
        ASTExpr* operand = parse_unary(expr);
        if (!operand) return NULL;
        return create_unary_expr(operand, op, loc);
    }

    return parse_postfix(expr, parse_primary(expr));
}

static ASTExpr* parse_string_literal(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
    (*expr)++;  // Skip opening quote
    
    ASTExpr* result_expr = NULL;
    
    size_t capacity = INITIAL_STRING_CAPACITY;
    char* buffer = malloc(capacity);
    if (!buffer) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory parsing string literal");
        return NULL;
    }
    size_t length = 0;
    
    while (**expr && **expr != '"') {
        if (length + 2 > capacity) {
            capacity *= 2;
            char* new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                ast_free_expr(result_expr);
                BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory parsing string literal");
                return NULL;
            }
            buffer = new_buffer;
        }

        // Check for interpolation: \(
        if (**expr == '\\' && *(*expr + 1) == '(') {
            if (length > 0 || result_expr == NULL) {
                // woah. 
            }
            
            if (length > 0) {
                buffer[length] = '\0';
                char* str_val = strdup(buffer);
                if (!str_val) {
                    free(buffer);
                    ast_free_expr(result_expr);
                    BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                    return NULL;
                }
                
                ASTExpr* str_node = ast_expr_new(AST_EXPR_STRING_LITERAL, loc);
                if (!str_node) {
                    free(str_val);
                    free(buffer);
                    ast_free_expr(result_expr);
                    return NULL;
                }
                str_node->as.string_literal.value = str_val;
                str_node->as.string_literal.length = length;
                str_node->tag.is_known = 1;
                str_node->tag.type = TYPE_STRING;
                
                if (result_expr) {
                    ASTExpr* combined = create_binary_expr(result_expr, str_node, '+', loc);
                    if (!combined) {
                         ast_free_expr(str_node); // Helper might not free right if fails?
                         free(buffer);
                         return NULL;
                    }
                    result_expr = combined;
                } else {
                    result_expr = str_node;
                }
                length = 0;
            }
            
            *expr += 2; // Skip \ and (
            
            // 2. Parse expression inside ()
            ASTExpr* interpolated_expr = parse_expression(expr);
            if (!interpolated_expr) {
                free(buffer);
                ast_free_expr(result_expr);
                return NULL;
            }
            
            skip_whitespace(expr);
            if (**expr != ')') {
                free(buffer);
                ast_free_expr(interpolated_expr);
                ast_free_expr(result_expr);
                BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing ')' in string interpolation");
                return NULL;
            }
            (*expr)++; // Skip )
            
            ASTExpr* call_node = ast_expr_new(AST_EXPR_CALL, loc);
            if (!call_node) {
                free(buffer);
                ast_free_expr(interpolated_expr);
                ast_free_expr(result_expr);
                return NULL;
            }
            call_node->as.call.name = strdup("str");
            if (!call_node->as.call.name) {
                free(buffer);
                ast_free_expr(interpolated_expr);
                ast_free_expr(result_expr);
                free(call_node); // ast_free_expr would work too
                BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                return NULL;
            }
            call_node->as.call.arg_count = 1;
            call_node->as.call.args = malloc(sizeof(ASTExpr*));
            if (!call_node->as.call.args) {
                 free(buffer);
                 free(call_node->as.call.name);
                 free(call_node);
                 ast_free_expr(interpolated_expr);
                 ast_free_expr(result_expr);
                 BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                 return NULL;
            }
            call_node->as.call.args[0] = interpolated_expr;
            
            // 4. Concatenate
            if (result_expr) {
                ASTExpr* combined = create_binary_expr(result_expr, call_node, '+', loc);
                if (!combined) {
                    free(buffer);
                    return NULL;
                }
                result_expr = combined;
            } else {
                result_expr = call_node;
            }
            
            continue;
        }

        // Regular character or escape sequence
        char c = **expr;
        if (c == '\\' && *(*expr + 1)) {
            (*expr)++;
            char escaped = **expr;
            
            switch (escaped) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case '(': c = '('; break;
                case 'u':
                    buffer[length++] = '\\';
                    c = 'u';
                    break;
                default:
                    buffer[length++] = '\\';
                    c = escaped;
                    break;
            }
        }
        
        buffer[length++] = c;
        (*expr)++;
    }
    
    if (**expr != '"') {
        free(buffer);
        ast_free_expr(result_expr);
        BREAD_ERROR_SET_SYNTAX_ERROR("Unterminated string literal");
        return NULL;
    }
    (*expr)++;  // Skip closing quote
    
    // Add final segment
    if (length > 0 || result_expr == NULL) {
        buffer[length] = '\0';
        char* str_val = strdup(buffer);
        if (!str_val) {
            free(buffer);
            ast_free_expr(result_expr);
            BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
            return NULL;
        }
        
        ASTExpr* str_node = ast_expr_new(AST_EXPR_STRING_LITERAL, loc);
        if (!str_node) {
            free(str_val);
            free(buffer);
            ast_free_expr(result_expr);
            return NULL;
        }
        str_node->as.string_literal.value = str_val;
        str_node->as.string_literal.length = length;
        str_node->tag.is_known = 1;
        str_node->tag.type = TYPE_STRING;
        
        if (result_expr) {
            ASTExpr* combined = create_binary_expr(result_expr, str_node, '+', loc);
            if (!combined) {
                free(buffer);
                return NULL;
            }
            result_expr = combined;
        } else {
            result_expr = str_node;
        }
    }
    
    free(buffer);
    return result_expr;
}

static ASTExpr* parse_number(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
    const char* start = *expr;
    int has_dot = 0;
    
    while (**expr && (isdigit((unsigned char)**expr) || **expr == '.')) {
        if (**expr == '.') {
            if (has_dot) break;
            has_dot = 1;
        }
        (*expr)++;
    }
    
    if (*expr == start) return NULL;
    
    char num_str[MAX_TOKEN_LEN];
    size_t len = (size_t)(*expr - start);
    if (len >= sizeof(num_str)) {
        BREAD_ERROR_SET_SYNTAX_ERROR("Number too long");
        return NULL;
    }
    memcpy(num_str, start, len);
    num_str[len] = '\0';
    
    ASTExpr* e;
    if (has_dot) {
        double val = strtod(num_str, NULL);
        e = ast_expr_new(AST_EXPR_DOUBLE, loc);
        if (!e) return NULL;
        e->as.double_val = val;
        e->tag.type = TYPE_DOUBLE;
    } else {
        int val = atoi(num_str);
        e = ast_expr_new(AST_EXPR_INT, loc);
        if (!e) return NULL;
        e->as.int_val = val;
        e->tag.type = TYPE_INT;
    }
    e->tag.is_known = 1;
    return e;
}

static ASTExpr* parse_primary(const char** expr) {
    SourceLoc loc = ast_parser_get_loc(*expr);
    skip_whitespace(expr);
    if (strncmp(*expr, "nil", 3) == 0 && !is_ident_char(*(*expr + 3))) {
        *expr += 3;
        return ast_expr_new(AST_EXPR_NIL, loc);
    }

    if (strncmp(*expr, "true", 4) == 0 && !is_ident_char(*(*expr + 4))) {
        *expr += 4;
        ASTExpr* e = ast_expr_new(AST_EXPR_BOOL, loc);
        if (!e) return NULL;
        e->as.bool_val = 1;
        e->tag.is_known = 1;
        e->tag.type = TYPE_BOOL;
        return e;
    }

    if (strncmp(*expr, "false", 5) == 0 && !is_ident_char(*(*expr + 5))) {
        *expr += 5;
        ASTExpr* e = ast_expr_new(AST_EXPR_BOOL, loc);
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
        return parse_string_literal(expr);
    }

    if (isdigit((unsigned char)**expr) || 
        (**expr == '.' && isdigit((unsigned char)*(*expr + 1)))) {
        return parse_number(expr);
    }

    if (**expr == '[') {
        return parse_array_or_dict(expr);
    }

    if (is_ident_start(**expr)) {
        return parse_identifier_expr(expr);
    }

    char error_msg[64];
    snprintf(error_msg, sizeof(error_msg), "Unexpected character '%c'", **expr);
    BREAD_ERROR_SET_SYNTAX_ERROR(error_msg);
    return NULL;
}

static ASTExpr* parse_postfix(const char** expr, ASTExpr* base) {
    SourceLoc loc = ast_parser_get_loc(*expr);
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
                BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing ']' in indexing");
                return NULL;
            }
            (*expr)++;

            ASTExpr* out = ast_expr_new(AST_EXPR_INDEX, loc);
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
            BREAD_ERROR_SET_SYNTAX_ERROR("Expected member name after '.'");
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
        
        // Method call
        if (**expr == '(') {
            (*expr)++;
            int count;
            ASTExpr** args = parse_argument_list(expr, &count);
            if (!args && count < 0) {
                free(member);
                ast_free_expr(base);
                return NULL;
            }

            ASTExpr* out = ast_expr_new(AST_EXPR_METHOD_CALL, loc);
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

        // Member access
        ASTExpr* out = ast_expr_new(AST_EXPR_MEMBER, loc);
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

static ASTExpr** parse_argument_list(const char** expr, int* out_count) {
    skip_whitespace(expr);
    
    if (**expr == ')') {
        (*expr)++;
        *out_count = 0;
        return NULL;
    }
    
    int cap = INITIAL_ARRAY_CAPACITY;
    int count = 0;
    ASTExpr** args = malloc(sizeof(ASTExpr*) * cap);
    if (!args) {
        BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
        *out_count = -1;
        return NULL;
    }
    
    while (**expr && **expr != ')') {
        ASTExpr* arg = parse_expression(expr);
        if (!arg) {
            ast_free_expr_list(args, count);
            *out_count = -1;
            return NULL;
        }
        
        if (count >= cap) {
            cap *= 2;
            ASTExpr** new_args = realloc(args, sizeof(ASTExpr*) * cap);
            if (!new_args) {
                ast_free_expr(arg);
                ast_free_expr_list(args, count);
                BREAD_ERROR_SET_MEMORY_ALLOCATION("Out of memory");
                *out_count = -1;
                return NULL;
            }
            args = new_args;
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
    
    skip_whitespace(expr);
    if (**expr != ')') {
        ast_free_expr_list(args, count);
        BREAD_ERROR_SET_SYNTAX_ERROR("Missing closing parenthesis");
        *out_count = -1;
        return NULL;
    }
    (*expr)++;
    
    *out_count = count;
    return args;
}

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
        } else if (**code == '/' && *(*code + 1) == '/') {
            while (**code && **code != '\n') {
                (*code)++;
            }
        } else {
            break;
        }
    }
}