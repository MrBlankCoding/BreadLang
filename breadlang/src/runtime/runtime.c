#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>

#include "runtime/runtime.h"
#include "runtime/memory.h"
#include "runtime/error.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/parser/expr.h"

// Can toggle debug from here
#ifndef BREAD_DEBUG_VARS
#define BREAD_DEBUG_VARS 0
#endif

#define DEBUGF(...) \
    do { if (BREAD_DEBUG_VARS) { printf(__VA_ARGS__); fflush(stdout); } } while (0)

void* bread_alloc(size_t size) {
    if (size == 0) return NULL;
    return bread_memory_alloc(size, BREAD_OBJ_UNKNOWN);
}

void* bread_realloc(void* ptr, size_t new_size) {
    if (new_size == 0) {
        bread_memory_free(ptr);
        return NULL;
    }
    return bread_memory_realloc(ptr, new_size);
}

void bread_free(void* ptr) {
    if (ptr) {
        bread_memory_free(ptr);
    }
}

static inline ExprResult expr_from_value(const BreadValue* v) {
    ExprResult r;
    memset(&r, 0, sizeof(r));
    if (v) {
        r.type  = v->type;
        r.value = v->value;
    }
    return r;
}

static inline int is_suspicious_pointer(const void* p) {
    return ((uintptr_t)p) < 0x1000;
}

int bread_var_decl(const char* name, VarType type, int is_const, const BreadValue* init) {
    DEBUGF("bread_var_decl(name='%s', type=%d, const=%d)\n",
           name ? name : "NULL", type, is_const);

    if (!name) return 0;

    VarValue zero;
    memset(&zero, 0, sizeof(zero));

    if (!declare_variable_raw(name, type, zero, is_const ? 1 : 0)) {
        return 0;
    }

    if (!init) return 1;

    ExprResult r = expr_from_value(init);
    return bread_init_variable_from_expr_result(name, &r);
}

int bread_var_decl_if_missing(const char* name, VarType type, int is_const, const BreadValue* init) {
    DEBUGF("bread_var_decl_if_missing(name='%s')\n", name ? name : "NULL");

    if (!name) return 0;
    if (get_variable((char*)name)) {
        return 1;
    }
    return bread_var_decl(name, type, is_const, init);
}

int bread_var_assign(const char* name, const BreadValue* value) {
    if (!name || !value) return 0;
    ExprResult r = expr_from_value(value);
    return bread_assign_variable_from_expr_result(name, &r);
}

#define MAX_LEV_LEN 20

static int levenshtein_distance(const char* s1, const char* s2) {
    int len1 = (int)strlen(s1);
    int len2 = (int)strlen(s2);

    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    if (len1 > MAX_LEV_LEN || len2 > MAX_LEV_LEN) return len1 + len2;

    int m[MAX_LEV_LEN + 1][MAX_LEV_LEN + 1];

    for (int i = 0; i <= len1; i++) m[i][0] = i;
    for (int j = 0; j <= len2; j++) m[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] != s2[j - 1]);
            int del  = m[i - 1][j] + 1;
            int ins  = m[i][j - 1] + 1;
            int sub  = m[i - 1][j - 1] + cost;

            int best = del < ins ? del : ins;
            m[i][j]  = best < sub ? best : sub;
        }
    }

    return m[len1][len2];
}

static char* find_similar_variable(const char* name) {
    static const char* common_vars[] = {
        "i", "j", "k", "x", "y", "z",
        "n", "count", "index", "value",
        "result", "temp", "data", "item",
        "list", "array", "string", NULL
    };

    int best_dist = INT_MAX;
    const char* best = NULL;

    for (int i = 0; common_vars[i]; i++) {
        int d = levenshtein_distance(name, common_vars[i]);
        if (d < best_dist && d <= 2) {
            best_dist = d;
            best = common_vars[i];
        }
    }

    if (!best) return NULL;

    char* out = malloc(strlen(best) + 1);
    if (out) strcpy(out, best);
    return out;
}

int bread_var_load(const char* name, BreadValue* out) {
    if (!name || !out) return 0;

    if (is_suspicious_pointer(name)) {
        BREAD_ERROR_SET_UNDEFINED_VARIABLE("Invalid variable name pointer");
        return 0;
    }

    Variable* var = get_variable((char*)name);
    if (!var) {
        char* suggestion = find_similar_variable(name);
        char msg[512];

        if (suggestion) {
            snprintf(msg, sizeof(msg),
                     "Unknown variable '%s'. Did you mean '%s'?",
                     name, suggestion);
            free(suggestion);
        } else {
            snprintf(msg, sizeof(msg),
                     "Unknown variable '%s'", name);
        }

        BREAD_ERROR_SET_UNDEFINED_VARIABLE(msg);
        return 0;
    }

    BreadValue tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.type  = var->type;
    tmp.value = var->value;

    *out = bread_value_clone(tmp);
    return 1;
}

void bread_push_scope(void) { push_scope(); }
void bread_pop_scope(void)  { pop_scope(); }
int  bread_can_pop_scope(void) { return can_pop_scope(); }
int  bread_scope_depth(void) { return scope_depth_current(); }
void bread_pop_to_scope_depth(int depth) { pop_to_scope_depth(depth); }

BreadValue bread_box_int(int v) {
    return (BreadValue){ .type = TYPE_INT, .value.int_val = v };
}

BreadValue bread_box_double(double v) {
    return (BreadValue){ .type = TYPE_DOUBLE, .value.double_val = v };
}

BreadValue bread_box_bool(int v) {
    return (BreadValue){ .type = TYPE_BOOL, .value.bool_val = v ? 1 : 0 };
}

int bread_unbox_int(const BreadValue* v) {
    if (!v) return 0;
    switch (v->type) {
        case TYPE_INT:    return v->value.int_val;
        case TYPE_BOOL:   return v->value.bool_val;
        case TYPE_DOUBLE: return (int)v->value.double_val;
        case TYPE_FLOAT:  return (int)v->value.float_val;
        default:          return 0;
    }
}

double bread_unbox_double(const BreadValue* v) {
    if (!v) return 0.0;
    switch (v->type) {
        case TYPE_DOUBLE: return v->value.double_val;
        case TYPE_FLOAT:  return (double)v->value.float_val;
        case TYPE_INT:    return (double)v->value.int_val;
        case TYPE_BOOL:   return v->value.bool_val ? 1.0 : 0.0;
        default:          return 0.0;
    }
}

int bread_unbox_bool(const BreadValue* v) {
    if (!v) return 0;
    switch (v->type) {
        case TYPE_BOOL:   return v->value.bool_val;
        case TYPE_INT:    return v->value.int_val != 0;
        case TYPE_DOUBLE: return v->value.double_val != 0.0;
        case TYPE_FLOAT:  return v->value.float_val != 0.0f;
        default:          return 0;
    }
}
