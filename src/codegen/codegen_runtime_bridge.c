#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "codegen/codegen.h"
#include "core/value.h"
#include "runtime/runtime.h"
#include "runtime/value_ops.h"

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

// LLVM codegen and bread runtime bridge. 
static LLVMExecutionEngineRef g_execution_engine = NULL;
static LLVMModuleRef g_current_module = NULL;
typedef struct CompiledMethodInfo {
    char* class_name;
    char* method_name;
    char* llvm_function_name;
    BreadCompiledMethod compiled_fn;
    struct CompiledMethodInfo* next;
} CompiledMethodInfo;

static CompiledMethodInfo* g_compiled_methods = NULL;

void cg_set_jit_module(LLVMModuleRef module, LLVMExecutionEngineRef engine) {
    g_current_module = module;
    g_execution_engine = engine;
}

static BreadCompiledMethod get_function_pointer(const char* function_name) {
    if (!function_name || !g_execution_engine) return NULL;

    uint64_t fn_addr = LLVMGetFunctionAddress(g_execution_engine, function_name);
    if (!fn_addr) {
        fprintf(stderr, "JIT: Failed to get function address for '%s'\n", function_name);
        return NULL;
    }

    printf("JIT: Compiled '%s' @ 0x%llx\n", function_name, fn_addr);
    return (BreadCompiledMethod)fn_addr;
}

static BreadCompiledMethod llvm_to_runtime_method(
    LLVMValueRef llvm_fn,
    const char* class_name,
    const char* method_name
) {
    if (!llvm_fn || !class_name || !method_name) return NULL;

    const char* llvm_name = LLVMGetValueName(llvm_fn);
    if (!llvm_name || llvm_name[0] == '\0') {
        fprintf(stderr,
                "JIT: Function has no name (%s::%s)\n",
                class_name, method_name);
        return NULL;
    }

    BreadCompiledMethod compiled_fn = get_function_pointer(llvm_name);
    if (!compiled_fn) return NULL;

    CompiledMethodInfo* info = malloc(sizeof(*info));
    if (!info) return NULL;

    info->class_name = strdup(class_name);
    info->method_name = strdup(method_name);
    info->llvm_function_name = strdup(llvm_name);
    info->compiled_fn = compiled_fn;
    info->next = g_compiled_methods;
    g_compiled_methods = info;

    printf("JIT: Registered %s::%s -> %s\n",
           class_name, method_name, llvm_name);

    return compiled_fn;
}

int cg_execute_compiled_method(
    BreadCompiledMethod compiled_fn,
    BreadClass* self,
    int argc,
    const BreadValue* args,
    BreadValue* out
) {
    if (!compiled_fn || !self || !out) return 0;

    BreadValue ret;
    BreadValue self_val;

    bread_value_set_nil(&ret);
    bread_value_set_class(&self_val, self);

    switch (argc) {
        case 0:
            ((void(*)(void*, void*))compiled_fn)(&ret, &self_val);
            break;
        case 1:
            ((void(*)(void*, void*, void*))compiled_fn)(
                &ret, &self_val, (void*)&args[0]);
            break;
        case 2:
            ((void(*)(void*, void*, void*, void*))compiled_fn)(
                &ret, &self_val,
                (void*)&args[0], (void*)&args[1]);
            break;
        case 3:
            ((void(*)(void*, void*, void*, void*, void*))compiled_fn)(
                &ret, &self_val,
                (void*)&args[0], (void*)&args[1], (void*)&args[2]);
            break;
        default:
            bread_value_set_nil(out);
            bread_value_release(&ret);
            bread_value_release(&self_val);
            return 1;
    }

    *out = bread_value_clone(ret);
    bread_value_release(&ret);
    bread_value_release(&self_val);
    return 1;
}

typedef struct ClassRegistryEntry {
    char* class_name;
    BreadClass* runtime_class;
    struct ClassRegistryEntry* next;
} ClassRegistryEntry;

static ClassRegistryEntry* g_class_registry = NULL;

static void register_runtime_class(const char* name, BreadClass* cls) {
    ClassRegistryEntry* entry = malloc(sizeof(*entry));
    if (!entry) return;

    entry->class_name = strdup(name);
    entry->runtime_class = cls;
    entry->next = g_class_registry;
    g_class_registry = entry;

    bread_class_register_definition(cls);
    printf("JIT: Registered runtime class '%s'\n", name);
}

static BreadClass* find_runtime_class(const char* name) {
    for (ClassRegistryEntry* e = g_class_registry; e; e = e->next) {
        if (strcmp(e->class_name, name) == 0)
            return e->runtime_class;
    }
    return NULL;
}

int cg_connect_class_to_runtime(
    Cg* cg,
    CgClass* cg_class,
    BreadClass* runtime_class
) {
    if (!cg || !cg_class || !runtime_class) return 0;

    printf("JIT: Connecting class '%s'\n", cg_class->name);

    if (!g_execution_engine) {
        printf("JIT: No execution engine — skipping\n");
        return 1;
    }

    if (cg_class->constructor && cg_class->constructor_function) {
        BreadCompiledMethod ctor =
            llvm_to_runtime_method(
                cg_class->constructor_function,
                cg_class->name,
                "init");

        if (!ctor) {
            fprintf(stderr,
                    "JIT ERROR: Failed to compile %s::init\n",
                    cg_class->name);
            return 0;
        }

        bread_class_set_compiled_constructor(runtime_class, ctor);
    }

    int connected = 0;
    for (int i = 0; i < cg_class->method_count; i++) {
        LLVMValueRef fn = cg_class->method_functions[i];
        if (!fn) continue;

        BreadCompiledMethod compiled =
            llvm_to_runtime_method(
                fn,
                cg_class->name,
                cg_class->method_names[i]);

        int idx =
            bread_class_find_method_index(
                runtime_class,
                cg_class->method_names[i]);

        if (idx >= 0 && compiled) {
            bread_class_set_compiled_method(runtime_class, idx, compiled);
            connected++;
        }
    }

    printf("JIT: %d methods connected for '%s'\n",
           connected, cg_class->name);
    return 1;
}

int cg_connect_all_classes_to_runtime(Cg* cg) {
    if (!cg) return 0;

    printf("JIT: Connecting all classes\n");
    fflush(stdout);

    CgClass* classes = cg->classes;
    printf("JIT: cg->classes=%p\n", (void*)classes);
    fflush(stdout);
    fprintf(stderr, "JIT: cg->classes=%p\n", (void*)classes);
    if (classes && (uintptr_t)classes < 4096) {
        fprintf(stderr, "JIT ERROR: Invalid cg->classes pointer\n");
        return 0;
    }

    for (CgClass* cls = classes; cls; cls = cls->next) {
        if ((uintptr_t)cls < 4096) {
            fprintf(stderr, "JIT ERROR: Invalid CgClass pointer\n");
            return 0;
        }
        if (!cls->name || (uintptr_t)cls->name < 4096) {
            fprintf(stderr, "JIT ERROR: Invalid CgClass name pointer\n");
            return 0;
        }
        BreadClass* runtime = find_runtime_class(cls->name);

        if (!runtime) {
            runtime =
                bread_class_new_with_methods(
                    cls->name,
                    cls->parent_name,
                    cls->field_count,
                    cls->field_names,
                    cls->method_count,
                    cls->method_names);

            if (!runtime) return 0;
            register_runtime_class(cls->name, runtime);
        }

        if (!cg_connect_class_to_runtime(cg, cls, runtime))
            return 0;
    }

    bread_class_resolve_inheritance();
    return 1;
}

int cg_is_jit_available(void) {
    return g_execution_engine != NULL;
}

BreadClass* cg_get_runtime_class(const char* name) {
    return find_runtime_class(name);
}

void cg_cleanup_class_registry(void) {
    while (g_class_registry) {
        ClassRegistryEntry* next = g_class_registry->next;
        free(g_class_registry->class_name);
        free(g_class_registry);
        g_class_registry = next;
    }
}

void cg_cleanup_jit_engine(void) {
    while (g_compiled_methods) {
        CompiledMethodInfo* next = g_compiled_methods->next;
        free(g_compiled_methods->class_name);
        free(g_compiled_methods->method_name);
        free(g_compiled_methods->llvm_function_name);
        free(g_compiled_methods);
        g_compiled_methods = next;
    }

    g_execution_engine = NULL;
    g_current_module = NULL;

    printf("JIT: Engine cleaned up\n");
}
