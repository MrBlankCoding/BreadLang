#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/optimization/optimization.h"
#include <llvm-c/Core.h>

// this file doesnt exsist

static OptimizationCtx* g_opt_ctx = NULL;

static int estimate_instruction_count(ASTStmt* stmt) {
    if (!stmt) return 0;
    
    int count = 1; 
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL:
            count += 2;
            if (stmt->as.var_decl.init) {
                count += 3;
            }
            break;
            
        case AST_STMT_VAR_ASSIGN:
            count += 2;
            break;
            
        case AST_STMT_PRINT:
            count += 5;
            break;
            
        case AST_STMT_IF:
            count += 2;
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    count += estimate_instruction_count(s);
                }
            }
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    count += estimate_instruction_count(s);
                }
            }
            break;
            
        case AST_STMT_WHILE:
            count += 3;
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    count += estimate_instruction_count(s) * 2; // Assume 2 iterations
                }
            }
            break;
            
        case AST_STMT_FOR:
            count += 4;
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    count += estimate_instruction_count(s) * 2;
                }
            }
            break;
            
        case AST_STMT_RETURN:
            count += 1;
            break;
            
        case AST_STMT_EXPR:
            count += 2;
            break;
            
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            count += 1;
            break;
            
        default:
            count += 1;
            break;
    }
    
    return count;
}

static int count_expr_function_calls(ASTExpr* expr) {
    if (!expr) return 0;
    
    int calls = 0;
    
    switch (expr->kind) {
        case AST_EXPR_CALL:
            calls += 1;
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                calls += count_expr_function_calls(expr->as.call.args[i]);
            }
            break;
            
        case AST_EXPR_METHOD_CALL:
            calls += 1;
            calls += count_expr_function_calls(expr->as.method_call.target);
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                calls += count_expr_function_calls(expr->as.method_call.args[i]);
            }
            break;
            
        case AST_EXPR_BINARY:
            calls += count_expr_function_calls(expr->as.binary.left);
            calls += count_expr_function_calls(expr->as.binary.right);
            break;
            
        case AST_EXPR_UNARY:
            calls += count_expr_function_calls(expr->as.unary.operand);
            break;
            
        case AST_EXPR_INDEX:
            calls += count_expr_function_calls(expr->as.index.target);
            calls += count_expr_function_calls(expr->as.index.index);
            break;
            
        case AST_EXPR_MEMBER:
            calls += count_expr_function_calls(expr->as.member.target);
            break;
            
        case AST_EXPR_ARRAY:
            for (int i = 0; i < expr->as.array.item_count; i++) {
                calls += count_expr_function_calls(expr->as.array.items[i]);
            }
            break;
            
        case AST_EXPR_DICT:
            for (int i = 0; i < expr->as.dict.entry_count; i++) {
                calls += count_expr_function_calls(expr->as.dict.entries[i].key);
                calls += count_expr_function_calls(expr->as.dict.entries[i].value);
            }
            break;
            
        default:
            break;
    }
    
    return calls;
}

static int count_function_calls(ASTStmt* stmt) {
    if (!stmt) return 0;
    
    int calls = 0;
    
    switch (stmt->kind) {
        case AST_STMT_PRINT:
            calls += 1;
            break;
            
        case AST_STMT_IF:
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    calls += count_function_calls(s);
                }
            }
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    calls += count_function_calls(s);
                }
            }
            break;
            
        case AST_STMT_WHILE:
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    calls += count_function_calls(s);
                }
            }
            break;
            
        case AST_STMT_FOR:
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    calls += count_function_calls(s);
                }
            }
            break;
            
        case AST_STMT_EXPR:
            calls += count_expr_function_calls(stmt->as.expr.expr);
            break;
            
        default:
            break;
    }
    
    return calls;
}

static int check_expr_for_recursive_call(ASTExpr* expr, const char* func_name) {
    if (!expr || !func_name) return 0;
    
    switch (expr->kind) {
        case AST_EXPR_CALL:
            if (strcmp(expr->as.call.name, func_name) == 0) {
                return 1;
            }
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                if (check_expr_for_recursive_call(expr->as.call.args[i], func_name)) {
                    return 1;
                }
            }
            break;
            
        case AST_EXPR_BINARY:
            if (check_expr_for_recursive_call(expr->as.binary.left, func_name)) return 1;
            if (check_expr_for_recursive_call(expr->as.binary.right, func_name)) return 1;
            break;
            
        case AST_EXPR_UNARY:
            if (check_expr_for_recursive_call(expr->as.unary.operand, func_name)) return 1;
            break;
            
        case AST_EXPR_INDEX:
            if (check_expr_for_recursive_call(expr->as.index.target, func_name)) return 1;
            if (check_expr_for_recursive_call(expr->as.index.index, func_name)) return 1;
            break;
            
        case AST_EXPR_MEMBER:
            if (check_expr_for_recursive_call(expr->as.member.target, func_name)) return 1;
            break;
            
        case AST_EXPR_METHOD_CALL:
            if (check_expr_for_recursive_call(expr->as.method_call.target, func_name)) return 1;
            for (int i = 0; i < expr->as.method_call.arg_count; i++) {
                if (check_expr_for_recursive_call(expr->as.method_call.args[i], func_name)) {
                    return 1;
                }
            }
            break;
            
        case AST_EXPR_ARRAY:
            for (int i = 0; i < expr->as.array.item_count; i++) {
                if (check_expr_for_recursive_call(expr->as.array.items[i], func_name)) {
                    return 1;
                }
            }
            break;
            
        case AST_EXPR_DICT:
            for (int i = 0; i < expr->as.dict.entry_count; i++) {
                if (check_expr_for_recursive_call(expr->as.dict.entries[i].key, func_name)) return 1;
                if (check_expr_for_recursive_call(expr->as.dict.entries[i].value, func_name)) return 1;
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

static int check_stmt_for_recursive_call(ASTStmt* stmt, const char* func_name) {
    if (!stmt || !func_name) return 0;
    
    switch (stmt->kind) {
        case AST_STMT_VAR_DECL:
            if (stmt->as.var_decl.init && check_expr_for_recursive_call(stmt->as.var_decl.init, func_name)) {
                return 1;
            }
            break;
            
        case AST_STMT_VAR_ASSIGN:
            if (check_expr_for_recursive_call(stmt->as.var_assign.value, func_name)) {
                return 1;
            }
            break;
            
        case AST_STMT_PRINT:
            if (check_expr_for_recursive_call(stmt->as.print.expr, func_name)) {
                return 1;
            }
            break;
            
        case AST_STMT_EXPR:
            if (check_expr_for_recursive_call(stmt->as.expr.expr, func_name)) {
                return 1;
            }
            break;
            
        case AST_STMT_IF:
            if (check_expr_for_recursive_call(stmt->as.if_stmt.condition, func_name)) {
                return 1;
            }
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    if (check_stmt_for_recursive_call(s, func_name)) return 1;
                }
            }
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    if (check_stmt_for_recursive_call(s, func_name)) return 1;
                }
            }
            break;
            
        case AST_STMT_WHILE:
            if (check_expr_for_recursive_call(stmt->as.while_stmt.condition, func_name)) {
                return 1;
            }
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    if (check_stmt_for_recursive_call(s, func_name)) return 1;
                }
            }
            break;
            
        case AST_STMT_FOR:
            if (check_expr_for_recursive_call(stmt->as.for_stmt.range_expr, func_name)) {
                return 1;
            }
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    if (check_stmt_for_recursive_call(s, func_name)) return 1;
                }
            }
            break;
            
        case AST_STMT_RETURN:
            if (stmt->as.ret.expr && check_expr_for_recursive_call(stmt->as.ret.expr, func_name)) {
                return 1;
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

static int is_recursive_function(ASTStmtFuncDecl* func) {
    // Check if function name appears in any call expression in body
    if (!func || !func->name || !func->body) return 0;
    
    for (ASTStmt* stmt = func->body->head; stmt; stmt = stmt->next) {
        if (check_stmt_for_recursive_call(stmt, func->name)) {
            return 1;
        }
    }
    
    return 0;
}

static void analyze_function_optimization(ASTStmtFuncDecl* func) {
    if (!func || !g_opt_ctx) return;
    
    if (g_opt_ctx->function_count >= 64) return; // Limit for now
    
    FunctionOptInfo* info = &g_opt_ctx->function_info[g_opt_ctx->function_count++];
    memset(info, 0, sizeof(FunctionOptInfo));
    
    func->opt_info = info;
    
    if (func->body) {
        for (ASTStmt* s = func->body->head; s; s = s->next) {
            info->instruction_count += estimate_instruction_count(s);
        }
    }
    
    if (func->body) {
        for (ASTStmt* s = func->body->head; s; s = s->next) {
            info->call_count += count_function_calls(s);
        }
    }
    
    info->is_leaf = (info->call_count == 0);
    info->is_recursive = is_recursive_function(func);
    info->parameter_count = func->param_count;
    
    if (info->is_recursive) {
        info->inline_hint = INLINE_NEVER;
    } else if (info->instruction_count <= 3 && info->is_leaf) {
        info->inline_hint = INLINE_ALWAYS;
    } else if (info->instruction_count <= 10 && info->is_leaf) {
        info->inline_hint = INLINE_HOT;
    } else if (info->instruction_count <= 25) {
        info->inline_hint = INLINE_NORMAL;
    } else if (info->instruction_count <= 50) {
        info->inline_hint = INLINE_COLD;
    } else {
        info->inline_hint = INLINE_NEVER;
    }
    
    if (info->parameter_count > 4 && info->inline_hint > INLINE_COLD) {
        info->inline_hint--;
    }
}

static void analyze_stmt_optimization(ASTStmt* stmt) {
    if (!stmt || !g_opt_ctx) return;
    if (g_opt_ctx->stmt_count >= 1024) return; // Limit for now
    
    OptimizationHints* hints = &g_opt_ctx->stmt_hints[g_opt_ctx->stmt_count++];
    memset(hints, 0, sizeof(OptimizationHints));
    
    stmt->opt_hints = hints;
    
    switch (stmt->kind) {
        case AST_STMT_IF:
            hints->branch_probability = 70;
            hints->can_speculate = 1;
            
            if (stmt->as.if_stmt.then_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.then_branch->head; s; s = s->next) {
                    analyze_stmt_optimization(s);
                }
            }
            if (stmt->as.if_stmt.else_branch) {
                for (ASTStmt* s = stmt->as.if_stmt.else_branch->head; s; s = s->next) {
                    analyze_stmt_optimization(s);
                }
            }
            break;
            
        case AST_STMT_WHILE:
            hints->is_hot_path = 1;
            if (stmt->as.while_stmt.body) {
                for (ASTStmt* s = stmt->as.while_stmt.body->head; s; s = s->next) {
                    analyze_stmt_optimization(s);
                }
            }
            break;
            
        case AST_STMT_FOR:
            hints->is_hot_path = 1;
            
            if (stmt->as.for_stmt.body) {
                for (ASTStmt* s = stmt->as.for_stmt.body->head; s; s = s->next) {
                    analyze_stmt_optimization(s);
                }
            }
            break;
            
        case AST_STMT_FUNC_DECL:
            analyze_function_optimization(&stmt->as.func_decl);
            if (stmt->as.func_decl.body) {
                for (ASTStmt* s = stmt->as.func_decl.body->head; s; s = s->next) {
                    analyze_stmt_optimization(s);
                }
            }
            break;
            
        case AST_STMT_PRINT:
            hints->is_pure = 0;
            break;
            
        case AST_STMT_VAR_DECL:
        case AST_STMT_VAR_ASSIGN:
            hints->is_pure = 1;
            hints->can_speculate = 1;
            break;
            
        case AST_STMT_RETURN:
            hints->is_cold_path = 1;
            break;
            
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            hints->is_cold_path = 1;
            break;
            
        default:
            hints->is_pure = 1;
            break;
    }
}

int optimization_analyze(ASTStmtList* program) {
    if (!program) return 0;
    
    g_opt_ctx = malloc(sizeof(OptimizationCtx));
    if (!g_opt_ctx) return 0;
    
    memset(g_opt_ctx, 0, sizeof(OptimizationCtx));
    
    g_opt_ctx->function_info = malloc(64 * sizeof(FunctionOptInfo));
    g_opt_ctx->stmt_hints = malloc(1024 * sizeof(OptimizationHints));
    g_opt_ctx->expr_hints = malloc(2048 * sizeof(OptimizationHints));
    
    if (!g_opt_ctx->function_info || !g_opt_ctx->stmt_hints || !g_opt_ctx->expr_hints) {
        free(g_opt_ctx->function_info);
        free(g_opt_ctx->stmt_hints);
        free(g_opt_ctx->expr_hints);
        free(g_opt_ctx);
        g_opt_ctx = NULL;
        return 0;
    }
    
    for (ASTStmt* stmt = program->head; stmt; stmt = stmt->next) {
        analyze_stmt_optimization(stmt);
    }
    
    return 1;
}

FunctionOptInfo* get_function_opt_info(ASTStmtFuncDecl* func) {
    if (!func) return NULL;
    return (FunctionOptInfo*)func->opt_info;
}

OptimizationHints* get_stmt_hints(ASTStmt* stmt) {
    if (!stmt) return NULL;
    return (OptimizationHints*)stmt->opt_hints;
}

OptimizationHints* get_expr_hints(ASTExpr* expr) {
    if (!expr) return NULL;
    return (OptimizationHints*)expr->opt_hints;
}

void attach_optimization_metadata(LLVMValueRef value, OptimizationHints* hints) {
    if (!value || !hints) return;
    
    LLVMContextRef ctx = LLVMGetModuleContext(LLVMGetGlobalParent(value));
    
    if (hints->is_hot_path) {
        LLVMValueRef hot_val = LLVMConstInt(LLVMInt32TypeInContext(ctx), 1, 0);
        LLVMValueRef md_vals[] = { hot_val };
        LLVMValueRef md = LLVMMDNodeInContext(ctx, md_vals, 1);
        LLVMSetMetadata(value, LLVMGetMDKindIDInContext(ctx, "hot", 3), md);
    }
    
    if (hints->is_cold_path) {
        LLVMValueRef cold_val = LLVMConstInt(LLVMInt32TypeInContext(ctx), 1, 0);
        LLVMValueRef md_vals[] = { cold_val };
        LLVMValueRef md = LLVMMDNodeInContext(ctx, md_vals, 1);
        LLVMSetMetadata(value, LLVMGetMDKindIDInContext(ctx, "cold", 4), md);
    }
}

void set_function_attributes(LLVMValueRef function, FunctionOptInfo* info) {
    if (!function || !info) return;
    
    (void)function;
    (void)info;
}

void add_branch_weights(LLVMValueRef branch, int true_weight, int false_weight) {
    if (!branch) return;
    
    LLVMContextRef ctx = LLVMGetModuleContext(LLVMGetGlobalParent(branch));
    
    LLVMValueRef weights[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), true_weight, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), false_weight, 0)
    };
    
    LLVMValueRef md = LLVMMDNodeInContext(ctx, weights, 2);
    LLVMSetMetadata(branch, LLVMGetMDKindIDInContext(ctx, "prof", 4), md);
}