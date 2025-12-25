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

int declare_variable_raw(const char* name, VarType type, VarValue value, int is_const);

#ifndef BREAD_DEBUG_VARS
#define BREAD_DEBUG_VARS 0
#endif

static int coerce_and_assign(Variable* var, VarType type, VarValue value);


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
    
    char name_buf[256];
    size_t name_len = 0;
    const char* p = name;
    for (size_t i = 0; i < 255; i++) {
        char c;
        volatile const char* vp = (volatile const char*)p;
        c = *vp;
        if (c == '\0') {
            break;
        }
        name_buf[i] = c;
        name_len++;
        p++;
    }
    
    if (name_len >= 255) {
        BREAD_ERROR_SET_UNDEFINED_VARIABLE("Variable name too long");
        return 0;
    }
    
    name_buf[name_len] = '\0';
    Variable* var = get_variable(name_buf);
    if (!var) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Unknown variable '%s'", name_buf);
        BREAD_ERROR_SET_UNDEFINED_VARIABLE(error_msg);
        return 0;
    }
    
    if (var->is_const) {
        BREAD_ERROR_SET_RUNTIME("Cannot assign to constant variable");
        return 0;
    }
    
    // Convert BreadValue to VarValue and assign directly
    VarValue var_value;
    memset(&var_value, 0, sizeof(var_value));
    switch (value->type) {
        case TYPE_INT:
            var_value.int_val = value->value.int_val;
            break;
        case TYPE_FLOAT:
            var_value.float_val = value->value.float_val;
            break;
        case TYPE_DOUBLE:
            var_value.double_val = value->value.double_val;
            break;
        case TYPE_BOOL:
            var_value.bool_val = value->value.bool_val;
            break;
        case TYPE_STRING:
            if (value->value.string_val) {
                var_value.string_val = value->value.string_val;
                bread_string_retain(var_value.string_val);
            } else {
                var_value.string_val = bread_string_new("");
            }
            break;
        default:
            BREAD_ERROR_SET_RUNTIME("Unsupported variable type for assignment");
            return 0;
    }
    
    return coerce_and_assign(var, value->type, var_value);
}

static int coerce_and_assign(Variable* var, VarType src_type, VarValue src) {
    if (!var) return 0;
    if (var->type == TYPE_STRING && var->value.string_val) {
        bread_string_release(var->value.string_val);
        var->value.string_val = NULL;
    }

    VarValue dst;
    memset(&dst, 0, sizeof(dst));

    switch (var->type) {
        case TYPE_INT:
            switch (src_type) {
                case TYPE_INT:    dst.int_val = src.int_val; break;
                case TYPE_BOOL:   dst.int_val = src.bool_val; break;
                case TYPE_FLOAT:  dst.int_val = (int)src.float_val; break;
                case TYPE_DOUBLE: dst.int_val = (int)src.double_val; break;
                case TYPE_STRING:
                    dst.int_val = src.string_val ? atoi(bread_string_cstr(src.string_val)) : 0;
                    break;
                default:
                    goto type_error;
            }
            break;

        case TYPE_FLOAT:
            switch (src_type) {
                case TYPE_INT:    dst.float_val = (float)src.int_val; break;
                case TYPE_BOOL:   dst.float_val = (float)src.bool_val; break;
                case TYPE_FLOAT:  dst.float_val = src.float_val; break;
                case TYPE_DOUBLE: dst.float_val = (float)src.double_val; break;
                case TYPE_STRING:
                    dst.float_val = src.string_val
                        ? (float)atof(bread_string_cstr(src.string_val))
                        : 0.0f;
                    break;
                default:
                    goto type_error;
            }
            break;

        case TYPE_DOUBLE:
            switch (src_type) {
                case TYPE_INT:    dst.double_val = (double)src.int_val; break;
                case TYPE_BOOL:   dst.double_val = (double)src.bool_val; break;
                case TYPE_FLOAT:  dst.double_val = (double)src.float_val; break;
                case TYPE_DOUBLE: dst.double_val = src.double_val; break;
                case TYPE_STRING:
                    dst.double_val = src.string_val
                        ? atof(bread_string_cstr(src.string_val))
                        : 0.0;
                    break;
                default:
                    goto type_error;
            }
            break;

        case TYPE_BOOL:
            switch (src_type) {
                case TYPE_BOOL:   dst.bool_val = src.bool_val; break;
                case TYPE_INT:    dst.bool_val = src.int_val != 0; break;
                case TYPE_FLOAT:  dst.bool_val = src.float_val != 0.0f; break;
                case TYPE_DOUBLE: dst.bool_val = src.double_val != 0.0; break;
                case TYPE_STRING:
                    dst.bool_val = src.string_val &&
                                   bread_string_cstr(src.string_val)[0] != '\0';
                    break;
                default:
                    goto type_error;
            }
            break;

        case TYPE_STRING: {
            char buf[64];

            switch (src_type) {
                case TYPE_STRING:
                    if (src.string_val) {
                        dst.string_val = src.string_val;
                        bread_string_retain(dst.string_val);
                    } else {
                        dst.string_val = bread_string_new("");
                    }
                    break;

                case TYPE_INT:
                    snprintf(buf, sizeof(buf), "%lld", (long long)src.int_val);
                    dst.string_val = bread_string_new(buf);
                    break;

                case TYPE_FLOAT:
                    snprintf(buf, sizeof(buf), "%f", src.float_val);
                    dst.string_val = bread_string_new(buf);
                    break;

                case TYPE_DOUBLE:
                    snprintf(buf, sizeof(buf), "%lf", src.double_val);
                    dst.string_val = bread_string_new(buf);
                    break;

                case TYPE_BOOL:
                    dst.string_val = bread_string_new(src.bool_val ? "true" : "false");
                    break;

                default:
                    goto type_error;
            }
            break;
        }

        default:
            goto type_error;
    }

    // commit
    var->value = dst;
    return 1;

type_error:
    BREAD_ERROR_SET_RUNTIME("Invalid type coercion in assignment");
    return 0;
}



int bread_var_load(const char* name, BreadValue* out) {
    if (!name || !out) return 0;

    if (is_suspicious_pointer(name)) {
        BREAD_ERROR_SET_UNDEFINED_VARIABLE("Invalid variable name pointer");
        return 0;
    }

    Variable* var = get_variable((char*)name);
    if (!var) {
        BREAD_ERROR_SET_UNDEFINED_VARIABLE("Unknown variable");
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
