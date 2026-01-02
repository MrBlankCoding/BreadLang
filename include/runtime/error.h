#ifndef BREAD_ERROR_H
#define BREAD_ERROR_H

#include <stddef.h>

typedef enum {
    BREAD_ERROR_NONE = 0,
    BREAD_ERROR_TYPE_MISMATCH,
    BREAD_ERROR_INDEX_OUT_OF_BOUNDS,
    BREAD_ERROR_DIVISION_BY_ZERO,
    BREAD_ERROR_UNDEFINED_VARIABLE,
    BREAD_ERROR_MEMORY_ALLOCATION,
    BREAD_ERROR_RUNTIME_ERROR,
    BREAD_ERROR_SYNTAX_ERROR,
    BREAD_ERROR_PARSE_ERROR,
    BREAD_ERROR_COMPILE_ERROR
} BreadErrorType;

typedef struct {
    BreadErrorType type;
    char* message;
    char* filename;
    int line;
    int column;
    char* context;  // Additional context information
} BreadError;

// Global error state management
void bread_error_init(void);
void bread_error_cleanup(void);

// Error setting functions
void bread_error_set(BreadErrorType type, const char* message, const char* filename, int line, int column);
void bread_error_set_with_context(BreadErrorType type, const char* message, const char* filename, int line, int column, const char* context);
void bread_error_clear(void);

// Error retrieval functions
BreadError* bread_error_get_current(void);
int bread_error_has_error(void);
BreadErrorType bread_error_get_type(void);
const char* bread_error_get_message(void);

// Compilation error tracking
int bread_error_has_compilation_errors(void);
void bread_error_mark_compilation_failed(void);
void bread_error_reset_compilation_state(void);

// Error formatting and display
char* bread_error_format_message(const BreadError* error);
void bread_error_print(const BreadError* error);
void bread_error_print_current(void);

// Error type utilities
const char* bread_error_type_to_string(BreadErrorType type);

// Convenience macros for common error patterns
#define BREAD_ERROR_SET_RUNTIME(msg) \
    bread_error_set(BREAD_ERROR_RUNTIME_ERROR, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_TYPE_MISMATCH(msg) \
    bread_error_set(BREAD_ERROR_TYPE_MISMATCH, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(msg) \
    bread_error_set(BREAD_ERROR_INDEX_OUT_OF_BOUNDS, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_DIVISION_BY_ZERO(msg) \
    bread_error_set(BREAD_ERROR_DIVISION_BY_ZERO, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_MEMORY_ALLOCATION(msg) \
    bread_error_set(BREAD_ERROR_MEMORY_ALLOCATION, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_UNDEFINED_VARIABLE(msg) \
    bread_error_set(BREAD_ERROR_UNDEFINED_VARIABLE, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_SYNTAX_ERROR(msg) \
    bread_error_set(BREAD_ERROR_SYNTAX_ERROR, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_PARSE_ERROR(msg) \
    bread_error_set(BREAD_ERROR_PARSE_ERROR, (msg), __FILE__, __LINE__, 0)

#define BREAD_ERROR_SET_COMPILE_ERROR(msg) \
    bread_error_set(BREAD_ERROR_COMPILE_ERROR, (msg), __FILE__, __LINE__, 0)

// Error context tracking for compilation
typedef struct BreadErrorContext {
    const char* current_file;
    int current_line;
    int current_column;
    const char* current_function;
} BreadErrorContext;

void bread_error_context_push(const char* filename, int line, int column, const char* function);
void bread_error_context_pop(void);
BreadErrorContext* bread_error_context_current(void);

#endif // BREAD_ERROR_H