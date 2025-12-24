#ifndef MODULE_H
#define MODULE_H

#include "compiler/ast/ast.h"
#include "core/var.h"
#include "core/function.h"

typedef struct Module Module;
typedef struct ModuleSymbol ModuleSymbol;
typedef struct ModuleRegistry ModuleRegistry;

typedef enum {
    MODULE_SYMBOL_FUNCTION,
    MODULE_SYMBOL_CLASS,
    MODULE_SYMBOL_STRUCT,
    MODULE_SYMBOL_VARIABLE
} ModuleSymbolType;

typedef struct ModuleSymbol {
    char* name;                    
    char* alias;                   
    ModuleSymbolType type;         
    void* symbol_ptr;              
    int is_default;                
    struct ModuleSymbol* next;     
} ModuleSymbol;

typedef struct Module {
    char* name;                    
    char* file_path;               
    char* resolved_path;           
    ASTStmtList* ast;              
    ModuleSymbol* exports;        
    struct Module* dependencies;   
    int is_compiled;               
    int is_loading;                
    struct Module* next;           
} Module;

typedef struct ModuleRegistry {
    Module* modules;               
    char** search_paths;           
    int search_path_count;         
} ModuleRegistry;

void module_system_init(void);
void module_system_cleanup(void);

Module* module_load(const char* module_path, const char* current_file_dir);
Module* module_find_loaded(const char* resolved_path);
char* module_resolve_path(const char* module_path, const char* current_file_dir);

int module_compile(Module* module);
int module_add_export(Module* module, const char* symbol_name, const char* alias, 
                     ModuleSymbolType type, void* symbol_ptr, int is_default);
ModuleSymbol* module_find_export(Module* module, const char* symbol_name);
ModuleSymbol* module_get_default_export(Module* module);

int process_import_statement(const ASTStmtImport* import_stmt, const char* current_file_dir);
int process_export_statement(const ASTStmtExport* export_stmt, Module* current_module);

void* module_lookup_symbol(const char* qualified_name, ModuleSymbolType* out_type);
int module_register_qualified_symbol(const char* module_name, const char* symbol_name, 
                                    ModuleSymbolType type, void* symbol_ptr);

char* module_get_name_from_path(const char* file_path);
int module_is_valid_identifier(const char* name);
void module_add_search_path(const char* path);

extern char* module_last_error;
void module_set_error(const char* format, ...);
const char* module_get_error(void);

#endif