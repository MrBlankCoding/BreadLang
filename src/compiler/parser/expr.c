#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "compiler/parser/expr.h"
#include "core/var.h"
#include "core/function.h"
#include "core/value.h"

#define MAX_EXPR_LEN 1024

static ExprResult parse_expression(const char** expr);
static ExprResult parse_logical_or(const char** expr);
static ExprResult parse_logical_and(const char** expr);
static ExprResult parse_comparison(const char** expr);
static ExprResult parse_term(const char** expr);
static ExprResult parse_factor(const char** expr);
static ExprResult parse_unary(const char** expr);
static ExprResult parse_primary(const char** expr);
static void skip_whitespace(const char** expr);
static ExprResult create_error_result();
static ExprResult create_int_result(int val);
static ExprResult create_bool_result(int val);
static ExprResult create_double_result(double val);
static ExprResult create_string_result(BreadString* val);

static void release_expr_result(ExprResult* r);
static ExprResult parse_postfix(const char** expr, ExprResult base);
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

static void release_expr_result(ExprResult* r) {
    if (!r || r->is_error) return;
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = r->type;
    v.value = r->value;
    bread_value_release(&v);
    memset(&r->value, 0, sizeof(r->value));
    r->type = TYPE_NIL;
}

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

char* trim_expr(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

ExprResult evaluate_expression(const char* expr) {
    const char* expr_ptr = expr;
    ExprResult result = parse_expression(&expr_ptr);
    skip_whitespace(&expr_ptr);
    if (*expr_ptr != '\0') {
        printf("Error: Unexpected characters in expression: '%s'\n", expr_ptr);
        return create_error_result();
    }
    return result;
}

static ExprResult parse_expression(const char** expr) {
    return parse_logical_or(expr);
}

static ExprResult parse_logical_or(const char** expr) {
    ExprResult left = parse_logical_and(expr);
    if (left.is_error) return left;

    skip_whitespace(expr);
    while (**expr == '|' && *(*expr + 1) == '|') {
        *expr += 2;
        ExprResult right = parse_logical_and(expr);
        if (right.is_error) return right;

        left = evaluate_binary_op(left, right, '|');
        if (left.is_error) return left;
        skip_whitespace(expr);
    }
    return left;
}

static ExprResult parse_logical_and(const char** expr) {
    ExprResult left = parse_comparison(expr);
    if (left.is_error) return left;

    skip_whitespace(expr);
    while (**expr == '&' && *(*expr + 1) == '&') {
        *expr += 2;
        ExprResult right = parse_comparison(expr);
        if (right.is_error) return right;

        left = evaluate_binary_op(left, right, '&');
        if (left.is_error) return left;
        skip_whitespace(expr);
    }
    return left;
}

static ExprResult parse_comparison(const char** expr) {
    ExprResult left = parse_term(expr);
    if (left.is_error) return left;

    skip_whitespace(expr);
    if ((**expr == '=' && *(*expr + 1) == '=') ||
        (**expr == '!' && *(*expr + 1) == '=') ||
        (**expr == '<' && *(*expr + 1) == '=') ||
        (**expr == '>' && *(*expr + 1) == '=')) {
        char op = **expr;
        if (op == '<') op = 'l';
        else if (op == '>') op = 'g';
        *expr += 2;
        ExprResult right = parse_term(expr);
        if (right.is_error) return right;

        return evaluate_binary_op(left, right, op);
    } else if (**expr == '<' || **expr == '>') {
        char op = **expr;
        (*expr)++;
        ExprResult right = parse_term(expr);
        if (right.is_error) return right;

        return evaluate_binary_op(left, right, op);
    }
    return left;
}

static ExprResult parse_term(const char** expr) {
    ExprResult left = parse_factor(expr);
    if (left.is_error) return left;

    skip_whitespace(expr);
    while (**expr == '+' || **expr == '-') {
        char op = **expr;
        (*expr)++;
        ExprResult right = parse_factor(expr);
        if (right.is_error) return right;

        left = evaluate_binary_op(left, right, op);
        if (left.is_error) return left;
        skip_whitespace(expr);
    }
    return left;
}

static ExprResult parse_factor(const char** expr) {
    ExprResult left = parse_unary(expr);
    if (left.is_error) return left;

    skip_whitespace(expr);
    while (**expr == '*' || **expr == '/' || **expr == '%') {
        char op = **expr;
        (*expr)++;
        ExprResult right = parse_unary(expr);
        if (right.is_error) return right;

        left = evaluate_binary_op(left, right, op);
        if (left.is_error) return left;
        skip_whitespace(expr);
    }
    return left;
}

static ExprResult parse_unary(const char** expr) {
    skip_whitespace(expr);
    if (**expr == '!') {
        (*expr)++;
        ExprResult operand = parse_unary(expr);
        if (operand.is_error) return operand;
        return evaluate_unary_op(operand, '!');
    }
    ExprResult prim = parse_primary(expr);
    if (prim.is_error) return prim;
    return parse_postfix(expr, prim);
}

static ExprResult parse_primary(const char** expr) {
    skip_whitespace(expr);

    // nil literal
    if (strncmp(*expr, "nil", 3) == 0 && !is_ident_char(*(*expr + 3))) {
        *expr += 3;
        ExprResult r;
        memset(&r, 0, sizeof(r));
        r.is_error = 0;
        r.type = TYPE_NIL;
        return r;
    }

    // Array or dictionary literal: [1, 2] or ["k": v]
    if (**expr == '[') {
        (*expr)++; // consume '['
        skip_whitespace(expr);

        // Empty literal: [] is an empty array
        if (**expr == ']') {
            (*expr)++;
            BreadArray* a = bread_array_new();
            if (!a) {
                printf("Error: Out of memory\n");
                return create_error_result();
            }
            ExprResult out;
            memset(&out, 0, sizeof(out));
            out.is_error = 0;
            out.type = TYPE_ARRAY;
            out.value.array_val = a;
            return out;
        }

        // Heuristic: if the first top-level separator is ':' -> dict, else array
        const char* look = *expr;
        int depth = 0;
        int in_string = 0;
        int escape = 0;
        int is_dict = 0;
        while (*look) {
            char c = *look;
            if (in_string) {
                if (escape) escape = 0;
                else if (c == '\\') escape = 1;
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
            BreadDict* d = bread_dict_new();
            if (!d) {
                printf("Error: Out of memory\n");
                return create_error_result();
            }

            while (**expr) {
                skip_whitespace(expr);
                ExprResult key_r = parse_expression(expr);
                if (key_r.is_error) {
                    bread_dict_release(d);
                    return key_r;
                }
                if (key_r.type != TYPE_STRING) {
                    printf("Error: Dictionary keys must be strings\n");
                    release_expr_result(&key_r);
                    bread_dict_release(d);
                    return create_error_result();
                }

                skip_whitespace(expr);
                if (**expr != ':') {
                    printf("Error: Expected ':' in dictionary literal\n");
                    release_expr_result(&key_r);
                    bread_dict_release(d);
                    return create_error_result();
                }
                (*expr)++; // consume ':'

                ExprResult val_r = parse_expression(expr);
                if (val_r.is_error) {
                    release_expr_result(&key_r);
                    bread_dict_release(d);
                    return val_r;
                }

                BreadValue v = bread_value_from_expr_result(val_r);
                if (!bread_dict_set(d, key_r.value.string_val ? bread_string_cstr(key_r.value.string_val) : "", v)) {
                    printf("Error: Out of memory\n");
                    release_expr_result(&key_r);
                    release_expr_result(&val_r);
                    bread_dict_release(d);
                    return create_error_result();
                }
                release_expr_result(&key_r);
                release_expr_result(&val_r);

                skip_whitespace(expr);
                if (**expr == ',') {
                    (*expr)++;
                    continue;
                }
                break;
            }

            skip_whitespace(expr);
            if (**expr != ']') {
                printf("Error: Missing closing ']' in dictionary literal\n");
                bread_dict_release(d);
                return create_error_result();
            }
            (*expr)++; // consume ']'

            ExprResult out;
            memset(&out, 0, sizeof(out));
            out.is_error = 0;
            out.type = TYPE_DICT;
            out.value.dict_val = d;
            return out;
        }

        BreadArray* a = bread_array_new();
        if (!a) {
            printf("Error: Out of memory\n");
            return create_error_result();
        }

        while (**expr) {
            ExprResult item = parse_expression(expr);
            if (item.is_error) {
                bread_array_release(a);
                return item;
            }
            BreadValue v = bread_value_from_expr_result(item);
            if (!bread_array_append(a, v)) {
                printf("Error: Out of memory\n");
                release_expr_result(&item);
                bread_array_release(a);
                return create_error_result();
            }
            release_expr_result(&item);
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
            printf("Error: Missing closing ']' in array literal\n");
            bread_array_release(a);
            return create_error_result();
        }
        (*expr)++; // consume ']'

        ExprResult out;
        memset(&out, 0, sizeof(out));
        out.is_error = 0;
        out.type = TYPE_ARRAY;
        out.value.array_val = a;
        return out;
    }

    // Parenthesized expression
    if (**expr == '(') {
        (*expr)++;
        ExprResult result = parse_expression(expr);
        if (result.is_error) return result;
        skip_whitespace(expr);
        if (**expr != ')') {
            printf("Error: Missing closing parenthesis\n");
            return create_error_result();
        }
        (*expr)++;
        return result;
    }

    // String literal
    if (**expr == '"') {
        (*expr)++;
        const char* start = *expr;
        while (**expr && **expr != '"') {
            if (**expr == '\\' && *(*expr + 1)) {
                (*expr)++; // Skip escape character
            }
            (*expr)++;
        }
        if (**expr != '"') {
            printf("Error: Unterminated string literal\n");
            return create_error_result();
        }
        size_t len = *expr - start;
        BreadString* str = bread_string_new_len(start, len);
        if (!str) {
            printf("Error: Out of memory\n");
            return create_error_result();
        }
        (*expr)++;
        return create_string_result(str);
    }

    // Boolean literals
    if (strncmp(*expr, "true", 4) == 0 && !isalnum(*(*expr + 4))) {
        *expr += 4;
        return create_bool_result(1);
    }
    if (strncmp(*expr, "false", 5) == 0 && !isalnum(*(*expr + 5))) {
        *expr += 5;
        return create_bool_result(0);
    }

    // Number literals
    const char* start = *expr;
    int has_dot = 0;
    while (**expr && (isdigit(**expr) || **expr == '.')) {
        if (**expr == '.') {
            if (has_dot) break;
            has_dot = 1;
        }
        (*expr)++;
    }

    if (*expr > start) {
        char num_str[MAX_EXPR_LEN];
        size_t len = *expr - start;
        if (len >= MAX_EXPR_LEN) {
            printf("Error: Number too long\n");
            return create_error_result();
        }
        strncpy(num_str, start, len);
        num_str[len] = '\0';

        if (has_dot) {
            // Float or double
            double val = strtod(num_str, NULL);
            return create_double_result(val);
        } else {
            // Integer
            int val = atoi(num_str);
            return create_int_result(val);
        }
    }

    // Variable reference
    start = *expr;
    while (**expr && (isalnum(**expr) || **expr == '_')) {
        (*expr)++;
    }

    if (*expr > start) {
        char* var_name = dup_range(start, *expr);
        if (!var_name) {
            printf("Error: Out of memory\n");
            return create_error_result();
        }

        Variable* var = get_variable(var_name);
        if (!var) {
            printf("Error: Unknown variable '%s'\n", var_name);
            free(var_name);
            return create_error_result();
        }
        free(var_name);

        ExprResult result;
        result.is_error = 0;
        result.type = var->type;
        switch (var->type) {
            case TYPE_STRING:
                result.value.string_val = var->value.string_val;
                bread_string_retain(result.value.string_val);
                break;
            case TYPE_INT:
                result.value.int_val = var->value.int_val;
                break;
            case TYPE_BOOL:
                result.value.bool_val = var->value.bool_val;
                break;
            case TYPE_FLOAT:
                result.value.float_val = var->value.float_val;
                break;
            case TYPE_DOUBLE:
                result.value.double_val = var->value.double_val;
                break;
            case TYPE_ARRAY:
                result.value.array_val = var->value.array_val;
                bread_array_retain(result.value.array_val);
                break;
            case TYPE_DICT:
                result.value.dict_val = var->value.dict_val;
                bread_dict_retain(result.value.dict_val);
                break;
            case TYPE_OPTIONAL:
                result.value.optional_val = var->value.optional_val;
                bread_optional_retain(result.value.optional_val);
                break;
            case TYPE_NIL:
                break;
            default:
                printf("Error: Unsupported variable type\n");
                return create_error_result();
        }
        return result;
    }

    printf("Error: Unexpected character '%c'\n", **expr);
    return create_error_result();
}

static ExprResult parse_postfix(const char** expr, ExprResult base) {
    while (1) {
        skip_whitespace(expr);

        // Indexing: expr[ idx ]
        if (**expr == '[') {
            (*expr)++; // consume '['

            ExprResult idx_r = parse_expression(expr);
            if (idx_r.is_error) {
                release_expr_result(&base);
                return idx_r;
            }

            skip_whitespace(expr);
            if (**expr != ']') {
                printf("Error: Missing closing ']' in indexing\n");
                release_expr_result(&idx_r);
                release_expr_result(&base);
                return create_error_result();
            }
            (*expr)++; // consume ']'

            // Optional unwrap behavior for indexing
            ExprResult target = base;
            int target_owned = 0;
            if (target.type == TYPE_OPTIONAL) {
                BreadOptional* o = target.value.optional_val;
                if (!o || !o->is_some) {
                    release_expr_result(&idx_r);
                    release_expr_result(&target);
                    ExprResult nil_r;
                    memset(&nil_r, 0, sizeof(nil_r));
                    nil_r.is_error = 0;
                    nil_r.type = TYPE_NIL;
                    return nil_r;
                }
                BreadValue inner = bread_value_clone(o->value);
                release_expr_result(&target);
                target = bread_expr_result_from_value(inner);
                target_owned = 1;
            }

            ExprResult out;
            memset(&out, 0, sizeof(out));
            out.is_error = 0;
            out.type = TYPE_NIL;

            if (target.type == TYPE_STRING) {
                if (idx_r.type != TYPE_INT) {
                    printf("Error: String index must be Int\n");
                    release_expr_result(&idx_r);
                    if (target_owned) release_expr_result(&target);
                    else release_expr_result(&base);
                    return create_error_result();
                }
                
                int index = idx_r.value.int_val;
                size_t len = bread_string_len(target.value.string_val);
                
                // Handle negative indices (Python-style)
                if (index < 0) {
                    index = (int)len + index;
                }
                
                if (index < 0 || index >= (int)len) {
                    printf("Error: String index %d out of bounds (length %zu)\n", idx_r.value.int_val, len);
                    release_expr_result(&idx_r);
                    if (target_owned) release_expr_result(&target);
                    else release_expr_result(&base);
                    return create_error_result();
                }
                
                char ch = bread_string_get_char(target.value.string_val, (size_t)index);
                char ch_str[2] = {ch, '\0'};
                BreadString* result_str = bread_string_new(ch_str);
                if (!result_str) {
                    printf("Error: Out of memory\n");
                    release_expr_result(&idx_r);
                    if (target_owned) release_expr_result(&target);
                    else release_expr_result(&base);
                    return create_error_result();
                }
                out.type = TYPE_STRING;
                out.value.string_val = result_str;
            } else if (target.type == TYPE_ARRAY) {
                if (idx_r.type != TYPE_INT) {
                    printf("Error: Array index must be Int\n");
                    release_expr_result(&idx_r);
                    if (target_owned) release_expr_result(&target);
                    else release_expr_result(&base);
                    return create_error_result();
                }
                BreadValue* at = bread_array_get(target.value.array_val, idx_r.value.int_val);
                if (at) {
                    BreadValue cloned = bread_value_clone(*at);
                    out = bread_expr_result_from_value(cloned);
                }
            } else if (target.type == TYPE_DICT) {
                if (idx_r.type != TYPE_STRING) {
                    printf("Error: Dictionary key must be String\n");
                    release_expr_result(&idx_r);
                    if (target_owned) release_expr_result(&target);
                    else release_expr_result(&base);
                    return create_error_result();
                }
                BreadValue* v = bread_dict_get(target.value.dict_val, idx_r.value.string_val ? bread_string_cstr(idx_r.value.string_val) : "");
                if (v) {
                    BreadValue cloned = bread_value_clone(*v);
                    out = bread_expr_result_from_value(cloned);
                }
            } else {
                printf("Error: Type does not support indexing\n");
                release_expr_result(&idx_r);
                if (target_owned) release_expr_result(&target);
                else release_expr_result(&base);
                return create_error_result();
            }

            release_expr_result(&idx_r);
            if (target_owned) release_expr_result(&target);
            else release_expr_result(&base);
            base = out;
            continue;
        }

        // Member access '.' or optional chaining '?.'
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
            printf("Error: Expected member name after '.'\n");
            release_expr_result(&base);
            return create_error_result();
        }

        const char* mstart = *expr;
        (*expr)++;
        while (**expr && is_ident_char(**expr)) (*expr)++;
        BreadString* member = bread_string_new_len(mstart, *expr - mstart);
        if (!member) {
            printf("Error: Out of memory\n");
            release_expr_result(&base);
            return create_error_result();
        }

        // unwrap for optional chaining
        ExprResult target = base;
        int target_owned = 0;
        if (is_optional_chain) {
            if (target.type == TYPE_NIL) {
                bread_string_release(member);
                release_expr_result(&target);
                ExprResult nil_r;
                memset(&nil_r, 0, sizeof(nil_r));
                nil_r.is_error = 0;
                nil_r.type = TYPE_NIL;
                return nil_r;
            }
            if (target.type == TYPE_OPTIONAL) {
                BreadOptional* o = target.value.optional_val;
                if (!o || !o->is_some) {
                    bread_string_release(member);
                    release_expr_result(&target);
                    ExprResult nil_r;
                    memset(&nil_r, 0, sizeof(nil_r));
                    nil_r.is_error = 0;
                    nil_r.type = TYPE_NIL;
                    return nil_r;
                }
                BreadValue inner = bread_value_clone(o->value);
                release_expr_result(&target);
                target = bread_expr_result_from_value(inner);
                target_owned = 1;
            }
        }

        skip_whitespace(expr);

        // Method call: .append(...)
        if (strcmp(bread_string_cstr(member), "append") == 0 && **expr == '(') {
            (*expr)++; // consume '('
            ExprResult arg = parse_expression(expr);
            if (arg.is_error) {
                bread_string_release(member);
                if (target_owned) release_expr_result(&target);
                else release_expr_result(&base);
                return arg;
            }
            skip_whitespace(expr);
            if (**expr != ')') {
                printf("Error: Missing ')' in append call\n");
                bread_string_release(member);
                release_expr_result(&arg);
                if (target_owned) release_expr_result(&target);
                else release_expr_result(&base);
                return create_error_result();
            }
            (*expr)++; // consume ')'

            if (target.type != TYPE_ARRAY) {
                printf("Error: append() is only supported on arrays\n");
                bread_string_release(member);
                release_expr_result(&arg);
                if (target_owned) release_expr_result(&target);
                else release_expr_result(&base);
                return create_error_result();
            }

            BreadValue av = bread_value_from_expr_result(arg);
            if (!bread_array_append(target.value.array_val, av)) {
                printf("Error: Out of memory\n");
                bread_string_release(member);
                release_expr_result(&arg);
                if (target_owned) release_expr_result(&target);
                else release_expr_result(&base);
                return create_error_result();
            }

            bread_string_release(member);
            release_expr_result(&arg);
            if (target_owned) release_expr_result(&target);
            else release_expr_result(&base);

            ExprResult nil_out;
            memset(&nil_out, 0, sizeof(nil_out));
            nil_out.is_error = 0;
            nil_out.type = TYPE_NIL;
            base = nil_out;
            continue;
        }

        // Property: .length
        if (strcmp(bread_string_cstr(member), "length") == 0) {
            ExprResult out;
            memset(&out, 0, sizeof(out));
            out.is_error = 0;
            out.type = TYPE_INT;
            if (target.type == TYPE_ARRAY) out.value.int_val = target.value.array_val ? target.value.array_val->count : 0;
            else if (target.type == TYPE_DICT) out.value.int_val = target.value.dict_val ? target.value.dict_val->count : 0;
            else {
                printf("Error: length is only supported on arrays and dictionaries\n");
                bread_string_release(member);
                if (target_owned) release_expr_result(&target);
                else release_expr_result(&base);
                return create_error_result();
            }
            bread_string_release(member);
            if (target_owned) release_expr_result(&target);
            else release_expr_result(&base);
            base = out;
            continue;
        }

        // Dictionary member sugar: obj.name -> obj["name"]
        if (target.type == TYPE_DICT) {
            BreadValue* v = bread_dict_get(target.value.dict_val, bread_string_cstr(member));
            ExprResult out;
            memset(&out, 0, sizeof(out));
            out.is_error = 0;
            out.type = TYPE_NIL;
            if (v) {
                BreadValue cloned = bread_value_clone(*v);
                out = bread_expr_result_from_value(cloned);
            }
            bread_string_release(member);
            if (target_owned) release_expr_result(&target);
            else release_expr_result(&base);
            base = out;
            continue;
        }

        printf("Error: Unsupported member access\n");
        bread_string_release(member);
        if (target_owned) release_expr_result(&target);
        else release_expr_result(&base);
        return create_error_result();
    }

    return base;
}

static void skip_whitespace(const char** expr) {
    while (**expr && isspace(**expr)) {
        (*expr)++;
    }
}

static ExprResult create_error_result() {
    ExprResult result;
    result.is_error = 1;
    return result;
}

static ExprResult create_int_result(int val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_INT;
    result.value.int_val = val;
    return result;
}

static ExprResult create_bool_result(int val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_BOOL;
    result.value.bool_val = val;
    return result;
}

static ExprResult create_double_result(double val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_DOUBLE;
    result.value.double_val = val;
    return result;
}

static ExprResult create_string_result(BreadString* val) {
    ExprResult result;
    result.is_error = 0;
    result.type = TYPE_STRING;
    result.value.string_val = val;
    return result;
}