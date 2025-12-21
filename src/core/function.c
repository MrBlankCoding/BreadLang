#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/function.h"
#include "compiler/ast.h"
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
    fn->body = NULL;
    fn->body_is_ast = 0;
    fn->name = NULL;
    fn->param_count = 0;
    fn->param_names = NULL;
    fn->param_types = NULL;
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
        if (!dst->param_names || !dst->param_types) {
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

static ASTExecSignal exec_ast_body(const Function* fn, ExprResult* out) {
    if (!fn || !fn->body) return AST_EXEC_SIGNAL_NONE;
    return ast_execute_stmt_list((ASTStmtList*)fn->body, out);
}

ExprResult call_function_values(const char* name, int arg_count, ExprResult* arg_vals) {
    const Function* fn = get_function(name);
    if (!fn) {
        printf("Error: Unknown function '%s'\n", name ? name : "");
        ExprResult err;
        err.is_error = 1;
        return err;
    }

    if (arg_count != fn->param_count) {
        printf("Error: Function '%s' expected %d args but got %d\n", fn->name, fn->param_count, arg_count);
        ExprResult err;
        err.is_error = 1;
        return err;
    }

    push_scope();

    for (int i = 0; i < fn->param_count; i++) {
        ExprResult arg_val = arg_vals[i];

        if (!type_compatible(fn->param_types[i], arg_val.type)) {
            printf("Error: Type mismatch for parameter '%s' in call to '%s'\n", fn->param_names[i], fn->name);
            ExprResult err;
            err.is_error = 1;
            pop_scope();
            return err;
        }

        VarValue v = coerce_value(fn->param_types[i], arg_val);
        if (!declare_variable_raw(fn->param_names[i], fn->param_types[i], v, 1)) {
            ExprResult err;
            err.is_error = 1;
            pop_scope();
            return err;
        }

        if (fn->param_types[i] == TYPE_OPTIONAL && arg_val.type != TYPE_OPTIONAL) {
            if (v.optional_val) bread_optional_release(v.optional_val);
        }
    }

    ExprResult body_val;
    memset(&body_val, 0, sizeof(body_val));
    body_val.is_error = 0;
    body_val.type = fn->return_type;

    ASTExecSignal sig = exec_ast_body(fn, &body_val);
    if (sig != AST_EXEC_SIGNAL_RETURN) {
        printf("Error: Function '%s' ended without return\n", fn->name);
        body_val.is_error = 1;
        pop_scope();
        return body_val;
    }

    pop_scope();
    return body_val;
}

ExprResult call_function(const char* name, int arg_count, const char** arg_exprs) {
    const Function* fn = get_function(name);
    if (!fn) {
        printf("Error: Unknown function '%s'\n", name ? name : "");
        ExprResult err;
        err.is_error = 1;
        return err;
    }

    if (arg_count != fn->param_count) {
        printf("Error: Function '%s' expected %d args but got %d\n", fn->name, fn->param_count, arg_count);
        ExprResult err;
        err.is_error = 1;
        return err;
    }

    ExprResult* arg_vals = NULL;
    if (arg_count > 0) {
        arg_vals = malloc(sizeof(ExprResult) * (size_t)arg_count);
        if (!arg_vals) {
            printf("Error: Out of memory\n");
            ExprResult err;
            err.is_error = 1;
            return err;
        }
    }

    for (int i = 0; i < arg_count; i++) {
        arg_vals[i] = evaluate_expression(arg_exprs[i]);
        if (arg_vals[i].is_error) {
            for (int j = 0; j < i; j++) {
                BreadValue tmp = bread_value_from_expr_result(arg_vals[j]);
                bread_value_release(&tmp);
            }
            ExprResult err = arg_vals[i];
            free(arg_vals);
            return err;
        }
    }

    ExprResult out = call_function_values(name, arg_count, arg_vals);

    for (int i = 0; i < arg_count; i++) {
        BreadValue tmp = bread_value_from_expr_result(arg_vals[i]);
        bread_value_release(&tmp);
    }
    free(arg_vals);

    return out;
}
