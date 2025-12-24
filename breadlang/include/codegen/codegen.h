#ifndef BREAD_CODEGEN_H
#define BREAD_CODEGEN_H

#include <llvm-c/Core.h>
#include "compiler/ast/ast.h"

// Forward declaration
// Define CG directly

typedef struct CgVar {
    char* name;
    LLVMValueRef alloca;
    VarType type;
    TypeDescriptor* type_desc;
    UnboxedType unboxed_type;  // stored unboxed ???!
    int is_const;
    int is_initialized;
    struct CgVar* next;
} CgVar;

typedef struct CgScope {
    CgVar* vars;
    struct CgScope* parent;
    int depth;
} CgScope;

typedef struct CgFunction {
    char* name;
    LLVMValueRef fn;
    LLVMTypeRef type;
    ASTStmtList* body;
    int param_count;
    int required_param_count;
    char** param_names;
    TypeDescriptor** param_type_descs;
    ASTExpr** param_defaults;
    VarType return_type;
    TypeDescriptor* return_type_desc;
    struct CgFunction* next;
    CgScope* scope;
    LLVMValueRef ret_slot;
    LLVMValueRef runtime_scope_base_depth_slot;
    
    // Method context for self/super support
    struct CgClass* current_class;  // Current class if this is a method
    LLVMValueRef self_param;        // Self parameter for methods
    int is_method;                  // Flag indicating if this is a method
} CgFunction;

typedef struct CgClass {
    char* name;
    char* parent_name;
    int field_count;
    char** field_names;
    TypeDescriptor** field_types;
    int method_count;
    ASTStmtFuncDecl** methods;
    ASTStmtFuncDecl* constructor;
    struct CgClass* next;
    
    // Runtime method information
    LLVMValueRef* method_functions;  // Generated LLVM functions for methods
    char** method_names;             // Method names for lookup
    LLVMValueRef constructor_function; // Generated LLVM function for constructor
} CgClass;

typedef struct CgStruct {
    char* name;
    int field_count;
    char** field_names;
    TypeDescriptor** field_types;
    struct CgStruct* next;
} CgStruct;

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
    LLVMValueRef fn_index_set_op;
    LLVMValueRef fn_member_op;
    LLVMTypeRef ty_member_set_op;
    LLVMValueRef fn_member_set_op;
    LLVMValueRef fn_method_call_op;
    LLVMValueRef fn_dict_set_value;
    LLVMValueRef fn_array_append_value;
    LLVMValueRef fn_var_decl;
    LLVMValueRef fn_var_decl_if_missing;
    LLVMValueRef fn_var_assign;
    LLVMValueRef fn_var_load;
    LLVMValueRef fn_push_scope;
    LLVMValueRef fn_pop_scope;
    LLVMTypeRef ty_can_pop_scope;
    LLVMValueRef fn_can_pop_scope;
    LLVMTypeRef ty_scope_depth;
    LLVMValueRef fn_scope_depth;
    LLVMTypeRef ty_pop_to_scope_depth;
    LLVMValueRef fn_pop_to_scope_depth;
    LLVMValueRef fn_bread_memory_init;
    LLVMValueRef fn_bread_memory_cleanup;
    LLVMValueRef fn_bread_string_intern_init;
    LLVMValueRef fn_bread_string_intern_cleanup;
    LLVMValueRef fn_bread_builtin_init;
    LLVMValueRef fn_bread_builtin_cleanup;
    LLVMValueRef fn_bread_error_init;
    LLVMValueRef fn_bread_error_cleanup;
    LLVMValueRef fn_init_variables;
    LLVMValueRef fn_cleanup_variables;
    LLVMValueRef fn_init_functions;
    LLVMValueRef fn_cleanup_functions;
    LLVMValueRef fn_array_new;
    LLVMValueRef fn_array_release;
    LLVMValueRef fn_dict_new;
    LLVMValueRef fn_dict_release;
    LLVMValueRef fn_dict_keys;
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
    LLVMValueRef fn_value_get_double;
    LLVMValueRef fn_value_get_bool;
    
    // Boxing/unboxing functions for unboxed primitives
    LLVMValueRef fn_bread_box_int;
    LLVMValueRef fn_bread_box_double;
    LLVMValueRef fn_bread_box_bool;
    LLVMValueRef fn_bread_unbox_int;
    LLVMValueRef fn_bread_unbox_double;
    LLVMValueRef fn_bread_unbox_bool;

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
    LLVMTypeRef ty_index_set_op;
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
    LLVMTypeRef ty_bread_memory_init;
    LLVMTypeRef ty_bread_memory_cleanup;
    LLVMTypeRef ty_bread_string_intern_init;
    LLVMTypeRef ty_bread_string_intern_cleanup;
    LLVMTypeRef ty_bread_builtin_init;
    LLVMTypeRef ty_bread_builtin_cleanup;
    LLVMTypeRef ty_bread_error_init;
    LLVMTypeRef ty_bread_error_cleanup;
    LLVMTypeRef ty_init_variables;
    LLVMTypeRef ty_cleanup_variables;
    LLVMTypeRef ty_init_functions;
    LLVMTypeRef ty_cleanup_functions;
    LLVMTypeRef ty_array_new;
    LLVMTypeRef ty_array_release;
    LLVMTypeRef ty_dict_new;
    LLVMTypeRef ty_dict_release;
    LLVMTypeRef ty_dict_keys;
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
    LLVMTypeRef ty_value_get_double;
    LLVMTypeRef ty_value_get_bool;
    
    // Boxing/unboxing function types
    LLVMTypeRef ty_bread_box_int;
    LLVMTypeRef ty_bread_box_double;
    LLVMTypeRef ty_bread_box_bool;
    LLVMTypeRef ty_bread_unbox_int;
    LLVMTypeRef ty_bread_unbox_double;
    LLVMTypeRef ty_bread_unbox_bool;

    int loop_depth;
    int tmp_counter;
    
    LLVMBasicBlockRef current_loop_end;
    LLVMBasicBlockRef current_loop_continue;
    LLVMValueRef current_loop_scope_base_depth_slot;

    CgFunction* functions;
    CgStruct* structs;
    CgClass* classes;
    LLVMTypeRef value_type;
    LLVMTypeRef value_ptr_type;
    
    CgScope* global_scope;
    int scope_depth;
    int had_error;
} Cg;

LLVMValueRef cg_declare_fn(Cg* cg, const char* name, LLVMTypeRef fn_type);
int cg_define_functions(Cg* cg);
LLVMValueRef cg_value_size(Cg* cg);
int cg_build_stmt_list(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmtList* program);
LLVMValueRef cg_build_expr(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTExpr* expr);
int cg_build_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt);
CgVar* cg_scope_add_var(CgScope* scope, const char* name, LLVMValueRef alloca);
CgVar* cg_scope_find_var(CgScope* scope, const char* name);

// support funcs
typedef enum {
    CG_VALUE_BOXED,        
    CG_VALUE_UNBOXED_INT,  
    CG_VALUE_UNBOXED_BOOL,
    CG_VALUE_UNBOXED_DOUBLE 
} CgValueType;

typedef struct {
    CgValueType type;
    LLVMValueRef value;
    LLVMTypeRef llvm_type;
} CgValue;

CgValue cg_create_value(CgValueType type, LLVMValueRef value, LLVMTypeRef llvm_type);
CgValue cg_build_expr_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* expr);
LLVMValueRef cg_box_value(Cg* cg, CgValue val);
CgValue cg_unbox_value(Cg* cg, LLVMValueRef boxed_val, VarType expected_type);
int cg_can_unbox_expr(Cg* cg, ASTExpr* expr);
CgValue cg_build_binary_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* left, ASTExpr* right, char op);
CgValue cg_build_unary_unboxed(Cg* cg, CgFunction* cg_fn, ASTExpr* operand, char op);

int cg_semantic_analyze(Cg* cg, ASTStmtList* program);
int cg_analyze_stmt(Cg* cg, ASTStmt* stmt);
int cg_analyze_expr(Cg* cg, ASTExpr* expr);
void cg_enter_scope(Cg* cg);
void cg_leave_scope(Cg* cg);
int cg_declare_var(Cg* cg, const char* name, const TypeDescriptor* type_desc, int is_const);
CgVar* cg_find_var(Cg* cg, const char* name);
int cg_declare_function_from_ast(Cg* cg, const ASTStmtFuncDecl* func_decl, const SourceLoc* loc);
CgFunction* cg_find_function(Cg* cg, const char* name);
int cg_declare_class_from_ast(Cg* cg, const ASTStmtClassDecl* class_decl, const SourceLoc* loc);
CgClass* cg_find_class(Cg* cg, const char* name);
int cg_collect_all_fields(Cg* cg, CgClass* class_def, char*** all_field_names, int* total_field_count);
int cg_collect_all_methods(Cg* cg, CgClass* class_def, char*** all_method_names, int* total_method_count);
void cg_error(Cg* cg, const char* msg, const char* name);
void cg_error_at(Cg* cg, const char* msg, const char* name, const SourceLoc* loc);
void cg_type_error_at(Cg* cg, const char* msg, const TypeDescriptor* expected, const TypeDescriptor* actual, const SourceLoc* loc);

// Type checking functions (simplified for Phase 2)
VarType cg_infer_expr_type_simple(Cg* cg, ASTExpr* expr);
TypeDescriptor* cg_infer_expr_type_desc_simple(Cg* cg, ASTExpr* expr);
TypeDescriptor* cg_infer_expr_type_desc_with_function(Cg* cg, CgFunction* cg_fn, ASTExpr* expr);
int cg_check_condition_type_simple(Cg* cg, ASTExpr* condition);
int cg_check_condition_type_desc_simple(Cg* cg, ASTExpr* condition);
void cg_type_error(Cg* cg, const char* msg, const TypeDescriptor* expected, const TypeDescriptor* actual);

#endif
