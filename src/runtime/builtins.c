#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "runtime/runtime.h"
#include "core/value.h"

#define MAX_BUILTINS 64
static BuiltinFunction builtins[MAX_BUILTINS];
static int builtin_count = 0;
static void register_builtin_functions(void);

void bread_builtin_init(void) {
    builtin_count = 0;
    register_builtin_functions();
}

void bread_builtin_cleanup(void) {
    for (int i = 0; i < builtin_count; i++) {
        free(builtins[i].name);
        free(builtins[i].param_types);
        builtins[i].name = NULL;
        builtins[i].param_types = NULL;
    }
    builtin_count = 0;
}

int bread_builtin_register(const BuiltinFunction* builtin) {
    if (!builtin || !builtin->name || !builtin->implementation) return 0;
    if (builtin_count >= MAX_BUILTINS) {
        printf("Error: Too many built-in functions\n");
        return 0;
    }

    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(builtins[i].name, builtin->name) == 0) {
            printf("Error: Built-in function '%s' already registered\n", builtin->name);
            return 0;
        }
    }

    BuiltinFunction* dst = &builtins[builtin_count];
    dst->name = strdup(builtin->name);
    if (!dst->name) return 0;
    
    dst->param_count = builtin->param_count;
    dst->return_type = builtin->return_type;
    dst->implementation = builtin->implementation;
    
    if (builtin->param_count > 0 && builtin->param_types) {
        dst->param_types = malloc(sizeof(VarType) * builtin->param_count);
        if (!dst->param_types) {
            free(dst->name);
            return 0;
        }
        memcpy(dst->param_types, builtin->param_types, sizeof(VarType) * builtin->param_count);
    } else {
        dst->param_types = NULL;
    }

    builtin_count++;
    return 1;
}

const BuiltinFunction* bread_builtin_lookup(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            return &builtins[i];
        }
    }
    return NULL;
}

BreadValue bread_builtin_call(const char* name, BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    const BuiltinFunction* builtin = bread_builtin_lookup(name);
    if (!builtin) {
        printf("Error: Unknown built-in function '%s'\n", name ? name : "");
        return result;
    }
    
    if (builtin->param_count != arg_count) {
        printf("Error: Built-in function '%s' expects %d arguments, got %d\n", 
               name, builtin->param_count, arg_count);
        return result;
    }
    
    for (int i = 0; i < arg_count && builtin->param_types; i++) {
        if (builtin->param_types[i] != TYPE_NIL && args[i].type != builtin->param_types[i]) {
            if (!(builtin->param_types[i] == TYPE_DOUBLE && 
                  (args[i].type == TYPE_INT || args[i].type == TYPE_FLOAT))) {
                printf("Error: Built-in function '%s' parameter %d expects type %d, got %d\n", 
                       name, i, builtin->param_types[i], args[i].type);
                return result;
            }
        }
    }
    
    return builtin->implementation(args, arg_count);
}

// Built-in function implementations

// len() function - returns length of strings and arrays
BreadValue bread_builtin_len(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        printf("Error: len() expects 1 argument\n");
        return result;
    }
    
    BreadValue* arg = &args[0];
    int length = 0;
    
    switch (arg->type) {
        case TYPE_STRING:
            length = (int)bread_string_len(arg->value.string_val);
            break;
        case TYPE_ARRAY:
            length = bread_array_length(arg->value.array_val);
            break;
        case TYPE_DICT:
            length = arg->value.dict_val ? arg->value.dict_val->count : 0;
            break;
        default:
            printf("Error: len() not supported for this type\n");
            return result;
    }
    
    bread_value_set_int(&result, length);
    return result;
}

// type() function - returns string representation of type
BreadValue bread_builtin_type(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        printf("Error: type() expects 1 argument\n");
        return result;
    }
    
    BreadValue* arg = &args[0];
    const char* type_name = NULL;
    
    switch (arg->type) {
        case TYPE_NIL:
            type_name = "nil";
            break;
        case TYPE_BOOL:
            type_name = "bool";
            break;
        case TYPE_INT:
            type_name = "int";
            break;
        case TYPE_FLOAT:
            type_name = "float";
            break;
        case TYPE_DOUBLE:
            type_name = "double";
            break;
        case TYPE_STRING:
            type_name = "string";
            break;
        case TYPE_ARRAY:
            type_name = "array";
            break;
        case TYPE_DICT:
            type_name = "dict";
            break;
        case TYPE_OPTIONAL:
            type_name = "optional";
            break;
        default:
            type_name = "unknown";
            break;
    }
    
    bread_value_set_string(&result, type_name);
    return result;
}

// str() function - converts any value to string representation
BreadValue bread_builtin_str(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        printf("Error: str() expects 1 argument\n");
        return result;
    }
    
    BreadValue* arg = &args[0];
    char buffer[256];
    
    switch (arg->type) {
        case TYPE_NIL:
            bread_value_set_string(&result, "nil");
            break;
        case TYPE_BOOL:
            bread_value_set_string(&result, arg->value.bool_val ? "true" : "false");
            break;
        case TYPE_INT:
            snprintf(buffer, sizeof(buffer), "%d", arg->value.int_val);
            bread_value_set_string(&result, buffer);
            break;
        case TYPE_FLOAT:
            snprintf(buffer, sizeof(buffer), "%f", arg->value.float_val);
            bread_value_set_string(&result, buffer);
            break;
        case TYPE_DOUBLE:
            snprintf(buffer, sizeof(buffer), "%lf", arg->value.double_val);
            bread_value_set_string(&result, buffer);
            break;
        case TYPE_STRING:
            // Return a copy of the string
            bread_value_set_string(&result, bread_string_cstr(arg->value.string_val));
            break;
        case TYPE_ARRAY:
            bread_value_set_string(&result, "[array]");
            break;
        case TYPE_DICT:
            bread_value_set_string(&result, "{dict}");
            break;
        case TYPE_OPTIONAL:
            bread_value_set_string(&result, "optional");
            break;
        default:
            bread_value_set_string(&result, "unknown");
            break;
    }
    
    return result;
}

// int() function - converts compatible values to integers
BreadValue bread_builtin_int(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        printf("Error: int() expects 1 argument\n");
        return result;
    }
    
    BreadValue* arg = &args[0];
    
    switch (arg->type) {
        case TYPE_INT:
            bread_value_set_int(&result, arg->value.int_val);
            break;
        case TYPE_FLOAT:
            bread_value_set_int(&result, (int)arg->value.float_val);
            break;
        case TYPE_DOUBLE:
            bread_value_set_int(&result, (int)arg->value.double_val);
            break;
        case TYPE_BOOL:
            bread_value_set_int(&result, arg->value.bool_val ? 1 : 0);
            break;
        case TYPE_STRING: {
            const char* str = bread_string_cstr(arg->value.string_val);
            char* endptr;
            long val = strtol(str, &endptr, 10);
            if (*endptr == '\0' && val >= INT_MIN && val <= INT_MAX) {
                bread_value_set_int(&result, (int)val);
            } else {
                printf("Error: Cannot convert string '%s' to int\n", str);
            }
            break;
        }
        default:
            printf("Error: Cannot convert this type to int\n");
            break;
    }
    
    return result;
}

// float() function - converts compatible values to floats
BreadValue bread_builtin_float(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        printf("Error: float() expects 1 argument\n");
        return result;
    }
    
    BreadValue* arg = &args[0];
    
    switch (arg->type) {
        case TYPE_INT:
            bread_value_set_double(&result, (double)arg->value.int_val);
            break;
        case TYPE_FLOAT:
            bread_value_set_double(&result, (double)arg->value.float_val);
            break;
        case TYPE_DOUBLE:
            bread_value_set_double(&result, arg->value.double_val);
            break;
        case TYPE_BOOL:
            bread_value_set_double(&result, arg->value.bool_val ? 1.0 : 0.0);
            break;
        case TYPE_STRING: {
            const char* str = bread_string_cstr(arg->value.string_val);
            char* endptr;
            double val = strtod(str, &endptr);
            if (*endptr == '\0') {
                bread_value_set_double(&result, val);
            } else {
                printf("Error: Cannot convert string '%s' to float\n", str);
            }
            break;
        }
        default:
            printf("Error: Cannot convert this type to float\n");
            break;
    }
    
    return result;
}
// Register
static void register_builtin_functions(void) {
    // len() function
    {
        VarType len_params[] = {TYPE_NIL};
        BuiltinFunction len_fn = {
            .name = "len",
            .param_count = 1,
            .param_types = len_params,
            .return_type = TYPE_INT,
            .implementation = bread_builtin_len
        };
        bread_builtin_register(&len_fn);
    }
    
    // type() function
    {
        VarType type_params[] = {TYPE_NIL};
        BuiltinFunction type_fn = {
            .name = "type",
            .param_count = 1,
            .param_types = type_params,
            .return_type = TYPE_STRING,
            .implementation = bread_builtin_type
        };
        bread_builtin_register(&type_fn);
    }
    
    // str() function
    {
        VarType str_params[] = {TYPE_NIL};
        BuiltinFunction str_fn = {
            .name = "str",
            .param_count = 1,
            .param_types = str_params,
            .return_type = TYPE_STRING,
            .implementation = bread_builtin_str
        };
        bread_builtin_register(&str_fn);
    }
    
    // int() function
    {
        VarType int_params[] = {TYPE_NIL};
        BuiltinFunction int_fn = {
            .name = "int",
            .param_count = 1,
            .param_types = int_params,
            .return_type = TYPE_INT,
            .implementation = bread_builtin_int
        };
        bread_builtin_register(&int_fn);
    }
    
    // float() function
    {
        VarType float_params[] = {TYPE_NIL};
        BuiltinFunction float_fn = {
            .name = "float",
            .param_count = 1,
            .param_types = float_params,
            .return_type = TYPE_DOUBLE,
            .implementation = bread_builtin_float
        };
        bread_builtin_register(&float_fn);
    }
}