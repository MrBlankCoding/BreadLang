#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "core/var.h"
#include "compiler/parser/expr.h"
#include "core/value.h"
#include "runtime/runtime.h"
#include "runtime/error.h"

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
static void release_expr_result(ExprResult* r);
static const char* type_name(VarType t);
static int coerce_and_assign(Variable* target, VarType source_type, VarValue source_value);

char* trim_var(char* str) {
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    
    char* end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void init_variables() {
    scopes[0].count = 0;
    scope_depth = 1;
}

void push_scope() {
    if (scope_depth >= MAX_SCOPES) {
        BREAD_ERROR_SET_RUNTIME("Scope stack overflow");
        return;
    }
    scopes[scope_depth].count = 0;
    scope_depth++;
}

void pop_scope() {
    if (scope_depth <= 1) {
        BREAD_ERROR_SET_RUNTIME("Cannot pop global scope");
        return;
    }
    VarScope* scope = &scopes[scope_depth - 1];
    for (int i = 0; i < scope->count; i++) {
        release_variable(&scope->vars[i]);
    }
    scope->count = 0;
    scope_depth--;
}

int can_pop_scope() {
    return scope_depth > 1;
}

int scope_depth_current(void) {
    return scope_depth;
}

void pop_to_scope_depth(int target_depth) {
    if (target_depth < 1 || target_depth > scope_depth) {
        BREAD_ERROR_SET_RUNTIME(target_depth < 1 ? 
            "Invalid target scope depth" : 
            "Cannot pop to a deeper scope depth");
        return;
    }
    while (scope_depth > target_depth) {
        pop_scope();
    }
}

Variable* get_variable(const char* name) {
    if (!name) return NULL;

    const char* start = name;
    while (*start && isspace((unsigned char)*start)) start++;

    size_t len = strnlen(start, 255);
    while (len > 0 && isspace((unsigned char)start[len - 1])) len--;

    for (int s = scope_depth - 1; s >= 0; s--) {
        for (int i = 0; i < scopes[s].count; i++) {
            const char* vname = scopes[s].vars[i].name;
            if (vname && strncmp(vname, start, len) == 0 && vname[len] == '\0') {
                return &scopes[s].vars[i];
            }
        }
    }
    return NULL;
}

static int retain_value_by_type(VarType type, VarValue* value) {
    switch (type) {
        case TYPE_STRING:
            if (value->string_val) {
                bread_string_retain(value->string_val);
            } else {
                value->string_val = bread_string_new("");
                if (!value->string_val) return 0;
            }
            break;
        case TYPE_ARRAY:
            bread_array_retain(value->array_val);
            break;
        case TYPE_DICT:
            bread_dict_retain(value->dict_val);
            break;
        case TYPE_OPTIONAL:
            bread_optional_retain(value->optional_val);
            break;
        case TYPE_STRUCT:
            bread_struct_retain(value->struct_val);
            break;
        case TYPE_CLASS:
            bread_class_retain(value->class_val);
            break;
        default:
            break;
    }
    return 1;
}

int declare_variable_raw(const char* name, VarType type, VarValue value, int is_const) {
    if (scope_depth <= 0) {
        BREAD_ERROR_SET_RUNTIME("Variable system not initialized");
        return 0;
    }

    VarScope* scope = &scopes[scope_depth - 1];

    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->vars[i].name, name) == 0) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Variable '%s' already declared", name);
            BREAD_ERROR_SET_RUNTIME(error_msg);
            return 0;
        }
    }

    if (scope->count >= MAX_VARS) {
        BREAD_ERROR_SET_RUNTIME("Too many variables in scope");
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
    var->value = value;

    if (!retain_value_by_type(type, &var->value)) {
        release_variable(var);
        return 0;
    }

    scope->count++;
    return 1;
}

static void release_variable(Variable* var) {
    if (var->type == TYPE_STRING && var->value.string_val) {
        bread_string_release(var->value.string_val);
    } else if (var->type == TYPE_ARRAY && var->value.array_val) {
        bread_array_release(var->value.array_val);
    } else if (var->type == TYPE_DICT && var->value.dict_val) {
        bread_dict_release(var->value.dict_val);
    } else if (var->type == TYPE_OPTIONAL && var->value.optional_val) {
        bread_optional_release(var->value.optional_val);
    } else if (var->type == TYPE_STRUCT && var->value.struct_val) {
        bread_struct_release(var->value.struct_val);
    } else if (var->type == TYPE_CLASS && var->value.class_val) {
        bread_class_release(var->value.class_val);
    }
    
    if (var->name) {
        free(var->name);
        var->name = NULL;
    }
    memset(&var->value, 0, sizeof(var->value));
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
        default: return "Unknown";
    }
}

static int try_numeric_coercion(VarType target_type, VarType source_type, 
                                VarValue source_value, VarValue* out_value) {
    if (target_type == TYPE_DOUBLE && source_type == TYPE_INT) {
        out_value->double_val = (double)source_value.int_val;
        return 1;
    }
    if (target_type == TYPE_DOUBLE && source_type == TYPE_FLOAT) {
        out_value->double_val = (double)source_value.float_val;
        return 1;
    }
    if (target_type == TYPE_FLOAT && source_type == TYPE_INT) {
        out_value->float_val = (float)source_value.int_val;
        return 1;
    }
    if (target_type == TYPE_FLOAT && source_type == TYPE_DOUBLE) {
        out_value->float_val = (float)source_value.double_val;
        return 1;
    }
    if (target_type == TYPE_INT && source_type == TYPE_DOUBLE) {
        out_value->int_val = (int)source_value.double_val;
        return 1;
    }
    if (target_type == TYPE_INT && source_type == TYPE_FLOAT) {
        out_value->int_val = (int)source_value.float_val;
        return 1;
    }
    return 0;
}

static int try_optional_coercion(VarType target_type, VarType source_type,
                                 VarValue source_value, VarValue* out_value) {
    if (target_type != TYPE_OPTIONAL) return 0;
    
    if (source_type == TYPE_NIL) {
        out_value->optional_val = bread_optional_new_none();
        return out_value->optional_val != NULL;
    }
    
    if (source_type != TYPE_OPTIONAL) {
        BreadValue inner;
        memset(&inner, 0, sizeof(inner));
        inner.type = source_type;
        inner.value = source_value;
        out_value->optional_val = bread_optional_new_some(inner);
        return out_value->optional_val != NULL;
    }
    
    return 0;
}

static int coerce_and_assign(Variable* target, VarType source_type, VarValue source_value) {
    VarValue coerced_value = source_value;
    VarType original_target_type = target->type;
    int needs_optional_cleanup = 0;

    if (target->type == TYPE_NIL) {
        target->type = source_type;
    } else if (target->type != source_type) {
        if (try_numeric_coercion(target->type, source_type, source_value, &coerced_value)) {
        } else if (try_optional_coercion(target->type, source_type, source_value, &coerced_value)) {
            needs_optional_cleanup = (source_type != TYPE_OPTIONAL);
        } else if (target->type == TYPE_STRUCT && source_type == TYPE_CLASS) {
            target->type = TYPE_CLASS;
        } else {
            printf("Error: Type mismatch: cannot assign %s to %s\n",
                   type_name(source_type), type_name(target->type));
            return 0;
        }
    }

    switch (target->type) {
        case TYPE_STRING: {
            BreadString* s = coerced_value.string_val;
            if (s) {
                bread_string_retain(s);
            } else {
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
            if (original_target_type == TYPE_STRUCT && source_type == TYPE_CLASS) {
                if (target->value.struct_val) bread_struct_release(target->value.struct_val);
                target->value.class_val = coerced_value.class_val;
                bread_class_retain(target->value.class_val);
            } else {
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

    if (needs_optional_cleanup && coerced_value.optional_val) {
        bread_optional_release(coerced_value.optional_val);
    }

    return 1;
}

static int set_variable_value_from_expr_result(Variable* target, const ExprResult* expr_result) {
    if (!target || !expr_result || expr_result->is_error) return 0;
    return coerce_and_assign(target, expr_result->type, expr_result->value);
}

static int set_variable_value(Variable* target, char* raw_value) {
    ExprResult expr_result = evaluate_expression(raw_value);
    if (expr_result.is_error) {
        return 0;
    }

    int success = coerce_and_assign(target, expr_result.type, expr_result.value);
    release_expr_result(&expr_result);
    return success;
}

static int parse_type(char* type_str, VarType* out_type) {
    size_t tlen = strlen(type_str);
    if (tlen > 0 && type_str[tlen - 1] == '?') {
        type_str[tlen - 1] = '\0';
        int result = parse_type(type_str, out_type);
        type_str[tlen - 1] = '?';
        if (result) {
            *out_type = TYPE_OPTIONAL;
        }
        return result;
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

    const struct { const char* name; VarType type; } type_map[] = {
        {"String", TYPE_STRING},
        {"Int", TYPE_INT},
        {"Bool", TYPE_BOOL},
        {"Float", TYPE_FLOAT},
        {"Double", TYPE_DOUBLE},
    };

    for (size_t i = 0; i < sizeof(type_map) / sizeof(type_map[0]); i++) {
        if (strcmp(type_str, type_map[i].name) == 0) {
            *out_type = type_map[i].type;
            return 1;
        }
    }

    printf("Error: Unknown type '%s'\n", type_str);
    return 0;
}

void execute_variable_declaration(char* line) {
    char* trimmed = trim_var(line);
    int is_const;
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
    
    char* value_trimmed = trim_var(equals + 1);
    
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
    if (var) {
        set_variable_value(var, value_trimmed);
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

    set_variable_value(var, value);
}

int bread_init_variable_from_expr_result(const char* name, const struct ExprResult* value) {
    if (!name || !value) return 0;
    
    Variable* var = get_variable(name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name);
        return 0;
    }

    return set_variable_value_from_expr_result(var, value);
}

int bread_assign_variable_from_expr_result(const char* name, const struct ExprResult* value) {
    if (!name || !value) return 0;
    
    Variable* var = get_variable(name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name);
        return 0;
    }
    
    if (var->is_const) {
        printf("Error: Cannot reassign constant '%s'\n", name);
        return 0;
    }
    
    return set_variable_value_from_expr_result(var, value);
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