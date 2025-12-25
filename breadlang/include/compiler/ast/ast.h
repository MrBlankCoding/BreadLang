#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdint.h>

#include "compiler/parser/expr.h"
#include "core/var.h"

typedef struct ASTExpr ASTExpr;
typedef struct ASTStmt ASTStmt;
typedef struct ASTStmtList ASTStmtList;

// Source location tracking for error reporting
typedef struct {
    int line;
    int column;
    const char* filename;
} SourceLoc;

typedef struct {
    int is_known;
    VarType type;
    TypeDescriptor* type_desc;
} ASTTypeTag;

typedef enum {
    AST_EXPR_NIL,
    AST_EXPR_BOOL,
    AST_EXPR_INT,
    AST_EXPR_DOUBLE,
    AST_EXPR_STRING,
    AST_EXPR_VAR,
    AST_EXPR_SELF,
    AST_EXPR_SUPER,
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_CALL,
    AST_EXPR_ARRAY,
    AST_EXPR_DICT,
    AST_EXPR_INDEX,
    AST_EXPR_MEMBER,
    AST_EXPR_METHOD_CALL,
    AST_EXPR_STRING_LITERAL,
    AST_EXPR_ARRAY_LITERAL,
    AST_EXPR_STRUCT_LITERAL,
    AST_EXPR_CLASS_LITERAL
} ASTExprKind;

typedef struct {
    ASTExpr* key;
    ASTExpr* value;
} ASTDictEntry;

struct ASTExpr {
    ASTExprKind kind;
    ASTTypeTag tag;
    SourceLoc loc;  // Source location for error reporting
    void* stability_info;  // TypeStabilityInfo*
    void* escape_info;     // EscapeInfo*
    void* opt_hints;       // OptimizationHints*
    union {
        int bool_val;
        int64_t int_val;
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
        struct {
            char* value;
            size_t length;
        } string_literal;
        struct {
            int element_count;
            ASTExpr** elements;
            VarType element_type;
        } array_literal;
        struct {
            char* struct_name;
            int field_count;
            char** field_names;
            ASTExpr** field_values;
        } struct_literal;
        struct {
            char* class_name;
            int field_count;
            char** field_names;
            ASTExpr** field_values;
        } class_literal;
    } as;
};

typedef enum {
    AST_STMT_VAR_DECL,
    AST_STMT_VAR_ASSIGN,
    AST_STMT_INDEX_ASSIGN,
    AST_STMT_MEMBER_ASSIGN,
    AST_STMT_PRINT,
    AST_STMT_EXPR,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_FOR,
    AST_STMT_FOR_IN,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_FUNC_DECL,
    AST_STMT_STRUCT_DECL,
    AST_STMT_CLASS_DECL,
    AST_STMT_RETURN,
    AST_STMT_IMPORT,
    AST_STMT_EXPORT
} ASTStmtKind;

typedef struct {
    char* var_name;
    VarType type;
    TypeDescriptor* type_desc;
    ASTExpr* init;
    int is_const;
} ASTStmtVarDecl;

typedef struct {
    char* var_name;
    ASTExpr* value;
    char op;
} ASTStmtVarAssign;

typedef struct {
    ASTExpr* target;
    ASTExpr* index;
    ASTExpr* value;
    char op;
} ASTStmtIndexAssign;

typedef struct {
    ASTExpr* target;
    char* member;
    ASTExpr* value;
    char op;
} ASTStmtMemberAssign;

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
    char* var_name;
    ASTExpr* iterable;  // Array or range expression
    ASTStmtList* body;
} ASTStmtForIn;

typedef struct {
    char* name;
    int param_count;
    char** param_names;
    TypeDescriptor** param_type_descs;
    ASTExpr** param_defaults;  // Default value expressions (NULL if no default)
    VarType return_type;
    TypeDescriptor* return_type_desc;
    ASTStmtList* body;
    void* opt_info;        // FunctionOptInfo*
} ASTStmtFuncDecl;

typedef struct {
    ASTExpr* expr;
} ASTStmtReturn;

typedef struct {
    char* name;
    int field_count;
    char** field_names;
    TypeDescriptor** field_types;
} ASTStmtStructDecl;

typedef struct {
    char* name;
    char* parent_name;  
    int field_count;
    char** field_names;
    TypeDescriptor** field_types;
    int method_count;
    ASTStmtFuncDecl** methods;
    ASTStmtFuncDecl* constructor;  
} ASTStmtClassDecl;

typedef struct {
    char* module_path;      
    char* alias;            
    int is_selective;       
    int symbol_count;       
    char** symbol_names;    
    char** symbol_aliases;  
} ASTStmtImport;

typedef struct {
    int is_default;         // 1 if this is a default export
    int symbol_count;       
    char** symbol_names;    
    char** symbol_aliases;  
} ASTStmtExport;

struct ASTStmt {
    ASTStmtKind kind;
    SourceLoc loc;  
    void* opt_hints;       // OptimizationHints*
    union {
        ASTStmtVarDecl var_decl;
        ASTStmtVarAssign var_assign;
        ASTStmtIndexAssign index_assign;
        ASTStmtMemberAssign member_assign;
        ASTStmtPrint print;
        ASTStmtExpr expr;
        ASTStmtIf if_stmt;
        ASTStmtWhile while_stmt;
        ASTStmtFor for_stmt;
        ASTStmtForIn for_in_stmt;
        ASTStmtFuncDecl func_decl;
        ASTStmtStructDecl struct_decl;
        ASTStmtClassDecl class_decl;
        ASTStmtReturn ret;
        ASTStmtImport import;
        ASTStmtExport export;
    } as;
    ASTStmt* next;
};

struct ASTStmtList {
    ASTStmt* head;
    ASTStmt* tail;
};

ASTStmtList* ast_parse_program(const char* code);
void ast_free_stmt_list(ASTStmtList* stmts);
void ast_dump_stmt_list(const ASTStmtList* stmts, FILE* out);

#endif
