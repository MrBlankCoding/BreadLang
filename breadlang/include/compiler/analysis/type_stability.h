#ifndef TYPE_STABILITY_H
#define TYPE_STABILITY_H

#include "compiler/ast/ast.h"

typedef enum {
    STABILITY_UNKNOWN,      
    STABILITY_STABLE,       
    STABILITY_UNSTABLE,     
    STABILITY_CONDITIONAL   
} TypeStability;

typedef struct {
    VarType type;
    TypeStability stability;
    int is_constant;        
    int is_local;          
    int mutation_count;    
    int usage_count;       
} TypeStabilityInfo;

// Analysis context
typedef struct {
    TypeStabilityInfo* expr_info;  // Parallel array to expressions
    int expr_count;
    int expr_capacity;
    int current_function_depth;
    int in_loop;
} TypeStabilityCtx;

int type_stability_analyze(ASTStmtList* program);
TypeStabilityInfo* get_expr_stability_info(ASTExpr* expr);
int is_type_stable(ASTExpr* expr);
int can_unbox_integer(ASTExpr* expr);

#endif