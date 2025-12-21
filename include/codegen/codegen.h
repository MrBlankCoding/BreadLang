#ifndef BREAD_CODEGEN_H
#define BREAD_CODEGEN_H

#include <llvm-c/Core.h>
#include "compiler/ast.h"

// Forward declaration
// Define CG directly

typedef struct CgVar {
    char* name;
    LLVMValueRef alloca;
    struct CgVar* next;
} CgVar;

typedef struct CgScope {
    CgVar* vars;
    struct CgScope* parent;
} CgScope;

typedef struct CgFunction {
    char* name;
    LLVMValueRef fn;
    LLVMTypeRef type;
    ASTStmtList* body;
    int param_count;
    char** param_names;
    struct CgFunction* next;
    CgScope* scope;
    LLVMValueRef ret_slot;
} CgFunction;

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
    LLVMValueRef fn_string_create;
    LLVMValueRef fn_string_concat;
    LLVMValueRef fn_string_get_char;
    LLVMValueRef fn_array_create;
    LLVMValueRef fn_array_get;
    LLVMValueRef fn_array_set;
    LLVMValueRef fn_array_length;
    LLVMValueRef fn_range_create;
    LLVMValueRef fn_range_simple;
    LLVMValueRef fn_value_get_int;

    LLVMTypeRef ty_bread_value_size;
    LLVMTypeRef ty_value_set_nil;
    LLVMTypeRef ty_value_set_bool;
    LLVMTypeRef ty_value_set_int;
    LLVMTypeRef ty_value_set_double;
    LLVMTypeRef ty_value_set_string;
    LLVMTypeRef ty_value_set_array;
    LLVMTypeRef ty_value_set_dict;
    LLVMTypeRef ty_value_copy;
    LLVMTypeRef ty_value_release;
    LLVMTypeRef ty_print;
    LLVMTypeRef ty_is_truthy;
    LLVMTypeRef ty_unary_not;
    LLVMTypeRef ty_binary_op;
    LLVMTypeRef ty_index_op;
    LLVMTypeRef ty_member_op;
    LLVMTypeRef ty_method_call_op;
    LLVMTypeRef ty_dict_set_value;
    LLVMTypeRef ty_array_append_value;
    LLVMTypeRef ty_var_decl;
    LLVMTypeRef ty_var_decl_if_missing;
    LLVMTypeRef ty_var_assign;
    LLVMTypeRef ty_var_load;
    LLVMTypeRef ty_push_scope;
    LLVMTypeRef ty_pop_scope;
    LLVMTypeRef ty_init_variables;
    LLVMTypeRef ty_cleanup_variables;
    LLVMTypeRef ty_init_functions;
    LLVMTypeRef ty_cleanup_functions;
    LLVMTypeRef ty_array_new;
    LLVMTypeRef ty_array_release;
    LLVMTypeRef ty_dict_new;
    LLVMTypeRef ty_dict_release;
    LLVMTypeRef ty_string_create;
    LLVMTypeRef ty_string_concat;
    LLVMTypeRef ty_string_get_char;
    LLVMTypeRef ty_array_create;
    LLVMTypeRef ty_array_get;
    LLVMTypeRef ty_array_set;
    LLVMTypeRef ty_array_length;
    LLVMTypeRef ty_range_create;
    LLVMTypeRef ty_range_simple;
    LLVMTypeRef ty_value_get_int;

    int loop_depth;
    int tmp_counter;
    
    // Loop context for break/continue
    LLVMBasicBlockRef current_loop_end;
    LLVMBasicBlockRef current_loop_continue;

    CgFunction* functions;
    LLVMTypeRef value_type;
    LLVMTypeRef value_ptr_type;
} Cg;

LLVMValueRef cg_declare_fn(Cg* cg, const char* name, LLVMTypeRef fn_type);
int cg_define_functions(Cg* cg);
LLVMValueRef cg_value_size(Cg* cg);
int cg_build_stmt_list(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmtList* program);
LLVMValueRef cg_build_expr(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTExpr* expr);
int cg_build_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt);
CgVar* cg_scope_add_var(CgScope* scope, const char* name, LLVMValueRef alloca);
CgVar* cg_scope_find_var(CgScope* scope, const char* name);

#endif
