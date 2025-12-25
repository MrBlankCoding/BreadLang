#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_memory.h"
#include "compiler/ast/ast_types.h"
#include "compiler/ast/ast_expr_parser.h"
#include "compiler/ast/ast_stmt_parser.h"

#define MAX_TOKEN_LEN 1024

static void skip_whitespace(const char** code);
static char* dup_range(const char* start, const char* end);

ASTStmtList* parse_block(const char** code) {
    ASTStmtList* list = ast_stmt_list_new();
    if (!list) return NULL;

    while (**code && **code != '}') {
        skip_whitespace(code);
        if (**code == '}') break;
        ASTStmt* st = parse_stmt(code);
        if (!st) {
            ast_free_stmt_list(list);
            return NULL;
        }
        ast_stmt_list_add(list, st);
    }

    return list;
}

ASTStmt* parse_stmt(const char** code) {
    skip_whitespace(code);
    if (strncmp(*code, "import ", 7) == 0) {
        *code += 7;
        skip_whitespace(code);
        
        ASTStmt* stmt = ast_stmt_new(AST_STMT_IMPORT);
        if (!stmt) return NULL;
        
        stmt->as.import.module_path = NULL;
        stmt->as.import.alias = NULL;
        stmt->as.import.is_selective = 0;
        stmt->as.import.symbol_count = 0;
        stmt->as.import.symbol_names = NULL;
        stmt->as.import.symbol_aliases = NULL;
        
        if (**code == '{') {
            (*code)++;
            skip_whitespace(code);
            
            stmt->as.import.is_selective = 1;
            
            int symbol_cap = 4;
            stmt->as.import.symbol_names = (char**)malloc(symbol_cap * sizeof(char*));
            stmt->as.import.symbol_aliases = (char**)malloc(symbol_cap * sizeof(char*));
            
            while (**code && **code != '}') {
                skip_whitespace(code);
                if (**code == '}') break;
    
                const char* start = *code;
                while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                if (*code == start) {
                    ast_free_stmt(stmt);
                    return NULL;
                }
                
                char* symbol_name = dup_range(start, *code);
                skip_whitespace(code);
                
                char* symbol_alias = NULL;
                if (strncmp(*code, "as ", 3) == 0) {
                    *code += 3;
                    skip_whitespace(code);
                    
                    const char* alias_start = *code;
                    while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                    if (*code == alias_start) {
                        free(symbol_name);
                        ast_free_stmt(stmt);
                        return NULL;
                    }
                    symbol_alias = dup_range(alias_start, *code);
                }
                
                if (stmt->as.import.symbol_count >= symbol_cap) {
                    symbol_cap *= 2;
                    stmt->as.import.symbol_names = (char**)realloc(stmt->as.import.symbol_names, 
                                                                  symbol_cap * sizeof(char*));
                    stmt->as.import.symbol_aliases = (char**)realloc(stmt->as.import.symbol_aliases, 
                                                                    symbol_cap * sizeof(char*));
                }
                
                stmt->as.import.symbol_names[stmt->as.import.symbol_count] = symbol_name;
                stmt->as.import.symbol_aliases[stmt->as.import.symbol_count] = symbol_alias;
                stmt->as.import.symbol_count++;
                
                skip_whitespace(code);
                if (**code == ',') {
                    (*code)++;
                    skip_whitespace(code);
                }
            }
            
            if (**code != '}') {
                ast_free_stmt(stmt);
                return NULL;
            }
            (*code)++;
            skip_whitespace(code);
            
            if (strncmp(*code, "from ", 5) != 0) {
                ast_free_stmt(stmt);
                return NULL;
            }
            *code += 5;
            skip_whitespace(code);
        }
        
        if (**code != '"') {
            ast_free_stmt(stmt);
            return NULL;
        }
        (*code)++;
        
        const char* path_start = *code;
        while (**code && **code != '"') (*code)++;
        if (**code != '"') {
            ast_free_stmt(stmt);
            return NULL;
        }
        
        stmt->as.import.module_path = dup_range(path_start, *code);
        (*code)++;
        skip_whitespace(code);
        
        if (!stmt->as.import.is_selective && strncmp(*code, "as ", 3) == 0) {
            *code += 3;
            skip_whitespace(code);
            
            const char* alias_start = *code;
            while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
            if (*code == alias_start) {
                ast_free_stmt(stmt);
                return NULL;
            }
            stmt->as.import.alias = dup_range(alias_start, *code);
        }
        
        return stmt;
    }
    
    if (strncmp(*code, "export ", 7) == 0) {
        *code += 7;
        skip_whitespace(code);
        
        ASTStmt* stmt = ast_stmt_new(AST_STMT_EXPORT);
        if (!stmt) return NULL;
        
        stmt->as.export.is_default = 0;
        stmt->as.export.symbol_count = 0;
        stmt->as.export.symbol_names = NULL;
        stmt->as.export.symbol_aliases = NULL;
        
        if (strncmp(*code, "default ", 8) == 0) {
            *code += 8;
            skip_whitespace(code);
            
            stmt->as.export.is_default = 1;
            stmt->as.export.symbol_count = 1;
            stmt->as.export.symbol_names = (char**)malloc(sizeof(char*));
            stmt->as.export.symbol_aliases = (char**)malloc(sizeof(char*));
            
            const char* start = *code;
            while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
            if (*code == start) {
                ast_free_stmt(stmt);
                return NULL;
            }
            
            stmt->as.export.symbol_names[0] = dup_range(start, *code);
            stmt->as.export.symbol_aliases[0] = NULL;
            
            return stmt;
        }
        
        // Parse symbol list: export { symbol1, symbol2 }
        if (**code != '{') {
            ast_free_stmt(stmt);
            return NULL;
        }
        (*code)++;
        skip_whitespace(code);
        
        int symbol_cap = 4;
        stmt->as.export.symbol_names = (char**)malloc(symbol_cap * sizeof(char*));
        stmt->as.export.symbol_aliases = (char**)malloc(symbol_cap * sizeof(char*));
        
        while (**code && **code != '}') {
            skip_whitespace(code);
            if (**code == '}') break;
            
            const char* start = *code;
            while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
            if (*code == start) {
                ast_free_stmt(stmt);
                return NULL;
            }
            
            char* symbol_name = dup_range(start, *code);
            skip_whitespace(code);
            
            char* symbol_alias = NULL;
            if (strncmp(*code, "as ", 3) == 0) {
                *code += 3;
                skip_whitespace(code);
                
                const char* alias_start = *code;
                while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                if (*code == alias_start) {
                    free(symbol_name);
                    ast_free_stmt(stmt);
                    return NULL;
                }
                symbol_alias = dup_range(alias_start, *code);
            }
            
            if (stmt->as.export.symbol_count >= symbol_cap) {
                symbol_cap *= 2;
                stmt->as.export.symbol_names = (char**)realloc(stmt->as.export.symbol_names, 
                                                              symbol_cap * sizeof(char*));
                stmt->as.export.symbol_aliases = (char**)realloc(stmt->as.export.symbol_aliases, 
                                                                symbol_cap * sizeof(char*));
            }
            
            stmt->as.export.symbol_names[stmt->as.export.symbol_count] = symbol_name;
            stmt->as.export.symbol_aliases[stmt->as.export.symbol_count] = symbol_alias;
            stmt->as.export.symbol_count++;
            
            skip_whitespace(code);
            if (**code == ',') {
                (*code)++;
                skip_whitespace(code);
            }
        }
        
        if (**code != '}') {
            ast_free_stmt(stmt);
            return NULL;
        }
        (*code)++;
        
        return stmt;
    }

    if (strncmp(*code, "def ", 4) == 0) {
        *code += 4;
        skip_whitespace(code);

        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* fn_name = dup_range(start, *code);
        if (!fn_name) return NULL;

        skip_whitespace(code);
        if (**code != '(') {
            free(fn_name);
            return NULL;
        }
        (*code)++;

        int param_cap = 0;
        int param_count = 0;
        char** param_names = NULL;
        TypeDescriptor** param_type_descs = NULL;
        ASTExpr** param_defaults = NULL;

        skip_whitespace(code);
        if (**code != ')') {
            while (**code) {
                skip_whitespace(code);
                const char* pstart = *code;
                while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                if (*code == pstart) {
                    free(fn_name);
                    return NULL;
                }
                char* p_name = dup_range(pstart, *code);
                if (!p_name) {
                    free(fn_name);
                    return NULL;
                }

                skip_whitespace(code);
                if (**code != ':') {
                    free(p_name);
                    free(fn_name);
                    return NULL;
                }
                (*code)++;

                TypeDescriptor* p_type_desc = parse_type_descriptor(code);
                if (!p_type_desc) {
                    free(p_name);
                    free(fn_name);
                    return NULL;
                }

                // Check for default value
                ASTExpr* default_expr = NULL;
                skip_whitespace(code);
                if (**code == '=') {
                    (*code)++;
                    skip_whitespace(code);
                    default_expr = parse_expression_str_as_ast(code);
                    if (!default_expr) {
                        free(p_name);
                        free(fn_name);
                        type_descriptor_free(p_type_desc);
                        for (int i = 0; i < param_count; i++) {
                            free(param_names[i]);
                            if (param_defaults[i]) ast_free_expr(param_defaults[i]);
                            if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
                        }
                        free(param_names);
                        free(param_type_descs);
                        free(param_defaults);
                        return NULL;
                    }
                }

                if (param_count >= param_cap) {
                    int new_cap = param_cap == 0 ? 4 : param_cap * 2;
                    char** new_names = malloc(sizeof(char*) * (size_t)new_cap);
                    TypeDescriptor** new_types = malloc(sizeof(TypeDescriptor*) * (size_t)new_cap);
                    ASTExpr** new_defaults = malloc(sizeof(ASTExpr*) * (size_t)new_cap);
                    if (!new_names || !new_types || !new_defaults) {
                        free(new_names);
                        free(new_types);
                        free(new_defaults);
                        free(p_name);
                        free(fn_name);
                        type_descriptor_free(p_type_desc);
                        for (int i = 0; i < param_count; i++) free(param_names[i]);
                        free(param_names);
                        for (int i = 0; i < param_count; i++) {
                            if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
                        }
                        free(param_type_descs);
                        free(param_defaults);
                        return NULL;
                    }
                    if (param_count > 0) {
                        memcpy(new_names, param_names, sizeof(char*) * (size_t)param_count);
                        memcpy(new_types, param_type_descs, sizeof(TypeDescriptor*) * (size_t)param_count);
                        memcpy(new_defaults, param_defaults, sizeof(ASTExpr*) * (size_t)param_count);
                    }
                    free(param_names);
                    free(param_type_descs);
                    free(param_defaults);
                    param_names = new_names;
                    param_type_descs = new_types;
                    param_defaults = new_defaults;
                    param_cap = new_cap;
                }
                param_names[param_count] = p_name;
                param_type_descs[param_count] = p_type_desc;
                param_defaults[param_count] = default_expr;
                param_count++;

                skip_whitespace(code);
                if (**code == ',') {
                    (*code)++;
                    continue;
                }
                break;
            }
        }

        skip_whitespace(code);
        if (**code != ')') {
            free(fn_name);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            return NULL;
        }
        (*code)++;

        VarType ret_type = TYPE_INT;
        TypeDescriptor* ret_type_desc = type_descriptor_create_primitive(TYPE_INT);
        skip_whitespace(code);
        if (**code == '-' && *(*code + 1) == '>') {
            *code += 2;
            type_descriptor_free(ret_type_desc);
            ret_type_desc = parse_type_descriptor(code);
            if (!ret_type_desc) {
                free(fn_name);
                for (int i = 0; i < param_count; i++) {
                    free(param_names[i]);
                    if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                    if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
                }
                free(param_names);
                free(param_type_descs);
                free(param_defaults);
                return NULL;
            }
            ret_type = ret_type_desc->base_type;
        }

        if (**code == '-' && *(*code + 1) == '>') {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            return NULL;
        }

        skip_whitespace(code);
        if (**code != '{') {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            return NULL;
        }
        (*code)++;

        ASTStmtList* body = parse_block(code);
        if (!body || **code != '}') {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            if (body) ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_FUNC_DECL);
        if (!s) {
            free(fn_name);
            type_descriptor_free(ret_type_desc);
            for (int i = 0; i < param_count; i++) {
                free(param_names[i]);
                if (param_defaults && param_defaults[i]) ast_free_expr(param_defaults[i]);
                if (param_type_descs && param_type_descs[i]) type_descriptor_free(param_type_descs[i]);
            }
            free(param_names);
            free(param_type_descs);
            free(param_defaults);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.func_decl.name = fn_name;
        s->as.func_decl.param_count = param_count;
        s->as.func_decl.param_names = param_names;
        s->as.func_decl.param_type_descs = param_type_descs;
        s->as.func_decl.param_defaults = param_defaults;
        s->as.func_decl.return_type = ret_type;
        s->as.func_decl.return_type_desc = ret_type_desc;
        s->as.func_decl.body = body;
        return s;
    }

    if (strncmp(*code, "return", 6) == 0 && !isalnum((unsigned char)*(*code + 6))) {
        *code += 6;
        skip_whitespace(code);
        ASTExpr* e = parse_expression_str_as_ast(code);
        if (!e) return NULL;
        ASTStmt* s = ast_stmt_new(AST_STMT_RETURN);
        if (!s) {
            ast_free_expr(e);
            return NULL;
        }
        s->as.ret.expr = e;

        const char* lookahead = *code;
        while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\r') {
            lookahead++;
        }
        if (*lookahead == '\n' || *lookahead == '\0' || *lookahead == '}' || *lookahead == ';') {
            *code = lookahead;
            if (**code == '\n' || **code == ';') (*code)++;
        }

        return s;
    }

    if (strncmp(*code, "struct ", 7) == 0) {
        *code += 7;
        skip_whitespace(code);

        // Parse struct name
        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* struct_name = dup_range(start, *code);
        if (!struct_name) return NULL;

        skip_whitespace(code);
        if (**code != '{') {
            free(struct_name);
            return NULL;
        }
        (*code)++;

        // Parse fields
        int field_cap = 0;
        int field_count = 0;
        char** field_names = NULL;
        TypeDescriptor** field_types = NULL;

        skip_whitespace(code);
        while (**code && **code != '}') {
            skip_whitespace(code);
            if (**code == '}') break;

            // Parse field name
            const char* field_start = *code;
            while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
            if (*code == field_start) {
                free(struct_name);
                for (int i = 0; i < field_count; i++) {
                    free(field_names[i]);
                    type_descriptor_free(field_types[i]);
                }
                free(field_names);
                free(field_types);
                return NULL;
            }
            char* field_name = dup_range(field_start, *code);
            if (!field_name) {
                free(struct_name);
                for (int i = 0; i < field_count; i++) {
                    free(field_names[i]);
                    type_descriptor_free(field_types[i]);
                }
                free(field_names);
                free(field_types);
                return NULL;
            }

            skip_whitespace(code);
            if (**code != ':') {
                free(field_name);
                free(struct_name);
                for (int i = 0; i < field_count; i++) {
                    free(field_names[i]);
                    type_descriptor_free(field_types[i]);
                }
                free(field_names);
                free(field_types);
                return NULL;
            }
            (*code)++;

            // Parse field type
            TypeDescriptor* field_type = parse_type_descriptor(code);
            if (!field_type) {
                free(field_name);
                free(struct_name);
                for (int i = 0; i < field_count; i++) {
                    free(field_names[i]);
                    type_descriptor_free(field_types[i]);
                }
                free(field_names);
                free(field_types);
                return NULL;
            }

            // Expand arrays if needed
            if (field_count >= field_cap) {
                int new_cap = field_cap == 0 ? 4 : field_cap * 2;
                char** new_names = malloc(sizeof(char*) * (size_t)new_cap);
                TypeDescriptor** new_types = malloc(sizeof(TypeDescriptor*) * (size_t)new_cap);
                if (!new_names || !new_types) {
                    free(new_names);
                    free(new_types);
                    free(field_name);
                    free(struct_name);
                    type_descriptor_free(field_type);
                    for (int i = 0; i < field_count; i++) {
                        free(field_names[i]);
                        type_descriptor_free(field_types[i]);
                    }
                    free(field_names);
                    free(field_types);
                    return NULL;
                }
                if (field_count > 0) {
                    memcpy(new_names, field_names, sizeof(char*) * (size_t)field_count);
                    memcpy(new_types, field_types, sizeof(TypeDescriptor*) * (size_t)field_count);
                }
                free(field_names);
                free(field_types);
                field_names = new_names;
                field_types = new_types;
                field_cap = new_cap;
            }

            field_names[field_count] = field_name;
            field_types[field_count] = field_type;
            field_count++;

            skip_whitespace(code);
            if (**code == '\n') (*code)++;
            skip_whitespace(code);
        }

        if (**code != '}') {
            free(struct_name);
            for (int i = 0; i < field_count; i++) {
                free(field_names[i]);
                type_descriptor_free(field_types[i]);
            }
            free(field_names);
            free(field_types);
            return NULL;
        }
        (*code)++;

        // Create AST node
        ASTStmt* s = ast_stmt_new(AST_STMT_STRUCT_DECL);
        if (!s) {
            free(struct_name);
            for (int i = 0; i < field_count; i++) {
                free(field_names[i]);
                type_descriptor_free(field_types[i]);
            }
            free(field_names);
            free(field_types);
            return NULL;
        }

        s->as.struct_decl.name = struct_name;
        s->as.struct_decl.field_count = field_count;
        s->as.struct_decl.field_names = field_names;
        s->as.struct_decl.field_types = field_types;

        return s;
    }

    if (strncmp(*code, "class ", 6) == 0) {
        *code += 6;
        skip_whitespace(code);

        // Parse class name
        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* class_name = dup_range(start, *code);
        if (!class_name) return NULL;

        skip_whitespace(code);
        
        // Check for inheritance
        char* parent_name = NULL;
        if (strncmp(*code, "extends ", 8) == 0) {
            *code += 8;
            skip_whitespace(code);
            
            const char* parent_start = *code;
            while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
            if (*code == parent_start) {
                free(class_name);
                return NULL;
            }
            parent_name = dup_range(parent_start, *code);
            if (!parent_name) {
                free(class_name);
                return NULL;
            }
            skip_whitespace(code);
        }

        if (**code != '{') {
            free(class_name);
            free(parent_name);
            return NULL;
        }
        (*code)++;

        // Parse fields and methods
        int field_cap = 0;
        int field_count = 0;
        char** field_names = NULL;
        TypeDescriptor** field_types = NULL;
        
        int method_cap = 0;
        int method_count = 0;
        ASTStmtFuncDecl** methods = NULL;
        ASTStmtFuncDecl* constructor = NULL;

        skip_whitespace(code);
        while (**code && **code != '}') {
            skip_whitespace(code);
            if (**code == '}') break;

            // Check if this is a method (starts with 'def') or constructor (starts with 'init')
            if (strncmp(*code, "init(", 5) == 0) {
                // Parse constructor - must be named 'init'
                *code += 4; // Skip "init"
                skip_whitespace(code);
                
                if (**code != '(') {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                (*code)++;

                // Parse constructor parameters
                int param_cap = 0;
                int param_count = 0;
                char** param_names = NULL;
                TypeDescriptor** param_type_descs = NULL;
                ASTExpr** param_defaults = NULL;

                skip_whitespace(code);
                if (**code != ')') {
                    while (**code) {
                        skip_whitespace(code);
                        const char* pstart = *code;
                        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                        if (*code == pstart) {
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }
                        char* p_name = dup_range(pstart, *code);
                        if (!p_name) {
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }

                        skip_whitespace(code);
                        if (**code != ':') {
                            free(p_name);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }
                        (*code)++;

                        TypeDescriptor* p_type_desc = parse_type_descriptor(code);
                        if (!p_type_desc) {
                            free(p_name);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }

                        // Check for default value
                        ASTExpr* default_expr = NULL;
                        skip_whitespace(code);
                        if (**code == '=') {
                            (*code)++;
                            skip_whitespace(code);
                            default_expr = parse_expression_str_as_ast(code);
                            if (!default_expr) {
                                free(p_name);
                                free(class_name);
                                free(parent_name);
                                type_descriptor_free(p_type_desc);
                                return NULL;
                            }
                        }

                        // Expand arrays if needed
                        if (param_count >= param_cap) {
                            int new_cap = param_cap == 0 ? 4 : param_cap * 2;
                            char** new_names = malloc(sizeof(char*) * (size_t)new_cap);
                            TypeDescriptor** new_types = malloc(sizeof(TypeDescriptor*) * (size_t)new_cap);
                            ASTExpr** new_defaults = malloc(sizeof(ASTExpr*) * (size_t)new_cap);
                            if (!new_names || !new_types || !new_defaults) {
                                free(new_names);
                                free(new_types);
                                free(new_defaults);
                                free(p_name);
                                free(class_name);
                                free(parent_name);
                                type_descriptor_free(p_type_desc);
                                return NULL;
                            }
                            if (param_count > 0) {
                                memcpy(new_names, param_names, sizeof(char*) * (size_t)param_count);
                                memcpy(new_types, param_type_descs, sizeof(TypeDescriptor*) * (size_t)param_count);
                                memcpy(new_defaults, param_defaults, sizeof(ASTExpr*) * (size_t)param_count);
                            }
                            free(param_names);
                            free(param_type_descs);
                            free(param_defaults);
                            param_names = new_names;
                            param_type_descs = new_types;
                            param_defaults = new_defaults;
                            param_cap = new_cap;
                        }
                        param_names[param_count] = p_name;
                        param_type_descs[param_count] = p_type_desc;
                        param_defaults[param_count] = default_expr;
                        param_count++;

                        skip_whitespace(code);
                        if (**code == ',') {
                            (*code)++;
                            continue;
                        }
                        break;
                    }
                }

                skip_whitespace(code);
                if (**code != ')') {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                (*code)++;

                skip_whitespace(code);
                if (**code != '{') {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                (*code)++;

                ASTStmtList* body = parse_block(code);
                if (!body || **code != '}') {
                    free(class_name);
                    free(parent_name);
                    if (body) ast_free_stmt_list(body);
                    return NULL;
                }
                (*code)++;

                // Create constructor function declaration
                constructor = malloc(sizeof(ASTStmtFuncDecl));
                if (!constructor) {
                    free(class_name);
                    free(parent_name);
                    ast_free_stmt_list(body);
                    return NULL;
                }
                
                constructor->name = strdup("init");
                constructor->param_count = param_count;
                constructor->param_names = param_names;
                constructor->param_type_descs = param_type_descs;
                constructor->param_defaults = param_defaults;
                constructor->return_type = TYPE_NIL;
                constructor->return_type_desc = type_descriptor_create_primitive(TYPE_NIL);
                constructor->body = body;
                constructor->opt_info = NULL;
                
            } else if (strncmp(*code, "def ", 4) == 0) {
                // Parse method
                *code += 4;
                skip_whitespace(code);

                const char* method_start = *code;
                while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                if (*code == method_start) {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                char* method_name = dup_range(method_start, *code);
                if (!method_name) {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }

                skip_whitespace(code);
                if (**code != '(') {
                    free(method_name);
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                (*code)++;

                // Parse method parameters
                int param_cap = 0;
                int param_count = 0;
                char** param_names = NULL;
                TypeDescriptor** param_type_descs = NULL;
                ASTExpr** param_defaults = NULL;

                skip_whitespace(code);
                if (**code != ')') {
                    while (**code) {
                        skip_whitespace(code);
                        const char* pstart = *code;
                        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                        if (*code == pstart) {
                            free(method_name);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }
                        char* p_name = dup_range(pstart, *code);
                        if (!p_name) {
                            free(method_name);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }

                        skip_whitespace(code);
                        if (**code != ':') {
                            free(p_name);
                            free(method_name);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }
                        (*code)++;

                        TypeDescriptor* p_type_desc = parse_type_descriptor(code);
                        if (!p_type_desc) {
                            free(p_name);
                            free(method_name);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }

                        // Check for default value
                        ASTExpr* default_expr = NULL;
                        skip_whitespace(code);
                        if (**code == '=') {
                            (*code)++;
                            skip_whitespace(code);
                            default_expr = parse_expression_str_as_ast(code);
                            if (!default_expr) {
                                free(p_name);
                                free(method_name);
                                free(class_name);
                                free(parent_name);
                                type_descriptor_free(p_type_desc);
                                return NULL;
                            }
                        }

                        // Expand arrays if needed
                        if (param_count >= param_cap) {
                            int new_cap = param_cap == 0 ? 4 : param_cap * 2;
                            char** new_names = malloc(sizeof(char*) * (size_t)new_cap);
                            TypeDescriptor** new_types = malloc(sizeof(TypeDescriptor*) * (size_t)new_cap);
                            ASTExpr** new_defaults = malloc(sizeof(ASTExpr*) * (size_t)new_cap);
                            if (!new_names || !new_types || !new_defaults) {
                                free(new_names);
                                free(new_types);
                                free(new_defaults);
                                free(p_name);
                                free(method_name);
                                free(class_name);
                                free(parent_name);
                                type_descriptor_free(p_type_desc);
                                return NULL;
                            }
                            if (param_count > 0) {
                                memcpy(new_names, param_names, sizeof(char*) * (size_t)param_count);
                                memcpy(new_types, param_type_descs, sizeof(TypeDescriptor*) * (size_t)param_count);
                                memcpy(new_defaults, param_defaults, sizeof(ASTExpr*) * (size_t)param_count);
                            }
                            free(param_names);
                            free(param_type_descs);
                            free(param_defaults);
                            param_names = new_names;
                            param_type_descs = new_types;
                            param_defaults = new_defaults;
                            param_cap = new_cap;
                        }
                        param_names[param_count] = p_name;
                        param_type_descs[param_count] = p_type_desc;
                        param_defaults[param_count] = default_expr;
                        param_count++;

                        skip_whitespace(code);
                        if (**code == ',') {
                            (*code)++;
                            continue;
                        }
                        break;
                    }
                }

                skip_whitespace(code);
                if (**code != ')') {
                    free(method_name);
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                (*code)++;

                // Parse return type
                VarType ret_type = TYPE_NIL;
                TypeDescriptor* ret_type_desc = type_descriptor_create_primitive(TYPE_NIL);
                skip_whitespace(code);
                if (**code == '-' && *(*code + 1) == '>') {
                    *code += 2;
                    type_descriptor_free(ret_type_desc);
                    ret_type_desc = parse_type_descriptor(code);
                    if (!ret_type_desc) {
                        free(method_name);
                        free(class_name);
                        free(parent_name);
                        return NULL;
                    }
                    ret_type = ret_type_desc->base_type;
                }

                skip_whitespace(code);
                if (**code != '{') {
                    free(method_name);
                    free(class_name);
                    free(parent_name);
                    type_descriptor_free(ret_type_desc);
                    return NULL;
                }
                (*code)++;

                ASTStmtList* body = parse_block(code);
                if (!body || **code != '}') {
                    free(method_name);
                    free(class_name);
                    free(parent_name);
                    type_descriptor_free(ret_type_desc);
                    if (body) ast_free_stmt_list(body);
                    return NULL;
                }
                (*code)++;

                // Create method function declaration
                ASTStmtFuncDecl* method_decl = malloc(sizeof(ASTStmtFuncDecl));
                if (!method_decl) {
                    free(method_name);
                    free(class_name);
                    free(parent_name);
                    type_descriptor_free(ret_type_desc);
                    ast_free_stmt_list(body);
                    return NULL;
                }
                
                method_decl->name = method_name;
                method_decl->param_count = param_count;
                method_decl->param_names = param_names;
                method_decl->param_type_descs = param_type_descs;
                method_decl->param_defaults = param_defaults;
                method_decl->return_type = ret_type;
                method_decl->return_type_desc = ret_type_desc;
                method_decl->body = body;
                method_decl->opt_info = NULL;
                
                // Check if this is the constructor (init method)
                if (strcmp(method_name, "init") == 0) {
                    if (constructor) {
                        // Multiple constructors not allowed
                        free(method_decl);
                        free(class_name);
                        free(parent_name);
                        return NULL;
                    }
                    constructor = method_decl;
                } else {
                    // Add to methods array
                    if (method_count >= method_cap) {
                        int new_cap = method_cap == 0 ? 4 : method_cap * 2;
                        ASTStmtFuncDecl** new_methods = realloc(methods, new_cap * sizeof(ASTStmtFuncDecl*));
                        if (!new_methods) {
                            free(method_decl);
                            free(class_name);
                            free(parent_name);
                            return NULL;
                        }
                        methods = new_methods;
                        method_cap = new_cap;
                    }
                    methods[method_count++] = method_decl;
                }
                
            } else {
                // Parse field
                const char* field_start = *code;
                while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
                if (*code == field_start) {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                char* field_name = dup_range(field_start, *code);
                if (!field_name) {
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }

                skip_whitespace(code);
                if (**code != ':') {
                    free(field_name);
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }
                (*code)++;

                // Parse field type
                TypeDescriptor* field_type = parse_type_descriptor(code);
                if (!field_type) {
                    free(field_name);
                    free(class_name);
                    free(parent_name);
                    return NULL;
                }

                // Expand arrays if needed
                if (field_count >= field_cap) {
                    int new_cap = field_cap == 0 ? 4 : field_cap * 2;
                    char** new_names = realloc(field_names, new_cap * sizeof(char*));
                    TypeDescriptor** new_types = realloc(field_types, new_cap * sizeof(TypeDescriptor*));
                    if (!new_names || !new_types) {
                        free(new_names);
                        free(new_types);
                        free(field_name);
                        free(class_name);
                        free(parent_name);
                        type_descriptor_free(field_type);
                        return NULL;
                    }
                    field_names = new_names;
                    field_types = new_types;
                    field_cap = new_cap;
                }

                field_names[field_count] = field_name;
                field_types[field_count] = field_type;
                field_count++;
            }

            skip_whitespace(code);
            if (**code == '\n') (*code)++;
            skip_whitespace(code);
        }

        if (**code != '}') {
            free(class_name);
            free(parent_name);
            // Free fields and methods
            return NULL;
        }
        (*code)++;

        // Require constructor for classes
        if (!constructor) {
            free(class_name);
            free(parent_name);
            // Free fields and methods
            for (int i = 0; i < field_count; i++) {
                free(field_names[i]);
                type_descriptor_free(field_types[i]);
            }
            free(field_names);
            free(field_types);
            for (int i = 0; i < method_count; i++) {
                // Free method declarations
                free(methods[i]);
            }
            free(methods);
            return NULL; // Error: class must have init constructor
        }

        // Create AST node
        ASTStmt* s = ast_stmt_new(AST_STMT_CLASS_DECL);
        if (!s) {
            free(class_name);
            free(parent_name);
            return NULL;
        }

        s->as.class_decl.name = class_name;
        s->as.class_decl.parent_name = parent_name;
        s->as.class_decl.field_count = field_count;
        s->as.class_decl.field_names = field_names;
        s->as.class_decl.field_types = field_types;
        s->as.class_decl.method_count = method_count;
        s->as.class_decl.methods = methods;
        s->as.class_decl.constructor = constructor;

        return s;
    }

    if (strncmp(*code, "let ", 4) == 0 || strncmp(*code, "var ", 4) == 0 || strncmp(*code, "const ", 6) == 0) {
        int is_const = strncmp(*code, "const ", 6) == 0;
        if (strncmp(*code, "const ", 6) == 0) *code += 6;
        else *code += 4;
        skip_whitespace(code);

        const char* start = *code;
        while (**code && (isalnum((unsigned char)**code) || **code == '_')) (*code)++;
        if (*code == start) return NULL;
        char* var_name = dup_range(start, *code);
        if (!var_name) return NULL;

        skip_whitespace(code);

        TypeDescriptor* type_desc = NULL;
        if (**code == ':') {
            (*code)++;
            type_desc = parse_type_descriptor(code);
            if (!type_desc) {
                free(var_name);
                return NULL;
            }
        }
        // If no explicit type, we'll infer it during semantic analysis
        // WOAH WOAH WOAH
        VarType type = type_desc ? type_desc->base_type : TYPE_NIL;

        skip_whitespace(code);
        if (**code != '=') {
            free(var_name);
            return NULL;
        }
        (*code)++;
        skip_whitespace(code);

        ASTExpr* init = parse_expression_str_as_ast(code);
        if (!init) {
            free(var_name);
            return NULL;
        }

        ASTStmt* s = ast_stmt_new(AST_STMT_VAR_DECL);
        if (!s) {
            free(var_name);
            type_descriptor_free(type_desc);
            ast_free_expr(init);
            return NULL;
        }
        s->as.var_decl.var_name = var_name;
        s->as.var_decl.type = type;
        s->as.var_decl.type_desc = type_desc;
        s->as.var_decl.init = init;
        s->as.var_decl.is_const = is_const;
        return s;
    }

    if (strncmp(*code, "print(", 6) == 0) {
        *code += 6;
        ASTExpr* e = parse_expression_str_as_ast(code);
        if (**code == ')') (*code)++;

        const char* lookahead = *code;
        while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\r') {
            lookahead++;
        }
        if (*lookahead == '\n' || *lookahead == '\0' || *lookahead == '}' || *lookahead == ';') {
            *code = lookahead;
            if (**code == '\n' || **code == ';') (*code)++;
            ASTStmt* s = ast_stmt_new(AST_STMT_PRINT);
            if (!s) {
                ast_free_expr(e);
                return NULL;
            }
            s->as.print.expr = e;
            return s;
        }

        ast_free_expr(e);
        return NULL;
    }

    if (strncmp(*code, "if ", 3) == 0) {
        *code += 3;
        skip_whitespace(code);
        ASTExpr* cond = parse_expression_str_as_ast(code);
        if (!cond) return NULL;
        skip_whitespace(code);
        if (**code != '{') {
            ast_free_expr(cond);
            return NULL;
        }
        (*code)++;
        ASTStmtList* then_branch = parse_block(code);
        if (**code != '}') {
            ast_free_expr(cond);
            ast_free_stmt_list(then_branch);
            return NULL;
        }
        (*code)++;

        ASTStmtList* else_branch = NULL;
        skip_whitespace(code);
        if (strncmp(*code, "else ", 5) == 0) {
            *code += 5;
            skip_whitespace(code);
            
            // Check for "else if" pattern
            if (strncmp(*code, "if ", 3) == 0) {
                // Parse the "else if" as a nested if statement
                ASTStmt* nested_if = parse_stmt(code);
                if (!nested_if) {
                    ast_free_expr(cond);
                    ast_free_stmt_list(then_branch);
                    return NULL;
                }
                
                // Create a statement list containing just the nested if
                else_branch = ast_stmt_list_new();
                if (!else_branch) {
                    ast_free_expr(cond);
                    ast_free_stmt_list(then_branch);
                    ast_free_stmt(nested_if);
                    return NULL;
                }
                ast_stmt_list_add(else_branch, nested_if);
            } else {
                // Regular else block
                if (**code != '{') {
                    ast_free_expr(cond);
                    ast_free_stmt_list(then_branch);
                    return NULL;
                }
                (*code)++;
                else_branch = parse_block(code);
                if (**code != '}') {
                    ast_free_expr(cond);
                    ast_free_stmt_list(then_branch);
                    ast_free_stmt_list(else_branch);
                    return NULL;
                }
                (*code)++;
            }
        }

        ASTStmt* s = ast_stmt_new(AST_STMT_IF);
        if (!s) {
            ast_free_expr(cond);
            ast_free_stmt_list(then_branch);
            if (else_branch) ast_free_stmt_list(else_branch);
            return NULL;
        }
        s->as.if_stmt.condition = cond;
        s->as.if_stmt.then_branch = then_branch;
        s->as.if_stmt.else_branch = else_branch;
        return s;
    }

    if (strncmp(*code, "while ", 6) == 0) {
        *code += 6;
        skip_whitespace(code);
        ASTExpr* cond = parse_expression_str_as_ast(code);
        if (!cond) return NULL;
        skip_whitespace(code);
        if (**code != '{') {
            ast_free_expr(cond);
            return NULL;
        }
        (*code)++;
        ASTStmtList* body = parse_block(code);
        if (**code != '}') {
            ast_free_expr(cond);
            ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_WHILE);
        if (!s) {
            ast_free_expr(cond);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.while_stmt.condition = cond;
        s->as.while_stmt.body = body;
        return s;
    }

    if (strncmp(*code, "for ", 4) == 0) {
        *code += 4;
        skip_whitespace(code);
        const char* start = *code;
        while (**code && **code != ' ' && **code != '\t') (*code)++;
        if (*code == start) return NULL;
        char* var_name = dup_range(start, *code);
        if (!var_name) return NULL;

        skip_whitespace(code);
        if (strncmp(*code, "in ", 3) != 0) {
            free(var_name);
            return NULL;
        }
        *code += 3;
        skip_whitespace(code);
        ASTExpr* range_expr = parse_expression_str_as_ast(code);
        if (!range_expr) {
            free(var_name);
            return NULL;
        }
        skip_whitespace(code);
        if (**code != '{') {
            free(var_name);
            ast_free_expr(range_expr);
            return NULL;
        }
        (*code)++;
        ASTStmtList* body = parse_block(code);
        if (**code != '}') {
            free(var_name);
            ast_free_expr(range_expr);
            ast_free_stmt_list(body);
            return NULL;
        }
        (*code)++;

        ASTStmt* s = ast_stmt_new(AST_STMT_FOR_IN);
        if (!s) {
            free(var_name);
            ast_free_expr(range_expr);
            ast_free_stmt_list(body);
            return NULL;
        }
        s->as.for_in_stmt.var_name = var_name;
        s->as.for_in_stmt.iterable = range_expr;
        s->as.for_in_stmt.body = body;
        return s;
    }

    if (strncmp(*code, "break", 5) == 0 && !isalnum((unsigned char)*(*code + 5))) {
        *code += 5;
        return ast_stmt_new(AST_STMT_BREAK);
    }

    if (strncmp(*code, "continue", 8) == 0 && !isalnum((unsigned char)*(*code + 8))) {
        *code += 8;
        return ast_stmt_new(AST_STMT_CONTINUE);
    }

    const char* start = *code;
    const char* scan = *code;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    int in_string = 0;
    int escape = 0;
    const char* eq_pos = NULL;
    char assign_op = 0;

    while (*scan) {
        char c = *scan;
        if (in_string) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') in_string = 0;
            scan++;
            continue;
        }
        if (c == '"') { in_string = 1; scan++; continue; }
        if (c == '(') paren++;
        else if (c == ')') { if (paren > 0) paren--; }
        else if (c == '[') bracket++;
        else if (c == ']') { if (bracket > 0) bracket--; }
        else if (c == '{') brace++;
        else if (c == '}') {
            if (brace == 0) break;
            brace--;
        }

        if (paren == 0 && bracket == 0 && brace == 0) {
            if (c == '=') {
                if (*(scan + 1) != '=') {
                    eq_pos = scan;
                    break;
                }
            }
            if ((c == '+' || c == '-' || c == '*' || c == '/' || c == '%') && *(scan + 1) == '=') {
                assign_op = c;
                eq_pos = scan;
                break;
            }
            if (c == '\n' || c == ';') {
                break;
            }
        }

        scan++;
    }

    if (eq_pos) {
        *code = eq_pos;
        size_t name_len = (size_t)(eq_pos - start);
        while (name_len > 0 && isspace((unsigned char)start[name_len - 1])) name_len--;
        while (name_len > 0 && isspace((unsigned char)*start)) { start++; name_len--; }

        char* var_name = dup_range(start, start + name_len);
        if (!var_name) return NULL;

        ASTExpr* lhs_index_target = NULL;
        ASTExpr* lhs_index_expr = NULL;
        ASTExpr* lhs_member_target = NULL;
        char* lhs_member_name = NULL;
        
        // Check for array indexing: obj[index] = value
        if (strchr(var_name, '[')) {
            const char* lhs_src = var_name;
            const char* lhs_ptr = lhs_src;
            ASTExpr* lhs = parse_expression_str_as_ast(&lhs_ptr);
            if (lhs && lhs->kind == AST_EXPR_INDEX) {
                lhs_index_target = lhs->as.index.target;
                lhs_index_expr = lhs->as.index.index;
                lhs->as.index.target = NULL;
                lhs->as.index.index = NULL;
                ast_free_expr(lhs);
            } else if (lhs) {
                ast_free_expr(lhs);
            }
        }
        // Check for member access: obj.field = value
        else if (strchr(var_name, '.')) {
            const char* lhs_src = var_name;
            const char* lhs_ptr = lhs_src;
            ASTExpr* lhs = parse_expression_str_as_ast(&lhs_ptr);
            if (lhs && lhs->kind == AST_EXPR_MEMBER) {
                lhs_member_target = lhs->as.member.target;
                lhs_member_name = lhs->as.member.member;
                lhs->as.member.target = NULL;
                lhs->as.member.member = NULL;
                ast_free_expr(lhs);
            } else if (lhs) {
                ast_free_expr(lhs);
            }
        }

        if (assign_op) {
            *code += 2;
        } else {
            (*code)++;
        }
        skip_whitespace(code);
        ASTExpr* rhs = parse_expression_str_as_ast(code);
        if (!rhs) {
            ast_free_expr(lhs_index_target);
            ast_free_expr(lhs_index_expr);
            ast_free_expr(lhs_member_target);
            free(lhs_member_name);
            free(var_name);
            return NULL;
        }

        if (lhs_index_target && lhs_index_expr) {
            ASTStmt* s = ast_stmt_new(AST_STMT_INDEX_ASSIGN);
            if (!s) {
                ast_free_expr(lhs_index_target);
                ast_free_expr(lhs_index_expr);
                ast_free_expr(rhs);
                free(var_name);
                return NULL;
            }
            s->as.index_assign.target = lhs_index_target;
            s->as.index_assign.index = lhs_index_expr;
            s->as.index_assign.value = rhs;
            s->as.index_assign.op = assign_op;
            free(var_name);
            return s;
        }
        
        if (lhs_member_target && lhs_member_name) {
            ASTStmt* s = ast_stmt_new(AST_STMT_MEMBER_ASSIGN);
            if (!s) {
                ast_free_expr(lhs_member_target);
                free(lhs_member_name);
                ast_free_expr(rhs);
                free(var_name);
                return NULL;
            }
            s->as.member_assign.target = lhs_member_target;
            s->as.member_assign.member = lhs_member_name;
            s->as.member_assign.value = rhs;
            s->as.member_assign.op = assign_op;
            free(var_name);
            return s;
        }

        ASTStmt* s = ast_stmt_new(AST_STMT_VAR_ASSIGN);
        if (!s) {
            free(var_name);
            ast_free_expr(rhs);
            return NULL;
        }
        s->as.var_assign.var_name = var_name;
        s->as.var_assign.value = rhs;
        s->as.var_assign.op = assign_op;
        return s;
    }

    ASTExpr* expr = parse_expression_str_as_ast(code);
    if (!expr) return NULL;

    ASTStmt* s = ast_stmt_new(AST_STMT_EXPR);
    if (!s) {
        ast_free_expr(expr);
        return NULL;
    }
    s->as.expr.expr = expr;
    return s;
}

// Utility functions
static char* dup_range(const char* start, const char* end) {
    size_t len = (size_t)(end - start);
    char* s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static void skip_whitespace(const char** code) {
    while (**code) {
        if (isspace((unsigned char)**code)) {
            (*code)++;
        }
        else if (**code == '/' && *(*code + 1) == '/') {
            while (**code && **code != '\n') {
                (*code)++;
            }
        } else {
            break;
        }
    }
}