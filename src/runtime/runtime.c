#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "runtime/runtime.h"
#include "runtime/memory.h"
#include "runtime/error.h"
#include "core/value.h"
#include "core/var.h"
#include "compiler/parser/expr.h"

// Memory management functions - now using enhanced memory manager
void* bread_alloc(size_t size) {
    return bread_memory_alloc(size, BREAD_OBJ_STRING);  // Default to string type
}

void* bread_realloc(void* ptr, size_t new_size) {
    return bread_memory_realloc(ptr, new_size);
}

void bread_free(void* ptr) {
    bread_memory_free(ptr);
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

// Helper function to calculate string similarity (Levenshtein distance)
static int levenshtein_distance(const char* s1, const char* s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    // Use a simple approach for small strings
    if (len1 > 20 || len2 > 20) return len1 + len2; // Too different
    
    int matrix[21][21]; // Max 20 chars + 1
    
    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;
    
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            matrix[i][j] = matrix[i-1][j] + 1; // deletion
            int insertion = matrix[i][j-1] + 1;
            int substitution = matrix[i-1][j-1] + cost;
            
            if (insertion < matrix[i][j]) matrix[i][j] = insertion;
            if (substitution < matrix[i][j]) matrix[i][j] = substitution;
        }
    }
    
    return matrix[len1][len2];
}

// Helper function to find similar variable names
static char* find_similar_variable(const char* name) {
    // This is a simplified implementation - in a real compiler,
    // you'd iterate through the symbol table
    const char* common_vars[] = {
        "i", "j", "k", "x", "y", "z", "n", "count", "index", "value", 
        "result", "temp", "data", "item", "list", "array", "string", NULL
    };
    
    int min_distance = INT_MAX;
    const char* best_match = NULL;
    
    for (int i = 0; common_vars[i]; i++) {
        int distance = levenshtein_distance(name, common_vars[i]);
        if (distance < min_distance && distance <= 2) { // Max 2 edits
            min_distance = distance;
            best_match = common_vars[i];
        }
    }
    
    if (best_match) {
        char* suggestion = malloc(strlen(best_match) + 1);
        if (suggestion) {
            strcpy(suggestion, best_match);
        }
        return suggestion;
    }
    
    return NULL;
}

int bread_var_load(const char* name, BreadValue* out) {
    if (!name || !out) return 0;
    Variable* var = get_variable((char*)name);
    if (!var) {
        char* suggestion = find_similar_variable(name);
        if (suggestion) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), 
                    "Unknown variable '%s'. Did you mean '%s'?", name, suggestion);
            BREAD_ERROR_SET_UNDEFINED_VARIABLE(error_msg);
            free(suggestion);
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Unknown variable '%s'", name);
            BREAD_ERROR_SET_UNDEFINED_VARIABLE(error_msg);
        }
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