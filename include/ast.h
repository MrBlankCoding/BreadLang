#ifndef AST_H
#define AST_H

#include <stdio.h>

#include "../include/expr.h"
#include "../include/var.h"

typedef struct ASTExpr ASTExpr;
typedef struct ASTStmt ASTStmt;
typedef struct ASTStmtList ASTStmtList;

typedef struct {
    int is_known;
    VarType type;
} ASTTypeTag;

typedef enum {
    AST_EXPR_NIL,
    AST_EXPR_BOOL,
    AST_EXPR_INT,
    AST_EXPR_DOUBLE,
    AST_EXPR_STRING,
    AST_EXPR_VAR,
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_CALL,
    AST_EXPR_ARRAY,
    AST_EXPR_DICT,
    AST_EXPR_INDEX,
    AST_EXPR_MEMBER,
    AST_EXPR_METHOD_CALL
} ASTExprKind;

typedef struct {
    ASTExpr* key;
    ASTExpr* value;
} ASTDictEntry;

struct ASTExpr {
    ASTExprKind kind;
    ASTTypeTag tag;
    union {
        int bool_val;
        int int_val;
        double double_val;
        char* string_val;
        char* var_name;
        struct {
            char op;
            ASTExpr* left;
            ASTExpr* right;
        } binary;
        struct {
            char op;
            ASTExpr* operand;
        } unary;
        struct {
            char* name;
            int arg_count;
            ASTExpr** args;
        } call;
        struct {
            int item_count;
            ASTExpr** items;
        } array;
        struct {
            int entry_count;
            ASTDictEntry* entries;
        } dict;
        struct {
            ASTExpr* target;
            ASTExpr* index;
        } index;
        struct {
            ASTExpr* target;
            char* member;
            int is_optional_chain;
        } member;
        struct {
            ASTExpr* target;
            char* name;
            int arg_count;
            ASTExpr** args;
            int is_optional_chain;
        } method_call;
    } as;
};

typedef enum {
    AST_STMT_VAR_DECL,
    AST_STMT_VAR_ASSIGN,
    AST_STMT_PRINT,
    AST_STMT_EXPR,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_FOR,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_FUNC_DECL,
    AST_STMT_RETURN
} ASTStmtKind;

typedef struct {
    char* var_name;
    VarType type;
    char* type_str;
    ASTExpr* init;
    int is_const;
} ASTStmtVarDecl;

typedef struct {
    char* var_name;
    ASTExpr* value;
} ASTStmtVarAssign;

typedef struct {
    ASTExpr* expr;
} ASTStmtPrint;

typedef struct {
    ASTExpr* expr;
} ASTStmtExpr;

typedef struct {
    ASTExpr* condition;
    ASTStmtList* then_branch;
    ASTStmtList* else_branch;
} ASTStmtIf;

typedef struct {
    ASTExpr* condition;
    ASTStmtList* body;
} ASTStmtWhile;

typedef struct {
    char* var_name;
    ASTExpr* range_expr;
    ASTStmtList* body;
} ASTStmtFor;

typedef struct {
    char* name;
    int param_count;
    char** param_names;
    VarType* param_types;
    VarType return_type;
    ASTStmtList* body;
} ASTStmtFuncDecl;

typedef struct {
    ASTExpr* expr;
} ASTStmtReturn;

struct ASTStmt {
    ASTStmtKind kind;
    union {
        ASTStmtVarDecl var_decl;
        ASTStmtVarAssign var_assign;
        ASTStmtPrint print;
        ASTStmtExpr expr;
        ASTStmtIf if_stmt;
        ASTStmtWhile while_stmt;
        ASTStmtFor for_stmt;
        ASTStmtFuncDecl func_decl;
        ASTStmtReturn ret;
    } as;
    ASTStmt* next;
};

struct ASTStmtList {
    ASTStmt* head;
    ASTStmt* tail;
};

typedef enum {
    AST_EXEC_SIGNAL_NONE,
    AST_EXEC_SIGNAL_BREAK,
    AST_EXEC_SIGNAL_CONTINUE,
    AST_EXEC_SIGNAL_RETURN
} ASTExecSignal;

void bread_set_trace(int enabled);
int bread_get_trace(void);

ASTStmtList* ast_parse_program(const char* code);
void ast_free_stmt_list(ASTStmtList* stmts);

void ast_dump_stmt_list(const ASTStmtList* stmts, FILE* out);

void ast_runtime_init(void);
void ast_runtime_cleanup(void);

ASTExecSignal ast_execute_stmt_list(ASTStmtList* stmts, ExprResult* out_return);

#endif
