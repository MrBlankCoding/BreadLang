#include "codegen_internal.h"

CgVar* cg_scope_find_var(CgScope* scope, const char* name) {
    for (CgScope* s = scope; s; s = s->parent) {
        for (CgVar* v = s->vars; v; v = v->next) {
            if (strcmp(v->name, name) == 0) {
                return v;
            }
        }
    }
    return NULL;
}

CgVar* cg_scope_add_var(CgScope* scope, const char* name, LLVMValueRef alloca) {
    CgVar* v = (CgVar*)malloc(sizeof(CgVar));
    v->name = strdup(name);
    v->alloca = alloca;
    v->type = TYPE_NIL;
    v->type_desc = NULL;
    v->is_const = 0;
    v->is_initialized = 0;
    v->next = scope->vars;
    scope->vars = v;
    return v;
}
