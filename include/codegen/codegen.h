#ifndef BREAD_CODEGEN_H
#define BREAD_CODEGEN_H

#include <llvm-c/Core.h>
#include "compiler/ast.h"

// Forward declaration for CompCtx, if needed from other parts of the compiler
// For now, let's keep it simple and define Cg directly.

typedef struct {
    LLVMModuleRef mod;
    LLVMBuilderRef builder;

    LLVMTypeRef i1;
    LLVMTypeRef i8;
    LLVMTypeRef i8_ptr;
    LLVMTypeRef i32;
    LLVMTypeRef i64;
    LLVMTypeRef f64;
    LLVMTypeRef void_ty;

    LLVMValueRef fn_bread_value_size;
    LLVMValueRef fn_value_set_nil;
    LLVMValueRef fn_value_set_bool;
    LLVMValueRef fn_value_set_int;
    LLVMValueRef fn_value_set_double;
    LLVMValueRef fn_value_set_string;
    LLVMValueRef fn_value_set_array;
    LLVMValueRef fn_value_set_dict;
    LLVMValueRef fn_value_copy;
    LLVMValueRef fn_value_release;
    LLVMValueRef fn_print;
    LLVMValueRef fn_is_truthy;
    LLVMValueRef fn_unary_not;
    LLVMValueRef fn_binary_op;
    LLVMValueRef fn_index_op;
    LLVMValueRef fn_member_op;
    LLVMValueRef fn_method_call_op;
    LLVMValueRef fn_dict_set_value;
    LLVMValueRef fn_array_append_value;
    LLVMValueRef fn_var_decl;
    LLVMValueRef fn_var_decl_if_missing;
    LLVMValueRef fn_var_assign;
    LLVMValueRef fn_var_load;
    LLVMValueRef fn_push_scope;
    LLVMValueRef fn_pop_scope;
    LLVMValueRef fn_init_variables;
    LLVMValueRef fn_cleanup_variables;
    LLVMValueRef fn_init_functions;
    LLVMValueRef fn_cleanup_functions;
    LLVMValueRef fn_array_new;
    LLVMValueRef fn_array_release;
    LLVMValueRef fn_dict_new;
    LLVMValueRef fn_dict_release;

    int loop_depth;
    int tmp_counter;
} Cg;

// Function declarations used in llvm_backend.c
LLVMValueRef cg_declare_fn(Cg* cg, const char* name, LLVMTypeRef fn_type);
int cg_define_functions(Cg* cg);
LLVMValueRef cg_value_size(Cg* cg);
int cg_build_stmt_list(Cg* cg, LLVMValueRef val_size, ASTStmtList* program);

#endif // BREAD_CODEGEN_H
