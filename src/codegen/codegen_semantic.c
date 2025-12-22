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
    new_func->return_type = func_decl->return_type;
    new_func->return_type_desc = type_descriptor_clone(func_decl->return_type_desc);
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
            CgVar* var = cg_find_var(cg, expr->as.var_name);
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
                
                if (left_type != right_type) {
                    cg_error(cg, "Type mismatch in binary operation", NULL);
                    return TYPE_NIL;
                }

                if (expr->as.binary.op == '+' && left_type == TYPE_STRING) {
                    return TYPE_STRING;
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
            
            if (target_type == TYPE_ARRAY) {
                return TYPE_INT; // TODO: Get actual element type
            } else if (target_type == TYPE_DICT) {
                return TYPE_INT; // TODO: Get actual value type
            } else if (target_type == TYPE_STRING) {
                return TYPE_STRING;
            }
            
            cg_error_at(cg, "Cannot index this type (only arrays, dictionaries, and strings can be indexed)", NULL, &expr->loc);
            return TYPE_NIL;
        }
        
        case AST_EXPR_MEMBER: {
            // Member access (e.g., .length) returns Int for now
            return TYPE_INT;
        }
        
        case AST_EXPR_STRUCT_LITERAL: {
            return TYPE_STRUCT;
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
            CgVar* var = cg_find_var(cg, expr->as.var_name);
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

            return type_descriptor_create_primitive(TYPE_NIL);
        }

        case AST_EXPR_METHOD_CALL:
            return type_descriptor_create_primitive(TYPE_NIL);

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

                // Phase 3: Enforce no implicit numeric coercion
                if (!type_descriptor_equals(left, right)) {
                    cg_type_error(cg, "Type mismatch in binary operation - no implicit coercion allowed", 
                                left, right);
                    type_descriptor_free(left);
                    type_descriptor_free(right);
                    return NULL;
                }

                if (expr->as.binary.op == '+' && left->base_type == TYPE_STRING) {
                    type_descriptor_free(right);
                    return left;
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
                TypeDescriptor* elem = type_descriptor_create_primitive(TYPE_NIL);
                if (!elem) return NULL;
                TypeDescriptor* out = type_descriptor_create_array(elem);
                if (!out) {
                    type_descriptor_free(elem);
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
                    cg_type_error(cg, "Array literal elements must have same type", elem_type, t);
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
                TypeDescriptor* key = type_descriptor_create_primitive(TYPE_NIL);
                TypeDescriptor* value = type_descriptor_create_primitive(TYPE_NIL);
                if (!key || !value) {
                    type_descriptor_free(key);
                    type_descriptor_free(value);
                    return NULL;
                }
                TypeDescriptor* out = type_descriptor_create_dict(key, value);
                if (!out) {
                    type_descriptor_free(key);
                    type_descriptor_free(value);
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

            TypeDescriptor* out = NULL;
            if (target->base_type == TYPE_ARRAY) {
                out = type_descriptor_clone(target->params.array.element_type);
            } else if (target->base_type == TYPE_DICT) {
                out = type_descriptor_clone(target->params.dict.value_type);
            } else if (target->base_type == TYPE_STRING) {
                out = type_descriptor_create_primitive(TYPE_STRING);
            } else {
                out = type_descriptor_create_primitive(TYPE_NIL);
            }

            type_descriptor_free(target);
            return out;
        }
        case AST_EXPR_MEMBER:
            return type_descriptor_create_primitive(TYPE_INT);

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
            CgVar* var = cg_find_var(cg, expr->as.var_name);
            if (!var) {
                cg_error(cg, "Undefined variable", expr->as.var_name);
                return 0;
            }
            break;
        }
        case AST_EXPR_CALL: {
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                if (expr->as.call.arg_count != 1) {
                    cg_error_at(cg, "Built-in function 'range' expects 1 argument", expr->as.call.name, &expr->loc);
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
                    if (!func) {
                        cg_error_at(cg, "Undefined function", expr->as.call.name, &expr->loc);
                        return 0;
                    }
                    if (func->param_count != expr->as.call.arg_count) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Function expects %d argument(s), got %d", 
                                func->param_count, expr->as.call.arg_count);
                        cg_error_at(cg, msg, expr->as.call.name, &expr->loc);
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
            if (!cg_declare_var(cg, stmt->as.var_decl.var_name, stmt->as.var_decl.type_desc, stmt->as.var_decl.is_const)) {
                return 0;
            }
            if (stmt->as.var_decl.init) {
                if (!cg_analyze_expr(cg, stmt->as.var_decl.init)) {
                    return 0;
                }
                
                if (stmt->as.var_decl.init->tag.is_known && stmt->as.var_decl.init->tag.type_desc) {
                    const TypeDescriptor* init_type = stmt->as.var_decl.init->tag.type_desc;
                    const TypeDescriptor* declared_type = stmt->as.var_decl.type_desc;

                    if (!type_descriptor_compatible(init_type, declared_type)) {
                        cg_type_error_at(cg, "Type mismatch in variable initialization", declared_type, init_type, &stmt->loc);
                        return 0;
                    }
                }
            }
            break;
        }
        case AST_STMT_VAR_ASSIGN: {
            CgVar* var = cg_find_var(cg, stmt->as.var_assign.var_name);
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
                    element_type = type_descriptor_create_primitive(TYPE_NIL);
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
            // Struct declarations are registered during semantic analysis
            // For now, we just validate that the struct is well-formed
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
            int has_return = 0;
            if (stmt->as.func_decl.body) {
                for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                    if (s->kind == AST_STMT_RETURN) {
                        has_return = 1;
                        
                        // Check return type compatibility
                        if (s->as.ret.expr) {
                            if (s->as.ret.expr->tag.is_known && s->as.ret.expr->tag.type_desc) {
                                const TypeDescriptor* return_type = s->as.ret.expr->tag.type_desc;
                                const TypeDescriptor* expected_type = stmt->as.func_decl.return_type_desc;
                                
                                if (expected_type && !type_descriptor_compatible(return_type, expected_type)) {
                                    cg_type_error(cg, "Return type mismatch", expected_type, return_type);
                                    return 0;
                                }
                            }
                        } else {
                            // Return with no expression - check if function expects void/nil
                            if (stmt->as.func_decl.return_type != TYPE_NIL) {
                                cg_error(cg, "Missing return value", NULL);
                                return 0;
                            }
                        }
                    }
                }
            }
            
            // Check if function needs a return statement
            if (stmt->as.func_decl.return_type != TYPE_NIL && !has_return) {
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
