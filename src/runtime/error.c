#include "runtime/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global error state
static BreadError g_current_error = {0};
static int g_error_initialized = 0;
static int g_compilation_failed = 0;  // Track if compilation has failed

// error context stack
#define MAX_ERROR_CONTEXT_DEPTH 32
static BreadErrorContext g_error_context_stack[MAX_ERROR_CONTEXT_DEPTH];
static int g_error_context_depth = 0;

void bread_error_init(void) {
    if (g_error_initialized) return;
    
    memset(&g_current_error, 0, sizeof(g_current_error));
    memset(g_error_context_stack, 0, sizeof(g_error_context_stack));
    g_error_context_depth = 0;
    g_compilation_failed = 0;
    g_error_initialized = 1;
}

void bread_error_cleanup(void) {
    if (!g_error_initialized) return;
    
    bread_error_clear();
    g_error_initialized = 0;
}

static void bread_error_free_strings(BreadError* error) {
    if (error->message) {
        free(error->message);
        error->message = NULL;
    }
    if (error->filename) {
        free(error->filename);
        error->filename = NULL;
    }
    if (error->context) {
        free(error->context);
        error->context = NULL;
    }
}

static char* bread_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

void bread_error_set(BreadErrorType type, const char* message, const char* filename, int line, int column) {
    bread_error_set_with_context(type, message, filename, line, column, NULL);
}

void bread_error_set_with_context(BreadErrorType type, const char* message, const char* filename, int line, int column, const char* context) {
    if (!g_error_initialized) {
        bread_error_init();
    }
    
    // clear errors before
    bread_error_free_strings(&g_current_error);
    
    // create new one
    g_current_error.type = type;
    g_current_error.message = bread_strdup(message);
    g_current_error.filename = bread_strdup(filename);
    g_current_error.line = line;
    g_current_error.column = column;
    g_current_error.context = bread_strdup(context);
    
    // Mark compilation as failed for compile-time errors
    if (type == BREAD_ERROR_TYPE_MISMATCH ||
        type == BREAD_ERROR_UNDEFINED_VARIABLE ||
        type == BREAD_ERROR_SYNTAX_ERROR ||
        type == BREAD_ERROR_PARSE_ERROR ||
        type == BREAD_ERROR_COMPILE_ERROR) {
        g_compilation_failed = 1;
    }
}

void bread_error_clear(void) {
    if (!g_error_initialized) return;
    
    bread_error_free_strings(&g_current_error);
    memset(&g_current_error, 0, sizeof(g_current_error));
}

BreadError* bread_error_get_current(void) {
    if (!g_error_initialized) {
        bread_error_init();
    }
    return &g_current_error;
}

int bread_error_has_error(void) {
    if (!g_error_initialized) return 0;
    return g_current_error.type != BREAD_ERROR_NONE;
}

BreadErrorType bread_error_get_type(void) {
    if (!g_error_initialized) return BREAD_ERROR_NONE;
    return g_current_error.type;
}

const char* bread_error_get_message(void) {
    if (!g_error_initialized) return NULL;
    return g_current_error.message;
}

const char* bread_error_type_to_string(BreadErrorType type) {
    switch (type) {
        case BREAD_ERROR_NONE: return "No Error";
        case BREAD_ERROR_TYPE_MISMATCH: return "Type Mismatch";
        case BREAD_ERROR_INDEX_OUT_OF_BOUNDS: return "Index Out of Bounds";
        case BREAD_ERROR_DIVISION_BY_ZERO: return "Division by Zero";
        case BREAD_ERROR_UNDEFINED_VARIABLE: return "Undefined Variable";
        case BREAD_ERROR_MEMORY_ALLOCATION: return "Memory Allocation Error";
        case BREAD_ERROR_RUNTIME_ERROR: return "Runtime Error";
        case BREAD_ERROR_SYNTAX_ERROR: return "Syntax Error";
        case BREAD_ERROR_PARSE_ERROR: return "Parse Error";
        case BREAD_ERROR_COMPILE_ERROR: return "Compile Error";
        default: return "Unknown Error";
    }
}

char* bread_error_format_message(const BreadError* error) {
    if (!error || error->type == BREAD_ERROR_NONE) {
        return NULL;
    }
    
    size_t size = 256; // Base size
    if (error->message) size += strlen(error->message);
    if (error->filename) size += strlen(error->filename);
    if (error->context) size += strlen(error->context);
    
    char* formatted = malloc(size);
    if (!formatted) return NULL;
    
    int pos = 0;
    pos += snprintf(formatted + pos, size - pos, "%s", bread_error_type_to_string(error->type));
    
    if (error->filename && error->line > 0) {
        pos += snprintf(formatted + pos, size - pos, " at %s:%d", error->filename, error->line);
        if (error->column > 0) {
            pos += snprintf(formatted + pos, size - pos, ":%d", error->column);
        }
    }
    
    if (error->message) {
        pos += snprintf(formatted + pos, size - pos, ": %s", error->message);
    }
    
    if (error->context) {
        pos += snprintf(formatted + pos, size - pos, "\nContext: %s", error->context);
    }
    
    return formatted;
}

void bread_error_print(const BreadError* error) {
    if (!error || error->type == BREAD_ERROR_NONE) {
        return;
    }
    
    char* formatted = bread_error_format_message(error);
    if (formatted) {
        fprintf(stderr, "Error: %s\n", formatted);
        free(formatted);
    } else {
        fprintf(stderr, "Error: %s\n", bread_error_type_to_string(error->type));
    }
}

void bread_error_print_current(void) {
    bread_error_print(bread_error_get_current());
}

// Error context management
void bread_error_context_push(const char* filename, int line, int column, const char* function) {
    if (g_error_context_depth >= MAX_ERROR_CONTEXT_DEPTH) {
        return; // Stack overflow protection
    }
    
    BreadErrorContext* ctx = &g_error_context_stack[g_error_context_depth++];
    ctx->current_file = filename;
    ctx->current_line = line;
    ctx->current_column = column;
    ctx->current_function = function;
}

void bread_error_context_pop(void) {
    if (g_error_context_depth > 0) {
        g_error_context_depth--;
    }
}

BreadErrorContext* bread_error_context_current(void) {
    if (g_error_context_depth == 0) {
        return NULL;
    }
    return &g_error_context_stack[g_error_context_depth - 1];
}

// Compilation error tracking functions
int bread_error_has_compilation_errors(void) {
    if (!g_error_initialized) return 0;
    return g_compilation_failed;
}

void bread_error_mark_compilation_failed(void) {
    if (!g_error_initialized) {
        bread_error_init();
    }
    g_compilation_failed = 1;
}

void bread_error_reset_compilation_state(void) {
    if (!g_error_initialized) return;
    g_compilation_failed = 0;
}