#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "runtime/runtime.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/expr.h"

// Memory management functions
void* bread_alloc(size_t size) {
    return malloc(size);
}

void* bread_realloc(void* ptr, size_t new_size) {
    return realloc(ptr, new_size);
}

void bread_free(void* ptr) {
    free(ptr);
}

// Variable management functions
int bread_var_decl(const char* name, VarType type, int is_const, const BreadValue* init) {
    if (!name) return 0;
    VarValue zero;
    memset(&zero, 0, sizeof(zero));
    if (!declare_variable_raw(name, type, zero, is_const ? 1 : 0)) return 0;
    if (!init) return 1;

    ExprResult r;
    memset(&r, 0, sizeof(r));
    r.is_error = 0;
    r.type = init->type;
    r.value = init->value;
    return bread_init_variable_from_expr_result(name, &r);
}

int bread_var_decl_if_missing(const char* name, VarType type, int is_const, const BreadValue* init) {
    if (!name) return 0;
    if (get_variable((char*)name)) return 1;
    return bread_var_decl(name, type, is_const, init);
}

int bread_var_assign(const char* name, const BreadValue* value) {
    if (!name || !value) return 0;
    ExprResult r;
    memset(&r, 0, sizeof(r));
    r.is_error = 0;
    r.type = value->type;
    r.value = value->value;
    return bread_assign_variable_from_expr_result(name, &r);
}

int bread_var_load(const char* name, BreadValue* out) {
    if (!name || !out) return 0;
    Variable* var = get_variable((char*)name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name);
        return 0;
    }

    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = var->type;
    v.value = var->value;
    *out = bread_value_clone(v);
    return 1;
}

void bread_push_scope(void) {
    push_scope();
}

void bread_pop_scope(void) {
    pop_scope();
}