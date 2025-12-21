#ifndef OPTIMIZATION_H
#define OPTIMIZATION_H

#include "compiler/ast.h"
#include <llvm-c/Core.h>

typedef enum {
    INLINE_NEVER,          
    INLINE_COLD,           
    INLINE_NORMAL,         
    INLINE_HOT,            
    INLINE_ALWAYS          
} InlineHeuristic;

// Function analysis info
typedef struct {
    InlineHeuristic inline_hint;
    int instruction_count;     
    int call_count;           
    int is_recursive;         
    int is_leaf;              
    int has_side_effects;     
    int parameter_count;      
} FunctionOptInfo;

typedef struct {
    int is_hot_path;          
    int is_cold_path;         
    int branch_probability;   
    int can_speculate;        
    int is_pure;              
} OptimizationHints;

typedef struct {
    FunctionOptInfo* function_info;
    OptimizationHints* stmt_hints;
    OptimizationHints* expr_hints;
    int function_count;
    int stmt_count;
    int expr_count;
} OptimizationCtx;

int optimization_analyze(ASTStmtList* program);
FunctionOptInfo* get_function_opt_info(ASTStmtFuncDecl* func);
OptimizationHints* get_stmt_hints(ASTStmt* stmt);
OptimizationHints* get_expr_hints(ASTExpr* expr);

void attach_optimization_metadata(LLVMValueRef value, OptimizationHints* hints);
void set_function_attributes(LLVMValueRef function, FunctionOptInfo* info);
void add_branch_weights(LLVMValueRef branch, int true_weight, int false_weight);

#endif