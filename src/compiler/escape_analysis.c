#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/escape_analysis.h"

static EscapeAnalysisCtx* g_escape_ctx = NULL;

static EscapeInfo* alloc_escape_info() {
    if (!g_escape_ctx) return NULL;
    
    if (g_escape_ctx->alloc_count >= g_escape_ctx->alloc_capacity) {
        int new_cap = g_escape_ctx->alloc_capacity * 2;
        if (new_cap < 64) new_cap = 64;
        
        EscapeInfo* new_info = realloc(g_escape_ctx->alloc_info, 
                                     new_cap * sizeof(EscapeInfo));
        if (!new_info) return NULL;
        
        g_escape_ctx->alloc_info = new_info;
        g_escape_ctx->alloc_capacity = new_cap;
    }
    
    EscapeInfo* info = &g_escape_ctx->alloc_info[g_escape_ctx->alloc_count++];
    memset(info, 0, sizeof(EscapeInfo));
    info->escape_kind = ESCAPE_UNKNOWN;
    info->can_stack_allocate = 1; // Happy or sad :) 
    info->lifetime_end = -1;
    return info;
}

static void mark_escape(EscapeInfo* info, EscapeKind kind) {
    if (!info) return;
    
    if (kind > info->escape_kind) {
        info->escape_kind = kind;
    }
    
    switch (kind) {
        case ESCAPE_NONE:
            info->can_stack_allocate = 1;
            break;
        case ESCAPE_RETURN:
        case ESCAPE_PARAMETER:
            info->can_stack_allocate = 0; // Must heap allocate
            break;
        case ESCAPE_GLOBAL:
        case ESCAPE_HEAP:
            info->can_stack_allocate = 0;
            break;
        default:
            break;
    }
}

static void analyze_expr_escape(ASTExpr* expr, int is_assignment_target) {
    if (!expr) return;
    
    EscapeInfo* info = alloc_escape_info();
    if (!info) return;
    
    expr->escape_info = info;
    info->ref_count = 1;
    
    switch (expr->kind) {
        case AST_EXPR_NIL:
        case AST_EXPR_BOOL:
        case AST_EXPR_INT:
        case AST_EXPR_DOUBLE:
        case AST_EXPR_STRING:
            mark_escape(info, ESCAPE_NONE);
            info->lifetime_end = g_escape_ctx->current_stmt_index;
            break;
            
        case AST_EXPR_VAR:
            if (is_assignment_target) {
                mark_escape(info, ESCAPE_NONE);
            } else {
                mark_escape(info, ESCAPE_NONE);
            }
            break;
            
        case AST_EXPR_BINARY:
            analyze_expr_escape(expr->as.binary.left, 0);
            analyze_expr_escape(expr->as.binary.right, 0);
            mark_escape(info, ESCAPE_NONE);
            info->lifetime_end = g_escape_ctx->current_stmt_index;
            break;
            
        case AST_EXPR_UNARY:
            analyze_expr_escape(expr->as.unary.operand, 0);
            mark_escape(info, ESCAPE_NONE);
            info->lifetime_end = g_escape_ctx->current_stmt_index;
            break;
            
        case AST_EXPR_CALL:
            mark_escape(info, ESCAPE_RETURN);
        
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                analyze_expr_escape(expr->as.call.args[i], 0);
                EscapeInfo* arg_info = (EscapeInfo*)expr->as.call.args[i]->escape_info;
                if (arg_info) {
                    mark_escape(arg_info, ESCAPE_PARAMETER);
                }
            }
            break;
            
        case AST_EXPR_INDEX:
            analyze_expr_escape(expr->as.index.target, 0);
            analyze_expr_escape(expr->as.index.index, 0);
            
            EscapeInfo* obj_info = (EscapeInfo*)expr->as.index.target->escape_info;
            if (obj_info && obj_info->escape_kind >= ESCAPE_HEAP) {
                mark_escape(info, ESCAPE_HEAP);
            } else {
                mark_escape(info, ESCAPE_NONE);
            }
            break;
            
        case AST_EXPR_MEMBER:
            analyze_expr_escape(expr->as.member.target, 0);
            
            EscapeInfo* member_obj_info = (EscapeInfo*)expr->as.member.target->escape_info;
            if (member_obj_info && member_obj_info->escape_kind >= ESCAPE_HEAP) {
                mark_escape(info, ESCAPE_HEAP);
            } else {
                mark_escape(info, ESCAPE_NONE);
            }
            break;
            
        case AST_EXPR_ARRAY:
            mark_escape(info, ESCAPE_HEAP);
            
            for (int i = 0; i < expr->as.array.item_count; i++) {
                analyze_expr_escape(expr->as.array.items[i], 0);
                EscapeInfo* item_info = (EscapeInfo*)expr->as.array.items[i]->escape_info;
                if (item_info) {
                    mark_escape(item_info, ESCAPE_HEAP);
                }
            }
            break;
            
        case AST_EXPR_DICT:
            mark_escape(info, ESCAPE_HEAP);
            
            for (int i = 0; i < expr->as.dict.entry_count; i++) {
                analyze_expr_escape(expr->as.dict.entries[i].key, 0);
                analyze_expr_escape(expr->as.dict.entries[i].value, 0);
                
                EscapeInfo* key_info = (EscapeInfo*)expr->as.dict.entries[i].key->escape_info;
                EscapeInfo* val_info = (EscapeInfo*)expr->as.dict.entries[i].value->escape_info;
                
                if (key_info) mark_escape(key_info, ESCAPE_HEAP);
                if (val_info) mark_escape(val_info, ESCAPE_HEAP);
            }
            break;
            
        case AST_EXPR_METHOD_CALL:
            analyze_expr_escape(expr->as.method_call.target, 0);
            mark_escape(info, ESCAPE_RETURN);
            
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                analyze_expr_escape(expr->as.method_call.args[i], 0);
                EscapeInfo* arg_info = (EscapeInfo*)expr->as.method_call.args[i]->escape_info;
                if (arg_info) {
                    mark_escape(arg_info, ESCAPE_PARAMETER);
                }
            }
            break;
    }
}

static void analyze_stmt_escape(ASTStmt* stmt) {
    if (!stmt) return;
    
    g_escape_ctx->current_stmt_index++;
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL:
            if (stmt->as.var_decl.init) {
                analyze_expr_escape(stmt->as.var_decl.init, 0);
                
                EscapeInfo* init_info = (EscapeInfo*)stmt->as.var_decl.init->escape_info;
                if (init_info) {
                    if (init_info->escape_kind == ESCAPE_NONE) {
                        init_info->lifetime_end = -1; // Unknown, depends on variable usage
                    }
                }
            }
            break;
            
        case AST_STMT_VAR_ASSIGN:
            analyze_expr_escape(stmt->as.var_assign.value, 0);
            
            // Assignment - check if variable is global or local
            // Look up variable in symbol table to determine scope
            // For now, assume local assignment (conservative approach)
            EscapeInfo* assign_info = (EscapeInfo*)stmt->as.var_assign.value->escape_info;
            if (assign_info) {
                // Local assignment doesn't cause escape by itself
                // But we mark it as potentially escaping if the variable might be accessed later
                if (g_escape_ctx->function_depth > 0) {
                    mark_escape(assign_info, ESCAPE_NONE);
                } else {
                    // Global scope assignment - more conservative
                    mark_escape(assign_info, ESCAPE_GLOBAL);
                }
            }
            break;
            
        case AST_STMT_PRINT:
            analyze_expr_escape(stmt->as.print.expr, 0);
            
            // Print doesn't cause escape, but value is used
            EscapeInfo* print_info = (EscapeInfo*)stmt->as.print.expr->escape_info;
            if (print_info) {
                print_info->ref_count++;
            }
            break;
            
        case AST_STMT_IF:
            analyze_expr_escape(stmt->as.if_stmt.condition, 0);
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    analyze_stmt_escape(s);
                }
            }
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    analyze_stmt_escape(s);
                }
            }
            break;
            
        case AST_STMT_WHILE:
            analyze_expr_escape(stmt->as.while_stmt.condition, 0);
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    analyze_stmt_escape(s);
                }
            }
            break;
            
        case AST_STMT_FOR:
            analyze_expr_escape(stmt->as.for_stmt.range_expr, 0);
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    analyze_stmt_escape(s);
                }
            }
            break;
            
        case AST_STMT_FUNC_DECL:
            g_escape_ctx->function_depth++;
            if (stmt->as.func_decl.body) {
                for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next) {
                    analyze_stmt_escape(s);
                }
            }
            g_escape_ctx->function_depth--;
            break;
            
        case AST_STMT_RETURN:
            if (stmt->as.ret.expr) {
                analyze_expr_escape(stmt->as.ret.expr, 0);
                
                // Return value escapes the function
                EscapeInfo* ret_info = (EscapeInfo*)stmt->as.ret.expr->escape_info;
                if (ret_info) {
                    mark_escape(ret_info, ESCAPE_RETURN);
                }
            }
            break;
            
        case AST_STMT_EXPR:
            analyze_expr_escape(stmt->as.expr.expr, 0);
            break;
            
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            // No expressions to analyze
            break;
    }
}

int escape_analysis_run(ASTStmtList* program) {
    if (!program) return 0;
    
    // Initialize context
    g_escape_ctx = malloc(sizeof(EscapeAnalysisCtx));
    if (!g_escape_ctx) return 0;
    
    memset(g_escape_ctx, 0, sizeof(EscapeAnalysisCtx));
    g_escape_ctx->alloc_capacity = 64;
    g_escape_ctx->alloc_info = malloc(g_escape_ctx->alloc_capacity * sizeof(EscapeInfo));
    
    if (!g_escape_ctx->alloc_info) {
        free(g_escape_ctx);
        g_escape_ctx = NULL;
        return 0;
    }
    
    // Analyze all statements
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        analyze_stmt_escape(stmt);
    }
    
    return 1;
}

EscapeInfo* get_escape_info(ASTExpr* expr) {
    if (!expr) return NULL;
    return (EscapeInfo*)expr->escape_info;
}

int can_stack_allocate(ASTExpr* expr) {
    EscapeInfo* info = get_escape_info(expr);
    return info && info->can_stack_allocate;
}

int get_value_lifetime(ASTExpr* expr) {
    EscapeInfo* info = get_escape_info(expr);
    return info ? info->lifetime_end : -1;
}