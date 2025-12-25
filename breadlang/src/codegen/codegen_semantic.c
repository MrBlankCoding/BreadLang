#include "codegen_internal.h"

#include "runtime/error.h"
#include "runtime/builtins.h"
#include "compiler/ast/ast_types.h"

TypeDescriptor* cg_infer_expr_type_desc_simple(Cg* cg, ASTExpr* expr);

void cg_error(Cg* cg, const char* msg, const char* name) {
    if (name) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "%s '%s'", msg, name);
        BREAD_ERROR_SET_COMPILE_ERROR(error_msg);
    } else {
        BREAD_ERROR_SET_COMPILE_ERROR(msg);
    }
    if (cg) cg->had_error = 1;
}

void cg_error_at(Cg* cg, const char* msg, const char* name, const SourceLoc* loc) {
    char error_msg[512];
    if (name) {
        snprintf(error_msg, sizeof(error_msg), "%s '%s'", msg, name);
    } else {
        snprintf(error_msg, sizeof(error_msg), "%s", msg);
    }
    
    if (loc && loc->filename) {
        bread_error_set(BREAD_ERROR_COMPILE_ERROR, error_msg, loc->filename, loc->line, loc->column);
    } else {
        BREAD_ERROR_SET_COMPILE_ERROR(error_msg);
    }
    
    if (cg) cg->had_error = 1;
}

void cg_type_error_at(Cg* cg, const char* msg, const TypeDescriptor* expected, const TypeDescriptor* actual, const SourceLoc* loc) {
    char expected_str[256], actual_str[256];
    type_descriptor_to_string(expected, expected_str, sizeof(expected_str));
    type_descriptor_to_string(actual, actual_str, sizeof(actual_str));
    
    char error_msg[512];
    snprintf(error_msg, sizeof(error_msg), "%s: expected %s, got %s", msg, expected_str, actual_str);
    
    if (loc && loc->filename) {
        bread_error_set(BREAD_ERROR_COMPILE_ERROR, error_msg, loc->filename, loc->line, loc->column);
    } else {
        BREAD_ERROR_SET_COMPILE_ERROR(error_msg);
    }
    
    if (cg) cg->had_error = 1;
}

void cg_type_error(Cg* cg, const char* msg, const TypeDescriptor* expected, const TypeDescriptor* actual) {
    char expected_str[256], actual_str[256];
    type_descriptor_to_string(expected, expected_str, sizeof(expected_str));
    type_descriptor_to_string(actual, actual_str, sizeof(actual_str));
    
    char error_msg[512];
    snprintf(error_msg, sizeof(error_msg), "%s: expected %s, got %s", msg, expected_str, actual_str);
    BREAD_ERROR_SET_COMPILE_ERROR(error_msg);
    
    if (cg) cg->had_error = 1;
}

void cg_enter_scope(Cg* cg) {
    if (cg) cg->scope_depth++;
}

void cg_leave_scope(Cg* cg) {
    if (!cg || !cg->global_scope) return;
    
    // Remove from current scope depth
    CgScope* scope = cg->global_scope;
    CgVar** var_ptr = &scope->vars;
    while (*var_ptr) {
        CgVar* var = *var_ptr;
        if (var->is_initialized >= cg->scope_depth) {  // use temp!
            *var_ptr = var->next;
            free(var->name);
            type_descriptor_free(var->type_desc);
            free(var);
        } else {
            var_ptr = &var->next;
        }
    }
    
    if (cg->scope_depth > 0) cg->scope_depth--;
}

int cg_declare_var(Cg* cg, const char* name, const TypeDescriptor* type_desc, int is_const) {
    if (!cg || !name) return 0;
    
    if (!cg->global_scope) {
        cg->global_scope = cg_scope_new(NULL);
    }
    
    // Check if its redefined
    for (CgVar* v = cg->global_scope->vars; v; v = v->next) {
        if (v->is_initialized == cg->scope_depth && strcmp(v->name, name) == 0) {
            cg_error(cg, "Variable already declared", name);
            return 0;
        }
    }
    
    // New var!
    CgVar* v = (CgVar*)malloc(sizeof(CgVar));
    v->name = strdup(name);
    v->alloca = NULL;  // is set duing codegen
    v->type_desc = type_descriptor_clone(type_desc);
    if (!v->type_desc) {
        free(v->name);
        free(v);
        return 0;
    }
    v->type = v->type_desc->base_type;
    v->is_const = is_const;
    v->is_initialized = cg->scope_depth;
    v->next = cg->global_scope->vars;
    cg->global_scope->vars = v;
    
    return 1;
}

static int cg_stmt_guarantees_return(const ASTStmt* stmt);

static int cg_stmt_list_guarantees_return(const ASTStmtList* list) {
    if (!list) return 0;

    for (const ASTStmt* s = list->head; s; s = s->next) {
        if (cg_stmt_guarantees_return(s)) {
            return 1;
        }
    }

    return 0;
}

static int cg_stmt_guarantees_return(const ASTStmt* stmt) {
    if (!stmt) return 0;

    switch (stmt->kind) {
        case AST_STMT_RETURN:
            return 1;
        case AST_STMT_IF: {
            const ASTStmtList* then_branch = stmt->as.if_stmt.then_branch;
            const ASTStmtList* else_branch = stmt->as.if_stmt.else_branch;
            if (!else_branch) return 0;
            return cg_stmt_list_guarantees_return(then_branch) && cg_stmt_list_guarantees_return(else_branch);
        }
        default:
            return 0;
    }
}

static int cg_check_return_stmt(Cg* cg, const ASTStmtFuncDecl* func_decl, const ASTStmt* ret_stmt) {
    if (!cg || !func_decl || !ret_stmt) return 0;

    if (ret_stmt->kind != AST_STMT_RETURN) return 1;

    if (ret_stmt->as.ret.expr) {
        if (ret_stmt->as.ret.expr->tag.is_known && ret_stmt->as.ret.expr->tag.type_desc) {
            const TypeDescriptor* return_type = ret_stmt->as.ret.expr->tag.type_desc;
            const TypeDescriptor* expected_type = func_decl->return_type_desc;

            if (expected_type && !type_descriptor_compatible(return_type, expected_type)) {
                cg_type_error(cg, "Return type mismatch", expected_type, return_type);
                return 0;
            }
        }
    } else {
        // Return with no expression - check if function expects void/nil
        if (func_decl->return_type != TYPE_NIL) {
            cg_error(cg, "Missing return value", NULL);
            return 0;
        }
    }

    return 1;
}

static int cg_check_returns_in_stmt(Cg* cg, const ASTStmtFuncDecl* func_decl, const ASTStmt* stmt) {
    if (!cg || !func_decl || !stmt) return 1;

    switch (stmt->kind) {
        case AST_STMT_RETURN:
            return cg_check_return_stmt(cg, func_decl, stmt);
        case AST_STMT_IF: {
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    if (!cg_check_returns_in_stmt(cg, func_decl, s)) return 0;
                }
            }
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    if (!cg_check_returns_in_stmt(cg, func_decl, s)) return 0;
                }
            }
            return 1;
        }
        case AST_STMT_WHILE: {
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    if (!cg_check_returns_in_stmt(cg, func_decl, s)) return 0;
                }
            }
            return 1;
        }
        case AST_STMT_FOR: {
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    if (!cg_check_returns_in_stmt(cg, func_decl, s)) return 0;
                }
            }
            return 1;
        }
        case AST_STMT_FOR_IN: {
            if (stmt->as.for_in_stmt.body) {
                for (ASTStmt* s = stmt->as.for_in_stmt.body->head; s; s = s->next) {
                    if (!cg_check_returns_in_stmt(cg, func_decl, s)) return 0;
                }
            }
            return 1;
        }
        default:
            return 1;
    }
}

int cg_check_condition_type_desc_simple(Cg* cg, ASTExpr* condition) {
    TypeDescriptor* cond_type = cg_infer_expr_type_desc_simple(cg, condition);
    if (!cond_type) return 0;

    if (cond_type->base_type != TYPE_BOOL) {
        type_descriptor_free(cond_type);
        cg_error(cg, "Condition must be Bool type", NULL);
        return 0;
    }

    type_descriptor_free(cond_type);
    return 1;
}

CgVar* cg_find_var(Cg* cg, const char* name) {
    if (!cg || !name || !cg->global_scope) return NULL;
    
    for (CgVar* v = cg->global_scope->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return NULL;
}

static CgVar* cg_find_var_in_scope(Cg* cg, const char* name) {
    if (!cg || !name || !cg->global_scope) return NULL;
    
    for (CgVar* v = cg->global_scope->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0 && v->is_initialized <= cg->scope_depth) {
            return v;
        }
    }
    return NULL;
}

static CgStruct* cg_find_struct(Cg* cg, const char* name) {
    if (!cg || !name) return NULL;

    for (CgStruct* s = cg->structs; s; s = s->next) {
        if (s->name && strcmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

static int cg_declare_struct_from_ast(Cg* cg, const ASTStmtStructDecl* struct_decl, const SourceLoc* loc) {
    if (!cg || !struct_decl || !struct_decl->name) return 0;

    if (cg_find_struct(cg, struct_decl->name)) {
        cg_error_at(cg, "Struct already declared", struct_decl->name, loc);
        return 0;
    }

    CgStruct* s = (CgStruct*)malloc(sizeof(CgStruct));
    if (!s) return 0;
    memset(s, 0, sizeof(CgStruct));

    s->name = strdup(struct_decl->name);
    s->field_count = struct_decl->field_count;
    s->field_names = struct_decl->field_names;
    s->field_types = struct_decl->field_types;

    s->next = cg->structs;
    cg->structs = s;
    return 1;
}

int cg_declare_function_from_ast(Cg* cg, const ASTStmtFuncDecl* func_decl, const SourceLoc* loc) {
    if (!cg || !func_decl || !func_decl->name) return 0;
    
    for (CgFunction* f = cg->functions; f; f = f->next) {
        if (strcmp(f->name, func_decl->name) == 0) {
            cg_error_at(cg, "Function already declared", func_decl->name, loc);
            return 0;
        }
    }
    
    // Create a new CgFunction node
    CgFunction* new_func = (CgFunction*)malloc(sizeof(CgFunction));
    if (!new_func) return 0;
    
    memset(new_func, 0, sizeof(CgFunction));
    new_func->name = strdup(func_decl->name);
    new_func->param_count = func_decl->param_count;
    new_func->required_param_count = func_decl->param_count;
    new_func->param_defaults = func_decl->param_defaults;
    new_func->return_type = func_decl->return_type;
    new_func->return_type_desc = type_descriptor_clone(func_decl->return_type_desc);

    if (func_decl->param_defaults) {
        int required = 0;
        for (int i = 0; i < func_decl->param_count; i++) {
            if (func_decl->param_defaults[i] == NULL) {
                required++;
            } else {
                break;
            }
        }
        new_func->required_param_count = required;
    }
    
    if (func_decl->param_count > 0) {
        new_func->param_names = (char**)malloc(sizeof(char*) * func_decl->param_count);
        new_func->param_type_descs = (TypeDescriptor**)malloc(sizeof(TypeDescriptor*) * func_decl->param_count);
        
        if (!new_func->param_names || !new_func->param_type_descs) {
            free(new_func->param_names);
            free(new_func->param_type_descs);
            free(new_func->name);
            free(new_func);
            return 0;
        }
        
        for (int i = 0; i < func_decl->param_count; i++) {
            new_func->param_names[i] = strdup(func_decl->param_names[i]);
            new_func->param_type_descs[i] = type_descriptor_clone(func_decl->param_type_descs[i]);
        }
    }
    
    new_func->next = cg->functions;
    cg->functions = new_func;
    
    return 1;
}

CgFunction* cg_find_function(Cg* cg, const char* name) {
    if (!cg || !name) return NULL;
    
    for (CgFunction* f = cg->functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            return f;
        }
    }
    return NULL;
}

CgClass* cg_find_class(Cg* cg, const char* name) {
    if (!cg || !name) return NULL;
    
    for (CgClass* c = cg->classes; c; c = c->next) {
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

 static ASTStmtFuncDecl* cg_find_class_method_decl(Cg* cg, CgClass* class_def, const char* method_name) {
     if (!cg || !class_def || !method_name) return NULL;

     for (int i = 0; i < class_def->method_count; i++) {
         ASTStmtFuncDecl* m = class_def->methods[i];
         if (m && m->name && strcmp(m->name, method_name) == 0) {
             return m;
         }
     }

     if (class_def->parent_name) {
         CgClass* parent = cg_find_class(cg, class_def->parent_name);
         if (parent) return cg_find_class_method_decl(cg, parent, method_name);
     }

     return NULL;
 }

 static const char* cg_type_desc_get_nominal_name(const TypeDescriptor* t) {
     if (!t) return NULL;
     if (t->base_type == TYPE_CLASS) return t->params.class_type.name;
     if (t->base_type == TYPE_STRUCT) return t->params.struct_type.name;
     return NULL;
 }

 static TypeDescriptor* cg_common_superclass_desc(Cg* cg, const TypeDescriptor* a, const TypeDescriptor* b) {
     if (!cg || !a || !b) return NULL;

     const char* a_name = cg_type_desc_get_nominal_name(a);
     const char* b_name = cg_type_desc_get_nominal_name(b);
     if (!a_name || !b_name) return NULL;

     CgClass* a_cls = cg_find_class(cg, a_name);
     CgClass* b_cls = cg_find_class(cg, b_name);
     if (!a_cls || !b_cls) return NULL;

     // Walk a's ancestors, then search for first match in b's ancestry.
     for (CgClass* a_it = a_cls; a_it; a_it = a_it->parent_name ? cg_find_class(cg, a_it->parent_name) : NULL) {
         for (CgClass* b_it = b_cls; b_it; b_it = b_it->parent_name ? cg_find_class(cg, b_it->parent_name) : NULL) {
             if (a_it->name && b_it->name && strcmp(a_it->name, b_it->name) == 0) {
                 return type_descriptor_create_class(a_it->name, a_it->parent_name,
                                                   a_it->field_count, a_it->field_names, a_it->field_types);
             }
         }
     }

     return NULL;
 }

// Helper function to collect all fields from a class hierarchy
int cg_collect_all_fields(Cg* cg, CgClass* class_def, char*** all_field_names, int* total_field_count) {
    if (!cg || !class_def || !all_field_names || !total_field_count) return 0;
    
    *total_field_count = 0;
    *all_field_names = NULL;
    
    // Count total fields including inherited ones
    CgClass* current = class_def;
    while (current) {
        *total_field_count += current->field_count;
        current = current->parent_name ? cg_find_class(cg, current->parent_name) : NULL;
    }
    
    if (*total_field_count == 0) return 1;
    
    // Allocate array for all field names
    *all_field_names = malloc(sizeof(char*) * (*total_field_count));
    if (!*all_field_names) return 0;
    
    // Collect field names (parent fields first, then child fields)
    int index = 0;
    
    // First, collect parent fields recursively
    if (class_def->parent_name) {
        CgClass* parent = cg_find_class(cg, class_def->parent_name);
        if (parent) {
            char** parent_fields;
            int parent_count;
            if (cg_collect_all_fields(cg, parent, &parent_fields, &parent_count)) {
                for (int i = 0; i < parent_count; i++) {
                    (*all_field_names)[index++] = strdup(parent_fields[i]);
                }
                // Free the temporary parent fields array
                for (int i = 0; i < parent_count; i++) {
                    free(parent_fields[i]);
                }
                free(parent_fields);
            }
        }
    }
    
    // Then add this class's own fields
    for (int i = 0; i < class_def->field_count; i++) {
        (*all_field_names)[index++] = strdup(class_def->field_names[i]);
    }
    
    return 1;
}

// Helper function to collect all methods from a class hierarchy
int cg_collect_all_methods(Cg* cg, CgClass* class_def, char*** all_method_names, int* total_method_count) {
    if (!cg || !class_def || !all_method_names || !total_method_count) return 0;
    
    // Count total methods
    int count = class_def->method_count;
    CgClass* parent = class_def->parent_name ? cg_find_class(cg, class_def->parent_name) : NULL;
    while (parent) {
        count += parent->method_count;
        parent = parent->parent_name ? cg_find_class(cg, parent->parent_name) : NULL;
    }
    
    if (count == 0) {
        *all_method_names = NULL;
        *total_method_count = 0;
        return 1;
    }
    
    // Allocate array for all method names
    char** methods = malloc(sizeof(char*) * count);
    if (!methods) return 0;
    
    int index = 0;
    
    // Add current class methods first
    for (int i = 0; i < class_def->method_count; i++) {
        if (class_def->method_names[i]) {
            methods[index] = strdup(class_def->method_names[i]);
            if (!methods[index]) {
                // Cleanup on failure
                for (int j = 0; j < index; j++) free(methods[j]);
                free(methods);
                return 0;
            }
            index++;
        }
    }
    
    // Add parent methods (avoiding duplicates)
    parent = class_def->parent_name ? cg_find_class(cg, class_def->parent_name) : NULL;
    while (parent) {
        for (int i = 0; i < parent->method_count; i++) {
            if (parent->method_names[i]) {
                // Check if method is already in the list (overridden)
                int duplicate = 0;
                for (int j = 0; j < index; j++) {
                    if (strcmp(methods[j], parent->method_names[i]) == 0) {
                        duplicate = 1;
                        break;
                    }
                }
                
                if (!duplicate) {
                    methods[index] = strdup(parent->method_names[i]);
                    if (!methods[index]) {
                        // Cleanup on failure
                        for (int j = 0; j < index; j++) free(methods[j]);
                        free(methods);
                        return 0;
                    }
                    index++;
                }
            }
        }
        parent = parent->parent_name ? cg_find_class(cg, parent->parent_name) : NULL;
    }
    
    *all_method_names = methods;
    *total_method_count = index;
    return 1;
}

int cg_declare_class_from_ast(Cg* cg, const ASTStmtClassDecl* class_decl, const SourceLoc* loc) {
    if (!cg || !class_decl || !class_decl->name) return 0;
    
    // Check if class already exists
    if (cg_find_class(cg, class_decl->name)) {
        cg_error_at(cg, "Class already declared", class_decl->name, loc);
        return 0;
    }
    
    // Create new class
    CgClass* new_class = malloc(sizeof(CgClass));
    if (!new_class) return 0;
    
    new_class->name = strdup(class_decl->name);
    new_class->parent_name = class_decl->parent_name ? strdup(class_decl->parent_name) : NULL;
    new_class->field_count = class_decl->field_count;
    new_class->field_names = class_decl->field_names;
    new_class->field_types = class_decl->field_types;
    new_class->method_count = class_decl->method_count;
    new_class->methods = class_decl->methods;
    new_class->constructor = class_decl->constructor;
    new_class->next = cg->classes;
    
    // Initialize runtime method information
    new_class->method_functions = NULL;
    new_class->method_names = NULL;
    new_class->constructor_function = NULL; // Initialize constructor function
    if (new_class->method_count > 0 && new_class->methods) {
        new_class->method_functions = malloc(sizeof(LLVMValueRef) * new_class->method_count);
        new_class->method_names = malloc(sizeof(char*) * new_class->method_count);
        if (!new_class->method_functions || !new_class->method_names) {
            free(new_class->method_functions);
            free(new_class->method_names);
            free(new_class->name);
            free(new_class->parent_name);
            free(new_class);
            return 0;
        }
        
        // Copy method names for runtime lookup
        for (int i = 0; i < new_class->method_count; i++) {
            new_class->method_functions[i] = NULL; // Will be set during codegen
            if (!new_class->methods[i] || !new_class->methods[i]->name) {
                new_class->method_names[i] = NULL;
                continue;
            }
            new_class->method_names[i] = strdup(new_class->methods[i]->name);
        }
    }
    
    cg->classes = new_class;
    
    return 1;
}

VarType cg_infer_expr_type_simple(Cg* cg, ASTExpr* expr) {
    if (!expr) return TYPE_NIL;
    
    switch (expr->kind) {
        case AST_EXPR_INT:
            return TYPE_INT;
        case AST_EXPR_DOUBLE:
            return TYPE_DOUBLE;
        case AST_EXPR_BOOL:
            return TYPE_BOOL;
        case AST_EXPR_STRING:
        case AST_EXPR_STRING_LITERAL:
            return TYPE_STRING;
        case AST_EXPR_NIL:
            return TYPE_NIL;
            
        case AST_EXPR_VAR: {
            CgVar* var = cg_find_var_in_scope(cg, expr->as.var_name);
            if (!var) return TYPE_NIL;
            return var->type;
        }
        
        case AST_EXPR_BINARY: {
            VarType left_type = cg_infer_expr_type_simple(cg, expr->as.binary.left);
            VarType right_type = cg_infer_expr_type_simple(cg, expr->as.binary.right);
            
            // Arithmetic operators: both operands must be same type
            // Special-case: String + String is concatenation
            if (expr->as.binary.op == '+' || expr->as.binary.op == '-' || 
                expr->as.binary.op == '*' || expr->as.binary.op == '/' || 
                expr->as.binary.op == '%') {
                
                // Special case for string concatenation
                if (expr->as.binary.op == '+' && 
                    left_type == TYPE_STRING && right_type == TYPE_STRING) {
                    return TYPE_STRING;
                }
                
                if (left_type != right_type) {
                    cg_error(cg, "Type mismatch in binary operation", NULL);
                    return TYPE_NIL;
                }
                
                if (left_type != TYPE_INT && left_type != TYPE_DOUBLE) {
                    cg_error(cg, "Arithmetic operations require numeric types", NULL);
                    return TYPE_NIL;
                }
                
                return left_type;
            }
            
            // Comparison operators: return Bool
            // Note: parser encodes <= as 'l', >= as 'g', == as '=', != as '!'
            if (expr->as.binary.op == '<' || expr->as.binary.op == '>' ||
                expr->as.binary.op == 'l' || expr->as.binary.op == 'g' ||
                expr->as.binary.op == '=' || expr->as.binary.op == '!') {
                return TYPE_BOOL;
            }
            
            if (expr->as.binary.op == '&' || expr->as.binary.op == '|') { // && ||
                if (left_type != TYPE_BOOL || right_type != TYPE_BOOL) {
                    cg_error(cg, "Logical operations require Bool operands", NULL);
                    return TYPE_NIL;
                }
                return TYPE_BOOL;
            }
            
            return TYPE_NIL;
        }

        case AST_EXPR_METHOD_CALL: {
            VarType target_type = cg_infer_expr_type_simple(cg, expr->as.method_call.target);
            if (target_type != TYPE_CLASS) return TYPE_NIL;

            // Best-effort: infer from current class context is not available here, so return Nil.
            // The full TypeDescriptor-based inference below handles proper lookup.
            return TYPE_NIL;
        }
        
        case AST_EXPR_UNARY: {
            VarType operand_type = cg_infer_expr_type_simple(cg, expr->as.unary.operand);
            
            if (expr->as.unary.op == '-') {
                // Numeric negation
                if (operand_type != TYPE_INT && operand_type != TYPE_DOUBLE) {
                    cg_error(cg, "Numeric negation requires numeric type", NULL);
                    return TYPE_NIL;
                }
                return operand_type;
            }
            
            if (expr->as.unary.op == '!') {
                // Logical negation
                if (operand_type != TYPE_BOOL) {
                    cg_error(cg, "Logical negation requires Bool type", NULL);
                    return TYPE_NIL;
                }
                return TYPE_BOOL;
            }
            
            return TYPE_NIL;
        }
        
        case AST_EXPR_ARRAY_LITERAL: {
            if (expr->as.array_literal.element_count == 0) {
                return TYPE_ARRAY; // Empty array
            }
            
            VarType element_type = cg_infer_expr_type_simple(cg, expr->as.array_literal.elements[0]);
            for (int i = 1; i < expr->as.array_literal.element_count; i++) {
                VarType elem_type = cg_infer_expr_type_simple(cg, expr->as.array_literal.elements[i]);
                if (elem_type != element_type) {
                    cg_error(cg, "Array literal elements must have same type", NULL);
                    return TYPE_NIL;
                }
            }
            
            return TYPE_ARRAY;
        }
        
        case AST_EXPR_DICT: {
            if (expr->as.dict.entry_count == 0) {
                return TYPE_DICT; // Empty dict
            }
            
            VarType key_type = cg_infer_expr_type_simple(cg, expr->as.dict.entries[0].key);
            VarType value_type = cg_infer_expr_type_simple(cg, expr->as.dict.entries[0].value);
            
            for (int i = 1; i < expr->as.dict.entry_count; i++) {
                VarType entry_key_type = cg_infer_expr_type_simple(cg, expr->as.dict.entries[i].key);
                VarType entry_value_type = cg_infer_expr_type_simple(cg, expr->as.dict.entries[i].value);
                
                if (entry_key_type != key_type || entry_value_type != value_type) {
                    cg_error(cg, "Dict literal entries must have consistent key and value types", NULL);
                    return TYPE_NIL;
                }
            }
            
            return TYPE_DICT;
        }
        
        case AST_EXPR_INDEX: {
            VarType target_type = cg_infer_expr_type_simple(cg, expr->as.index.target);

            if (target_type == TYPE_STRING) {
                return TYPE_STRING;
            }

            // If we have richer type info available, try to return the actual element/value type.
            TypeDescriptor* target_desc = NULL;
            if (expr->as.index.target && expr->as.index.target->tag.is_known && expr->as.index.target->tag.type_desc) {
                target_desc = type_descriptor_clone(expr->as.index.target->tag.type_desc);
            } else if (expr->as.index.target && expr->as.index.target->kind == AST_EXPR_VAR) {
                CgVar* v = cg_find_var_in_scope(cg, expr->as.index.target->as.var_name);
                if (v && v->type_desc) {
                    target_desc = type_descriptor_clone(v->type_desc);
                }
            } else if (expr->as.index.target && expr->as.index.target->kind == AST_EXPR_ARRAY_LITERAL) {
                if (expr->as.index.target->as.array_literal.element_count > 0) {
                    return cg_infer_expr_type_simple(cg, expr->as.index.target->as.array_literal.elements[0]);
                }
            } else if (expr->as.index.target && expr->as.index.target->kind == AST_EXPR_DICT) {
                if (expr->as.index.target->as.dict.entry_count > 0) {
                    return cg_infer_expr_type_simple(cg, expr->as.index.target->as.dict.entries[0].value);
                }
            }

            if (target_desc) {
                VarType out = TYPE_NIL;
                if (target_desc->base_type == TYPE_ARRAY && target_desc->params.array.element_type) {
                    out = target_desc->params.array.element_type->base_type;
                } else if (target_desc->base_type == TYPE_DICT && target_desc->params.dict.value_type) {
                    out = target_desc->params.dict.value_type->base_type;
                }
                type_descriptor_free(target_desc);
                if (out != TYPE_NIL) return out;
            }

            if (target_type == TYPE_ARRAY) {
                return TYPE_INT;
            } else if (target_type == TYPE_DICT) {
                return TYPE_INT;
            }

            cg_error_at(cg, "Cannot index this type (only arrays, dictionaries, and strings can be indexed)", NULL, &expr->loc);
            return TYPE_NIL;
        }
        
        case AST_EXPR_MEMBER: {
            // Member access (e.g., .length) returns Int for now
            return TYPE_INT;
        }
        
        case AST_EXPR_CALL: {
            // Check for class constructors
            CgClass* class = cg_find_class(cg, expr->as.call.name);
            if (class) {
                return TYPE_CLASS;
            }
            
            // Check for user-defined functions
            CgFunction* func = cg_find_function(cg, expr->as.call.name);
            if (func) {
                return func->return_type;
            }
            
            // Check for builtins
            const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
            if (builtin) {
                return builtin->return_type;
            }
            
            return TYPE_NIL;
        }
        
        case AST_EXPR_STRUCT_LITERAL: {
            return TYPE_STRUCT;
        }
        
        case AST_EXPR_CLASS_LITERAL: {
            return TYPE_CLASS;
        }
        
        default:
            return TYPE_NIL;
    }
}

TypeDescriptor* cg_infer_expr_type_desc_simple(Cg* cg, ASTExpr* expr) {
    if (!expr) return NULL;

    switch (expr->kind) {
        case AST_EXPR_INT:
            return type_descriptor_create_primitive(TYPE_INT);
        case AST_EXPR_DOUBLE:
            return type_descriptor_create_primitive(TYPE_DOUBLE);
        case AST_EXPR_BOOL:
            return type_descriptor_create_primitive(TYPE_BOOL);
        case AST_EXPR_STRING:
        case AST_EXPR_STRING_LITERAL:
            return type_descriptor_create_primitive(TYPE_STRING);
        case AST_EXPR_NIL:
            return type_descriptor_create_primitive(TYPE_NIL);

        case AST_EXPR_VAR: {
            CgVar* var = cg_find_var_in_scope(cg, expr->as.var_name);
            if (!var || !var->type_desc) return NULL;
            return type_descriptor_clone(var->type_desc);
        }

        case AST_EXPR_CALL: {
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                TypeDescriptor* elem = type_descriptor_create_primitive(TYPE_INT);
                if (!elem) return NULL;
                TypeDescriptor* out = type_descriptor_create_array(elem);
                if (!out) {
                    type_descriptor_free(elem);
                    return NULL;
                }
                return out;
            }

            const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
            if (builtin) {
                return type_descriptor_create_primitive(builtin->return_type);
            }

            // Check for user-defined functions
            CgFunction* func = cg_find_function(cg, expr->as.call.name);
            if (func) {
                if (func->return_type_desc) {
                    return type_descriptor_clone(func->return_type_desc);
                } else {
                    return type_descriptor_create_primitive(func->return_type);
                }
            }

            // Check for class constructors
            CgClass* class = cg_find_class(cg, expr->as.call.name);
            if (class) {
                return type_descriptor_create_class(class->name, class->parent_name, 
                                                  class->field_count, class->field_names, class->field_types);
            }

            cg_error_at(cg, "Unknown function or class", expr->as.call.name, &expr->loc);
            return NULL;
        }

        case AST_EXPR_METHOD_CALL: {
            TypeDescriptor* target_type = cg_infer_expr_type_desc_simple(cg, expr->as.method_call.target);
            if (!target_type) return NULL;

            // Only class method signatures are tracked at compile time right now.
            // Note: parse_type_descriptor treats unknown identifiers as TYPE_STRUCT; we
            // resolve TYPE_STRUCT(name) to a class if a matching class exists.
            const char* class_name = NULL;
            if (target_type->base_type == TYPE_CLASS) {
                class_name = target_type->params.class_type.name;
            } else if (target_type->base_type == TYPE_STRUCT) {
                class_name = target_type->params.struct_type.name;
            } else {
                type_descriptor_free(target_type);
                // Non-class targets may still support runtime-dispatched methods (e.g. Array/String/Dict).
                // We don't have static signatures for those here, so fall back to an "unknown" return type.
                return type_descriptor_create_primitive(TYPE_NIL);
            }

            const char* method_name = expr->as.method_call.name;
            if (!class_name || !method_name) {
                type_descriptor_free(target_type);
                cg_error_at(cg, "Invalid method call", NULL, &expr->loc);
                return NULL;
            }

            CgClass* class_def = cg_find_class(cg, class_name);
            if (!class_def) {
                type_descriptor_free(target_type);
                // If we can't resolve the nominal type as a class, treat as runtime dispatch.
                return type_descriptor_create_primitive(TYPE_NIL);
            }

            // Special-case constructor calls: init always returns Nil.
            if (strcmp(method_name, "init") == 0) {
                type_descriptor_free(target_type);
                return type_descriptor_create_primitive(TYPE_NIL);
            }

            ASTStmtFuncDecl* method_decl = cg_find_class_method_decl(cg, class_def, method_name);
            if (!method_decl) {
                type_descriptor_free(target_type);
                // Unknown method at compile-time; allow runtime dispatch.
                return type_descriptor_create_primitive(TYPE_NIL);
            }

            type_descriptor_free(target_type);

            if (method_decl->return_type_desc) {
                return type_descriptor_clone(method_decl->return_type_desc);
            }
            return type_descriptor_create_primitive(method_decl->return_type);
        }

        case AST_EXPR_BINARY: {
            TypeDescriptor* left = cg_infer_expr_type_desc_simple(cg, expr->as.binary.left);
            TypeDescriptor* right = cg_infer_expr_type_desc_simple(cg, expr->as.binary.right);
            if (!left || !right) {
                type_descriptor_free(left);
                type_descriptor_free(right);
                return NULL;
            }

            if (expr->as.binary.op == '+' || expr->as.binary.op == '-' ||
                expr->as.binary.op == '*' || expr->as.binary.op == '/' ||
                expr->as.binary.op == '%') {

                // Special case for string concatenation
                if (expr->as.binary.op == '+' && 
                    left->base_type == TYPE_STRING && right->base_type == TYPE_STRING) {
                    type_descriptor_free(right);
                    return left;
                }

                if (!type_descriptor_equals(left, right)) {
                    cg_type_error_at(cg, "Type mismatch in binary operation - no implicit coercion allowed", 
                                    left, right, &expr->loc);
                    type_descriptor_free(left);
                    type_descriptor_free(right);
                    return NULL;
                }

                if (left->base_type != TYPE_INT && left->base_type != TYPE_DOUBLE) {
                    cg_error(cg, "Arithmetic operations require numeric types", NULL);
                    type_descriptor_free(left);
                    type_descriptor_free(right);
                    return NULL;
                }

                type_descriptor_free(right);
                return left;
            }

            // Comparison operators: parser encodes <= as 'l', >= as 'g', == as '=', != as '!'
            if (expr->as.binary.op == '<' || expr->as.binary.op == '>' ||
                expr->as.binary.op == 'l' || expr->as.binary.op == 'g' ||
                expr->as.binary.op == '=' || expr->as.binary.op == '!') {
                type_descriptor_free(left);
                type_descriptor_free(right);
                return type_descriptor_create_primitive(TYPE_BOOL);
            }

            if (expr->as.binary.op == '&' || expr->as.binary.op == '|') {
                if (left->base_type != TYPE_BOOL || right->base_type != TYPE_BOOL) {
                    cg_error(cg, "Logical operations require Bool operands", NULL);
                    type_descriptor_free(left);
                    type_descriptor_free(right);
                    return NULL;
                }
                type_descriptor_free(left);
                type_descriptor_free(right);
                return type_descriptor_create_primitive(TYPE_BOOL);
            }

            type_descriptor_free(left);
            type_descriptor_free(right);
            return NULL;
        }

        case AST_EXPR_UNARY: {
            TypeDescriptor* operand = cg_infer_expr_type_desc_simple(cg, expr->as.unary.operand);
            if (!operand) return NULL;

            if (expr->as.unary.op == '-') {
                if (operand->base_type != TYPE_INT && operand->base_type != TYPE_DOUBLE) {
                    cg_error(cg, "Numeric negation requires numeric type", NULL);
                    type_descriptor_free(operand);
                    return NULL;
                }
                return operand;
            }

            if (expr->as.unary.op == '!') {
                if (operand->base_type != TYPE_BOOL) {
                    cg_error(cg, "Logical negation requires Bool type", NULL);
                    type_descriptor_free(operand);
                    return NULL;
                }
                type_descriptor_free(operand);
                return type_descriptor_create_primitive(TYPE_BOOL);
            }

            type_descriptor_free(operand);
            return NULL;
        }

        case AST_EXPR_ARRAY_LITERAL: {
            if (expr->as.array_literal.element_count == 0) {
                TypeDescriptor* elem_type = type_descriptor_create_primitive(TYPE_NIL);
                if (!elem_type) return NULL;
                TypeDescriptor* out = type_descriptor_create_array(elem_type);
                if (!out) {
                    type_descriptor_free(elem_type);
                    return NULL;
                }
                return out;
            }

            TypeDescriptor* elem_type = cg_infer_expr_type_desc_simple(cg, expr->as.array_literal.elements[0]);
            if (!elem_type) return NULL;

            for (int i = 1; i < expr->as.array_literal.element_count; i++) {
                TypeDescriptor* t = cg_infer_expr_type_desc_simple(cg, expr->as.array_literal.elements[i]);
                if (!t) {
                    type_descriptor_free(elem_type);
                    return NULL;
                }
                if (!type_descriptor_equals(elem_type, t)) {
                    TypeDescriptor* common = cg_common_superclass_desc(cg, elem_type, t);
                    if (common) {
                        type_descriptor_free(elem_type);
                        elem_type = common;
                        type_descriptor_free(t);
                        continue;
                    }

                    cg_type_error_at(cg, "Array literal elements must have same type", elem_type, t, &expr->loc);
                    type_descriptor_free(t);
                    type_descriptor_free(elem_type);
                    return NULL;
                }
                type_descriptor_free(t);
            }

            TypeDescriptor* out = type_descriptor_create_array(elem_type);
            if (!out) {
                type_descriptor_free(elem_type);
                return NULL;
            }
            return out;
        }

        case AST_EXPR_DICT: {
            if (expr->as.dict.entry_count == 0) {
                TypeDescriptor* key_type = type_descriptor_create_primitive(TYPE_NIL);
                TypeDescriptor* value_type = type_descriptor_create_primitive(TYPE_NIL);
                if (!key_type || !value_type) {
                    type_descriptor_free(key_type);
                    type_descriptor_free(value_type);
                    return NULL;
                }
                TypeDescriptor* out = type_descriptor_create_dict(key_type, value_type);
                if (!out) {
                    type_descriptor_free(key_type);
                    type_descriptor_free(value_type);
                    return NULL;
                }
                return out;
            }

            TypeDescriptor* key_type = cg_infer_expr_type_desc_simple(cg, expr->as.dict.entries[0].key);
            TypeDescriptor* value_type = cg_infer_expr_type_desc_simple(cg, expr->as.dict.entries[0].value);
            if (!key_type || !value_type) {
                type_descriptor_free(key_type);
                type_descriptor_free(value_type);
                return NULL;
            }

            for (int i = 1; i < expr->as.dict.entry_count; i++) {
                TypeDescriptor* kt = cg_infer_expr_type_desc_simple(cg, expr->as.dict.entries[i].key);
                TypeDescriptor* vt = cg_infer_expr_type_desc_simple(cg, expr->as.dict.entries[i].value);
                if (!kt || !vt) {
                    type_descriptor_free(kt);
                    type_descriptor_free(vt);
                    type_descriptor_free(key_type);
                    type_descriptor_free(value_type);
                    return NULL;
                }
                if (!type_descriptor_equals(key_type, kt) || !type_descriptor_equals(value_type, vt)) {
                    cg_error(cg, "Dict literal entries must have consistent key/value types", NULL);
                    type_descriptor_free(kt);
                    type_descriptor_free(vt);
                    type_descriptor_free(key_type);
                    type_descriptor_free(value_type);
                    return NULL;
                }
                type_descriptor_free(kt);
                type_descriptor_free(vt);
            }

            TypeDescriptor* out = type_descriptor_create_dict(key_type, value_type);
            if (!out) {
                type_descriptor_free(key_type);
                type_descriptor_free(value_type);
                return NULL;
            }
            return out;
        }

        case AST_EXPR_INDEX: {
            TypeDescriptor* target = cg_infer_expr_type_desc_simple(cg, expr->as.index.target);
            if (!target) return NULL;

            TypeDescriptor* index_type = cg_infer_expr_type_desc_simple(cg, expr->as.index.index);
            if (!index_type) {
                type_descriptor_free(target);
                return NULL;
            }

            TypeDescriptor* out = NULL;
            if (target->base_type == TYPE_ARRAY) {
                TypeDescriptor* expected_index = type_descriptor_create_primitive(TYPE_INT);
                if (!expected_index) {
                    type_descriptor_free(index_type);
                    type_descriptor_free(target);
                    return NULL;
                }
                if (!type_descriptor_compatible(index_type, expected_index)) {
                    cg_type_error_at(cg, "Array index must be Int", expected_index, index_type, &expr->loc);
                    type_descriptor_free(expected_index);
                    type_descriptor_free(index_type);
                    type_descriptor_free(target);
                    return NULL;
                }
                type_descriptor_free(expected_index);
                out = type_descriptor_clone(target->params.array.element_type);
            } else if (target->base_type == TYPE_DICT) {
                if (!target->params.dict.key_type) {
                    cg_error_at(cg, "Dictionary type is missing key type", NULL, &expr->loc);
                    type_descriptor_free(index_type);
                    type_descriptor_free(target);
                    return NULL;
                }
                if (!type_descriptor_compatible(index_type, target->params.dict.key_type)) {
                    cg_type_error_at(cg, "Dictionary index type mismatch", target->params.dict.key_type, index_type, &expr->loc);
                    type_descriptor_free(index_type);
                    type_descriptor_free(target);
                    return NULL;
                }
                out = type_descriptor_clone(target->params.dict.value_type);
            } else if (target->base_type == TYPE_STRING) {
                TypeDescriptor* expected_index = type_descriptor_create_primitive(TYPE_INT);
                if (!expected_index) {
                    type_descriptor_free(index_type);
                    type_descriptor_free(target);
                    return NULL;
                }
                if (!type_descriptor_compatible(index_type, expected_index)) {
                    cg_type_error_at(cg, "String index must be Int", expected_index, index_type, &expr->loc);
                    type_descriptor_free(expected_index);
                    type_descriptor_free(index_type);
                    type_descriptor_free(target);
                    return NULL;
                }
                type_descriptor_free(expected_index);
                out = type_descriptor_create_primitive(TYPE_STRING);
            } else {
                cg_error_at(cg, "Indexing is only valid on Array, Dict, or String", NULL, &expr->loc);
                type_descriptor_free(index_type);
                type_descriptor_free(target);
                return NULL;
            }

            type_descriptor_free(index_type);
            type_descriptor_free(target);
            return out;
        }
        case AST_EXPR_MEMBER: {
            // For member access, we need to infer the type based on the target and member
            TypeDescriptor* target_type = cg_infer_expr_type_desc_simple(cg, expr->as.member.target);
            if (!target_type) return NULL;
            
            // Special case for built-in properties
            if (expr->as.member.member && strcmp(expr->as.member.member, "length") == 0) {
                type_descriptor_free(target_type);
                return type_descriptor_create_primitive(TYPE_INT);
            }

            // Dictionary member access (e.g. dict.key) returns the dictionary's value type.
            // This enables ergonomics like student.metadata.major where metadata is [String: String].
            if (target_type->base_type == TYPE_DICT) {
                TypeDescriptor* out = NULL;
                if (target_type->params.dict.value_type) {
                    out = type_descriptor_clone(target_type->params.dict.value_type);
                }
                type_descriptor_free(target_type);
                if (!out) {
                    cg_error_at(cg, "Dictionary member access requires a known value type", NULL, &expr->loc);
                    return NULL;
                }
                return out;
            }
            
            // For struct field access, we need to look up the field type
            if (target_type->base_type == TYPE_STRUCT) {
                const char* struct_name = target_type->params.struct_type.name;
                const char* field_name = expr->as.member.member;

                if (struct_name && field_name) {
                    CgStruct* sdef = cg_find_struct(cg, struct_name);
                    if (sdef) {
                        for (int i = 0; i < sdef->field_count; i++) {
                            if (sdef->field_names[i] && strcmp(sdef->field_names[i], field_name) == 0) {
                                TypeDescriptor* field_type = type_descriptor_clone(sdef->field_types[i]);
                                type_descriptor_free(target_type);
                                return field_type ? field_type : type_descriptor_create_primitive(TYPE_NIL);
                            }
                        }
                    }
                }

                type_descriptor_free(target_type);
                return type_descriptor_create_primitive(TYPE_NIL);
            }
            
            // For class field access, look up the field type
            if (target_type->base_type == TYPE_CLASS) {
                const char* class_name = target_type->params.class_type.name;
                const char* field_name = expr->as.member.member;
                
                if (class_name && field_name) {
                    // Find the class definition
                    CgClass* class_def = cg_find_class(cg, class_name);
                    if (class_def) {
                        // Look for the field in this class and its parents
                        CgClass* current = class_def;
                        while (current) {
                            for (int i = 0; i < current->field_count; i++) {
                                if (current->field_names[i] && strcmp(current->field_names[i], field_name) == 0) {
                                    // Found the field, return its type
                                    TypeDescriptor* field_type = type_descriptor_clone(current->field_types[i]);
                                    type_descriptor_free(target_type);
                                    return field_type ? field_type : type_descriptor_create_primitive(TYPE_NIL);
                                }
                            }
                            // Check parent class
                            current = current->parent_name ? cg_find_class(cg, current->parent_name) : NULL;
                        }
                    }
                }
                
                // Field not found, return nil
                type_descriptor_free(target_type);
                cg_error_at(cg, "Unknown class field", field_name, &expr->loc);
                return NULL;
            }
            
            // For other types, return nil for now
            type_descriptor_free(target_type);
            cg_error_at(cg, "Member access is only valid on Dict, Struct, or Class", NULL, &expr->loc);
            return NULL;
        }

        case AST_EXPR_STRUCT_LITERAL: {
            // For struct literals, we need to look up the struct type
            // For now, create a basic struct type descriptor
            // In a full implementation, we'd validate against declared struct types
            char** field_names = NULL;
            TypeDescriptor** field_types = NULL;
            
            if (expr->as.struct_literal.field_count > 0) {
                field_names = malloc(expr->as.struct_literal.field_count * sizeof(char*));
                field_types = malloc(expr->as.struct_literal.field_count * sizeof(TypeDescriptor*));
                
                if (!field_names || !field_types) {
                    free(field_names);
                    free(field_types);
                    return NULL;
                }
                
                for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                    field_names[i] = strdup(expr->as.struct_literal.field_names[i]);
                    field_types[i] = cg_infer_expr_type_desc_simple(cg, expr->as.struct_literal.field_values[i]);
                    if (!field_types[i]) {
                        for (int j = 0; j < i; j++) {
                            free(field_names[j]);
                            type_descriptor_free(field_types[j]);
                        }
                        free(field_names);
                        free(field_types);
                        return NULL;
                    }
                }
            }
            
            TypeDescriptor* struct_desc = type_descriptor_create_struct(
                expr->as.struct_literal.struct_name,
                expr->as.struct_literal.field_count,
                field_names,
                field_types
            );
            
            // Clean up temporary arrays
            if (field_names) {
                for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                    free(field_names[i]);
                    type_descriptor_free(field_types[i]);
                }
                free(field_names);
                free(field_types);
            }
            
            return struct_desc;
        }

        case AST_EXPR_CLASS_LITERAL: {
            // For class literals, we need to look up the class type
            // For now, create a basic class type descriptor
            // In a full implementation, we'd validate against declared class types
            char** field_names = NULL;
            TypeDescriptor** field_types = NULL;
            
            if (expr->as.class_literal.field_count > 0) {
                field_names = malloc(expr->as.class_literal.field_count * sizeof(char*));
                field_types = malloc(expr->as.class_literal.field_count * sizeof(TypeDescriptor*));
                
                if (!field_names || !field_types) {
                    free(field_names);
                    free(field_types);
                    return NULL;
                }
                
                for (int i = 0; i < expr->as.class_literal.field_count; i++) {
                    field_names[i] = strdup(expr->as.class_literal.field_names[i]);
                    field_types[i] = cg_infer_expr_type_desc_simple(cg, expr->as.class_literal.field_values[i]);
                    if (!field_types[i]) {
                        for (int j = 0; j < i; j++) {
                            free(field_names[j]);
                            type_descriptor_free(field_types[j]);
                        }
                        free(field_names);
                        free(field_types);
                        return NULL;
                    }
                }
            }
            
            TypeDescriptor* class_desc = type_descriptor_create_class(
                expr->as.class_literal.class_name,
                NULL,  // No parent for literals
                expr->as.class_literal.field_count,
                field_names,
                field_types
            );
            
            // Clean up temporary arrays
            if (field_names) {
                for (int i = 0; i < expr->as.class_literal.field_count; i++) {
                    free(field_names[i]);
                    type_descriptor_free(field_types[i]);
                }
                free(field_names);
                free(field_types);
            }
            
            return class_desc;
        }

        default:
            return NULL;
    }
}

int cg_check_condition_type_simple(Cg* cg, ASTExpr* condition) {
    VarType cond_type = cg_infer_expr_type_simple(cg, condition);
    
    if (cond_type != TYPE_BOOL) {
        cg_error(cg, "Condition must be Bool type", NULL);
        return 0;
    }
    
    return 1;
}

int cg_analyze_expr(Cg* cg, ASTExpr* expr) {
    if (!cg || !expr) return 1;
    
    // First, recursively analyze sub-expressions
    switch (expr->kind) {
        case AST_EXPR_VAR: {
            CgVar* var = cg_find_var_in_scope(cg, expr->as.var_name);
            if (!var) {
                cg_error(cg, "Undefined variable", expr->as.var_name);
                return 0;
            }
            break;
        }
        case AST_EXPR_CALL: {
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                if (expr->as.call.arg_count < 1 || expr->as.call.arg_count > 3) {
                    cg_error_at(cg, "Built-in function 'range' expects 1 to 3 arguments", expr->as.call.name, &expr->loc);
                    return 0;
                }
            } else {
                const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
                if (builtin) {
                    if (builtin->param_count != expr->as.call.arg_count) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Built-in function expects %d argument(s), got %d", 
                                builtin->param_count, expr->as.call.arg_count);
                        cg_error_at(cg, msg, expr->as.call.name, &expr->loc);
                        return 0;
                    }
                } else {
                    CgFunction* func = cg_find_function(cg, expr->as.call.name);
                    CgClass* class = cg_find_class(cg, expr->as.call.name);
                    
                    if (func) {
                        if (expr->as.call.arg_count < func->required_param_count || expr->as.call.arg_count > func->param_count) {
                            char msg[256];
                            snprintf(msg, sizeof(msg), "Function expects %d to %d argument(s), got %d", 
                                    func->required_param_count, func->param_count, expr->as.call.arg_count);
                            cg_error_at(cg, msg, expr->as.call.name, &expr->loc);
                            return 0;
                        }
                    } else if (class) {
                        if (!class->constructor) {
                            cg_error_at(cg, "Class has no constructor", expr->as.call.name, &expr->loc);
                            return 0;
                        }
                        int required = class->constructor->param_count;
                        if (class->constructor->param_defaults) {
                            required = 0;
                            for (int i = 0; i < class->constructor->param_count; i++) {
                                if (class->constructor->param_defaults[i] == NULL) {
                                    required++;
                                } else {
                                    break;
                                }
                            }
                        }
                        if (expr->as.call.arg_count < required || expr->as.call.arg_count > class->constructor->param_count) {
                            char msg[256];
                            snprintf(msg, sizeof(msg), "Constructor expects %d to %d argument(s), got %d", 
                                    required, class->constructor->param_count, expr->as.call.arg_count);
                            cg_error_at(cg, msg, expr->as.call.name, &expr->loc);
                            return 0;
                        }
                    } else {
                        cg_error_at(cg, "Undefined function or class", expr->as.call.name, &expr->loc);
                        return 0;
                    }
                }
            }
            
            // Analyze arguments
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                if (!cg_analyze_expr(cg, expr->as.call.args[i])) return 0;
            }
            break;
        }
        case AST_EXPR_BINARY:
            if (!cg_analyze_expr(cg, expr->as.binary.left)) return 0;
            if (!cg_analyze_expr(cg, expr->as.binary.right)) return 0;
            break;
        case AST_EXPR_UNARY:
            if (!cg_analyze_expr(cg, expr->as.unary.operand)) return 0;
            break;
        case AST_EXPR_INDEX:
            if (!cg_analyze_expr(cg, expr->as.index.target)) return 0;
            if (!cg_analyze_expr(cg, expr->as.index.index)) return 0;
            break;
        case AST_EXPR_MEMBER:
            if (!cg_analyze_expr(cg, expr->as.member.target)) return 0;
            break;
        case AST_EXPR_METHOD_CALL:
            if (!cg_analyze_expr(cg, expr->as.method_call.target)) return 0;
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                if (!cg_analyze_expr(cg, expr->as.method_call.args[i])) return 0;
            }
            break;
        case AST_EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.element_count; i++) {
                if (!cg_analyze_expr(cg, expr->as.array_literal.elements[i])) return 0;
            }
            break;
        case AST_EXPR_DICT:
            for (int i = 0; i < expr->as.dict.entry_count; i++) {
                if (!cg_analyze_expr(cg, expr->as.dict.entries[i].key)) return 0;
                if (!cg_analyze_expr(cg, expr->as.dict.entries[i].value)) return 0;
            }
            break;
        case AST_EXPR_STRUCT_LITERAL:
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                if (!cg_analyze_expr(cg, expr->as.struct_literal.field_values[i])) return 0;
            }
            break;
        case AST_EXPR_CLASS_LITERAL:
            for (int i = 0; i < expr->as.class_literal.field_count; i++) {
                if (!cg_analyze_expr(cg, expr->as.class_literal.field_values[i])) return 0;
            }
            break;
        default:
            // Literals are valid
            break;
    }
    
    // Now perform type inference and checking
    TypeDescriptor* expr_type_desc = cg_infer_expr_type_desc_simple(cg, expr);
    if (!expr_type_desc) {
        if (expr->kind == AST_EXPR_NIL) {
            expr_type_desc = type_descriptor_create_primitive(TYPE_NIL);
            if (!expr_type_desc) return 0;
        } else {
            // If we already have an error, don't add another one
            if (!cg->had_error) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Could not infer expression type (expr kind %d)", (int)expr->kind);
                cg_error(cg, msg, NULL);
            }
            return 0;
        }
    }
    
    // Store inferred type in AST node
    type_descriptor_free(expr->tag.type_desc);
    expr->tag.is_known = 1;
    expr->tag.type = expr_type_desc->base_type;
    expr->tag.type_desc = expr_type_desc;
    
    return 1;
}

int cg_analyze_stmt(Cg* cg, ASTStmt* stmt) {
    if (!cg || !stmt) return 1;
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL: {
            // First analyze the initialization expression if present
            if (stmt->as.var_decl.init) {
                if (!cg_analyze_expr(cg, stmt->as.var_decl.init)) {
                    return 0;
                }
            }
            
            // Handle type inference when no explicit type is provided
            TypeDescriptor* actual_type_desc = stmt->as.var_decl.type_desc;
            if (!actual_type_desc) {
                cg_error_at(cg, "Type must be explicitly defined", stmt->as.var_decl.var_name, &stmt->loc);
                return 0;
            }
            
            // Check if the declared type is actually a class (parsed as struct) BEFORE declaring the variable
            if (actual_type_desc && actual_type_desc->base_type == TYPE_STRUCT) {
                CgClass* class = cg_find_class(cg, actual_type_desc->params.struct_type.name);
                if (class) {
                    // Replace struct type descriptor with class type descriptor
                    type_descriptor_free(actual_type_desc);
                    actual_type_desc = type_descriptor_create_class(class->name, class->parent_name, 
                                                                  class->field_count, class->field_names, class->field_types);
                    stmt->as.var_decl.type_desc = actual_type_desc;
                }
            }
            
            if (!cg_declare_var(cg, stmt->as.var_decl.var_name, stmt->as.var_decl.type_desc, stmt->as.var_decl.is_const)) {
                return 0;
            }
            
            // Type compatibility check (only if we have both types)
            if (stmt->as.var_decl.init && stmt->as.var_decl.init->tag.is_known && 
                stmt->as.var_decl.init->tag.type_desc && stmt->as.var_decl.type_desc) {
                const TypeDescriptor* init_type = stmt->as.var_decl.init->tag.type_desc;
                const TypeDescriptor* declared_type = stmt->as.var_decl.type_desc;

                if (!type_descriptor_compatible(init_type, declared_type)) {
                    cg_type_error_at(cg, "Type mismatch in variable initialization", declared_type, init_type, &stmt->loc);
                    return 0;
                }
            }
            break;
        }
        case AST_STMT_VAR_ASSIGN: {
            CgVar* var = cg_find_var_in_scope(cg, stmt->as.var_assign.var_name);
            if (!var) {
                cg_error_at(cg, "Undefined variable", stmt->as.var_assign.var_name, &stmt->loc);
                return 0;
            }
            if (var->is_const) {
                cg_error_at(cg, "Cannot assign to const variable", stmt->as.var_assign.var_name, &stmt->loc);
                return 0;
            }
            if (!cg_analyze_expr(cg, stmt->as.var_assign.value)) return 0;
            if (stmt->as.var_assign.value->tag.is_known && stmt->as.var_assign.value->tag.type_desc && var->type_desc) {
                const TypeDescriptor* value_type = stmt->as.var_assign.value->tag.type_desc;
                const TypeDescriptor* var_type = var->type_desc;

                if (!type_descriptor_compatible(value_type, var_type)) {
                    cg_type_error(cg, "Type mismatch in assignment", var_type, value_type);
                    return 0;
                }
            }
            break;
        }
        case AST_STMT_INDEX_ASSIGN:
            if (!cg_analyze_expr(cg, stmt->as.index_assign.target)) return 0;
            if (!cg_analyze_expr(cg, stmt->as.index_assign.index)) return 0;
            if (!cg_analyze_expr(cg, stmt->as.index_assign.value)) return 0;
            break;
        case AST_STMT_MEMBER_ASSIGN:
            if (!cg_analyze_expr(cg, stmt->as.member_assign.target)) return 0;
            if (!cg_analyze_expr(cg, stmt->as.member_assign.value)) return 0;
            break;
        case AST_STMT_PRINT:
            if (!cg_analyze_expr(cg, stmt->as.print.expr)) return 0;
            break;
        case AST_STMT_EXPR:
            if (!cg_analyze_expr(cg, stmt->as.expr.expr)) return 0;
            break;
        case AST_STMT_IF:
            if (!cg_analyze_expr(cg, stmt->as.if_stmt.condition)) return 0;
            if (!cg_check_condition_type_desc_simple(cg, stmt->as.if_stmt.condition)) return 0;
            
            cg_enter_scope(cg);
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
            }
            cg_leave_scope(cg);
            if (stmt->as.if_stmt.else_branch) {
                cg_enter_scope(cg);
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
                cg_leave_scope(cg);
            }
            break;
        case AST_STMT_WHILE:
            if (!cg_analyze_expr(cg, stmt->as.while_stmt.condition)) return 0;
            if (!cg_check_condition_type_desc_simple(cg, stmt->as.while_stmt.condition)) return 0;
            
            cg_enter_scope(cg);
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
            }
            cg_leave_scope(cg);
            break;
        case AST_STMT_FOR:
            if (!cg_analyze_expr(cg, stmt->as.for_stmt.range_expr)) return 0;
            cg_enter_scope(cg);
            {
                TypeDescriptor* int_type = type_descriptor_create_primitive(TYPE_INT);
                if (!int_type) return 0;
                int ok = cg_declare_var(cg, stmt->as.for_stmt.var_name, int_type, 0);
                type_descriptor_free(int_type);
                if (!ok) return 0;
            }
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
            }
            cg_leave_scope(cg);
            break;
        case AST_STMT_FOR_IN:
            if (!cg_analyze_expr(cg, stmt->as.for_in_stmt.iterable)) return 0;
            cg_enter_scope(cg);
            {
                // Infer the element type from the iterable
                TypeDescriptor* iterable_type = cg_infer_expr_type_desc_simple(cg, stmt->as.for_in_stmt.iterable);
                TypeDescriptor* element_type = NULL;
                
                if (iterable_type) {
                    if (iterable_type->base_type == TYPE_ARRAY && iterable_type->params.array.element_type) {
                        element_type = type_descriptor_clone(iterable_type->params.array.element_type);
                    } else if (iterable_type->base_type == TYPE_DICT && iterable_type->params.dict.key_type) {
                        // For dictionary iteration, we iterate over keys
                        element_type = type_descriptor_clone(iterable_type->params.dict.key_type);
                    }
                    type_descriptor_free(iterable_type);
                }
                
                if (!element_type) {
                    cg_error_at(cg, "Cannot infer element type for 'for-in' (expected Array or Dict with known element/key type)", NULL, &stmt->loc);
                    return 0;
                }
                
                int ok = cg_declare_var(cg, stmt->as.for_in_stmt.var_name, element_type, 0);
                type_descriptor_free(element_type);
                if (!ok) return 0;
            }
            if (stmt->as.for_in_stmt.body) {
                for (ASTStmt* s = stmt->as.for_in_stmt.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
            }
            cg_leave_scope(cg);
            break;
        case AST_STMT_FUNC_DECL: {
            // Function declarations are handled in the first pass of semantic analysis
            // This should not be reached in the second pass
            cg_error(cg, "Internal error: function declaration in second pass", stmt->as.func_decl.name);
            return 0;
        }
        case AST_STMT_STRUCT_DECL: {
            if (!cg_declare_struct_from_ast(cg, &stmt->as.struct_decl, &stmt->loc)) {
                return 0;
            }
            return 1;
        }
        case AST_STMT_CLASS_DECL: {
            // Register the class declaration
            if (!cg_declare_class_from_ast(cg, &stmt->as.class_decl, &stmt->loc)) {
                return 0;
            }
            return 1;
        }
        case AST_STMT_RETURN:
            if (stmt->as.ret.expr && !cg_analyze_expr(cg, stmt->as.ret.expr)) return 0;
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        default:
            break;
    }
    
    return 1;
}

int cg_semantic_analyze(Cg* cg, ASTStmtList* program) {
    if (!cg || !program) return 0;
    
    cg->had_error = 0;
    cg->scope_depth = 0;
    cg->global_scope = cg_scope_new(NULL);
    
    // Pass uno, declare functions
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_STMT_FUNC_DECL) {
            if (!cg_declare_function_from_ast(cg, &stmt->as.func_decl, &stmt->loc)) {
                return 0;
            }
        }
    }
    
    // Pass dos, analyze all statements (including function bodies)
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_STMT_FUNC_DECL) {
            // Skip the declaration part, just analyze the body
            cg_enter_scope(cg);
            // params as vars
            for (int i = 0; i < stmt->as.func_decl.param_count; i++) {
                if (!cg_declare_var(cg, stmt->as.func_decl.param_names[i], stmt->as.func_decl.param_type_descs[i], 0)) return 0;
            }
            
            // Analyze function body and check return statements
            if (stmt->as.func_decl.body) {
                for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                    if (!cg_check_returns_in_stmt(cg, &stmt->as.func_decl, s)) return 0;
                }
            }
            
            // Check if function needs a return statement
            if (stmt->as.func_decl.return_type != TYPE_NIL &&
                !cg_stmt_list_guarantees_return(stmt->as.func_decl.body)) {
                // Allow implicit return nil for optional return types
                if (stmt->as.func_decl.return_type_desc && 
                    stmt->as.func_decl.return_type_desc->base_type == TYPE_OPTIONAL) {
                    // Implicit return nil is allowed for optional types
                } else {
                    cg_error(cg, "Function must return a value", stmt->as.func_decl.name);
                    return 0;
                }
            }
            
            cg_leave_scope(cg);
        } else {
            if (!cg_analyze_stmt(cg, stmt)) {
                return 0;
            }
        }
    }
    
    return !cg->had_error;
}
// Function-aware type inference that can search both local and global scopes
TypeDescriptor* cg_infer_expr_type_desc_with_function(Cg* cg, CgFunction* cg_fn, ASTExpr* expr) {
    if (!expr) return NULL;

    switch (expr->kind) {
        case AST_EXPR_INT:
            return type_descriptor_create_primitive(TYPE_INT);
        case AST_EXPR_DOUBLE:
            return type_descriptor_create_primitive(TYPE_DOUBLE);
        case AST_EXPR_BOOL:
            return type_descriptor_create_primitive(TYPE_BOOL);
        case AST_EXPR_STRING:
        case AST_EXPR_STRING_LITERAL:
            return type_descriptor_create_primitive(TYPE_STRING);
        case AST_EXPR_NIL:
            return type_descriptor_create_primitive(TYPE_NIL);

        case AST_EXPR_VAR: {
            // First try to find in function local scope if available
            CgVar* var = NULL;
            if (cg_fn && cg_fn->scope) {
                var = cg_scope_find_var(cg_fn->scope, expr->as.var_name);
                if (var && var->type_desc) {
                    return type_descriptor_clone(var->type_desc);
                }
            }
            
            // If not found in local scope OR found but missing type descriptor, check function parameters directly
            if (cg_fn && cg_fn->param_names && cg_fn->param_type_descs) {
                for (int i = 0; i < cg_fn->param_count; i++) {
                    if (cg_fn->param_names[i] && strcmp(cg_fn->param_names[i], expr->as.var_name) == 0) {
                        if (cg_fn->param_type_descs[i]) {
                            return type_descriptor_clone(cg_fn->param_type_descs[i]);
                        }
                        break;
                    }
                }
            }
            
            // If not found in function context, try global scope
            if (!var) {
                var = cg_find_var(cg, expr->as.var_name);
            }
            
            if (!var || !var->type_desc) return NULL;
            return type_descriptor_clone(var->type_desc);
        }

        case AST_EXPR_CALL: {
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                TypeDescriptor* elem = type_descriptor_create_primitive(TYPE_INT);
                if (!elem) return NULL;
                TypeDescriptor* out = type_descriptor_create_array(elem);
                if (!out) {
                    type_descriptor_free(elem);
                    return NULL;
                }
                return out;
            }

            const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
            if (builtin) {
                return type_descriptor_create_primitive(builtin->return_type);
            }

            // Check for user-defined functions
            CgFunction* func = cg_find_function(cg, expr->as.call.name);
            if (func) {
                if (func->return_type_desc) {
                    return type_descriptor_clone(func->return_type_desc);
                } else {
                    return type_descriptor_create_primitive(func->return_type);
                }
            }

            return NULL;
        }

        case AST_EXPR_METHOD_CALL: {
            TypeDescriptor* target_type = cg_infer_expr_type_desc_with_function(cg, cg_fn, expr->as.method_call.target);
            if (!target_type) return NULL;

            if (target_type->base_type == TYPE_ARRAY) {
                if (strcmp(expr->as.method_call.name, "append") == 0) {
                    type_descriptor_free(target_type);
                    return type_descriptor_create_primitive(TYPE_NIL);
                }
            } else if (target_type->base_type == TYPE_DICT) {
                if (strcmp(expr->as.method_call.name, "set") == 0) {
                    type_descriptor_free(target_type);
                    return type_descriptor_create_primitive(TYPE_NIL);
                }
            } else if (target_type->base_type == TYPE_CLASS) {
                // For class method calls, we'd need more sophisticated type checking
                type_descriptor_free(target_type);
                return type_descriptor_create_primitive(TYPE_NIL);
            }

            type_descriptor_free(target_type);
            return NULL;
        }

        case AST_EXPR_BINARY: {
            TypeDescriptor* left = cg_infer_expr_type_desc_with_function(cg, cg_fn, expr->as.binary.left);
            TypeDescriptor* right = cg_infer_expr_type_desc_with_function(cg, cg_fn, expr->as.binary.right);
            if (!left || !right) {
                type_descriptor_free(left);
                type_descriptor_free(right);
                return NULL;
            }

            TypeDescriptor* result = NULL;
            switch (expr->as.binary.op) {
                case '+':
                case '-':
                case '*':
                case '/':
                case '%':
                    if (left->base_type == TYPE_INT && right->base_type == TYPE_INT) {
                        result = type_descriptor_create_primitive(TYPE_INT);
                    } else if ((left->base_type == TYPE_DOUBLE || left->base_type == TYPE_INT) &&
                               (right->base_type == TYPE_DOUBLE || right->base_type == TYPE_INT)) {
                        result = type_descriptor_create_primitive(TYPE_DOUBLE);
                    } else if (expr->as.binary.op == '+' && 
                               left->base_type == TYPE_STRING && right->base_type == TYPE_STRING) {
                        result = type_descriptor_create_primitive(TYPE_STRING);
                    }
                    break;
                case '<':
                case '>':
                case 'L': // <=
                case 'G': // >=
                case 'E': // ==
                case 'N': // !=
                    result = type_descriptor_create_primitive(TYPE_BOOL);
                    break;
                case '&': // &&
                case '|': // ||
                    if (left->base_type == TYPE_BOOL && right->base_type == TYPE_BOOL) {
                        result = type_descriptor_create_primitive(TYPE_BOOL);
                    }
                    break;
            }

            type_descriptor_free(left);
            type_descriptor_free(right);
            return result;
        }

        case AST_EXPR_UNARY: {
            TypeDescriptor* operand = cg_infer_expr_type_desc_with_function(cg, cg_fn, expr->as.unary.operand);
            if (!operand) return NULL;

            TypeDescriptor* result = NULL;
            switch (expr->as.unary.op) {
                case '-':
                    if (operand->base_type == TYPE_INT || operand->base_type == TYPE_DOUBLE) {
                        result = type_descriptor_clone(operand);
                    }
                    break;
                case '!':
                    if (operand->base_type == TYPE_BOOL) {
                        result = type_descriptor_create_primitive(TYPE_BOOL);
                    }
                    break;
            }

            type_descriptor_free(operand);
            return result;
        }

        // For other expression types, fall back to the original function
        default:
            return cg_infer_expr_type_desc_simple(cg, expr);
    }
}