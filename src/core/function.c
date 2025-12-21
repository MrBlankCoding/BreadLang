#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/function.h"
#include "compiler/ast/ast.h"
#include "core/var.h"
#include "core/value.h"

#define MAX_FUNCTIONS 256

static Function functions[MAX_FUNCTIONS];
static int function_count = 0;

static void free_function(Function* fn) {
    if (!fn) return;
    free(fn->name);
    for (int i = 0; i < fn->param_count; i++) {
        free(fn->param_names[i]);
    }
    free(fn->param_names);
    free(fn->param_types);
    if (fn->parameters) {
        for (int i = 0; i < fn->param_count; i++) {
            if (fn->parameters[i].has_default) {
                bread_value_release(&fn->parameters[i].default_value);
            }
        }
        free(fn->parameters);
    }
    fn->body = NULL;
    fn->body_is_ast = 0;
    fn->name = NULL;
    fn->param_count = 0;
    fn->param_names = NULL;
    fn->param_types = NULL;
    fn->parameters = NULL;
}

void init_functions() {
    function_count = 0;
}

void cleanup_functions() {
    for (int i = 0; i < function_count; i++) {
        free_function(&functions[i]);
    }
    function_count = 0;
}

static int same_name(const char* a, const char* b) {
    return strcmp(a ? a : "", b ? b : "") == 0;
}

int register_function(const Function* fn) {
    if (!fn || !fn->name) return 0;
    if (function_count >= MAX_FUNCTIONS) {
        printf("Error: Too many functions\n");
        return 0;
    }

    for (int i = 0; i < function_count; i++) {
        if (same_name(functions[i].name, fn->name)) {
            printf("Error: Function '%s' already declared\n", fn->name);
            return 0;
        }
    }

    Function* dst = &functions[function_count];
    memset(dst, 0, sizeof(Function));
    dst->name = strdup(fn->name);
    if (!dst->name) return 0;
    dst->param_count = fn->param_count;
    dst->return_type = fn->return_type;
    dst->body = fn->body;
    dst->body_is_ast = fn->body_is_ast;
    dst->hot_count = 0;
    dst->is_jitted = 0;
    dst->jit_fn = NULL;
    dst->jit_engine = NULL;

    if (fn->param_count > 0) {
        dst->param_names = malloc(sizeof(char*) * fn->param_count);
        dst->param_types = malloc(sizeof(VarType) * fn->param_count);
        dst->parameters = malloc(sizeof(FunctionParameter) * fn->param_count);
        if (!dst->param_names || !dst->param_types || !dst->parameters) {
            free_function(dst);
            return 0;
        }
        for (int i = 0; i < fn->param_count; i++) {
            dst->param_names[i] = strdup(fn->param_names[i]);
            if (!dst->param_names[i]) {
                free_function(dst);
                return 0;
            }
            dst->param_types[i] = fn->param_types[i];
            
            // Copy parameter info including defaults
            if (fn->parameters) {
                dst->parameters[i].name = strdup(fn->parameters[i].name);
                dst->parameters[i].has_default = fn->parameters[i].has_default;
                if (fn->parameters[i].has_default) {
                    dst->parameters[i].default_value = bread_value_clone(fn->parameters[i].default_value);
                } else {
                    memset(&dst->parameters[i].default_value, 0, sizeof(BreadValue));
                }
            } else {
                dst->parameters[i].name = strdup(fn->param_names[i]);
                dst->parameters[i].has_default = 0;
                memset(&dst->parameters[i].default_value, 0, sizeof(BreadValue));
            }
        }
    }

    function_count++;
    return 1;
}

const Function* get_function(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < function_count; i++) {
        if (same_name(functions[i].name, name)) return &functions[i];
    }
    return NULL;
}

int get_function_count() {
    return function_count;
}

const Function* get_function_at(int index) {
    if (index < 0 || index >= function_count) return NULL;
    return &functions[index];
}

int type_compatible(VarType expected, VarType actual) {
    if (expected == actual) return 1;
    if (expected == TYPE_OPTIONAL && actual == TYPE_NIL) return 1;
    if (expected == TYPE_OPTIONAL && actual != TYPE_OPTIONAL) return 1;
    if (expected == TYPE_DOUBLE && (actual == TYPE_INT || actual == TYPE_FLOAT)) return 1;
    if (expected == TYPE_FLOAT && actual == TYPE_INT) return 1;
    if (expected == TYPE_INT && (actual == TYPE_FLOAT || actual == TYPE_DOUBLE)) return 1;
    return 0;
}

VarValue coerce_value(VarType target, ExprResult val) {
    VarValue out;
    memset(&out, 0, sizeof(out));
    if (target == TYPE_OPTIONAL && val.type == TYPE_NIL) {
        out.optional_val = bread_optional_new_none();
        return out;
    }
    if (target == TYPE_OPTIONAL && val.type != TYPE_OPTIONAL) {
        BreadValue inner = bread_value_from_expr_result(val);
        out.optional_val = bread_optional_new_some(inner);
        return out;
    }
    if (target == val.type) {
        out = val.value;
        return out;
    }
    if (target == TYPE_DOUBLE && val.type == TYPE_INT) {
        out.double_val = (double)val.value.int_val;
        return out;
    }
    if (target == TYPE_DOUBLE && val.type == TYPE_FLOAT) {
        out.double_val = (double)val.value.float_val;
        return out;
    }
    if (target == TYPE_FLOAT && val.type == TYPE_INT) {
        out.float_val = (float)val.value.int_val;
        return out;
    }
    if (target == TYPE_INT && val.type == TYPE_DOUBLE) {
        out.int_val = (int)val.value.double_val;
        return out;
    }
    if (target == TYPE_INT && val.type == TYPE_FLOAT) {
        out.int_val = (int)val.value.float_val;
        return out;
    }
    out = val.value;
    return out;
}

// AST function execution removed - LLVM JIT only

ExprResult call_function_values(const char* name, int arg_count, ExprResult* arg_vals) {
    (void)arg_count;
    (void)arg_vals;
    
    printf("Error: Function '%s' calls are only supported through LLVM JIT compilation\n", name ? name : "unknown");
    ExprResult err;
    memset(&err, 0, sizeof(err));
    err.is_error = 1;
    return err;
}

ExprResult call_function(const char* name, int arg_count, const char** arg_exprs) {
    (void)arg_count;
    (void)arg_exprs;
    
    printf("Error: Function '%s' calls are only supported through LLVM JIT compilation\n", name ? name : "unknown");
    ExprResult err;
    memset(&err, 0, sizeof(err));
    err.is_error = 1;
    return err;
}

// Default parameter handling functions
int function_get_required_params(const Function* fn) {
    if (!fn || !fn->parameters) return fn ? fn->param_count : 0;
    
    int required = 0;
    for (int i = 0; i < fn->param_count; i++) {
        if (!fn->parameters[i].has_default) {
            required++;
        } else {
            break; // All defaults must be at the end
        }
    }
    return required;
}

int function_apply_defaults(const Function* fn, int provided_args, ExprResult* args, ExprResult** final_args) {
    if (!fn || !final_args) return 0;
    
    int required = function_get_required_params(fn);
    if (provided_args < required) {
        printf("Error: Function '%s' requires at least %d arguments, got %d\n", 
               fn->name ? fn->name : "unknown", required, provided_args);
        return 0;
    }
    
    if (provided_args > fn->param_count) {
        printf("Error: Function '%s' takes at most %d arguments, got %d\n", 
               fn->name ? fn->name : "unknown", fn->param_count, provided_args);
        return 0;
    }
    
    // Allocate array for final arguments
    *final_args = malloc(sizeof(ExprResult) * fn->param_count);
    if (!*final_args) return 0;
    
    // Copy provided arguments
    for (int i = 0; i < provided_args; i++) {
        (*final_args)[i] = args[i];
    }
    
    // Fill in default values for missing arguments
    for (int i = provided_args; i < fn->param_count; i++) {
        if (fn->parameters && fn->parameters[i].has_default) {
            // Convert BreadValue to ExprResult
            (*final_args)[i] = bread_expr_result_from_value(fn->parameters[i].default_value);
        } else {
            // This shouldn't happen if validation is correct
            free(*final_args);
            *final_args = NULL;
            return 0;
        }
    }
    
    return 1;
}
