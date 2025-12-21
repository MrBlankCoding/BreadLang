#include "codegen_internal.h"

#include "runtime/error.h"
#include "runtime/builtins.h"

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
            free(var);
        } else {
            var_ptr = &var->next;
        }
    }
    
    if (cg->scope_depth > 0) cg->scope_depth--;
}

int cg_declare_var(Cg* cg, const char* name, VarType var_type, int is_const) {
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
    v->type = var_type;
    v->is_const = is_const;
    v->is_initialized = cg->scope_depth;
    v->next = cg->global_scope->vars;
    cg->global_scope->vars = v;
    
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

int cg_declare_function(Cg* cg, const char* name, int param_count) {
    if (!cg || !name) return 0;
    (void)param_count;  // Suppress unused parameter warning
    
    for (CgFunction* f = cg->functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            cg_error(cg, "Function already declared", name);
            return 0;
        }
    }
    
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

int cg_analyze_expr(Cg* cg, ASTExpr* expr) {
    if (!cg || !expr) return 1;
    
    switch (expr->kind) {
        case AST_EXPR_VAR: {
            CgVar* var = cg_find_var(cg, expr->as.var_name);
            if (!var) {
                cg_error(cg, "Undefined variable", expr->as.var_name);
                return 0;
            }
            // Type inference will be done during codegen as well
            break;
        }
        case AST_EXPR_CALL: {
            if (expr->as.call.name && strcmp(expr->as.call.name, "range") == 0) {
                if (expr->as.call.arg_count != 1) {
                    cg_error(cg, "Built-in function argument count mismatch", expr->as.call.name);
                    return 0;
                }
            } else {
                const BuiltinFunction* builtin = bread_builtin_lookup(expr->as.call.name);
                if (builtin) {
                    if (builtin->param_count != expr->as.call.arg_count) {
                        cg_error(cg, "Built-in function argument count mismatch", expr->as.call.name);
                        return 0;
                    }
                } else {
                    CgFunction* func = cg_find_function(cg, expr->as.call.name);
                    if (!func) {
                        cg_error(cg, "Undefined function", expr->as.call.name);
                        return 0;
                    }
                    if (func->param_count != expr->as.call.arg_count) {
                        cg_error(cg, "Function argument count mismatch", expr->as.call.name);
                        return 0;
                    }
                }
            }
            
            // analisis timeeeeeeee -> 8 Ball mirror on the wall who is the fairest of them all?
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
        default:
            // your lit so validdddd. So punnyyyy
            break;
    }
    
    return 1;
}

int cg_analyze_stmt(Cg* cg, ASTStmt* stmt) {
    if (!cg || !stmt) return 1;
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL:
            if (!cg_declare_var(cg, stmt->as.var_decl.var_name, stmt->as.var_decl.type, stmt->as.var_decl.is_const)) {
                return 0;
            }
            if (stmt->as.var_decl.init && !cg_analyze_expr(cg, stmt->as.var_decl.init)) {
                return 0;
            }
            break;
        case AST_STMT_VAR_ASSIGN: {
            CgVar* var = cg_find_var(cg, stmt->as.var_assign.var_name);
            if (!var) {
                cg_error(cg, "Undefined variable", stmt->as.var_assign.var_name);
                return 0;
            }
            if (var->is_const) {
                cg_error(cg, "Cannot assign to const variable", stmt->as.var_assign.var_name);
                return 0;
            }
            if (!cg_analyze_expr(cg, stmt->as.var_assign.value)) return 0;
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
            if (!cg_declare_var(cg, stmt->as.for_stmt.var_name, TYPE_INT, 0)) return 0;
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
            if (!cg_declare_var(cg, stmt->as.for_in_stmt.var_name, TYPE_NIL, 0)) return 0;
            if (stmt->as.for_in_stmt.body) {
                for (ASTStmt* s = stmt->as.for_in_stmt.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
            }
            cg_leave_scope(cg);
            break;
        case AST_STMT_FUNC_DECL:
            if (!cg_declare_function(cg, stmt->as.func_decl.name, stmt->as.func_decl.param_count)) {
                return 0;
            }
            cg_enter_scope(cg);
            // params as vars
            for (int i = 0; i < stmt->as.func_decl.param_count; i++) {
                if (!cg_declare_var(cg, stmt->as.func_decl.param_names[i], TYPE_NIL, 0)) return 0;
            }
            if (stmt->as.func_decl.body) {
                for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next) {
                    if (!cg_analyze_stmt(cg, s)) return 0;
                }
            }
            cg_leave_scope(cg);
            break;
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
    
    // Pass uno, declare
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_STMT_FUNC_DECL) {
            if (!cg_declare_function(cg, stmt->as.func_decl.name, stmt->as.func_decl.param_count)) {
                return 0;
            }
        }
    }
    
    // Pass dos, analyze
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        if (!cg_analyze_stmt(cg, stmt)) {
            return 0;
        }
    }
    
    return !cg->had_error;
}
