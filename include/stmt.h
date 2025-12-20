#ifndef STMT_H
#define STMT_H

#include "../include/expr.h"

// Forward declarations
typedef struct Stmt Stmt;
typedef struct StmtList StmtList;

// Statement types
typedef enum {
    STMT_VAR_DECL,
    STMT_VAR_ASSIGN,
    STMT_PRINT,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_BLOCK,
    STMT_FUNC_DECL,
    STMT_RETURN
} StmtType;

// Statement structures
typedef struct {
    char* var_name;
    VarType type;
    char* type_str;
    char* expr_str;  // Expression string to evaluate
    int is_const;
} StmtVarDecl;

typedef struct {
    char* var_name;
    char* expr_str;
} StmtVarAssign;

typedef struct {
    char* expr_str;
} StmtPrint;

typedef struct {
    char* expr_str;
} StmtExpr;

typedef struct {
    char* name;
    int param_count;
    char** param_names;
    VarType* param_types;
    VarType return_type;
    StmtList* body;
} StmtFuncDecl;

typedef struct {
    char* expr_str;
} StmtReturn;

typedef struct {
    char* condition_str;
    StmtList* then_branch;
    StmtList* elif_branches;  // List of elif conditions and blocks
    StmtList* else_branch;
} StmtIf;

typedef struct {
    char* condition_str;
    StmtList* body;
} StmtWhile;

typedef struct {
    char* var_name;
    char* range_expr_str;  // e.g., "range(10)"
    StmtList* body;
} StmtFor;

typedef struct Stmt {
    StmtType type;
    union {
        StmtVarDecl var_decl;
        StmtVarAssign var_assign;
        StmtPrint print;
        StmtExpr expr;
        StmtIf if_stmt;
        StmtWhile while_stmt;
        StmtFor for_stmt;
        StmtFuncDecl func_decl;
        StmtReturn ret;
    } data;
    struct Stmt* next;  // For linked list
} Stmt;

typedef struct StmtList {
    Stmt* head;
    Stmt* tail;
} StmtList;

typedef enum {
    EXEC_SIGNAL_NONE,
    EXEC_SIGNAL_BREAK,
    EXEC_SIGNAL_CONTINUE,
    EXEC_SIGNAL_RETURN
} ExecSignal;

// Function declarations
StmtList* parse_statements(const char* code);
ExecSignal execute_statements(StmtList* stmts, ExprResult* out_return);
void free_stmt_list(StmtList* stmts);

#endif