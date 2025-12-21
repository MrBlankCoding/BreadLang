#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/analysis/type_stability.h"

#define MAX_TRACKED_VARS 256

typedef struct {
    char* name;
    int depth;
    int is_const;
    int is_local;
    int mutation_count;
    int usage_count;
    int declared_in_loop;
} TrackedVar;

typedef struct {
    TrackedVar vars[MAX_TRACKED_VARS];
    int var_count;
    int current_depth;
} VarTracker;

static TypeStabilityCtx* g_stability_ctx = NULL;
static VarTracker g_var_tracker = {0};

static TrackedVar* find_tracked_var(const char* name) {
    if (!name) return NULL;
    
    for (int i = g_var_tracker.var_count - 1; i >= 0; i--) {
        TrackedVar* var = &g_var_tracker.vars[i];
        if (var->name && strcmp(var->name, name) == 0) {
            return var;
        }
    }
    return NULL;
}

static void track_var_declaration(const char* name, int is_const, int is_local) {
    if (!name || g_var_tracker.var_count >= MAX_TRACKED_VARS) return;
    
    TrackedVar* var = &g_var_tracker.vars[g_var_tracker.var_count++];
    var->name = strdup(name);
    var->depth = g_var_tracker.current_depth;
    var->is_const = is_const;
    var->is_local = is_local;
    var->mutation_count = 0;
    var->usage_count = 0;
    var->declared_in_loop = g_stability_ctx ? g_stability_ctx->in_loop > 0 : 0;
}

static void track_var_mutation(const char* name) {
    TrackedVar* var = find_tracked_var(name);
    if (var) {
        var->mutation_count++;
    }
}

static void track_var_usage(const char* name) {
    TrackedVar* var = find_tracked_var(name);
    if (var) {
        var->usage_count++;
    }
}

static void enter_var_scope() {
    g_var_tracker.current_depth++;
}

static void leave_var_scope() {
    // Remove variables from current scope
    int new_count = g_var_tracker.var_count;
    while (new_count > 0) {
        TrackedVar* var = &g_var_tracker.vars[new_count - 1];
        if (var->depth < g_var_tracker.current_depth) break;
        free(var->name);
        memset(var, 0, sizeof(*var));
        new_count--;
    }
    g_var_tracker.var_count = new_count;
    
    if (g_var_tracker.current_depth > 0) {
        g_var_tracker.current_depth--;
    }
}

static void cleanup_var_tracker() {
    for (int i = 0; i < g_var_tracker.var_count; i++) {
        free(g_var_tracker.vars[i].name);
    }
    memset(&g_var_tracker, 0, sizeof(g_var_tracker));
}

static TypeStabilityInfo* alloc_stability_info() {
    if (!g_stability_ctx) return NULL;
    
    if (g_stability_ctx->expr_count >= g_stability_ctx->expr_capacity) {
        int new_cap = g_stability_ctx->expr_capacity * 2;
        if (new_cap < 64) new_cap = 64;
        
        TypeStabilityInfo* new_info = realloc(g_stability_ctx->expr_info, 
                                            new_cap * sizeof(TypeStabilityInfo));
        if (!new_info) return NULL;
        
        g_stability_ctx->expr_info = new_info;
        g_stability_ctx->expr_capacity = new_cap;
    }
    
    TypeStabilityInfo* info = &g_stability_ctx->expr_info[g_stability_ctx->expr_count++];
    memset(info, 0, sizeof(TypeStabilityInfo));
    info->type = TYPE_NIL;
    info->stability = STABILITY_UNKNOWN;
    return info;
}

static void analyze_expr_stability(ASTExpr* expr) {
    if (!expr) return;
    
    TypeStabilityInfo* info = alloc_stability_info();
    if (!info) return;
    expr->stability_info = info;
    
    switch (expr->kind) {
        case AST_EXPR_NIL:
        case AST_EXPR_BOOL:
        case AST_EXPR_INT:
        case AST_EXPR_DOUBLE:
        case AST_EXPR_STRING:
            info->stability = STABILITY_STABLE;
            info->is_constant = 1;
            if (expr->tag.is_known) {
                info->type = expr->tag.type;
            }
            break;
            
        case AST_EXPR_VAR:
            // Variable stability depends on mutability and scope
            info->stability = STABILITY_UNSTABLE; // Conservative default
            info->usage_count = 1;
            
            // Look up variable in symbol table to check:
            // - Is it const? -> STABILITY_STABLE
            // - Is it local and never mutated? -> STABILITY_STABLE
            // - Is it in a loop? -> STABILITY_UNSTABLE
            track_var_usage(expr->as.var_name);
            
            TrackedVar* var = find_tracked_var(expr->as.var_name);
            if (var) {
                if (var->is_const) {
                    info->stability = STABILITY_STABLE;
                    info->is_constant = 1;
                } else if (var->mutation_count == 0 && var->is_local) {
                    info->stability = STABILITY_STABLE;
                } else if (var->mutation_count <= 1 && !var->declared_in_loop && 
                          g_stability_ctx->in_loop == 0) {
                    info->stability = STABILITY_CONDITIONAL;
                } else {
                    info->stability = STABILITY_UNSTABLE;
                }
                
                info->is_local = var->is_local;
                info->mutation_count = var->mutation_count;
                info->usage_count = var->usage_count;
            }
            
            if (expr->tag.is_known) {
                info->type = expr->tag.type;
                // If type is known from semantic analysis, it's more stable
                if (info->stability == STABILITY_UNSTABLE) {
                    info->stability = STABILITY_CONDITIONAL;
                }
            }
            break;
            
        case AST_EXPR_BINARY:
            analyze_expr_stability(expr->as.binary.left);
            analyze_expr_stability(expr->as.binary.right);
            
            // Binary operation stability depends on operands
            TypeStabilityInfo* left_info = expr->as.binary.left->stability_info;
            TypeStabilityInfo* right_info = expr->as.binary.right->stability_info;
            
            if (left_info && right_info) {
                if (left_info->stability == STABILITY_STABLE && 
                    right_info->stability == STABILITY_STABLE) {
                    info->stability = STABILITY_STABLE;
                    info->is_constant = left_info->is_constant && right_info->is_constant;
                } else if (left_info->stability >= STABILITY_CONDITIONAL && 
                          right_info->stability >= STABILITY_CONDITIONAL) {
                    info->stability = STABILITY_CONDITIONAL;
                } else {
                    info->stability = STABILITY_UNSTABLE;
                }
                
                // Type inference for binary operations
                if (left_info->type == TYPE_INT && right_info->type == TYPE_INT) {
                    info->type = TYPE_INT;
                } else if ((left_info->type == TYPE_FLOAT || left_info->type == TYPE_DOUBLE) &&
                          (right_info->type == TYPE_FLOAT || right_info->type == TYPE_DOUBLE)) {
                    info->type = TYPE_DOUBLE;
                }
            }
            break;
            
        case AST_EXPR_UNARY:
            analyze_expr_stability(expr->as.unary.operand);
            
            TypeStabilityInfo* operand_info = expr->as.unary.operand->stability_info;
            if (operand_info) {
                info->stability = operand_info->stability;
                info->is_constant = operand_info->is_constant;
                info->type = operand_info->type;
            }
            break;
            
        case AST_EXPR_CALL:
            info->stability = STABILITY_UNSTABLE;
            
            // Analyze arguments
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                analyze_expr_stability(expr->as.call.args[i]);
            }
            break;
            
        case AST_EXPR_INDEX:
            analyze_expr_stability(expr->as.index.target);
            analyze_expr_stability(expr->as.index.index);
            
            info->stability = STABILITY_UNSTABLE;
            break;
            
        case AST_EXPR_MEMBER:
            analyze_expr_stability(expr->as.member.target);
            
            TypeStabilityInfo* obj_info = expr->as.member.target->stability_info;
            if (obj_info) {
                info->stability = obj_info->stability;
            } else {
                info->stability = STABILITY_UNSTABLE;
            }
            break;
            
        case AST_EXPR_ARRAY:
            for (int i = 0; i < expr->as.array.item_count; i++) {
                analyze_expr_stability(expr->as.array.items[i]);
            }
            info->stability = STABILITY_UNSTABLE; // Arrays are mutable
            break;
            
        case AST_EXPR_DICT:
            // Analyze dict entries
            for (int i = 0; i < expr->as.dict.entry_count; i++) {
                analyze_expr_stability(expr->as.dict.entries[i].key);
                analyze_expr_stability(expr->as.dict.entries[i].value);
            }
            info->stability = STABILITY_UNSTABLE; // Dicts are mutable
            break;
            
        case AST_EXPR_METHOD_CALL:
            analyze_expr_stability(expr->as.method_call.target);
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                analyze_expr_stability(expr->as.method_call.args[i]);
            }
            info->stability = STABILITY_UNSTABLE;
            break;
        case AST_EXPR_STRING_LITERAL:
            // String literals are stable
            info->stability = STABILITY_STABLE;
            info->is_constant = 1;
            break;
        case AST_EXPR_ARRAY_LITERAL:
            // Analyze array elements
            for (int i = 0; i < expr->as.array_literal.element_count; i++) {
                analyze_expr_stability(expr->as.array_literal.elements[i]);
            }
            info->stability = STABILITY_UNSTABLE; // Arrays are mutable
            break;
    }
}

static void analyze_stmt_stability(ASTStmt* stmt) {
    if (!stmt) return;
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL:
            if (stmt->as.var_decl.init) {
                analyze_expr_stability(stmt->as.var_decl.init);
            }
            // Track variable declaration
            track_var_declaration(stmt->as.var_decl.var_name, 
                                stmt->as.var_decl.is_const,
                                g_stability_ctx->current_function_depth > 0);
            break;
            
        case AST_STMT_VAR_ASSIGN:
            analyze_expr_stability(stmt->as.var_assign.value);
            // Mark variable as mutated in symbol table
            track_var_mutation(stmt->as.var_assign.var_name);
            break;
            
        case AST_STMT_INDEX_ASSIGN:
            analyze_expr_stability(stmt->as.index_assign.target);
            analyze_expr_stability(stmt->as.index_assign.index);
            analyze_expr_stability(stmt->as.index_assign.value);
            // Mark the target as mutated
            if (stmt->as.index_assign.target->kind == AST_EXPR_VAR) {
                track_var_mutation(stmt->as.index_assign.target->as.var_name);
            }
            break;
            
        case AST_STMT_PRINT:
            analyze_expr_stability(stmt->as.print.expr);
            break;
            
        case AST_STMT_IF:
            analyze_expr_stability(stmt->as.if_stmt.condition);
            if (stmt->as.if_stmt.then_branch) {
                enter_var_scope();
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    analyze_stmt_stability(s);
                }
                leave_var_scope();
            }
            if (stmt->as.if_stmt.else_branch) {
                enter_var_scope();
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    analyze_stmt_stability(s);
                }
                leave_var_scope();
            }
            break;
            
        case AST_STMT_WHILE:
            g_stability_ctx->in_loop++;
            analyze_expr_stability(stmt->as.while_stmt.condition);
            if (stmt->as.while_stmt.body) {
                enter_var_scope();
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    analyze_stmt_stability(s);
                }
                leave_var_scope();
            }
            g_stability_ctx->in_loop--;
            break;
            
        case AST_STMT_FOR:
            g_stability_ctx->in_loop++;
            analyze_expr_stability(stmt->as.for_stmt.range_expr);
            if (stmt->as.for_stmt.body) {
                enter_var_scope();
                // Track the loop variable
                track_var_declaration(stmt->as.for_stmt.var_name, 0, 1);
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    analyze_stmt_stability(s);
                }
                leave_var_scope();
            }
            g_stability_ctx->in_loop--;
            break;
            
        case AST_STMT_FOR_IN:
            g_stability_ctx->in_loop++;
            if (stmt->as.for_in_stmt.iterable) {
                analyze_expr_stability(stmt->as.for_in_stmt.iterable);
            }
            if (stmt->as.for_in_stmt.body) {
                enter_var_scope();
                // Track the loop variable
                track_var_declaration(stmt->as.for_in_stmt.var_name, 0, 1);
                for (ASTStmt* s = stmt->as.for_in_stmt.body->head; s; s = s->next) {
                    analyze_stmt_stability(s);
                }
                leave_var_scope();
            }
            g_stability_ctx->in_loop--;
            break;
            
        case AST_STMT_FUNC_DECL:
            g_stability_ctx->current_function_depth++;
            if (stmt->as.func_decl.body) {
                enter_var_scope();
                // Track function parameters
                for (int i = 0; i < stmt->as.func_decl.param_count; i++) {
                    track_var_declaration(stmt->as.func_decl.param_names[i], 1, 1);
                }
                for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next) {
                    analyze_stmt_stability(s);
                }
                leave_var_scope();
            }
            g_stability_ctx->current_function_depth--;
            break;
            
        case AST_STMT_RETURN:
            if (stmt->as.ret.expr) {
                analyze_expr_stability(stmt->as.ret.expr);
            }
            break;
            
        case AST_STMT_EXPR:
            analyze_expr_stability(stmt->as.expr.expr);
            break;
            
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            // No expressions to analyze
            break;
    }
}

int type_stability_analyze(ASTStmtList* program) {
    if (!program) return 0;
    
    // Initialize context
    g_stability_ctx = malloc(sizeof(TypeStabilityCtx));
    if (!g_stability_ctx) return 0;
    
    memset(g_stability_ctx, 0, sizeof(TypeStabilityCtx));
    g_stability_ctx->expr_capacity = 64;
    g_stability_ctx->expr_info = malloc(g_stability_ctx->expr_capacity * sizeof(TypeStabilityInfo));
    
    if (!g_stability_ctx->expr_info) {
        free(g_stability_ctx);
        g_stability_ctx = NULL;
        return 0;
    }
    
    // Initialize variable tracker
    memset(&g_var_tracker, 0, sizeof(g_var_tracker));
    
    // Analyze all statements
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        analyze_stmt_stability(stmt);
    }
    
    // Cleanup
    cleanup_var_tracker();
    
    return 1;
}

TypeStabilityInfo* get_expr_stability_info(ASTExpr* expr) {
    if (!expr) return NULL;
    return (TypeStabilityInfo*)expr->stability_info;
}

int is_type_stable(ASTExpr* expr) {
    TypeStabilityInfo* info = get_expr_stability_info(expr);
    return info && info->stability >= STABILITY_CONDITIONAL;
}

int can_unbox_integer(ASTExpr* expr) {
    TypeStabilityInfo* info = get_expr_stability_info(expr);
    return info && 
           info->type == TYPE_INT && 
           info->stability >= STABILITY_CONDITIONAL;
}