#include <stdio.h>
#include "codegen/codegen.h"

// Stub implementation for cg_declare_fn
LLVMValueRef cg_declare_fn(Cg* cg, const char* name, LLVMTypeRef fn_type) {
    (void)cg; // Unused parameter
    (void)name; // Unused parameter
    (void)fn_type; // Unused parameter
    // Placeholder: In a real implementation, this would declare a function in the LLVM module
    fprintf(stderr, "Warning: cg_declare_fn stub called for %s\n", name);
    return NULL; 
}

// Stub implementation for cg_define_functions
int cg_define_functions(Cg* cg) {
    (void)cg; // Unused parameter
    // Placeholder: In a real implementation, this would define common runtime functions in the LLVM module
    fprintf(stderr, "Warning: cg_define_functions stub called\n");
    return 1; // Return success to allow compilation
}

// Stub implementation for cg_value_size
LLVMValueRef cg_value_size(Cg* cg) {
    (void)cg; // Unused parameter
    // Placeholder: In a real implementation, this would return the size of a BreadValue
    fprintf(stderr, "Warning: cg_value_size stub called\n");
    return NULL; // Return NULL for now
}

// Stub implementation for cg_build_stmt_list
int cg_build_stmt_list(Cg* cg, LLVMValueRef val_size, ASTStmtList* program) {
    (void)cg; // Unused parameter
    (void)val_size; // Unused parameter
    (void)program; // Unused parameter
    // Placeholder: In a real implementation, this would translate the AST into LLVM IR
    fprintf(stderr, "Warning: cg_build_stmt_list stub called\n");
    return 1; // Return success to allow compilation
}

