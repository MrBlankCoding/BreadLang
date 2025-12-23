#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "codegen/codegen.h"
#include "core/value.h"
#include "runtime/runtime.h"
#include "runtime/value_ops.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

// Bridge between LLVM codegen and runtime class objects
// This connects generated LLVM functions to the runtime class system for full JIT compilation

// Global JIT execution engine for compiled methods
static LLVMExecutionEngineRef g_execution_engine = NULL;
static LLVMModuleRef g_current_module = NULL;

// Registry to track compiled method information
typedef struct CompiledMethodInfo {
    char* class_name;
    char* method_name;
    char* llvm_function_name;
    BreadCompiledMethod compiled_fn;
    struct CompiledMethodInfo* next;
} CompiledMethodInfo;

static CompiledMethodInfo* g_compiled_methods = NULL;

// Set the current module and execution engine for JIT compilation
void cg_set_jit_module(LLVMModuleRef module, LLVMExecutionEngineRef engine) {
    g_current_module = module;
    g_execution_engine = engine;
}

// Get function pointer from LLVM function using execution engine
static BreadCompiledMethod get_function_pointer(const char* function_name) {
    if (!function_name || !g_execution_engine) return NULL;
    
    // Get function pointer from execution engine
    uint64_t fn_addr = LLVMGetFunctionAddress(g_execution_engine, function_name);
    if (!fn_addr) {
        fprintf(stderr, "JIT: Failed to get function address for '%s'\n", function_name);
        return NULL;
    }
    
    printf("JIT: Successfully compiled function '%s' at address 0x%llx\n", function_name, fn_addr);
    return (BreadCompiledMethod)fn_addr;
}

// Convert LLVM function to runtime function pointer
static BreadCompiledMethod llvm_to_runtime_method(LLVMValueRef llvm_fn, const char* class_name, const char* method_name) {
    if (!llvm_fn || !class_name || !method_name) return NULL;
    
    // Get the function name from LLVM
    const char* llvm_name = LLVMGetValueName(llvm_fn);
    if (!llvm_name || strlen(llvm_name) == 0) {
        fprintf(stderr, "JIT: Function has no name, cannot compile %s::%s\n", class_name, method_name);
        return NULL;
    }
    
    // Get function pointer from execution engine
    BreadCompiledMethod compiled_fn = get_function_pointer(llvm_name);
    
    if (compiled_fn) {
        // Store in registry for tracking
        CompiledMethodInfo* info = malloc(sizeof(CompiledMethodInfo));
        if (info) {
            info->class_name = strdup(class_name);
            info->method_name = strdup(method_name);
            info->llvm_function_name = strdup(llvm_name);
            info->compiled_fn = compiled_fn;
            info->next = g_compiled_methods;
            g_compiled_methods = info;
            
            printf("JIT: Registered compiled method %s::%s -> %s\n", class_name, method_name, llvm_name);
        }
    }
    
    return compiled_fn;
}

// Execute compiled method with proper argument conversion
int cg_execute_compiled_method(BreadCompiledMethod compiled_fn, BreadClass* self, int argc, const BreadValue* args, BreadValue* out) {
    if (!compiled_fn || !self || !out) return 0;
    
    // Prepare return slot
    BreadValue ret_slot;
    bread_value_set_nil(&ret_slot);
    
    // Convert arguments to LLVM calling convention
    void** llvm_args = NULL;
    if (argc > 0) {
        llvm_args = malloc(argc * sizeof(void*));
        if (!llvm_args) return 0;
        
        for (int i = 0; i < argc; i++) {
            llvm_args[i] = (void*)&args[i];
        }
    }
    
    // Call the compiled function
    // The function signature is: void fn(void* ret_slot, void* self_ptr, void** args)
    compiled_fn(&ret_slot, self, llvm_args);
    
    // Copy result
    *out = bread_value_clone(ret_slot);
    
    // Cleanup
    bread_value_release(&ret_slot);
    free(llvm_args);
    
    return 1;
}

// Connect codegen class to runtime class - JIT ONLY
int cg_connect_class_to_runtime(Cg* cg, CgClass* cg_class, BreadClass* runtime_class) {
    if (!cg || !cg_class || !runtime_class) return 0;
    
    printf("JIT: Connecting class '%s' to runtime\n", cg_class->name);
    
    // Check if we have a JIT execution engine available
    if (!g_execution_engine) {
        printf("JIT: No execution engine available, skipping JIT connection for class '%s'\n", cg_class->name);
        return 1; // Return success but don't set up JIT function pointers
    }
    
    // Connect constructor - MUST be compiled, no fallback
    if (cg_class->constructor && cg_class->method_functions) {
        for (int i = 0; i < cg_class->method_count; i++) {
            if (cg_class->method_names[i] && strcmp(cg_class->method_names[i], "init") == 0) {
                LLVMValueRef constructor_fn = cg_class->method_functions[i];
                if (constructor_fn) {
                    BreadCompiledMethod compiled_constructor = llvm_to_runtime_method(
                        constructor_fn, cg_class->name, "init");
                    if (compiled_constructor) {
                        bread_class_set_compiled_constructor(runtime_class, compiled_constructor);
                        printf("JIT: Connected constructor for class '%s'\n", cg_class->name);
                    } else {
                        fprintf(stderr, "JIT ERROR: Failed to compile constructor for class '%s'\n", cg_class->name);
                        return 0;
                    }
                }
                break;
            }
        }
    }
    
    // Connect methods - MUST be compiled, no fallback
    int methods_connected = 0;
    for (int i = 0; i < cg_class->method_count; i++) {
        if (cg_class->method_functions && cg_class->method_functions[i]) {
            LLVMValueRef method_fn = cg_class->method_functions[i];
            BreadCompiledMethod compiled_method = llvm_to_runtime_method(
                method_fn, cg_class->name, cg_class->method_names[i]);
            
            // Find corresponding method index in runtime class
            int runtime_method_index = bread_class_find_method_index(runtime_class, cg_class->method_names[i]);
            if (runtime_method_index >= 0 && compiled_method) {
                bread_class_set_compiled_method(runtime_class, runtime_method_index, compiled_method);
                methods_connected++;
                printf("JIT: Connected method '%s::%s'\n", cg_class->name, cg_class->method_names[i]);
            } else if (strcmp(cg_class->method_names[i], "init") != 0) {
                // Don't error on constructor, it's handled separately
                fprintf(stderr, "JIT ERROR: Failed to compile method '%s::%s'\n", cg_class->name, cg_class->method_names[i]);
            }
        }
    }
    
    printf("JIT: Connected %d methods for class '%s'\n", methods_connected, cg_class->name);
    return 1;
}

// Global class registry for runtime classes
typedef struct ClassRegistryEntry {
    char* class_name;
    BreadClass* runtime_class;
    struct ClassRegistryEntry* next;
} ClassRegistryEntry;

static ClassRegistryEntry* g_class_registry = NULL;

// Register a runtime class
static void register_runtime_class(const char* class_name, BreadClass* runtime_class) {
    if (!class_name || !runtime_class) return;
    
    ClassRegistryEntry* entry = malloc(sizeof(ClassRegistryEntry));
    if (!entry) return;
    
    entry->class_name = strdup(class_name);
    entry->runtime_class = runtime_class;
    entry->next = g_class_registry;
    g_class_registry = entry;
    
    printf("JIT: Registered runtime class '%s'\n", class_name);
}

// Find runtime class by name
static BreadClass* find_runtime_class(const char* class_name) {
    if (!class_name) return NULL;
    
    for (ClassRegistryEntry* entry = g_class_registry; entry; entry = entry->next) {
        if (strcmp(entry->class_name, class_name) == 0) {
            return entry->runtime_class;
        }
    }
    return NULL;
}

// Connect all codegen classes to runtime - JIT ONLY
int cg_connect_all_classes_to_runtime(Cg* cg) {
    if (!cg) return 0;
    
    printf("JIT: Connecting all classes to runtime\n");
    
    if (!g_execution_engine) {
        printf("JIT: No execution engine available - running in executable mode\n");
        printf("JIT: Classes will use direct function calls instead of JIT compilation\n");
        return 1; // Return success - executable mode doesn't need JIT connection
    }
    
    printf("JIT: Running in JIT mode with execution engine\n");
    
    // Iterate through all codegen classes
    int classes_connected = 0;
    for (CgClass* cg_class = cg->classes; cg_class; cg_class = cg_class->next) {
        // Check if runtime class already exists
        BreadClass* runtime_class = find_runtime_class(cg_class->name);
        
        if (!runtime_class) {
            // Create runtime class with collected fields (including inherited)
            char** all_field_names;
            int total_field_count;
            
            if (cg_collect_all_fields(cg, cg_class, &all_field_names, &total_field_count)) {
                runtime_class = bread_class_new_with_methods(
                    cg_class->name,
                    cg_class->parent_name,
                    total_field_count,
                    all_field_names,
                    cg_class->method_count,
                    cg_class->method_names
                );
                
                // Free temporary field names array
                for (int i = 0; i < total_field_count; i++) {
                    free(all_field_names[i]);
                }
                free(all_field_names);
            } else {
                // Fallback to class's own fields only
                runtime_class = bread_class_new_with_methods(
                    cg_class->name,
                    cg_class->parent_name,
                    cg_class->field_count,
                    cg_class->field_names,
                    cg_class->method_count,
                    cg_class->method_names
                );
            }
            
            if (runtime_class) {
                register_runtime_class(cg_class->name, runtime_class);
            }
        }
        
        if (runtime_class) {
            // Connect the codegen class to runtime class
            if (cg_connect_class_to_runtime(cg, cg_class, runtime_class)) {
                classes_connected++;
            } else {
                fprintf(stderr, "JIT ERROR: Failed to connect class '%s'\n", cg_class->name);
                return 0;
            }
        }
    }
    
    printf("JIT: Successfully connected %d classes to runtime\n", classes_connected);
    return 1;
}

// Execute LLVM function with BreadValue arguments - JIT ONLY
int cg_execute_llvm_method(LLVMValueRef llvm_fn __attribute__((unused)), 
                           BreadClass* self __attribute__((unused)), 
                           int argc __attribute__((unused)), 
                           const BreadValue* args __attribute__((unused)), 
                           BreadValue* out) {
    if (!out) return 0;
    
    // This function is deprecated in JIT-only mode
    // All methods should be pre-compiled and called via compiled method pointers
    fprintf(stderr, "JIT ERROR: Direct LLVM function execution not supported in JIT-only mode\n");
    bread_value_set_nil(out);
    return 0;
}

// Create runtime method wrapper for LLVM function - DEPRECATED in JIT-only mode
BreadMethod cg_create_runtime_method_wrapper(LLVMValueRef llvm_fn __attribute__((unused))) {
    // This is deprecated in JIT-only mode
    fprintf(stderr, "JIT ERROR: Runtime method wrappers not supported in JIT-only mode\n");
    return NULL;
}

// Get runtime class by name (public interface)
BreadClass* cg_get_runtime_class(const char* class_name) {
    return find_runtime_class(class_name);
}

// Check if JIT compilation is available
int cg_is_jit_available() {
    return g_execution_engine != NULL;
}

// Cleanup class registry
void cg_cleanup_class_registry() {
    ClassRegistryEntry* current = g_class_registry;
    while (current) {
        ClassRegistryEntry* next = current->next;
        free(current->class_name);
        // Note: Don't free runtime_class here as it's managed elsewhere
        free(current);
        current = next;
    }
    g_class_registry = NULL;
}

// Cleanup JIT engine
void cg_cleanup_jit_engine() {
    // Free compiled method registry
    CompiledMethodInfo* current = g_compiled_methods;
    while (current) {
        CompiledMethodInfo* next = current->next;
        free(current->class_name);
        free(current->method_name);
        free(current->llvm_function_name);
        free(current);
        current = next;
    }
    g_compiled_methods = NULL;
    
    // Don't dispose execution engine here if it was set externally
    // The caller (LLVM backend) will handle cleanup
    g_execution_engine = NULL;
    g_current_module = NULL;
    
    printf("JIT: Cleaned up JIT engine\n");
}