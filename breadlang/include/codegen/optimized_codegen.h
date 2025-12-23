#ifndef OPTIMIZED_CODEGEN_H
#define OPTIMIZED_CODEGEN_H

#include "codegen/codegen.h"
#include "compiler/analysis/type_stability.h"
#include "compiler/analysis/escape_analysis.h"
#include "compiler/optimization/optimization.h"

typedef struct {
    Cg base;                    
    
    LLVMTypeRef unboxed_int;    
    LLVMTypeRef unboxed_double; 
    LLVMTypeRef unboxed_bool;   

    LLVMValueRef fn_value_get_int;
    LLVMValueRef fn_value_get_double;
    LLVMValueRef fn_value_get_bool;
    LLVMValueRef fn_value_get_type;

    LLVMTypeRef ty_value_get_int;
    LLVMTypeRef ty_value_get_double;
    LLVMTypeRef ty_value_get_bool;
    LLVMTypeRef ty_value_get_type;
    
    int stack_alloc_count;
    LLVMValueRef* stack_slots;
    int stack_capacity;
    
    int enable_unboxing;
    int enable_stack_alloc;
    int enable_inlining;
    
} OptimizedCg;

typedef enum {
    VALUE_BOXED,        
    VALUE_UNBOXED_INT,  
    VALUE_UNBOXED_DOUBLE, 
    VALUE_UNBOXED_BOOL, 
    VALUE_STACK_ALLOC   
} ValueRepresentation;

typedef struct {
    ValueRepresentation repr;
    LLVMValueRef value;
    LLVMTypeRef type;
} OptimizedValue;

// Here is the public API for the optimized code
int optimized_codegen_init(OptimizedCg* cg, LLVMModuleRef mod);
void optimized_codegen_cleanup(OptimizedCg* cg);

OptimizedValue optimized_build_expr(OptimizedCg* cg, CgFunction* cg_fn, ASTExpr* expr);

int optimized_build_stmt(OptimizedCg* cg, CgFunction* cg_fn, ASTStmt* stmt);

LLVMValueRef box_value(OptimizedCg* cg, OptimizedValue val);
OptimizedValue unbox_value(OptimizedCg* cg, LLVMValueRef boxed_val, VarType expected_type);

LLVMValueRef alloc_stack_value(OptimizedCg* cg, VarType type, const char* name);
void release_stack_value(OptimizedCg* cg, LLVMValueRef stack_val);

void apply_function_attributes(OptimizedCg* cg, LLVMValueRef function, FunctionOptInfo* info);
void apply_branch_hints(OptimizedCg* cg, LLVMValueRef branch, OptimizationHints* hints);

#endif