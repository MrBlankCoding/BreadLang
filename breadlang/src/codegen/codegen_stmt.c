#include "codegen_internal.h"
#include "core/type_descriptor.h"

static int handle_unboxed_var_assign(Cg* cg, CgFunction* cg_fn, CgVar* var, ASTExpr* value_expr) {
    CgValue value_unboxed = cg_build_expr_unboxed(cg, cg_fn, value_expr);
    
    if (value_unboxed.type != CG_VALUE_BOXED) {
        LLVMBuildStore(cg->builder, value_unboxed.value, var->alloca);
    } else {
        CgValue unboxed = cg_unbox_value(cg, value_unboxed.value, var->type);
        LLVMBuildStore(cg->builder, unboxed.value, var->alloca);
    }
    return 1;
}

static int handle_boxed_var_assign(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef value = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_assign.value);
    if (!value) return 0;
    
    if (cg_fn) {
        CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.var_assign.var_name);
        if (!var) {
            LLVMValueRef slot = cg_alloc_value(cg, stmt->as.var_assign.var_name);
            cg_copy_value_into(cg, slot, value);
            cg_scope_add_var(cg_fn->scope, stmt->as.var_assign.var_name, slot);
        } else {
            cg_copy_value_into(cg, var->alloca, value);
        }
    } else {
        LLVMValueRef name_ptr = cg_get_string_ptr(cg, stmt->as.var_assign.var_name);
        if (!name_ptr) {
            return 0;
        }
        LLVMValueRef args[] = {name_ptr, cg_value_to_i8_ptr(cg, value)};
        LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, args, 2, "");
    }
    return 1;
}

static LLVMValueRef get_condition_bool(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTExpr* condition) {
    CgValue cond_val = cg_build_expr_unboxed(cg, cg_fn, condition);
    
    if (cond_val.type == CG_VALUE_UNBOXED_BOOL) {
        return cond_val.value;
    }
    
    LLVMValueRef cond = (cond_val.type == CG_VALUE_BOXED && cond_val.value) 
        ? cond_val.value 
        : cg_build_expr(cg, cg_fn, val_size, condition);
    
    if (!cond) return NULL;
    
    LLVMValueRef truthy_args[] = {cg_value_to_i8_ptr(cg, cond)};
    LLVMValueRef is_truthy = LLVMBuildCall2(cg->builder, cg->ty_is_truthy, cg->fn_is_truthy, truthy_args, 1, "");
    return LLVMBuildICmp(cg->builder, LLVMIntNE, is_truthy, LLVMConstInt(cg->i32, 0, 0), "");
}

static void push_scope_if_global(Cg* cg, CgFunction* cg_fn, LLVMValueRef* out_base) {
    if (!cg_fn) {
        *out_base = LLVMBuildCall2(cg->builder, cg->ty_scope_depth, cg->fn_scope_depth, NULL, 0, "");
        LLVMBuildCall2(cg->builder, cg->ty_push_scope, cg->fn_push_scope, NULL, 0, "");
    }
}

static void pop_scope_if_needed(Cg* cg, CgFunction* cg_fn, LLVMValueRef base_depth) {
    if (!cg_fn && base_depth && !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        LLVMValueRef pop_args[] = {base_depth};
        LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
    }
}

static LLVMTypeRef get_unboxed_alloc_type(Cg* cg, UnboxedType unboxed_type) {
    switch (unboxed_type) {
        case UNBOXED_INT: return cg->i64;
        case UNBOXED_DOUBLE: return cg->f64;
        case UNBOXED_BOOL: return cg->i1;
        default: return cg->value_type;
    }
}

static void store_unboxed_value(Cg* cg, LLVMValueRef slot, CgValue init_val, VarType var_type) {
    if (init_val.type != CG_VALUE_BOXED) {
        LLVMBuildStore(cg->builder, init_val.value, slot);
    } else {
        CgValue unboxed = cg_unbox_value(cg, init_val.value, var_type);
        LLVMBuildStore(cg->builder, unboxed.value, slot);
    }
}

static void declare_var_if_missing(Cg* cg, const char* var_name, VarType var_type, int is_const, LLVMValueRef slot) {
    LLVMValueRef name_ptr = cg_get_string_ptr(cg, var_name);
    LLVMValueRef type = LLVMConstInt(cg->i32, var_type, 0);
    LLVMValueRef is_const_val = LLVMConstInt(cg->i32, is_const, 0);
    LLVMValueRef decl_args[] = {name_ptr, type, is_const_val, cg_value_to_i8_ptr(cg, slot)};
    LLVMBuildCall2(cg->builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");
}

static void init_var_metadata(CgVar* var, ASTStmt* stmt, UnboxedType unboxed_type) {
    if (!var) return;
    
    var->type = stmt->as.var_decl.type;
    var->type_desc = stmt->as.var_decl.type_desc 
        ? type_descriptor_clone(stmt->as.var_decl.type_desc) 
        : type_descriptor_create_primitive(stmt->as.var_decl.type);
    var->unboxed_type = unboxed_type;
    var->is_const = stmt->as.var_decl.is_const;
    var->is_initialized = 1;
}

static int handle_unboxed_var_decl(Cg* cg, CgFunction* cg_fn, ASTStmt* stmt, UnboxedType unboxed_type) {
    CgValue init_val = cg_build_expr_unboxed(cg, cg_fn, stmt->as.var_decl.init);
    
    LLVMTypeRef alloc_type = get_unboxed_alloc_type(cg, unboxed_type);
    LLVMValueRef slot = LLVMBuildAlloca(cg->builder, alloc_type, stmt->as.var_decl.var_name);
    
    store_unboxed_value(cg, slot, init_val, stmt->as.var_decl.type);
    
    CgVar* var = cg_scope_add_var(cg_fn->scope, stmt->as.var_decl.var_name, slot);
    init_var_metadata(var, stmt, unboxed_type);
    
    LLVMValueRef boxed_slot = cg_alloc_value(cg, stmt->as.var_decl.var_name);
    LLVMValueRef boxed_val = cg_box_value(cg, init_val);
    cg_copy_value_into(cg, boxed_slot, boxed_val);
    
    declare_var_if_missing(cg, stmt->as.var_decl.var_name, stmt->as.var_decl.type, 
                          stmt->as.var_decl.is_const, boxed_slot);
    return 1;
}

static int handle_boxed_var_decl(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef init = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_decl.init);
    if (!init) return 0;

    if (cg_fn) {
        LLVMValueRef slot = cg_alloc_value(cg, stmt->as.var_decl.var_name);
        cg_copy_value_into(cg, slot, init);
        CgVar* var = cg_scope_add_var(cg_fn->scope, stmt->as.var_decl.var_name, slot);
        init_var_metadata(var, stmt, UNBOXED_NONE);
        declare_var_if_missing(cg, stmt->as.var_decl.var_name, stmt->as.var_decl.type, 
                              stmt->as.var_decl.is_const, slot);
    } else {
        const char* var_name = stmt->as.var_decl.var_name;
        size_t name_len = strlen(var_name);
        LLVMTypeRef str_buf_ty = LLVMArrayType(cg->i8, (unsigned)(name_len + 1));
        LLVMValueRef str_buf = LLVMBuildAlloca(cg->builder, str_buf_ty, "var_decl_name_buf");
        LLVMValueRef name_glob = cg_get_string_global(cg, var_name);
        if (!name_glob) {
            return 0;
        }

        LLVMValueRef zero = LLVMConstInt(cg->i32, 0, 0);
        LLVMTypeRef arr_ty = LLVMGlobalGetValueType(name_glob);
        LLVMValueRef glob_ptr = LLVMBuildInBoundsGEP2(cg->builder, arr_ty, name_glob, &zero, 1, "glob_ptr");
        glob_ptr = LLVMBuildBitCast(cg->builder, glob_ptr, cg->i8_ptr, "glob_ptr_cast");
        LLVMValueRef buf_ptr = LLVMBuildInBoundsGEP2(cg->builder, str_buf_ty, str_buf, &zero, 1, "buf_ptr");
        buf_ptr = LLVMBuildBitCast(cg->builder, buf_ptr, cg->i8_ptr, "buf_ptr_cast");
        LLVMBasicBlockRef copy_entry = LLVMGetInsertBlock(cg->builder);
        LLVMBasicBlockRef copy_loop = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(copy_entry), "copy_loop");
        LLVMBasicBlockRef copy_body = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(copy_entry), "copy_body");
        LLVMBasicBlockRef copy_done = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(copy_entry), "copy_done");
        LLVMValueRef idx_slot = LLVMBuildAlloca(cg->builder, cg->i32, "copy_idx");
        LLVMBuildStore(cg->builder, zero, idx_slot);
        LLVMBuildBr(cg->builder, copy_loop);
        LLVMPositionBuilderAtEnd(cg->builder, copy_loop);
        LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i32, idx_slot, "idx");
        LLVMValueRef done = LLVMBuildICmp(cg->builder, LLVMIntUGE, idx, 
            LLVMConstInt(cg->i32, (unsigned)(name_len + 1), 0), "done");
        LLVMBuildCondBr(cg->builder, done, copy_done, copy_body);
        LLVMPositionBuilderAtEnd(cg->builder, copy_body);
        LLVMValueRef idx_i64 = LLVMBuildZExt(cg->builder, idx, cg->i64, "idx_i64");
        LLVMValueRef src_gep = LLVMBuildInBoundsGEP2(cg->builder, cg->i8, glob_ptr, &idx_i64, 1, "src_byte");
        LLVMValueRef dst_gep = LLVMBuildInBoundsGEP2(cg->builder, cg->i8, buf_ptr, &idx_i64, 1, "dst_byte");
        LLVMValueRef byte_val = LLVMBuildLoad2(cg->builder, cg->i8, src_gep, "byte");
        LLVMBuildStore(cg->builder, byte_val, dst_gep);
        LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i32, 1, 0), "next_idx");
        LLVMBuildStore(cg->builder, next_idx, idx_slot);
        LLVMBuildBr(cg->builder, copy_loop);
        LLVMPositionBuilderAtEnd(cg->builder, copy_done);
        LLVMValueRef type = LLVMConstInt(cg->i32, stmt->as.var_decl.type, 0);
        LLVMValueRef is_const = LLVMConstInt(cg->i32, stmt->as.var_decl.is_const, 0);
        LLVMValueRef args[] = {buf_ptr, type, is_const, cg_value_to_i8_ptr(cg, init)};
        LLVMBuildCall2(cg->builder, cg->ty_var_decl, cg->fn_var_decl, args, 4, "");
    }
    return 1;
}

static int parse_range_literal(ASTExpr* expr, int* value) {
    if (!expr || expr->kind != AST_EXPR_INT) {
        fprintf(stderr, "Error: LLVM for-loop currently requires range(Int literal)\n");
        return 0;
    }
    *value = expr->as.int_val;
    return 1;
}

static int parse_range_args(ASTExpr* range_expr, int* start, int* end, int* step) {
    if (!range_expr || range_expr->kind != AST_EXPR_CALL || 
        strcmp(range_expr->as.call.name, "range") != 0) {
        fprintf(stderr, "Error: LLVM for-loop only supports range()\n");
        return 0;
    }
    
    int arg_count = range_expr->as.call.arg_count;
    if (arg_count < 1 || arg_count > 3) {
        fprintf(stderr, "Error: range() expects 1-3 arguments\n");
        return 0;
    }
    
    *start = 0;
    *step = 1;
    
    if (arg_count == 1) {
        return parse_range_literal(range_expr->as.call.args[0], end);
    }
    
    if (arg_count >= 2) {
        if (!parse_range_literal(range_expr->as.call.args[0], start) ||
            !parse_range_literal(range_expr->as.call.args[1], end)) {
            return 0;
        }
    }
    
    if (arg_count == 3) {
        return parse_range_literal(range_expr->as.call.args[2], step);
    }
    
    return 1;
}

static void setup_loop_state(Cg* cg, LLVMBasicBlockRef end_block, LLVMBasicBlockRef continue_block,
                             LLVMBasicBlockRef* prev_end, LLVMBasicBlockRef* prev_continue, 
                             LLVMValueRef* prev_scope) {
    *prev_end = cg->current_loop_end;
    *prev_continue = cg->current_loop_continue;
    *prev_scope = cg->current_loop_scope_base_depth_slot;
    cg->current_loop_end = end_block;
    cg->current_loop_continue = continue_block;
}

static void restore_loop_state(Cg* cg, LLVMBasicBlockRef prev_end, LLVMBasicBlockRef prev_continue, 
                               LLVMValueRef prev_scope) {
    cg->current_loop_end = prev_end;
    cg->current_loop_continue = prev_continue;
    cg->current_loop_scope_base_depth_slot = prev_scope;
}

static LLVMValueRef setup_loop_scope(Cg* cg, CgFunction* cg_fn, const char* prefix) {
    if (cg_fn) return NULL;
    
    LLVMValueRef scope_base = LLVMBuildCall2(cg->builder, cg->ty_scope_depth, cg->fn_scope_depth, NULL, 0, "");
    LLVMBuildCall2(cg->builder, cg->ty_push_scope, cg->fn_push_scope, NULL, 0, "");
    
    char slot_name[64];
    snprintf(slot_name, sizeof(slot_name), "%s.scope.base", prefix);
    LLVMValueRef base_slot = LLVMBuildAlloca(cg->builder, cg->i32, slot_name);
    LLVMBuildStore(cg->builder, scope_base, base_slot);
    cg->current_loop_scope_base_depth_slot = base_slot;
    
    return scope_base;
}

static void ensure_function_return(Cg* cg, CgFunction* cg_fn, int is_constructor) {
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        return;
    }
    
    if (!is_constructor) {
        LLVMValueRef nil_val = cg_alloc_value(cg, "nil_ret");
        LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, nil_val)};
        LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, args, 1, "");
        cg_copy_value_into(cg, cg_fn->ret_slot, nil_val);
    }
    
    LLVMValueRef loaded_base = LLVMBuildLoad2(cg->builder, cg->i32, cg_fn->runtime_scope_base_depth_slot, "");
    LLVMValueRef pop_args[] = {loaded_base};
    LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
    LLVMBuildRetVoid(cg->builder);
}

static LLVMTypeRef* create_method_param_types(Cg* cg, int param_count, int* total_count) {
    *total_count = param_count + 2;
    LLVMTypeRef* param_types = malloc(sizeof(LLVMTypeRef) * (*total_count));
    if (!param_types) return NULL;
    
    param_types[0] = cg->value_ptr_type;
    param_types[1] = cg->value_ptr_type;
    for (int i = 0; i < param_count; i++) {
        param_types[i + 2] = cg->value_ptr_type;
    }
    
    return param_types;
}

static void init_method_function(CgFunction* cg_fn, char* name, LLVMValueRef fn, 
                                 LLVMTypeRef type, ASTStmtFuncDecl* decl, CgClass* class) {
    memset(cg_fn, 0, sizeof(CgFunction));
    cg_fn->name = name;
    cg_fn->fn = fn;
    cg_fn->type = type;
    cg_fn->body = decl->body;
    cg_fn->param_count = decl->param_count;
    cg_fn->param_names = decl->param_names;
    cg_fn->return_type = decl->return_type;
    cg_fn->return_type_desc = decl->return_type_desc;
    cg_fn->scope = cg_scope_new(NULL);
    cg_fn->ret_slot = LLVMGetParam(fn, 0);
    cg_fn->current_class = class;
    cg_fn->self_param = LLVMGetParam(fn, 1);
    cg_fn->is_method = 1;
    if (!class) {
        fprintf(stderr, "Codegen: init_method_function '%s' with NULL class\n", name ? name : "");
    } else {
        fprintf(stderr, "Codegen: init_method_function '%s' for class '%s'\n", name ? name : "", class->name ? class->name : "");
    }
}

static void setup_method_scope(Cg* cg, CgFunction* cg_fn) {
    LLVMValueRef base_depth = LLVMBuildCall2(cg->builder, cg->ty_scope_depth, cg->fn_scope_depth, NULL, 0, "");
    cg_fn->runtime_scope_base_depth_slot = LLVMBuildAlloca(cg->builder, cg->i32, "scope.base");
    LLVMBuildStore(cg->builder, base_depth, cg_fn->runtime_scope_base_depth_slot);
    LLVMBuildCall2(cg->builder, cg->ty_push_scope, cg->fn_push_scope, NULL, 0, "");
}

static void add_method_params(CgFunction* cg_fn, ASTStmtFuncDecl* decl) {
    for (int i = 0; i < decl->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(cg_fn->fn, i + 2);
        cg_scope_add_var(cg_fn->scope, decl->param_names[i], param);
    }
}

static __attribute__((unused)) int build_method_or_constructor(Cg* cg, CgClass* class, ASTStmtFuncDecl* func_decl, 
                                       int is_constructor, LLVMValueRef val_size) {
    char fn_name[256];
    snprintf(fn_name, sizeof(fn_name), "%s_%s", class->name, is_constructor ? "init" : func_decl->name);
    
    int param_total;
    LLVMTypeRef* param_types = create_method_param_types(cg, func_decl->param_count, &param_total);
    if (!param_types) return 0;
    
    LLVMTypeRef fn_type = LLVMFunctionType(cg->void_ty, param_types, param_total, 0);
    LLVMValueRef fn = LLVMAddFunction(cg->mod, fn_name, fn_type);
    free(param_types);
    
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlock(fn, "entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);

    CgFunction cg_fn;
    init_method_function(&cg_fn, fn_name, fn, fn_type, func_decl, class);
    setup_method_scope(cg, &cg_fn);
    add_method_params(&cg_fn, func_decl);
    
    if (func_decl->body) {
        cg_build_stmt_list(cg, &cg_fn, val_size, func_decl->body);
    }
    
    ensure_function_return(cg, &cg_fn, is_constructor);
    return 1;
}

static int build_expr_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.expr.expr);
    return val != NULL;
}

static int handle_unboxed_var_compound_assign(Cg* cg, CgFunction* cg_fn, CgVar* var, ASTExpr* value_expr, char op) {
    CgValue current_val;
    current_val.type = CG_VALUE_BOXED; 
    LLVMTypeRef ty = NULL;
    
    switch (var->unboxed_type) {
        case UNBOXED_INT: 
            ty = cg->i64; 
            current_val.type = CG_VALUE_UNBOXED_INT; 
            break;
        case UNBOXED_DOUBLE: 
            ty = cg->f64; 
            current_val.type = CG_VALUE_UNBOXED_DOUBLE; 
            break;
        case UNBOXED_BOOL: 
            ty = cg->i1; 
            current_val.type = CG_VALUE_UNBOXED_BOOL; 
            break;
        default: return 0;
    }
    
    current_val.value = LLVMBuildLoad2(cg->builder, ty, var->alloca, "curr_val");
    current_val.llvm_type = ty;
    
    CgValue rhs_val = cg_build_expr_unboxed(cg, cg_fn, value_expr);
    
    LLVMValueRef result = NULL;
    
    if (current_val.type == CG_VALUE_UNBOXED_INT && rhs_val.type == CG_VALUE_UNBOXED_INT) {
        switch (op) {
            case '+': result = LLVMBuildAdd(cg->builder, current_val.value, rhs_val.value, "add"); break;
            case '-': result = LLVMBuildSub(cg->builder, current_val.value, rhs_val.value, "sub"); break;
            case '*': result = LLVMBuildMul(cg->builder, current_val.value, rhs_val.value, "mul"); break;
            case '/': result = LLVMBuildSDiv(cg->builder, current_val.value, rhs_val.value, "div"); break;
            case '%': result = LLVMBuildSRem(cg->builder, current_val.value, rhs_val.value, "mod"); break;
        }
    } else if (current_val.type == CG_VALUE_UNBOXED_DOUBLE && rhs_val.type == CG_VALUE_UNBOXED_DOUBLE) {
        switch (op) {
            case '+': result = LLVMBuildFAdd(cg->builder, current_val.value, rhs_val.value, "fadd"); break;
            case '-': result = LLVMBuildFSub(cg->builder, current_val.value, rhs_val.value, "fsub"); break;
            case '*': result = LLVMBuildFMul(cg->builder, current_val.value, rhs_val.value, "fmul"); break;
            case '/': result = LLVMBuildFDiv(cg->builder, current_val.value, rhs_val.value, "fdiv"); break;
        }
    }
    
    if (result) {
        LLVMBuildStore(cg->builder, result, var->alloca);
        return 1;
    }
    
    // Fallback: Box everything, do op, unbox result
    LLVMValueRef boxed_lhs = cg_box_value(cg, current_val);
    LLVMValueRef boxed_rhs = (rhs_val.type == CG_VALUE_BOXED) ? rhs_val.value : cg_box_value(cg, rhs_val);
    LLVMValueRef boxed_res = cg_alloc_value(cg, "bin_res");
    
    LLVMValueRef args[] = {
        LLVMConstInt(cg->i8, op, 0),
        cg_value_to_i8_ptr(cg, boxed_lhs),
        cg_value_to_i8_ptr(cg, boxed_rhs),
        cg_value_to_i8_ptr(cg, boxed_res)
    };
    LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
    
    CgValue unboxed_res = cg_unbox_value(cg, boxed_res, var->type);
    if (unboxed_res.value) {
        LLVMBuildStore(cg->builder, unboxed_res.value, var->alloca);
        return 1;
    }
    return 0;
}

static int handle_boxed_var_compound_assign(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    ASTExpr load_expr;
    memset(&load_expr, 0, sizeof(ASTExpr));
    load_expr.kind = AST_EXPR_VAR;
    load_expr.as.var_name = stmt->as.var_assign.var_name;
    load_expr.loc = stmt->loc;
    
    LLVMValueRef lhs = cg_build_expr(cg, cg_fn, val_size, &load_expr);
    if (!lhs) return 0;
    
    LLVMValueRef rhs = cg_build_expr(cg, cg_fn, val_size, stmt->as.var_assign.value);
    if (!rhs) return 0;
    
    LLVMValueRef res = cg_alloc_value(cg, "compound_res");
    LLVMValueRef args[] = {
        LLVMConstInt(cg->i8, stmt->as.var_assign.op, 0),
        cg_value_to_i8_ptr(cg, lhs),
        cg_value_to_i8_ptr(cg, rhs),
        cg_value_to_i8_ptr(cg, res)
    };
    LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, args, 4, "");
    
    if (cg_fn) {
        CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.var_assign.var_name);
        if (var) {
             cg_copy_value_into(cg, var->alloca, res);
             return 1;
        }
    }
    
    LLVMValueRef name_ptr = cg_get_string_ptr(cg, stmt->as.var_assign.var_name);
    LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, res)};
    LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");
    
    return 1;
}

static int build_var_assign_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    if (cg_fn) {
        CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.var_assign.var_name);
        if (var && var->unboxed_type != UNBOXED_NONE) {
            if (stmt->as.var_assign.op) {
                return handle_unboxed_var_compound_assign(cg, cg_fn, var, stmt->as.var_assign.value, stmt->as.var_assign.op);
            }
            return handle_unboxed_var_assign(cg, cg_fn, var, stmt->as.var_assign.value);
        }
    }
    if (stmt->as.var_assign.op) {
        return handle_boxed_var_compound_assign(cg, cg_fn, val_size, stmt);
    }
    return handle_boxed_var_assign(cg, cg_fn, val_size, stmt);
}

static int build_index_assign_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef idx = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.index);
    if (!idx) return 0;
    
    LLVMValueRef target_ptr = NULL;
    if (stmt->as.index_assign.target && stmt->as.index_assign.target->kind == AST_EXPR_VAR && cg_fn) {
        CgVar* var = cg_scope_find_var(cg_fn->scope, stmt->as.index_assign.target->as.var_name);
        if (var) target_ptr = var->alloca;
    }

    if (!target_ptr) {
        target_ptr = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.target);
        if (!target_ptr) return 0;
    }

    LLVMValueRef value = NULL;

    if (stmt->as.index_assign.op) {
        // Compound
        LLVMValueRef current = cg_alloc_value(cg, "idx_curr");
        LLVMValueRef get_args[] = {
            cg_value_to_i8_ptr(cg, target_ptr),
            cg_value_to_i8_ptr(cg, idx),
            cg_value_to_i8_ptr(cg, current)
        };
        LLVMBuildCall2(cg->builder, cg->ty_index_op, cg->fn_index_op, get_args, 3, "");
        
        LLVMValueRef rhs = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.value);
        if (!rhs) return 0;
        
        value = cg_alloc_value(cg, "idx_compound_res");
        LLVMValueRef op_args[] = {
            LLVMConstInt(cg->i8, stmt->as.index_assign.op, 0),
            cg_value_to_i8_ptr(cg, current),
            cg_value_to_i8_ptr(cg, rhs),
            cg_value_to_i8_ptr(cg, value)
        };
        LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, op_args, 4, "");
    } else {
        value = cg_build_expr(cg, cg_fn, val_size, stmt->as.index_assign.value);
        if (!value) return 0;
    }

    LLVMValueRef args[] = {
        cg_value_to_i8_ptr(cg, target_ptr),
        cg_value_to_i8_ptr(cg, idx),
        cg_value_to_i8_ptr(cg, value)
    };
    LLVMBuildCall2(cg->builder, cg->ty_index_set_op, cg->fn_index_set_op, args, 3, "");
    return 1;
}

static int build_member_assign_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef target = cg_build_expr(cg, cg_fn, val_size, stmt->as.member_assign.target);
    if (!target) return 0;
    
    const char* member = stmt->as.member_assign.member ? stmt->as.member_assign.member : "";
    LLVMValueRef member_ptr = cg_get_string_ptr(cg, member);
    
    LLVMValueRef value = NULL;

    if (stmt->as.member_assign.op) {
        // Compound
        LLVMValueRef current = cg_alloc_value(cg, "member_curr");
        LLVMValueRef is_opt = LLVMConstInt(cg->i32, 0, 0);
        
        LLVMValueRef get_args[] = {
            cg_value_to_i8_ptr(cg, target),
            member_ptr,
            is_opt,
            cg_value_to_i8_ptr(cg, current)
        };
        LLVMBuildCall2(cg->builder, cg->ty_member_op, cg->fn_member_op, get_args, 4, "");
        
        LLVMValueRef rhs = cg_build_expr(cg, cg_fn, val_size, stmt->as.member_assign.value);
        if (!rhs) return 0;
        
        value = cg_alloc_value(cg, "member_compound_res");
        LLVMValueRef op_args[] = {
            LLVMConstInt(cg->i8, stmt->as.member_assign.op, 0),
            cg_value_to_i8_ptr(cg, current),
            cg_value_to_i8_ptr(cg, rhs),
            cg_value_to_i8_ptr(cg, value)
        };
        LLVMBuildCall2(cg->builder, cg->ty_binary_op, cg->fn_binary_op, op_args, 4, "");
    } else {
        value = cg_build_expr(cg, cg_fn, val_size, stmt->as.member_assign.value);
        if (!value) return 0;
    }
    
    LLVMValueRef args[] = {
        cg_value_to_i8_ptr(cg, target),
        member_ptr,
        cg_value_to_i8_ptr(cg, value)
    };
    LLVMBuildCall2(cg->builder, cg->ty_member_set_op, cg->fn_member_set_op, args, 3, "");
    return 1;
}

static int build_print_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.print.expr);
    if (!val) return 0;
    
    LLVMValueRef args[] = {cg_value_to_i8_ptr(cg, val)};
    LLVMBuildCall2(cg->builder, cg->ty_print, cg->fn_print, args, 1, "");
    return 1;
}

static int build_var_decl_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    int can_unbox = stmt->as.var_decl.init && 
                   var_type_can_unbox(stmt->as.var_decl.type) && 
                   cg_can_unbox_expr(cg, stmt->as.var_decl.init);
    
    if (can_unbox && cg_fn) {
        UnboxedType unboxed_type = var_type_to_unboxed(stmt->as.var_decl.type);
        return handle_unboxed_var_decl(cg, cg_fn, stmt, unboxed_type);
    }
    return handle_boxed_var_decl(cg, cg_fn, val_size, stmt);
}

static int build_if_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef cond_i1 = get_condition_bool(cg, cg_fn, val_size, stmt->as.if_stmt.condition);
    if (!cond_i1) return 0;

    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
    if (!current_block) return 0;
    
    LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);
    LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(fn, "then");
    LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(fn, "else");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(fn, "ifcont");

    LLVMBuildCondBr(cg->builder, cond_i1, then_block, else_block);

    LLVMPositionBuilderAtEnd(cg->builder, then_block);
    LLVMValueRef then_scope_base = NULL;
    push_scope_if_global(cg, cg_fn, &then_scope_base);
    if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.if_stmt.then_branch)) return 0;
    pop_scope_if_needed(cg, cg_fn, then_scope_base);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        LLVMBuildBr(cg->builder, merge_block);
    }

    LLVMPositionBuilderAtEnd(cg->builder, else_block);
    if (stmt->as.if_stmt.else_branch) {
        LLVMValueRef else_scope_base = NULL;
        push_scope_if_global(cg, cg_fn, &else_scope_base);
        if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.if_stmt.else_branch)) return 0;
        pop_scope_if_needed(cg, cg_fn, else_scope_base);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        LLVMBuildBr(cg->builder, merge_block);
    }

    LLVMPositionBuilderAtEnd(cg->builder, merge_block);
    return 1;
}

static int build_while_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));
    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "while.cond");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "while.body");
    LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "while.end");

    LLVMBasicBlockRef prev_loop_end, prev_loop_continue;
    LLVMValueRef prev_loop_scope_base;
    setup_loop_state(cg, end_block, cond_block, &prev_loop_end, &prev_loop_continue, &prev_loop_scope_base);

    LLVMBuildBr(cg->builder, cond_block);

    LLVMPositionBuilderAtEnd(cg->builder, cond_block);
    LLVMValueRef cond_i1 = get_condition_bool(cg, cg_fn, val_size, stmt->as.while_stmt.condition);
    if (!cond_i1) return 0;
    
    LLVMBuildCondBr(cg->builder, cond_i1, body_block, end_block);

    LLVMPositionBuilderAtEnd(cg->builder, body_block);
    LLVMValueRef while_scope_base = setup_loop_scope(cg, cg_fn, "while");
    
    if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.while_stmt.body)) return 0;
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        if (!cg_fn && while_scope_base) {
            LLVMValueRef pop_args[] = {while_scope_base};
            LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
        }
        LLVMBuildBr(cg->builder, cond_block);
    }

    restore_loop_state(cg, prev_loop_end, prev_loop_continue, prev_loop_scope_base);
    LLVMPositionBuilderAtEnd(cg->builder, end_block);
    return 1;
}

static void declare_loop_variable(Cg* cg, const char* var_name, VarType var_type, int initial_int_value) {
    LLVMValueRef name_ptr = cg_get_string_ptr(cg, var_name);
    LLVMValueRef init_tmp = cg_alloc_value(cg, "loop.init");

    if (var_type == TYPE_INT) {
        LLVMValueRef init_val = LLVMConstInt(cg->i64, initial_int_value, 0);
        LLVMValueRef set_args[] = {cg_value_to_i8_ptr(cg, init_tmp), init_val};
        LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, set_args, 2, "");
    } else {
        LLVMValueRef set_nil_args[] = {cg_value_to_i8_ptr(cg, init_tmp)};
        LLVMBuildCall2(cg->builder, cg->ty_value_set_nil, cg->fn_value_set_nil, set_nil_args, 1, "");
    }
    
    LLVMValueRef decl_type = LLVMConstInt(cg->i32, var_type, 0);
    LLVMValueRef decl_const = LLVMConstInt(cg->i32, 0, 0);
    LLVMValueRef decl_args[] = {name_ptr, decl_type, decl_const, cg_value_to_i8_ptr(cg, init_tmp)};
    LLVMBuildCall2(cg->builder, cg->ty_var_decl_if_missing, cg->fn_var_decl_if_missing, decl_args, 4, "");
}

static void assign_loop_variable(Cg* cg, const char* var_name, LLVMValueRef value) {
    LLVMValueRef name_ptr = cg_get_string_ptr(cg, var_name);
    LLVMValueRef iter_tmp = cg_alloc_value(cg, "loop.iter");

    LLVMValueRef value_64 = LLVMBuildSExt(cg->builder, value, cg->i64, "idx.ext");
    LLVMValueRef set_iter_args[] = {cg_value_to_i8_ptr(cg, iter_tmp), value_64};
    LLVMBuildCall2(cg->builder, cg->ty_value_set_int, cg->fn_value_set_int, set_iter_args, 2, "");
    
    LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, iter_tmp)};
    LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");
}

static int build_for_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    int start, end, step;
    if (!parse_range_args(stmt->as.for_stmt.range_expr, &start, &end, &step)) {
        return 0;
    }

    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
    if (!current_block) return 0;
    
    LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);
    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "for.cond");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "for.body");
    LLVMBasicBlockRef inc_block = LLVMAppendBasicBlock(fn, "for.inc");
    LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "for.end");

    LLVMBasicBlockRef prev_loop_end, prev_loop_continue;
    LLVMValueRef prev_loop_scope_base;
    setup_loop_state(cg, end_block, inc_block, &prev_loop_end, &prev_loop_continue, &prev_loop_scope_base);

    LLVMValueRef i_slot = LLVMBuildAlloca(cg->builder, cg->i32, "for.i");
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i32, start, 0), i_slot);

    declare_loop_variable(cg, stmt->as.for_stmt.var_name, TYPE_INT, start);
    LLVMBuildBr(cg->builder, cond_block);

    LLVMPositionBuilderAtEnd(cg->builder, cond_block);
    LLVMValueRef i_val = LLVMBuildLoad2(cg->builder, cg->i32, i_slot, "");
    LLVMValueRef cmp = (step > 0)
        ? LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, LLVMConstInt(cg->i32, end, 0), "")
        : LLVMBuildICmp(cg->builder, LLVMIntSGT, i_val, LLVMConstInt(cg->i32, end, 0), "");
    LLVMBuildCondBr(cg->builder, cmp, body_block, end_block);

    LLVMPositionBuilderAtEnd(cg->builder, body_block);
    LLVMValueRef for_scope_base = setup_loop_scope(cg, cg_fn, "for");
    assign_loop_variable(cg, stmt->as.for_stmt.var_name, i_val);

    if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.for_stmt.body)) return 0;
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        if (for_scope_base) {
            LLVMValueRef pop_args[] = {for_scope_base};
            LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
        }
        LLVMBuildBr(cg->builder, inc_block);
    }

    LLVMPositionBuilderAtEnd(cg->builder, inc_block);
    LLVMValueRef next_i = LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(cg->i32, step, 0), "");
    LLVMBuildStore(cg->builder, next_i, i_slot);
    LLVMBuildBr(cg->builder, cond_block);

    restore_loop_state(cg, prev_loop_end, prev_loop_continue, prev_loop_scope_base);
    LLVMPositionBuilderAtEnd(cg->builder, end_block);
    return 1;
}

static LLVMValueRef get_iterable_for_loop(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, 
                                          ASTExpr* iterable_expr, TypeDescriptor** out_type,
                                          LLVMBasicBlockRef fail_target) {
    LLVMValueRef iterable = cg_build_expr(cg, cg_fn, val_size, iterable_expr);
    if (!iterable) return NULL;

    *out_type = cg_infer_expr_type_desc_with_function(cg, cg_fn, iterable_expr);
    if (!*out_type) return NULL;

    if ((*out_type)->base_type == TYPE_DICT) {
        LLVMValueRef keys_array = cg_alloc_value(cg, "dict.keys");
        LLVMValueRef get_keys_args[] = {cg_value_to_i8_ptr(cg, iterable), cg_value_to_i8_ptr(cg, keys_array)};
        LLVMValueRef success = LLVMBuildCall2(cg->builder, cg->ty_dict_keys, cg->fn_dict_keys, get_keys_args, 2, "");
        
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));
        LLVMValueRef success_check = LLVMBuildICmp(cg->builder, LLVMIntNE, success, LLVMConstInt(cg->i32, 0, 0), "");
        LLVMBasicBlockRef keys_success_block = LLVMAppendBasicBlock(fn, "keys.success");
        LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(fn, "keys.fail");
        
        LLVMBuildCondBr(cg->builder, success_check, keys_success_block, fail_block);

        LLVMPositionBuilderAtEnd(cg->builder, fail_block);
        LLVMBuildBr(cg->builder, fail_target);

        LLVMPositionBuilderAtEnd(cg->builder, keys_success_block);
        
        return keys_array;
    }
    
    if ((*out_type)->base_type != TYPE_ARRAY) {
        type_descriptor_free(*out_type);
        *out_type = NULL;
        return NULL;
    }

    return iterable;
}

static int build_for_in_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(cg->builder);
    if (!current_block) return 0;
    
    LLVMValueRef fn = LLVMGetBasicBlockParent(current_block);
    LLVMBasicBlockRef setup_block = LLVMAppendBasicBlock(fn, "forin.setup");
    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(fn, "forin.cond");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(fn, "forin.body");
    LLVMBasicBlockRef inc_block = LLVMAppendBasicBlock(fn, "forin.inc");
    LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(fn, "forin.end");

    LLVMBasicBlockRef prev_loop_end, prev_loop_continue;
    LLVMValueRef prev_loop_scope_base;
    setup_loop_state(cg, end_block, inc_block, &prev_loop_end, &prev_loop_continue, &prev_loop_scope_base);

    LLVMBuildBr(cg->builder, setup_block);
    LLVMPositionBuilderAtEnd(cg->builder, setup_block);
    
    TypeDescriptor* iterable_type;
    LLVMValueRef actual_iterable = get_iterable_for_loop(cg, cg_fn, val_size, stmt->as.for_in_stmt.iterable, &iterable_type, end_block);
    if (!actual_iterable) return 0;

    LLVMValueRef index_slot = LLVMBuildAlloca(cg->builder, cg->i32, "forin.index");
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i32, 0, 0), index_slot);

    LLVMValueRef length = LLVMBuildCall2(cg->builder, cg->ty_array_length, cg->fn_array_length,
                                        (LLVMValueRef[]){cg_value_to_i8_ptr(cg, actual_iterable)}, 1, "forin.length");

    LLVMValueRef length_check = LLVMBuildICmp(cg->builder, LLVMIntSGT, length, LLVMConstInt(cg->i32, 0, 0), "");
    LLVMBasicBlockRef valid_length_block = LLVMAppendBasicBlock(fn, "forin.valid_length");
    LLVMBuildCondBr(cg->builder, length_check, valid_length_block, end_block);

    LLVMPositionBuilderAtEnd(cg->builder, valid_length_block);
    VarType element_var_type = TYPE_INT;
    if (iterable_type) {
        if (iterable_type->base_type == TYPE_ARRAY && iterable_type->params.array.element_type) {
            element_var_type = iterable_type->params.array.element_type->base_type;
        } else if (iterable_type->base_type == TYPE_DICT && iterable_type->params.dict.key_type) {
            element_var_type = iterable_type->params.dict.key_type->base_type;
        }
    }
    declare_loop_variable(cg, stmt->as.for_in_stmt.var_name, TYPE_NIL, 0);
    LLVMBuildBr(cg->builder, cond_block);

    LLVMPositionBuilderAtEnd(cg->builder, cond_block);
    LLVMValueRef index_phi = LLVMBuildPhi(cg->builder, cg->i32, "forin.index.phi");
    LLVMValueRef phi_vals[] = {LLVMConstInt(cg->i32, 0, 0)};
    LLVMBasicBlockRef phi_blocks[] = {valid_length_block};
    LLVMAddIncoming(index_phi, phi_vals, phi_blocks, 1);

    LLVMValueRef cmp = LLVMBuildICmp(cg->builder, LLVMIntSLT, index_phi, length, "forin.cond");
    LLVMBuildCondBr(cg->builder, cmp, body_block, end_block);

    LLVMPositionBuilderAtEnd(cg->builder, body_block);
    LLVMValueRef element_tmp = cg_alloc_value(cg, "forin.element");
    LLVMValueRef get_args[] = {cg_value_to_i8_ptr(cg, actual_iterable), index_phi, cg_value_to_i8_ptr(cg, element_tmp)};
    LLVMValueRef get_success = LLVMBuildCall2(cg->builder, cg->ty_array_get, cg->fn_array_get, get_args, 3, "");
    
    LLVMValueRef success_cmp = LLVMBuildICmp(cg->builder, LLVMIntNE, get_success, LLVMConstInt(cg->i32, 0, 0), "");
    LLVMBasicBlockRef assign_block = LLVMAppendBasicBlock(fn, "forin.assign");
    LLVMBuildCondBr(cg->builder, success_cmp, assign_block, end_block);
    
    LLVMPositionBuilderAtEnd(cg->builder, assign_block);
    LLVMValueRef forin_scope_base = setup_loop_scope(cg, cg_fn, "forin");
    
    LLVMValueRef name_ptr = cg_get_string_ptr(cg, stmt->as.for_in_stmt.var_name);
    LLVMValueRef assign_args[] = {name_ptr, cg_value_to_i8_ptr(cg, element_tmp)};
    LLVMBuildCall2(cg->builder, cg->ty_var_assign, cg->fn_var_assign, assign_args, 2, "");
    
    if (!cg_build_stmt_list(cg, cg_fn, val_size, stmt->as.for_in_stmt.body)) return 0;
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        if (forin_scope_base) {
            LLVMValueRef pop_args[] = {forin_scope_base};
            LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
        }
        LLVMBuildBr(cg->builder, inc_block);
    }
    
    LLVMPositionBuilderAtEnd(cg->builder, inc_block);
    LLVMValueRef next_index = LLVMBuildAdd(cg->builder, index_phi, LLVMConstInt(cg->i32, 1, 0), "forin.next");
    LLVMValueRef inc_phi_vals[] = {next_index};
    LLVMBasicBlockRef inc_phi_blocks[] = {inc_block};
    LLVMAddIncoming(index_phi, inc_phi_vals, inc_phi_blocks, 1);
    LLVMBuildBr(cg->builder, cond_block);
    
    restore_loop_state(cg, prev_loop_end, prev_loop_continue, prev_loop_scope_base);
    type_descriptor_free(iterable_type);
    LLVMPositionBuilderAtEnd(cg->builder, end_block);
    return 1;
}

static int calculate_required_param_count(ASTStmtFuncDecl* func_decl) {
    if (!func_decl->param_defaults) {
        return func_decl->param_count;
    }
    
    int required = 0;
    for (int i = 0; i < func_decl->param_count; i++) {
        if (func_decl->param_defaults[i] == NULL) {
            required++;
        } else {
            break;
        }
    }
    return required;
}

static int build_func_decl_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    (void)cg;
    (void)cg_fn;
    (void)val_size;
    int param_total = stmt->as.func_decl.param_count + 1;
    LLVMTypeRef* param_types = malloc(sizeof(LLVMTypeRef) * param_total);
    if (!param_types) return 0;
    
    param_types[0] = cg->value_ptr_type;
    for (int i = 0; i < stmt->as.func_decl.param_count; i++) {
        param_types[i + 1] = cg->value_ptr_type;
    }
    
    LLVMTypeRef fn_type = LLVMFunctionType(cg->void_ty, param_types, param_total, 0);
    free(param_types);

    LLVMValueRef fn = LLVMAddFunction(cg->mod, stmt->as.func_decl.name, fn_type);

    CgFunction* new_cg_fn = malloc(sizeof(CgFunction));
    new_cg_fn->name = strdup(stmt->as.func_decl.name);
    new_cg_fn->fn = fn;
    new_cg_fn->type = fn_type;
    new_cg_fn->body = stmt->as.func_decl.body;
    new_cg_fn->param_count = stmt->as.func_decl.param_count;
    new_cg_fn->required_param_count = calculate_required_param_count(&stmt->as.func_decl);
    new_cg_fn->param_names = stmt->as.func_decl.param_names;
    new_cg_fn->param_type_descs = stmt->as.func_decl.param_type_descs;
    new_cg_fn->param_defaults = stmt->as.func_decl.param_defaults;
    new_cg_fn->scope = cg_scope_new(NULL);
    new_cg_fn->next = cg->functions;
    new_cg_fn->ret_slot = NULL;
    new_cg_fn->runtime_scope_base_depth_slot = NULL;
    cg->functions = new_cg_fn;
    
    return 1;
}

static int build_return_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    LLVMValueRef val = cg_build_expr(cg, cg_fn, val_size, stmt->as.ret.expr);
    if (!val) return 0;
    
    if (!cg_fn || !cg_fn->ret_slot) {
        fprintf(stderr, "Error: return outside of function\n");
        return 0;
    }
    
    cg_copy_value_into(cg, cg_fn->ret_slot, val);

    if (cg_fn->runtime_scope_base_depth_slot) {
        LLVMValueRef base_depth = LLVMBuildLoad2(cg->builder, cg->i32, cg_fn->runtime_scope_base_depth_slot, "");
        LLVMValueRef min_depth = LLVMConstInt(cg->i32, 1, 0);
        LLVMValueRef safe_depth = LLVMBuildSelect(cg->builder, 
            LLVMBuildICmp(cg->builder, LLVMIntSGE, base_depth, min_depth, ""), 
            base_depth, min_depth, "safe_depth");
        LLVMValueRef pop_args[] = {safe_depth};
        LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
    }

    LLVMBuildRetVoid(cg->builder);
    return 1;
}

static int build_break_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    (void)cg_fn;
    (void)val_size;
    (void)stmt;
    if (!cg->current_loop_end) {
        fprintf(stderr, "Error: break outside of loop\n");
        return 0;
    }
    
    if (cg->current_loop_scope_base_depth_slot) {
        LLVMValueRef base_depth = LLVMBuildLoad2(cg->builder, cg->i32, cg->current_loop_scope_base_depth_slot, "");
        LLVMValueRef pop_args[] = {base_depth};
        LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
    }
    
    LLVMBuildBr(cg->builder, cg->current_loop_end);
    return 1;
}

static int build_continue_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    (void)cg_fn;
    (void)val_size;
    (void)stmt;
    if (!cg->current_loop_continue) {
        fprintf(stderr, "Error: continue outside of loop\n");
        return 0;
    }
    
    if (cg->current_loop_scope_base_depth_slot) {
        LLVMValueRef base_depth = LLVMBuildLoad2(cg->builder, cg->i32, cg->current_loop_scope_base_depth_slot, "");
        LLVMValueRef pop_args[] = {base_depth};
        LLVMBuildCall2(cg->builder, cg->ty_pop_to_scope_depth, cg->fn_pop_to_scope_depth, pop_args, 1, "");
    }
    
    LLVMBuildBr(cg->builder, cg->current_loop_continue);
    return 1;
}

static int build_constructor(Cg* cg, CgClass* class, LLVMValueRef val_size) {
    if (!class->constructor) return 1;
    
    char constructor_name[256];
    snprintf(constructor_name, sizeof(constructor_name), "%s_init", class->name);
    
    int param_total;
    LLVMTypeRef* param_types = create_method_param_types(cg, class->constructor->param_count, &param_total);
    if (!param_types) return 0;
    
    LLVMTypeRef constructor_type = LLVMFunctionType(cg->void_ty, param_types, param_total, 0);
    LLVMValueRef constructor_fn = LLVMAddFunction(cg->mod, constructor_name, constructor_type);
    free(param_types);
    
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlock(constructor_fn, "entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);

    CgFunction cg_constructor;
    init_method_function(&cg_constructor, constructor_name, constructor_fn, constructor_type, 
                        class->constructor, class);
    setup_method_scope(cg, &cg_constructor);
    add_method_params(&cg_constructor, class->constructor);
    
    if (class->constructor->body) {
        cg_build_stmt_list(cg, &cg_constructor, val_size, class->constructor->body);
    }
    
    ensure_function_return(cg, &cg_constructor, 1);
    class->constructor_function = constructor_fn;
    
    return 1;
}

static int build_method(Cg* cg, CgClass* class, ASTStmtFuncDecl* method, int method_index, LLVMValueRef val_size) {
    char method_name[256];
    snprintf(method_name, sizeof(method_name), "%s_%s", class->name, method->name);
    
    int param_total;
    LLVMTypeRef* param_types = create_method_param_types(cg, method->param_count, &param_total);
    if (!param_types) return 0;
    
    LLVMTypeRef method_type = LLVMFunctionType(cg->void_ty, param_types, param_total, 0);
    LLVMValueRef method_fn = LLVMAddFunction(cg->mod, method_name, method_type);
    free(param_types);
    
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlock(method_fn, "entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    
    CgFunction cg_method;
    init_method_function(&cg_method, method_name, method_fn, method_type, method, class);
    setup_method_scope(cg, &cg_method);
    add_method_params(&cg_method, method);
    
    if (method->body) {
        cg_build_stmt_list(cg, &cg_method, val_size, method->body);
    }
    
    ensure_function_return(cg, &cg_method, 0);
    class->method_functions[method_index] = method_fn;
    
    return 1;
}

static int build_class_decl_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    (void)cg_fn;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(cg->builder);
    CgClass* class = cg_find_class(cg, stmt->as.class_decl.name);
    if (!class) {
        fprintf(stderr, "Error: Class '%s' not found during codegen\n", stmt->as.class_decl.name);
        return 0;
    }
    
    if (!build_constructor(cg, class, val_size)) {
        return 0;
    }
    
    for (int i = 0; i < class->method_count; i++) {
        ASTStmtFuncDecl* method = class->methods[i];
        if (!method || strcmp(method->name, "init") == 0) continue;
        
        if (!build_method(cg, class, method, i, val_size)) {
            return 0;
        }
    }
    
    if (saved_bb) {
        LLVMPositionBuilderAtEnd(cg->builder, saved_bb);
    }
    
    return 1;
}

int cg_build_stmt(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmt* stmt) {
    if (!cg || !stmt) return 0;
    
    switch (stmt->kind) {
        case AST_STMT_EXPR:         return build_expr_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_VAR_ASSIGN:   return build_var_assign_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_INDEX_ASSIGN: return build_index_assign_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_MEMBER_ASSIGN:return build_member_assign_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_PRINT:        return build_print_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_VAR_DECL:     return build_var_decl_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_IF:           return build_if_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_WHILE:        return build_while_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_FOR:          return build_for_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_FOR_IN:       return build_for_in_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_FUNC_DECL:    return build_func_decl_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_RETURN:       return build_return_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_BREAK:        return build_break_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_CONTINUE:     return build_continue_stmt(cg, cg_fn, val_size, stmt);
        case AST_STMT_STRUCT_DECL:  return 1;
        case AST_STMT_CLASS_DECL:   return build_class_decl_stmt(cg, cg_fn, val_size, stmt);
        default:
            fprintf(stderr, "Codegen not implemented for stmt kind %d\n", stmt->kind);
            return 0;
    }
}

int cg_build_stmt_list(Cg* cg, CgFunction* cg_fn, LLVMValueRef val_size, ASTStmtList* list) {
    if (!cg || !list) return 1;
    
    for (ASTStmt* st = list->head; st; st = st->next) {
        if (!cg_build_stmt(cg, cg_fn, val_size, st)) {
            return 0;
        }
    }
    return 1;
}
