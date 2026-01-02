#ifndef ESCAPE_ANALYSIS_H
#define ESCAPE_ANALYSIS_H

#include "compiler/ast/ast.h"

typedef enum {
    ESCAPE_UNKNOWN,         // Hasnt been analyzed yet
    ESCAPE_NONE,           
    ESCAPE_RETURN,         
    ESCAPE_GLOBAL,         
    ESCAPE_PARAMETER,      
    ESCAPE_HEAP            
} EscapeKind;

typedef struct {
    EscapeKind escape_kind;
    int can_stack_allocate; // SAFE! 3rd base -> RUNNER UP! 
    int lifetime_end;       
    int ref_count;          
} EscapeInfo;

// Analysis context
typedef struct {
    EscapeInfo** alloc_info;  // array of pointers to EscapeInfo
    int alloc_count;
    int alloc_capacity;
    int current_stmt_index;
    int function_depth;
} EscapeAnalysisCtx;

int escape_analysis_run(ASTStmtList* program);
EscapeInfo* get_escape_info(ASTExpr* expr);
int can_stack_allocate(ASTExpr* expr);
int get_value_lifetime(ASTExpr* expr);

#endif
