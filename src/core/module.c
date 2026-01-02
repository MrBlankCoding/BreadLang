#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <limits.h>

#include "core/module.h"
#include "core/value.h"
#include "core/type_descriptor.h"
#include "compiler/ast/ast.h"
#include "compiler/ast/ast_memory.h"
#include "runtime/error.h"
#include "runtime/memory.h"

#define MODULE_MAX_SEARCH_PATHS 32
#define MODULE_MAX_PATH_LENGTH 4096
#define MODULE_EXTENSION ".bread"
#define MODULE_EXTENSION_LEN 6

static ModuleRegistry* g_module_registry = NULL;
char* module_last_error = NULL;

typedef struct QualifiedSymbol {
    char* qualified_name;
    ModuleSymbolType type;
    void* symbol_ptr;
    struct QualifiedSymbol* next;
} QualifiedSymbol;

static QualifiedSymbol* g_qualified_symbols = NULL;
static void module_clear_qualified_symbols(void);
static char* module_get_dir_from_path(const char* path);
static int module_preprocess_module(Module* module);
static int module_preprocess_stmt_list(ASTStmtList* list, Module* current_module);
static void ast_stmt_list_append_stmt(ASTStmtList* list, ASTStmt* stmt);
static int splice_module_into_program(ASTStmtList* program, Module* module);

void module_set_error(const char* format, ...) {
    free(module_last_error);
    module_last_error = NULL;
    
    va_list args;
    va_start(args, format);
    
    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (size < 0) {
        return;
    }
    
    module_last_error = (char*)malloc(size + 1);
    if (!module_last_error) {
        return;
    }
    
    va_start(args, format);
    vsnprintf(module_last_error, size + 1, format, args);
    va_end(args);
}

const char* module_get_error(void) {
    return module_last_error ? module_last_error : "Unknown error";
}

static void module_clear_qualified_symbols(void) {
    QualifiedSymbol* current = g_qualified_symbols;
    while (current) {
        QualifiedSymbol* next = current->next;
        free(current->qualified_name);
        free(current);
        current = next;
    }
    g_qualified_symbols = NULL;
}

int module_register_qualified_symbol(const char* module_name, const char* symbol_name, 
                                     ModuleSymbolType type, void* symbol_ptr) {
    if (!module_name || !*module_name || !symbol_name || !*symbol_name || !symbol_ptr) {
        module_set_error("Invalid parameters for module_register_qualified_symbol");
        return 0;
    }

    if (!module_is_valid_identifier(module_name) || !module_is_valid_identifier(symbol_name)) {
        module_set_error("Invalid module or symbol name: '%s.%s'", module_name, symbol_name);
        return 0;
    }

    // Check for duplicates
    size_t len = strlen(module_name) + strlen(symbol_name) + 2;
    char* qualified = (char*)malloc(len);
    if (!qualified) {
        module_set_error("Out of memory");
        return 0;
    }
    snprintf(qualified, len, "%s.%s", module_name, symbol_name);

    for (QualifiedSymbol* current = g_qualified_symbols; current; current = current->next) {
        if (strcmp(current->qualified_name, qualified) == 0) {
            free(qualified);
            module_set_error("Qualified symbol already registered: %s", current->qualified_name);
            return 0;
        }
    }

    QualifiedSymbol* entry = (QualifiedSymbol*)malloc(sizeof(QualifiedSymbol));
    if (!entry) {
        free(qualified);
        module_set_error("Out of memory");
        return 0;
    }

    entry->qualified_name = qualified;
    entry->type = type;
    entry->symbol_ptr = symbol_ptr;
    entry->next = g_qualified_symbols;
    g_qualified_symbols = entry;

    return 1;
}

void* module_lookup_symbol(const char* qualified_name, ModuleSymbolType* out_type) {
    if (!qualified_name || !*qualified_name) {
        module_set_error("Invalid qualified name");
        return NULL;
    }

    for (QualifiedSymbol* current = g_qualified_symbols; current; current = current->next) {
        if (strcmp(current->qualified_name, qualified_name) == 0) {
            if (out_type) {
                *out_type = current->type;
            }
            return current->symbol_ptr;
        }
    }

    module_set_error("Qualified symbol not found: %s", qualified_name);
    return NULL;
}
void module_system_init(void) {
    if (g_module_registry) {
        return; // Already initialized
    }
    
    g_module_registry = (ModuleRegistry*)calloc(1, sizeof(ModuleRegistry));
    if (!g_module_registry) {
        module_set_error("Failed to allocate module registry");
        return;
    }
    
    module_add_search_path(".");
    module_add_search_path("./lib");
    module_add_search_path("./modules");
}

void module_system_cleanup(void) {
    if (!g_module_registry) {
        return;
    }
    
    Module* current = g_module_registry->modules;
    while (current) {
        Module* next = current->next;
        
        free(current->name);
        free(current->file_path);
        free(current->resolved_path);
        free(current->default_export_name);
        type_descriptor_free(current->default_export_type_desc);
        // free exports
        ModuleSymbol* symbol = current->exports;
        while (symbol) {
            ModuleSymbol* next_symbol = symbol->next;
            free(symbol->name);
            free(symbol->alias);
            free(symbol);
            symbol = next_symbol;
        }
        
        free(current);
        current = next;
    }
    
    // Free search paths
    for (int i = 0; i < g_module_registry->search_path_count; i++) {
        free(g_module_registry->search_paths[i]);
    }
    free(g_module_registry->search_paths);
    
    free(g_module_registry);
    g_module_registry = NULL;

    module_clear_qualified_symbols();
    
    free(module_last_error);
    module_last_error = NULL;
}

void module_add_search_path(const char* path) {
    if (!g_module_registry || !path) {
        return;
    }
    
    // Check limit
    if (g_module_registry->search_path_count >= MODULE_MAX_SEARCH_PATHS) {
        module_set_error("Maximum search paths (%d) exceeded", MODULE_MAX_SEARCH_PATHS);
        return;
    }
    
    for (int i = 0; i < g_module_registry->search_path_count; i++) {
        if (strcmp(g_module_registry->search_paths[i], path) == 0) {
            return; // Already exists
        }
    }
    
    char** new_paths = (char**)realloc(g_module_registry->search_paths, 
                                       (g_module_registry->search_path_count + 1) * sizeof(char*));
    if (!new_paths) {
        module_set_error("Failed to allocate memory for search path");
        return;
    }
    
    g_module_registry->search_paths = new_paths;
    g_module_registry->search_paths[g_module_registry->search_path_count] = strdup(path);
    
    if (!g_module_registry->search_paths[g_module_registry->search_path_count]) {
        module_set_error("Failed to duplicate search path");
        return;
    }
    
    g_module_registry->search_path_count++;
}

static int file_exists_and_regular(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static char* try_path_with_extension(const char* base_path) {
    if (file_exists_and_regular(base_path)) {
        return strdup(base_path);
    }
    
    // Try with .bread extension
    size_t len = strlen(base_path) + MODULE_EXTENSION_LEN + 1;
    char* with_ext = (char*)malloc(len);
    if (!with_ext) {
        return NULL;
    }
    
    snprintf(with_ext, len, "%s%s", base_path, MODULE_EXTENSION);
    
    if (file_exists_and_regular(with_ext)) {
        return with_ext;
    }
    
    free(with_ext);
    return NULL;
}

char* module_resolve_path(const char* module_path, const char* current_file_dir) {
    if (!module_path) {
        module_set_error("Module path is NULL");
        return NULL;
    }
    
    if (module_path[0] == '/') {
        char* resolved = try_path_with_extension(module_path);
        if (resolved) {
            return resolved;
        }
        module_set_error("Module file not found: %s", module_path);
        return NULL;
    }
    
    if (current_file_dir) {
        char relative_path[MODULE_MAX_PATH_LENGTH];
        int written = snprintf(relative_path, sizeof(relative_path), "%s/%s", 
                              current_file_dir, module_path);
        
        if (written < 0 || written >= (int)sizeof(relative_path)) {
            module_set_error("Path too long");
            return NULL;
        }
        
        char* resolved = try_path_with_extension(relative_path);
        if (resolved) {
            return resolved;
        }
    }
    
    if (g_module_registry) {
        for (int i = 0; i < g_module_registry->search_path_count; i++) {
            char search_path[MODULE_MAX_PATH_LENGTH];
            int written = snprintf(search_path, sizeof(search_path), "%s/%s", 
                                  g_module_registry->search_paths[i], module_path);
            
            if (written < 0 || written >= (int)sizeof(search_path)) {
                continue; // Path too long, try next
            }
            
            char* resolved = try_path_with_extension(search_path);
            if (resolved) {
                return resolved;
            }
        }
    }
    
    module_set_error("Module file not found: %s", module_path);
    return NULL;
}

static char* module_get_dir_from_path(const char* path) {
    if (!path) {
        return NULL;
    }
    
    char* copy = strdup(path);
    if (!copy) {
        return NULL;
    }
    
    char* dir = dirname(copy);
    if (!dir) {
        free(copy);
        return NULL;
    }
    
    char* result = strdup(dir);
    free(copy);
    return result;
}

char* module_get_name_from_path(const char* file_path) {
    if (!file_path) {
        return NULL;
    }
    
    char* path_copy = strdup(file_path);
    if (!path_copy) {
        return NULL;
    }
    
    char* base_name = basename(path_copy);
    
    // Remove .bread extension if present
    char* dot = strrchr(base_name, '.');
    if (dot && strcmp(dot, MODULE_EXTENSION) == 0) {
        *dot = '\0';
    }
    
    char* result = strdup(base_name);
    free(path_copy);
    return result;
}

int module_is_valid_identifier(const char* name) {
    if (!name || !*name) {
        return 0;
    }
    
    if (!isalpha((unsigned char)*name) && *name != '_') {
        return 0;
    }
    
    for (const char* p = name + 1; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            return 0;
        }
    }
    
    return 1;
}

Module* module_find_loaded(const char* resolved_path) {
    if (!g_module_registry || !resolved_path) {
        return NULL;
    }
    
    Module* current = g_module_registry->modules;
    while (current) {
        if (current->resolved_path && strcmp(current->resolved_path, resolved_path) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

Module* module_load(const char* module_path, const char* current_file_dir) {
    if (!g_module_registry) {
        module_set_error("Module system not initialized");
        return NULL;
    }
    
    char* resolved_path = module_resolve_path(module_path, current_file_dir);
    if (!resolved_path) {
        return NULL; // Error already set
    }
    
    Module* existing = module_find_loaded(resolved_path);
    if (existing) {
        free(resolved_path);
        return existing;
    }
    
    Module* module = (Module*)calloc(1, sizeof(Module));
    if (!module) {
        free(resolved_path);
        module_set_error("Failed to allocate module");
        return NULL;
    }
    
    module->name = module_get_name_from_path(resolved_path);
    module->file_path = strdup(module_path);
    module->resolved_path = resolved_path;
    module->default_export_type = TYPE_NIL;
    
    if (!module->name || !module->file_path) {
        free(module->name);
        free(module->file_path);
        free(module->resolved_path);
        free(module);
        module_set_error("Out of memory");
        return NULL;
    }
    
    module->next = g_module_registry->modules;
    g_module_registry->modules = module;
    
    return module;
}

int module_compile(Module* module) {
    if (!module) {
        module_set_error("Module is NULL");
        return 0;
    }
    
    if (module->is_compiled) {
        return 1; // Already compiled
    }
    
    if (module->is_loading) {
        module_set_error("Circular dependency detected for module '%s'", module->name);
        return 0;
    }
    
    module->is_loading = 1;
    
    FILE* file = fopen(module->resolved_path, "rb");
    if (!file) {
        module_set_error("Failed to open module file: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        module_set_error("Failed to seek in file: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    long file_size_long = ftell(file);
    if (file_size_long == -1 || file_size_long < 0) {
        fclose(file);
        module_set_error("Failed to get file size: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    rewind(file);
    
    size_t file_size = (size_t)file_size_long;
    char* source = (char*)malloc(file_size + 1);
    if (!source) {
        fclose(file);
        module_set_error("Out of memory");
        module->is_loading = 0;
        return 0;
    }
    
    size_t bytes_read = fread(source, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        free(source);
        module_set_error("Failed to read module file: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    source[file_size] = '\0';
    
    // Parse source code
    module->ast = ast_parse_program(module->resolved_path, source);
    free(source);
    
    if (!module->ast) {
        module_set_error("Failed to parse module: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    module->is_compiled = 1;
    module->is_loading = 0;
    return 1;
}

int module_add_export(Module* module, const char* symbol_name, const char* alias, 
                     ModuleSymbolType type, void* symbol_ptr, int is_default) {
    if (!module || !symbol_name || !symbol_ptr) {
        module_set_error("Invalid parameters for module_add_export");
        return 0;
    }
    
    ModuleSymbol* existing = module_find_export(module, symbol_name);
    if (existing) {
        module_set_error("Symbol '%s' is already exported", symbol_name);
        return 0;
    }
    
    if (is_default && module_get_default_export(module)) {
        module_set_error("Module already has a default export");
        return 0;
    }
    
    ModuleSymbol* symbol = (ModuleSymbol*)calloc(1, sizeof(ModuleSymbol));
    if (!symbol) {
        module_set_error("Failed to allocate module symbol");
        return 0;
    }
    
    symbol->name = strdup(symbol_name);
    symbol->alias = alias ? strdup(alias) : NULL;
    symbol->type = type;
    symbol->symbol_ptr = symbol_ptr;
    symbol->is_default = is_default;
    
    if (!symbol->name || (alias && !symbol->alias)) {
        free(symbol->name);
        free(symbol->alias);
        free(symbol);
        module_set_error("Out of memory");
        return 0;
    }
    
    symbol->next = module->exports;
    module->exports = symbol;
    
    return 1;
}

ModuleSymbol* module_find_export(Module* module, const char* symbol_name) {
    if (!module || !symbol_name) {
        return NULL;
    }
    
    ModuleSymbol* current = module->exports;
    while (current) {
        if (strcmp(current->name, symbol_name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

ModuleSymbol* module_get_default_export(Module* module) {
    if (!module) {
        return NULL;
    }
    
    ModuleSymbol* current = module->exports;
    while (current) {
        if (current->is_default) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

static int module_preprocess_stmt_list(ASTStmtList* list, Module* current_module) {
    if (!list) {
        return 1;
    }
    
    for (ASTStmt* st = list->head; st; st = st->next) {
        if (st->kind == AST_STMT_IMPORT) {
            char* module_dir = current_module ? 
                module_get_dir_from_path(current_module->resolved_path) : NULL;
            
            int ok = process_import_statement(&st->as.import, module_dir);
            free(module_dir);
            
            if (!ok) {
                return 0;
            }

            char* imported_dir = current_module ? 
                module_get_dir_from_path(current_module->resolved_path) : NULL;
            
            Module* imported = module_load(st->as.import.module_path, imported_dir);
            free(imported_dir);
            
            if (!imported || !module_preprocess_module(imported)) {
                return 0;
            }
            
        } else if (st->kind == AST_STMT_EXPORT) {
            if (!process_export_statement(&st->as.export, current_module)) {
                return 0;
            }

            if (st->as.export.is_default) {
                if (current_module->default_export_name) {
                    module_set_error("Module '%s' already has a default export", 
                                   current_module->name);
                    return 0;
                }
                
                current_module->default_export_name = strdup(st->as.export.symbol_names[0]);
                if (!current_module->default_export_name) {
                    module_set_error("Out of memory");
                    return 0;
                }

                Variable* var = get_variable(current_module->default_export_name);
                if (!var) {
                    module_set_error("Default export '%s' must be a global variable", 
                                   current_module->default_export_name);
                    return 0;
                }
                
                current_module->default_export_type = var->type;
                current_module->default_export_type_desc = 
                    type_descriptor_create_primitive(var->type);
                
                if (!current_module->default_export_type_desc) {
                    module_set_error("Failed to create type descriptor for default export");
                    return 0;
                }
            }
            
        } else if (st->kind == AST_STMT_FUNC_DECL) {
            Function fn = {0};
            fn.name = st->as.func_decl.name;
            fn.param_count = st->as.func_decl.param_count;
            fn.param_names = st->as.func_decl.param_names;
            
            if (fn.param_count > 0) {
                fn.param_types = malloc(sizeof(VarType) * (size_t)fn.param_count);
                if (!fn.param_types) {
                    module_set_error("Out of memory");
                    return 0;
                }
                
                for (int i = 0; i < fn.param_count; i++) {
                    fn.param_types[i] = st->as.func_decl.param_type_descs[i] ? 
                        st->as.func_decl.param_type_descs[i]->base_type : TYPE_NIL;
                }
            }
            
            fn.return_type = st->as.func_decl.return_type;
            fn.body = st->as.func_decl.body;
            fn.body_is_ast = 1;
            
            if (!register_function(&fn)) {
                free(fn.param_types);
                module_set_error("Failed to register function '%s'", st->as.func_decl.name);
                return 0;
            }
            
            free(fn.param_types);
            
        } else if (st->kind == AST_STMT_VAR_DECL) {
            if (!bread_var_decl_if_missing(st->as.var_decl.var_name, 
                                          st->as.var_decl.type, 
                                          st->as.var_decl.is_const, NULL)) {
                module_set_error("Failed to declare variable '%s'", 
                               st->as.var_decl.var_name);
                return 0;
            }
        }
    }
    
    return 1;
}

static int module_preprocess_module(Module* module) {
    if (!module) {
        return 0;
    }
    
    if (module->is_preprocessed) {
        return 1;
    }
    
    if (!module_compile(module)) {
        return 0;
    }
    
    if (!module->ast) {
        module_set_error("Module has no AST: %s", module->name);
        return 0;
    }
    
    if (!module_preprocess_stmt_list(module->ast, module)) {
        return 0;
    }
    
    module->is_preprocessed = 1;
    return 1;
}

static void ast_stmt_list_append_stmt(ASTStmtList* list, ASTStmt* stmt) {
    if (!list || !stmt) {
        return;
    }
    
    stmt->next = NULL;
    
    if (!list->head) {
        list->head = stmt;
        list->tail = stmt;
        return;
    }
    
    list->tail->next = stmt;
    list->tail = stmt;
}

static int splice_module_into_program(ASTStmtList* program, Module* module) {
    if (!program || !module || !module->ast) {
        return 0;
    }
    
    ASTStmt* st = module->ast->head;
    while (st) {
        ASTStmt* next = st->next;
        st->next = NULL;
        if (st->kind == AST_STMT_IMPORT || st->kind == AST_STMT_EXPORT) {
            ast_free_stmt(st);
            st = next;
            continue;
        }

        ast_stmt_list_append_stmt(program, st);
        st = next;
    }

    module->ast->head = NULL;
    module->ast->tail = NULL;
    module->is_included = 1;
    
    return 1;
}

int module_preprocess_program(ASTStmtList* program, const char* entry_file_path) {
    if (!program) {
        module_set_error("Program is NULL");
        return 0;
    }

    (void)entry_file_path;

    ASTStmtList* out = ast_stmt_list_new();
    if (!out) {
        module_set_error("Out of memory");
        return 0;
    }

    ASTStmt* st = program->head;
    while (st) {
        ASTStmt* next = st->next;
        st->next = NULL;

        if (st->kind == AST_STMT_IMPORT) {
            Module* imported = module_load(st->as.import.module_path, NULL);
            if (!imported) {
                ast_free_stmt(st);
                ast_free_stmt_list(out);
                return 0;
            }
            
            if (!module_preprocess_module(imported)) {
                ast_free_stmt(st);
                ast_free_stmt_list(out);
                return 0;
            }
            
            if (!imported->default_export_name || !imported->default_export_type_desc) {
                module_set_error("Module '%s' has no default export", imported->name);
                ast_free_stmt(st);
                ast_free_stmt_list(out);
                return 0;
            }

            if (!imported->is_included) {
                if (!splice_module_into_program(out, imported)) {
                    ast_free_stmt(st);
                    ast_free_stmt_list(out);
                    return 0;
                }
            }

            if (st->as.import.alias) {
                ASTStmt* decl = ast_stmt_new(AST_STMT_VAR_DECL, st->loc);
                if (!decl) {
                    module_set_error("Out of memory");
                    ast_free_stmt(st);
                    ast_free_stmt_list(out);
                    return 0;
                }

                decl->as.var_decl.var_name = strdup(st->as.import.alias);
                decl->as.var_decl.type = imported->default_export_type;
                decl->as.var_decl.type_desc = 
                    type_descriptor_clone(imported->default_export_type_desc);
                decl->as.var_decl.init = ast_expr_new(AST_EXPR_VAR, st->loc);
                decl->as.var_decl.is_const = 1;
                
                if (!decl->as.var_decl.var_name || 
                    !decl->as.var_decl.type_desc || 
                    !decl->as.var_decl.init) {
                    module_set_error("Out of memory");
                    ast_free_stmt(decl);
                    ast_free_stmt(st);
                    ast_free_stmt_list(out);
                    return 0;
                }
                
                decl->as.var_decl.init->as.var_name = 
                    strdup(imported->default_export_name);
                
                if (!decl->as.var_decl.init->as.var_name) {
                    module_set_error("Out of memory");
                    ast_free_stmt(decl);
                    ast_free_stmt(st);
                    ast_free_stmt_list(out);
                    return 0;
                }

                ast_stmt_list_add(out, decl);
            }

            ast_free_stmt(st);
            st = next;
            continue;
        }

        if (st->kind == AST_STMT_EXPORT) {
            ast_free_stmt(st);
            st = next;
            continue;
        }

        ast_stmt_list_append_stmt(out, st);
        st = next;
    }

    program->head = out->head;
    program->tail = out->tail;
    free(out);
    return 1;
}

int process_import_statement(const ASTStmtImport* import_stmt, const char* current_file_dir) {
    if (!import_stmt || !import_stmt->module_path) {
        module_set_error("Invalid import statement");
        return 0;
    }
    
    Module* module = module_load(import_stmt->module_path, current_file_dir);
    if (!module) {
        return 0; // Error already set
    }
    
    if (!module_compile(module)) {
        return 0; // Error already set
    }
    
    if (import_stmt->is_selective) {
        // Selective import: import { symbol1, symbol2 } from "module"
        for (int i = 0; i < import_stmt->symbol_count; i++) {
            const char* symbol_name = import_stmt->symbol_names[i];
            const char* symbol_alias = import_stmt->symbol_aliases[i];
            
            ModuleSymbol* exported_symbol = module_find_export(module, symbol_name);
            if (!exported_symbol) {
                module_set_error("Symbol '%s' is not exported by module '%s'", 
                               symbol_name, module->name);
                return 0;
            }
            
            const char* local_name = symbol_alias ? symbol_alias : symbol_name;
            if (!module_register_qualified_symbol(module->name, local_name, 
                                                 exported_symbol->type, exported_symbol->symbol_ptr)) {
                return 0;
            }
        }
    } else {
        const char* module_alias = import_stmt->alias ? import_stmt->alias : module->name;
        
        ModuleSymbol* current = module->exports;
        while (current) {
            if (!module_register_qualified_symbol(module_alias, current->name, 
                                                 current->type, current->symbol_ptr)) {
                return 0;
            }
            current = current->next;
        }
    }
    
    return 1;
}

int process_export_statement(const ASTStmtExport* export_stmt, Module* current_module) {
    if (!export_stmt || !current_module) {
        module_set_error("Invalid export statement or module");
        return 0;
    }
    
    if (export_stmt->is_default && export_stmt->symbol_count != 1) {
        module_set_error("Default export must specify exactly one symbol");
        return 0;
    }
    
    for (int i = 0; i < export_stmt->symbol_count; i++) {
        const char* symbol_name = export_stmt->symbol_names[i];
        const char* symbol_alias = export_stmt->symbol_aliases ? export_stmt->symbol_aliases[i] : NULL;
        if (!module_is_valid_identifier(symbol_name)) {
            module_set_error("Invalid symbol name for export: '%s'", symbol_name);
            return 0;
        }

         void* symbol_ptr = NULL;
         ModuleSymbolType symbol_type = MODULE_SYMBOL_VARIABLE;

         const Function* fn = get_function(symbol_name);
         if (fn) {
             symbol_type = MODULE_SYMBOL_FUNCTION;
             symbol_ptr = (void*)fn;
         } else {
             Variable* var = get_variable(symbol_name);
             if (var) {
                 symbol_type = MODULE_SYMBOL_VARIABLE;
                 symbol_ptr = (void*)var;
             } else {
                 BreadClass* class_def = bread_class_find_definition(symbol_name);
                 if (class_def) {
                     symbol_type = MODULE_SYMBOL_CLASS;
                     symbol_ptr = (void*)class_def;
                 }
             }
         }

         if (!symbol_ptr) {
             module_set_error("Cannot export unknown symbol '%s'", symbol_name);
             return 0;
         }

         if (!module_add_export(current_module, symbol_name, symbol_alias, symbol_type, symbol_ptr, 
                                export_stmt->is_default ? 1 : 0)) {
             return 0;
         }
    }
    
    return 1;
}
