#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
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
        BREAD_ERROR_SET_RUNTIME("Too many built-in functions");
        return 0;
    }

    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(builtins[i].name, builtin->name) == 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Built-in function '%s' already registered", builtin->name);
            BREAD_ERROR_SET_RUNTIME(error_msg);
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
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Unknown built-in function '%s'", name ? name : "");
        BREAD_ERROR_SET_RUNTIME(error_msg);
        return result;
    }
    
    if (builtin->param_count != arg_count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Built-in function '%s' expects %d arguments, got %d", 
                name, builtin->param_count, arg_count);
        BREAD_ERROR_SET_RUNTIME(error_msg);
        return result;
    }
    
    for (int i = 0; i < arg_count && builtin->param_types; i++) {
        if (builtin->param_types[i] != TYPE_NIL && args[i].type != builtin->param_types[i]) {
            if (!(builtin->param_types[i] == TYPE_DOUBLE && 
                  (args[i].type == TYPE_INT || args[i].type == TYPE_FLOAT))) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Built-in function '%s' parameter %d expects type %d, got %d", 
                        name, i, builtin->param_types[i], args[i].type);
                BREAD_ERROR_SET_TYPE_MISMATCH(error_msg);
                return result;
            }
        }
    }
    
    return builtin->implementation(args, arg_count);
}

void bread_builtin_call_out(const char* name, BreadValue* args, int arg_count, BreadValue* out) {
    if (!out) return;
    BreadValue result = bread_builtin_call(name, args, arg_count);
    bread_value_copy(&result, out);
    bread_value_release_value(&result);
}

// Built-in function implementations

// len() function - returns length of strings and arrays
BreadValue bread_builtin_len(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        BREAD_ERROR_SET_RUNTIME("len() expects 1 argument");
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
            BREAD_ERROR_SET_RUNTIME("len() not supported for this type");
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
        BREAD_ERROR_SET_RUNTIME("type() expects 1 argument");
        return result;
    }
    
    BreadValue* arg = &args[0];
    const char* type_name = NULL;
    
    switch (arg->type) {
        case TYPE_NIL:
            type_name = "Nil";
            break;
        case TYPE_BOOL:
            type_name = "Bool";
            break;
        case TYPE_INT:
            type_name = "Int";
            break;
        case TYPE_FLOAT:
            type_name = "Float";
            break;
        case TYPE_DOUBLE:
            type_name = "Double";
            break;
        case TYPE_STRING:
            type_name = "String";
            break;
        case TYPE_ARRAY:
            type_name = "Array";
            break;
        case TYPE_DICT:
            type_name = "Dict";
            break;
        case TYPE_OPTIONAL:
            type_name = "Optional";
            break;
        default:
            type_name = "Unknown";
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
        BREAD_ERROR_SET_RUNTIME("str() expects 1 argument");
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
            snprintf(buffer, sizeof(buffer), "%.6g", (double)arg->value.float_val);
            bread_value_set_string(&result, buffer);
            break;
        case TYPE_DOUBLE: {
            // Use a more robust double-to-string conversion
            double val = arg->value.double_val;
            if (val == (int)val && val >= -2147483648.0 && val <= 2147483647.0) {
                // If it's a whole number within int range, format as int
                snprintf(buffer, sizeof(buffer), "%d", (int)val);
            } else {
                snprintf(buffer, sizeof(buffer), "%.6g", val);
            }
            bread_value_set_string(&result, buffer);
            break;
        }
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
        case TYPE_STRUCT: {
            BreadStruct* s = arg->value.struct_val;
            if (s && s->type_name) {
                char struct_str[256];
                snprintf(struct_str, sizeof(struct_str), "%s{}", s->type_name);
                bread_value_set_string(&result, struct_str);
            } else {
                bread_value_set_string(&result, "struct");
            }
            break;
        }
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
        BREAD_ERROR_SET_RUNTIME("int() expects 1 argument");
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
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Cannot convert string '%s' to int", str);
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
        }
        default:
            BREAD_ERROR_SET_RUNTIME("Cannot convert this type to int");
            break;
    }
    
    return result;
}

// float() function - converts compatible values to floats
BreadValue bread_builtin_float(BreadValue* args, int arg_count) {
    BreadValue result;
    bread_value_set_nil(&result);
    
    if (arg_count != 1) {
        BREAD_ERROR_SET_RUNTIME("float() expects 1 argument");
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
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Cannot convert string '%s' to float", str);
                BREAD_ERROR_SET_RUNTIME(error_msg);
            }
            break;
        }
        default:
            BREAD_ERROR_SET_RUNTIME("Cannot convert this type to float");
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