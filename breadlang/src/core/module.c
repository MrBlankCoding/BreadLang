#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>

#include "core/module.h"
#include "compiler/ast/ast.h"
#include "runtime/error.h"
#include "runtime/memory.h"

static ModuleRegistry* g_module_registry = NULL;
char* module_last_error = NULL;

void module_system_init(void) {
    if (g_module_registry) {
        return; // Already initialized
    }
    
    g_module_registry = (ModuleRegistry*)malloc(sizeof(ModuleRegistry));
    if (!g_module_registry) {
        module_set_error("Failed to allocate module registry");
        return;
    }
    
    g_module_registry->modules = NULL;
    g_module_registry->search_paths = NULL;
    g_module_registry->search_path_count = 0;
    
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
    
    for (int i = 0; i < g_module_registry->search_path_count; i++) {
        free(g_module_registry->search_paths[i]);
    }
    free(g_module_registry->search_paths);
    
    free(g_module_registry);
    g_module_registry = NULL;
    
    free(module_last_error);
    module_last_error = NULL;
}

void module_set_error(const char* format, ...) {
    free(module_last_error);
    
    va_list args;
    va_start(args, format);
    
    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (size < 0) {
        module_last_error = NULL;
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

void module_add_search_path(const char* path) {
    if (!g_module_registry || !path) {
        return;
    }
    
    char** new_paths = (char**)realloc(g_module_registry->search_paths, 
                                      (g_module_registry->search_path_count + 1) * sizeof(char*));
    if (!new_paths) {
        module_set_error("Failed to allocate memory for search path");
        return;
    }
    
    g_module_registry->search_paths = new_paths;
    g_module_registry->search_paths[g_module_registry->search_path_count] = strdup(path);
    g_module_registry->search_path_count++;
}

char* module_resolve_path(const char* module_path, const char* current_file_dir) {
    if (!module_path) {
        module_set_error("Module path is NULL");
        return NULL;
    }
    
    char* resolved_path = NULL;
    struct stat st;
    
    if (module_path[0] == '/') {
        if (stat(module_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(module_path);
        }
        
        char* with_ext = (char*)malloc(strlen(module_path) + 7);
        sprintf(with_ext, "%s.bread", module_path);
        if (stat(with_ext, &st) == 0 && S_ISREG(st.st_mode)) {
            return with_ext;
        }
        free(with_ext);
        
        module_set_error("Module file not found: %s", module_path);
        return NULL;
    }
    
    if (current_file_dir) {
        char* relative_path = (char*)malloc(strlen(current_file_dir) + strlen(module_path) + 2);
        sprintf(relative_path, "%s/%s", current_file_dir, module_path);
        
        if (stat(relative_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return relative_path;
        }
        
        char* with_ext = (char*)malloc(strlen(relative_path) + 7);
        sprintf(with_ext, "%s.bread", relative_path);
        if (stat(with_ext, &st) == 0 && S_ISREG(st.st_mode)) {
            free(relative_path);
            return with_ext;
        }
        
        free(relative_path);
        free(with_ext);
    }
    
    for (int i = 0; i < g_module_registry->search_path_count; i++) {
        char* search_path = (char*)malloc(strlen(g_module_registry->search_paths[i]) + 
                                         strlen(module_path) + 2);
        sprintf(search_path, "%s/%s", g_module_registry->search_paths[i], module_path);
        
        if (stat(search_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return search_path;
        }
        
        char* with_ext = (char*)malloc(strlen(search_path) + 7);
        sprintf(with_ext, "%s.bread", search_path);
        if (stat(with_ext, &st) == 0 && S_ISREG(st.st_mode)) {
            free(search_path);
            return with_ext;
        }
        
        free(search_path);
        free(with_ext);
    }
    
    module_set_error("Module file not found: %s", module_path);
    return NULL;
}

char* module_get_name_from_path(const char* file_path) {
    if (!file_path) {
        return NULL;
    }
    
    char* path_copy = strdup(file_path);
    char* base_name = basename(path_copy);
    
    char* dot = strrchr(base_name, '.');
    if (dot && strcmp(dot, ".bread") == 0) {
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
    
    if (!isalpha(*name) && *name != '_') {
        return 0;
    }
    
    for (const char* p = name + 1; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
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
    
    Module* module = (Module*)malloc(sizeof(Module));
    if (!module) {
        free(resolved_path);
        module_set_error("Failed to allocate module");
        return NULL;
    }
    
    module->name = module_get_name_from_path(resolved_path);
    module->file_path = strdup(module_path);
    module->resolved_path = resolved_path;
    module->ast = NULL;
    module->exports = NULL;
    module->dependencies = NULL;
    module->is_compiled = 0;
    module->is_loading = 0;
    module->next = NULL;
    
    module->next = g_module_registry->modules;
    g_module_registry->modules = module;
    
    return module;
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
    
    ModuleSymbol* symbol = (ModuleSymbol*)malloc(sizeof(ModuleSymbol));
    if (!symbol) {
        module_set_error("Failed to allocate module symbol");
        return 0;
    }
    
    symbol->name = strdup(symbol_name);
    symbol->alias = alias ? strdup(alias) : NULL;
    symbol->type = type;
    symbol->symbol_ptr = symbol_ptr;
    symbol->is_default = is_default;
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
    
    FILE* file = fopen(module->resolved_path, "r");
    if (!file) {
        module_set_error("Failed to open module file: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        module_set_error("Module file is empty: %s", module->resolved_path);
        module->is_loading = 0;
        return 0;
    }
    
    char* source = (char*)malloc(file_size + 1);
    if (!source) {
        fclose(file);
        module_set_error("Failed to allocate memory for module source");
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
    
    module->ast = ast_parse_program(source);
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
            char* qualified_name = (char*)malloc(strlen(module_alias) + strlen(current->name) + 2);
            sprintf(qualified_name, "%s.%s", module_alias, current->name);
            
            if (!module_register_qualified_symbol(module_alias, current->name, 
                                                 current->type, current->symbol_ptr)) {
                free(qualified_name);
                return 0;
            }
            
            free(qualified_name);
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
    
    // TODO: This would need to be integrated with the symbol resolution system
    // For now, we'll just validate the export statement structure
    
    if (export_stmt->is_default && export_stmt->symbol_count != 1) {
        module_set_error("Default export must specify exactly one symbol");
        return 0;
    }
    
    for (int i = 0; i < export_stmt->symbol_count; i++) {
        const char* symbol_name = export_stmt->symbol_names[i];
        if (!module_is_valid_identifier(symbol_name)) {
            module_set_error("Invalid symbol name for export: '%s'", symbol_name);
            return 0;
        }
    }
    
    return 1;
}

void* module_lookup_symbol(const char* qualified_name, ModuleSymbolType* out_type) {
    // TODO: Implement symbol lookup with module qualification
    // This would integrate with the existing symbol tables
    module_set_error("Module symbol lookup not yet implemented");
    return NULL;
}

int module_register_qualified_symbol(const char* module_name, const char* symbol_name, 
                                    ModuleSymbolType type, void* symbol_ptr) {
    // TODO: Implement qualified symbol registration
    // This would integrate with the existing symbol tables
    module_set_error("Module symbol registration not yet implemented");
    return 0;
}