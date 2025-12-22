#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "core/var.h"
#include "compiler/parser/expr.h"
#include "core/value.h"
#include "runtime/runtime.h"

#define MAX_VARS 256
#define MAX_LINE 1024

#define MAX_SCOPES 64

typedef struct {
    Variable vars[MAX_VARS];
    int count;
} VarScope;

static VarScope scopes[MAX_SCOPES];
static int scope_depth = 0;

static void release_variable(Variable* var);

char* trim_var(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void init_variables() {
    scope_depth = 0;
    scopes[0].count = 0;
    scope_depth = 1;
}

void push_scope() {
    if (scope_depth >= MAX_SCOPES) {
        printf("Error: Scope stack overflow\n");
        return;
    }
    scopes[scope_depth].count = 0;
    scope_depth++;
}

void pop_scope() {
    if (scope_depth <= 1) {
        printf("Error: Cannot pop global scope\n");
        return;
    }
    VarScope* scope = &scopes[scope_depth - 1];
    for (int i = 0; i < scope->count; i++) {
        release_variable(&scope->vars[i]);
    }
    scope->count = 0;
    scope_depth--;
}

Variable* get_variable(const char* name) {
    char buf[256];
    strncpy(buf, name, 255);
    buf[255] = '\0';
    char* trimmed_name = trim_var(buf);
    for (int s = scope_depth - 1; s >= 0; s--) {
        for (int i = 0; i < scopes[s].count; i++) {
            if (strcmp(scopes[s].vars[i].name, trimmed_name) == 0) {
                return &scopes[s].vars[i];
            }
        }
    }
    return NULL;
}

int declare_variable_raw(const char* name, VarType type, VarValue value, int is_const) {
    if (scope_depth <= 0) {
        printf("Error: Variable system not initialized\n");
        return 0;
    }

    VarScope* scope = &scopes[scope_depth - 1];

    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->vars[i].name, name) == 0) {
            printf("Error: Variable '%s' already declared\n", name);
            return 0;
        }
    }

    if (scope->count >= MAX_VARS) {
        printf("Error: Too many variables in scope\n");
        return 0;
    }

    Variable* var = &scope->vars[scope->count];
    memset(var, 0, sizeof(Variable));
    var->name = strdup(name);
    if (!var->name) {
        printf("Error: Out of memory\n");
        return 0;
    }
    var->type = type;
    var->is_const = is_const;

    switch (type) {
        case TYPE_STRING:
            if (value.string_val) {
                var->value.string_val = value.string_val;
                bread_string_retain(var->value.string_val);
            } else {
                var->value.string_val = bread_string_new("");
                if (!var->value.string_val) {
                    release_variable(var);
                    return 0;
                }
            }
            break;
        case TYPE_INT:
            var->value.int_val = value.int_val;
            break;
        case TYPE_BOOL:
            var->value.bool_val = value.bool_val;
            break;
        case TYPE_FLOAT:
            var->value.float_val = value.float_val;
            break;
        case TYPE_DOUBLE:
            var->value.double_val = value.double_val;
            break;
        case TYPE_ARRAY:
            var->value.array_val = value.array_val;
            bread_array_retain(var->value.array_val);
            break;
        case TYPE_DICT:
            var->value.dict_val = value.dict_val;
            bread_dict_retain(var->value.dict_val);
            break;
        case TYPE_OPTIONAL:
            var->value.optional_val = value.optional_val;
            bread_optional_retain(var->value.optional_val);
            break;
        case TYPE_STRUCT:
            var->value.struct_val = value.struct_val;
            bread_struct_retain(var->value.struct_val);
            break;
        case TYPE_CLASS:
            var->value.class_val = value.class_val;
            bread_class_retain(var->value.class_val);
            break;
        case TYPE_NIL:
            break;
        default:
            release_variable(var);
            return 0;
    }

    scope->count++;
    return 1;
}

static void set_string_value(Variable* var, BreadString* new_value) {
    if (var->value.string_val) {
        bread_string_release(var->value.string_val);
    }
    var->value.string_val = new_value;
}

static void release_variable(Variable* var) {
    if (var->type == TYPE_STRING && var->value.string_val) {
        bread_string_release(var->value.string_val);
        var->value.string_val = NULL;
    } else if (var->type == TYPE_ARRAY && var->value.array_val) {
        bread_array_release(var->value.array_val);
        var->value.array_val = NULL;
    } else if (var->type == TYPE_DICT && var->value.dict_val) {
        bread_dict_release(var->value.dict_val);
        var->value.dict_val = NULL;
    } else if (var->type == TYPE_OPTIONAL && var->value.optional_val) {
        bread_optional_release(var->value.optional_val);
        var->value.optional_val = NULL;
    } else if (var->type == TYPE_STRUCT && var->value.struct_val) {
        bread_struct_release(var->value.struct_val);
        var->value.struct_val = NULL;
    }
    if (var->name) {
        free(var->name);
        var->name = NULL;
    }
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

static const char* type_name(VarType t) {
    switch (t) {
        case TYPE_STRING: return "String";
        case TYPE_INT: return "Int";
        case TYPE_BOOL: return "Bool";
        case TYPE_FLOAT: return "Float";
        case TYPE_DOUBLE: return "Double";
        case TYPE_ARRAY: return "Array";
        case TYPE_DICT: return "Dict";
        case TYPE_OPTIONAL: return "Optional";
        case TYPE_STRUCT: return "Struct";
        case TYPE_CLASS: return "Class";
        case TYPE_NIL: return "Nil";
        default: return "Nil";
    }
}

static int set_variable_value_from_expr_result(Variable* target, const ExprResult* expr_result) {
    if (!target || !expr_result || expr_result->is_error) return 0;

    int can_assign = 0;
    VarValue coerced_value;
    memset(&coerced_value, 0, sizeof(coerced_value));

    if (target->type == TYPE_NIL) {
        can_assign = 1;
        coerced_value = expr_result->value;
        target->type = expr_result->type;
    } else if (target->type == expr_result->type) {
        can_assign = 1;
        coerced_value = expr_result->value;
    } else if (target->type == TYPE_OPTIONAL && expr_result->type == TYPE_NIL) {
        can_assign = 1;
        coerced_value.optional_val = bread_optional_new_none();
        if (!coerced_value.optional_val) return 0;
    } else if (target->type == TYPE_OPTIONAL && expr_result->type != TYPE_OPTIONAL) {
        can_assign = 1;
        BreadValue inner = bread_value_from_expr_result(*expr_result);
        coerced_value.optional_val = bread_optional_new_some(inner);
        if (!coerced_value.optional_val) return 0;
    } else if (target->type == TYPE_DOUBLE && expr_result->type == TYPE_INT) {
        can_assign = 1;
        coerced_value.double_val = (double)expr_result->value.int_val;
    } else if (target->type == TYPE_DOUBLE && expr_result->type == TYPE_FLOAT) {
        can_assign = 1;
        coerced_value.double_val = (double)expr_result->value.float_val;
    } else if (target->type == TYPE_FLOAT && expr_result->type == TYPE_INT) {
        can_assign = 1;
        coerced_value.float_val = (float)expr_result->value.int_val;
    } else if (target->type == TYPE_FLOAT && expr_result->type == TYPE_DOUBLE) {
        can_assign = 1;
        coerced_value.float_val = (float)expr_result->value.double_val;
    } else if (target->type == TYPE_INT && expr_result->type == TYPE_DOUBLE) {
        can_assign = 1;
        coerced_value.int_val = (int)expr_result->value.double_val;
    } else if (target->type == TYPE_INT && expr_result->type == TYPE_FLOAT) {
        can_assign = 1;
        coerced_value.int_val = (int)expr_result->value.float_val;
    } else if (target->type == TYPE_STRUCT && expr_result->type == TYPE_CLASS) {
        // Allow assigning class to struct variable if they have the same name
        // This handles the case where a class type was parsed as a struct type
        can_assign = 1;
        coerced_value.class_val = expr_result->value.class_val;  // Extract class value
    } else {
        printf("Error: Type mismatch: cannot assign expression result of type %s to variable of type %s\n",
               type_name(expr_result->type),
               type_name(target->type));
        return 0;
    }

    if (!can_assign) return 0;

    switch (target->type) {
        case TYPE_STRING: {
            BreadString* s = expr_result->value.string_val;
            if (s) bread_string_retain(s);
            else {
                s = bread_string_new("");
                if (!s) return 0;
            }
            if (target->value.string_val) bread_string_release(target->value.string_val);
            target->value.string_val = s;
            break;
        }
        case TYPE_INT:
            target->value.int_val = coerced_value.int_val;
            break;
        case TYPE_BOOL:
            target->value.bool_val = coerced_value.bool_val;
            break;
        case TYPE_FLOAT:
            target->value.float_val = coerced_value.float_val;
            break;
        case TYPE_DOUBLE:
            target->value.double_val = coerced_value.double_val;
            break;
        case TYPE_ARRAY:
            if (target->value.array_val) bread_array_release(target->value.array_val);
            target->value.array_val = coerced_value.array_val;
            bread_array_retain(target->value.array_val);
            break;
        case TYPE_DICT:
            if (target->value.dict_val) bread_dict_release(target->value.dict_val);
            target->value.dict_val = coerced_value.dict_val;
            bread_dict_retain(target->value.dict_val);
            break;
        case TYPE_OPTIONAL:
            if (target->value.optional_val) bread_optional_release(target->value.optional_val);
            target->value.optional_val = coerced_value.optional_val;
            bread_optional_retain(target->value.optional_val);
            break;
        case TYPE_STRUCT:
            if (expr_result->type == TYPE_CLASS) {
                // Handle class assignment to struct variable
                if (target->value.struct_val) bread_struct_release(target->value.struct_val);
                // Convert the variable type to class and assign the class value
                target->type = TYPE_CLASS;
                target->value.class_val = coerced_value.class_val;
                bread_class_retain(target->value.class_val);
            } else {
                // Normal struct assignment
                if (target->value.struct_val) bread_struct_release(target->value.struct_val);
                target->value.struct_val = coerced_value.struct_val;
                bread_struct_retain(target->value.struct_val);
            }
            break;
        case TYPE_CLASS:
            if (target->value.class_val) bread_class_release(target->value.class_val);
            target->value.class_val = coerced_value.class_val;
            bread_class_retain(target->value.class_val);
            break;
        case TYPE_NIL:
            break;
        default:
            return 0;
    }

    if (target->type == TYPE_OPTIONAL && expr_result->type != TYPE_OPTIONAL) {
        if (coerced_value.optional_val) bread_optional_release(coerced_value.optional_val);
    }

    return 1;
}

static int set_variable_value(Variable* target, char* raw_value) {
    // First try to evaluate as an expression
    ExprResult expr_result = evaluate_expression(raw_value);
    if (!expr_result.is_error) {
        // Expression evaluated successfully, assign the result with type coercion
        int can_assign = 0;
        VarValue coerced_value;

        // Handle type coercion
        if (target->type == TYPE_NIL) {
            can_assign = 1;
            coerced_value = expr_result.value;
            target->type = expr_result.type;
        } else if (target->type == expr_result.type) {
            // Same type, direct assignment
            can_assign = 1;
            coerced_value = expr_result.value;
        } else if (target->type == TYPE_OPTIONAL && expr_result.type == TYPE_NIL) {
            can_assign = 1;
            coerced_value.optional_val = bread_optional_new_none();
            if (!coerced_value.optional_val) {
                release_expr_result(&expr_result);
                return 0;
            }
        } else if (target->type == TYPE_OPTIONAL && expr_result.type != TYPE_OPTIONAL) {
            can_assign = 1;
            BreadValue inner = bread_value_from_expr_result(expr_result);
            coerced_value.optional_val = bread_optional_new_some(inner);
            if (!coerced_value.optional_val) {
                release_expr_result(&expr_result);
                return 0;
            }
        } else if (target->type == TYPE_DOUBLE && expr_result.type == TYPE_INT) {
            can_assign = 1;
            coerced_value.double_val = (double)expr_result.value.int_val;
        } else if (target->type == TYPE_DOUBLE && expr_result.type == TYPE_FLOAT) {
            can_assign = 1;
            coerced_value.double_val = (double)expr_result.value.float_val;
        } else if (target->type == TYPE_FLOAT && expr_result.type == TYPE_INT) {
            can_assign = 1;
            coerced_value.float_val = (float)expr_result.value.int_val;
        } else if (target->type == TYPE_FLOAT && expr_result.type == TYPE_DOUBLE) {
            can_assign = 1;
            coerced_value.float_val = (float)expr_result.value.double_val;
        } else if (target->type == TYPE_INT && expr_result.type == TYPE_DOUBLE) {
            can_assign = 1;
            coerced_value.int_val = (int)expr_result.value.double_val;
        } else if (target->type == TYPE_INT && expr_result.type == TYPE_FLOAT) {
            can_assign = 1;
            coerced_value.int_val = (int)expr_result.value.float_val;
        } else {
            printf("Error: Type mismatch: cannot assign expression result of type %s to variable of type %s\n",
                   expr_result.type == TYPE_STRING ? "String" :
                   expr_result.type == TYPE_INT ? "Int" :
                   expr_result.type == TYPE_BOOL ? "Bool" :
                   expr_result.type == TYPE_FLOAT ? "Float" :
                   expr_result.type == TYPE_DOUBLE ? "Double" :
                   expr_result.type == TYPE_ARRAY ? "Array" :
                   expr_result.type == TYPE_DICT ? "Dict" :
                   expr_result.type == TYPE_OPTIONAL ? "Optional" : "Nil",
                   target->type == TYPE_STRING ? "String" :
                   target->type == TYPE_INT ? "Int" :
                   target->type == TYPE_BOOL ? "Bool" :
                   target->type == TYPE_FLOAT ? "Float" :
                   target->type == TYPE_DOUBLE ? "Double" :
                   target->type == TYPE_ARRAY ? "Array" :
                   target->type == TYPE_DICT ? "Dict" :
                   target->type == TYPE_OPTIONAL ? "Optional" : "Nil");
            release_expr_result(&expr_result);
            return 0;
        }

        if (can_assign) {
            switch (target->type) {
                case TYPE_STRING: {
                    BreadString* s = expr_result.value.string_val;
                    if (s) bread_string_retain(s);
                    else {
                        s = bread_string_new("");
                        if (!s) return 0;
                    }
                    set_string_value(target, s);
                    break;
                }
                case TYPE_INT:
                    target->value.int_val = coerced_value.int_val;
                    break;
                case TYPE_BOOL:
                    target->value.bool_val = coerced_value.bool_val;
                    break;
                case TYPE_FLOAT:
                    target->value.float_val = coerced_value.float_val;
                    break;
                case TYPE_DOUBLE:
                    target->value.double_val = coerced_value.double_val;
                    break;
                case TYPE_ARRAY:
                    if (target->value.array_val) bread_array_release(target->value.array_val);
                    target->value.array_val = coerced_value.array_val;
                    bread_array_retain(target->value.array_val);
                    break;
                case TYPE_DICT:
                    if (target->value.dict_val) bread_dict_release(target->value.dict_val);
                    target->value.dict_val = coerced_value.dict_val;
                    bread_dict_retain(target->value.dict_val);
                    break;
                case TYPE_OPTIONAL:
                    if (target->value.optional_val) bread_optional_release(target->value.optional_val);
                    target->value.optional_val = coerced_value.optional_val;
                    bread_optional_retain(target->value.optional_val);
                    break;
                case TYPE_STRUCT:
                    if (target->value.struct_val) bread_struct_release(target->value.struct_val);
                    target->value.struct_val = coerced_value.struct_val;
                    bread_struct_retain(target->value.struct_val);
                    break;
                case TYPE_CLASS:
                    if (target->value.class_val) bread_class_release(target->value.class_val);
                    target->value.class_val = coerced_value.class_val;
                    bread_class_retain(target->value.class_val);
                    break;
                case TYPE_NIL:
                    break;
                default:
                    printf("Error: Unsupported type for expression assignment\n");
                    return 0;
            }
            if (target->type == TYPE_OPTIONAL && expr_result.type != TYPE_OPTIONAL) {
                release_expr_result(&expr_result);
            } else {
                release_expr_result(&expr_result);
            }
            return 1;
        }
    }

    return 0;
}

static int parse_type(char* type_str, VarType* out_type) {
    size_t tlen = strlen(type_str);
    if (tlen > 0 && type_str[tlen - 1] == '?') {
        type_str[tlen - 1] = '\0';
        if (!parse_type(type_str, out_type)) {
            type_str[tlen - 1] = '?';
            return 0;
        }
        *out_type = TYPE_OPTIONAL;
        type_str[tlen - 1] = '?';
        return 1;
    }

    if (type_str[0] == '[') {
        char* end = strrchr(type_str, ']');
        if (!end) {
            printf("Error: Unknown type '%s'\n", type_str);
            return 0;
        }
        int depth = 0;
        for (char* p = type_str + 1; p < end; p++) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == ':' && depth == 0) {
                *out_type = TYPE_DICT;
                return 1;
            }
        }
        *out_type = TYPE_ARRAY;
        return 1;
    }

    if (strcmp(type_str, "String") == 0) {
        *out_type = TYPE_STRING;
        return 1;
    }
    if (strcmp(type_str, "Int") == 0) {
        *out_type = TYPE_INT;
        return 1;
    }
    if (strcmp(type_str, "Bool") == 0) {
        *out_type = TYPE_BOOL;
        return 1;
    }
    if (strcmp(type_str, "Float") == 0) {
        *out_type = TYPE_FLOAT;
        return 1;
    }
    if (strcmp(type_str, "Double") == 0) {
        *out_type = TYPE_DOUBLE;
        return 1;
    }

    printf("Error: Unknown type '%s'\n", type_str);
    return 0;
}

void execute_variable_declaration(char* line) {
    char* trimmed = trim_var(line);
    int is_const = 0;
    char* start;
    
    if (strncmp(trimmed, "let ", 4) == 0) {
        start = trimmed + 4;
        is_const = 0;
    } else if (strncmp(trimmed, "const ", 6) == 0) {
        start = trimmed + 6;
        is_const = 1;
    } else {
        return;
    }
    
    // Parse: name: Type = value
    char* colon = strchr(start, ':');
    if (!colon) {
        printf("Error: Missing type annotation\n");
        return;
    }
    
    char var_name[MAX_LINE];
    int name_len = colon - start;
    strncpy(var_name, start, name_len);
    var_name[name_len] = '\0';
    char* var_name_trimmed = trim_var(var_name);
    char* type_start = colon + 1;
    char* equals = strchr(type_start, '=');
    if (!equals) {
        printf("Error: Missing assignment\n");
        return;
    }
    
    char type_str[MAX_LINE];
    int type_len = equals - type_start;
    strncpy(type_str, type_start, type_len);
    type_str[type_len] = '\0';
    char* type_trimmed = trim_var(type_str);
    char* value_start = equals + 1;
    char* value_trimmed = trim_var(value_start);
    
    VarType parsed_type;
    if (!parse_type(type_trimmed, &parsed_type)) {
        return;
    }

    VarValue initial_value;
    memset(&initial_value, 0, sizeof(VarValue));
    if (!declare_variable_raw(var_name_trimmed, parsed_type, initial_value, is_const)) {
        return;
    }

    Variable* var = get_variable(var_name_trimmed);
    if (!var) {
        return;
    }

    if (!set_variable_value(var, value_trimmed)) {
        (void)0;
        return;
    }
}

void execute_variable_assignment(char* line) {
    char* equals = strchr(line, '=');
    if (!equals) {
        printf("Error: Missing assignment operator\n");
        return;
    }

    char name_buf[MAX_LINE];
    int name_len = equals - line;
    strncpy(name_buf, line, name_len);
    name_buf[name_len] = '\0';
    char* var_name = trim_var(name_buf);

    if (strlen(var_name) == 0) {
        printf("Error: Missing variable name\n");
        return;
    }

    Variable* var = get_variable(var_name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", var_name);
        return;
    }

    if (var->is_const) {
        printf("Error: Cannot reassign constant '%s'\n", var_name);
        return;
    }

    char* value = trim_var(equals + 1);
    if (strlen(value) == 0) {
        printf("Error: Missing value for '%s'\n", var_name);
        return;
    }

    if (!set_variable_value(var, value)) {
        return;
    }
}

int bread_init_variable_from_expr_result(const char* name, const struct ExprResult* value) {
    if (!name) return 0;
    if (!value) return 0;
    Variable* var = get_variable((char*)name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name);
        return 0;
    }

    return set_variable_value_from_expr_result(var, value);
}

int bread_assign_variable_from_expr_result(const char* name, const struct ExprResult* value) {
    if (!name) return 0;
    if (!value) return 0;
    Variable* var = get_variable((char*)name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name);
        return 0;
    }
    if (var->is_const) {
        printf("Error: Cannot reassign constant '%s'\n", name);
        return 0;
    }
    return bread_init_variable_from_expr_result(name, value);
}

void cleanup_variables() {
    for (int s = scope_depth - 1; s >= 0; s--) {
        for (int i = 0; i < scopes[s].count; i++) {
            release_variable(&scopes[s].vars[i]);
        }
        scopes[s].count = 0;
    }
    scope_depth = 0;
}