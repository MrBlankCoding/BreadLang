#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler.h"
#include "core/function.h"
#include "core/value.h"

typedef struct {
    int count;
    int capacity;
    int* items;
} IntList;

typedef struct {
    IntList breaks;
    IntList continues;
    int continue_target;
} LoopCtx;

typedef struct {
    BytecodeChunk* chunk;

    int loop_count;
    int loop_capacity;
    LoopCtx* loops;

    int for_tmp_counter;
} CompCtx;

static void int_list_init(IntList* l) {
    if (!l) return;
    memset(l, 0, sizeof(*l));
}

static void int_list_free(IntList* l) {
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->capacity = 0;
}

static int int_list_push(IntList* l, int v) {
    if (!l) return 0;
    if (l->count + 1 > l->capacity) {
        int new_cap = l->capacity == 0 ? 8 : l->capacity * 2;
        int* new_items = realloc(l->items, sizeof(int) * (size_t)new_cap);
        if (!new_items) return 0;
        l->items = new_items;
        l->capacity = new_cap;
    }
    l->items[l->count++] = v;
    return 1;
}

static int emit_u8(CompCtx* c, uint8_t b) {
    return bc_chunk_write(c->chunk, b);
}

static int emit_u16(CompCtx* c, uint16_t v) {
    return bc_chunk_write_u16(c->chunk, v);
}

static int emit_op(CompCtx* c, OpCode op) {
    return emit_u8(c, (uint8_t)op);
}

static int add_string_const(CompCtx* c, const char* s) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = TYPE_STRING;
    v.value.string_val = bread_string_new(s ? s : "");
    if (!v.value.string_val) return -1;
    int idx = (int)bc_chunk_add_constant(c->chunk, v);
    bread_value_release(&v);
    return idx;
}

static int add_int_const(CompCtx* c, int x) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = TYPE_INT;
    v.value.int_val = x;
    return (int)bc_chunk_add_constant(c->chunk, v);
}

static int add_double_const(CompCtx* c, double x) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = TYPE_DOUBLE;
    v.value.double_val = x;
    return (int)bc_chunk_add_constant(c->chunk, v);
}

static int emit_jump(CompCtx* c, OpCode op) {
    if (!emit_op(c, op)) return -1;
    int operand_off = c->chunk->count;
    if (!emit_u16(c, 0xffff)) return -1;
    return operand_off;
}

static int patch_jump_forward(CompCtx* c, int operand_off, int target) {
    if (operand_off < 0) return 0;
    int after = operand_off + 2;
    int diff = target - after;
    if (diff < 0 || diff > 0xffff) {
        printf("Error: Jump offset out of range\n");
        return 0;
    }
    c->chunk->code[operand_off] = (uint8_t)((diff >> 8) & 0xff);
    c->chunk->code[operand_off + 1] = (uint8_t)(diff & 0xff);
    return 1;
}

static int patch_jump_backward(CompCtx* c, int operand_off, int target) {
    if (operand_off < 0) return 0;
    int after = operand_off + 2;
    int diff = after - target;
    if (diff < 0 || diff > 0xffff) {
        printf("Error: Loop offset out of range\n");
        return 0;
    }
    c->chunk->code[operand_off] = (uint8_t)((diff >> 8) & 0xff);
    c->chunk->code[operand_off + 1] = (uint8_t)(diff & 0xff);
    return 1;
}

static int emit_trace_stmt(CompCtx* c, const char* name) {
    int idx = add_string_const(c, name);
    if (idx < 0 || idx > 0xffff) return 0;
    if (!emit_op(c, OP_TRACE)) return 0;
    if (!emit_u16(c, (uint16_t)idx)) return 0;
    return 1;
}

static int compile_expr(CompCtx* c, ASTExpr* e);
static int compile_stmt_list(CompCtx* c, ASTStmtList* list);

static int compile_expr(CompCtx* c, ASTExpr* e) {
    if (!c || !e) return 0;

    switch (e->kind) {
        case AST_EXPR_NIL:
            return emit_op(c, OP_NIL);
        case AST_EXPR_BOOL:
            return emit_op(c, e->as.bool_val ? OP_TRUE : OP_FALSE);
        case AST_EXPR_INT: {
            int idx = add_int_const(c, e->as.int_val);
            if (idx < 0 || idx > 0xffff) return 0;
            if (!emit_op(c, OP_CONSTANT)) return 0;
            if (!emit_u16(c, (uint16_t)idx)) return 0;
            return 1;
        }
        case AST_EXPR_DOUBLE: {
            int idx = add_double_const(c, e->as.double_val);
            if (idx < 0 || idx > 0xffff) return 0;
            if (!emit_op(c, OP_CONSTANT)) return 0;
            if (!emit_u16(c, (uint16_t)idx)) return 0;
            return 1;
        }
        case AST_EXPR_STRING: {
            int idx = add_string_const(c, e->as.string_val ? e->as.string_val : "");
            if (idx < 0 || idx > 0xffff) return 0;
            if (!emit_op(c, OP_CONSTANT)) return 0;
            if (!emit_u16(c, (uint16_t)idx)) return 0;
            return 1;
        }
        case AST_EXPR_VAR: {
            int idx = add_string_const(c, e->as.var_name ? e->as.var_name : "");
            if (idx < 0 || idx > 0xffff) return 0;
            if (!emit_op(c, OP_LOAD_VAR)) return 0;
            if (!emit_u16(c, (uint16_t)idx)) return 0;
            return 1;
        }
        case AST_EXPR_UNARY: {
            if (!compile_expr(c, e->as.unary.operand)) return 0;
            if (e->as.unary.op == '!') return emit_op(c, OP_NOT);
            printf("Error: Unsupported unary op\n");
            return 0;
        }
        case AST_EXPR_BINARY: {
            if (!compile_expr(c, e->as.binary.left)) return 0;
            if (!compile_expr(c, e->as.binary.right)) return 0;
            if (!emit_op(c, OP_BINARY)) return 0;
            if (!emit_u8(c, (uint8_t)e->as.binary.op)) return 0;
            return 1;
        }
        case AST_EXPR_CALL: {
            for (int i = 0; i < e->as.call.arg_count; i++) {
                if (!compile_expr(c, e->as.call.args[i])) return 0;
            }
            int name_idx = add_string_const(c, e->as.call.name ? e->as.call.name : "");
            if (name_idx < 0 || name_idx > 0xffff) return 0;
            if (!emit_op(c, OP_CALL)) return 0;
            if (!emit_u16(c, (uint16_t)name_idx)) return 0;
            if (!emit_u8(c, (uint8_t)e->as.call.arg_count)) return 0;
            return 1;
        }
        case AST_EXPR_ARRAY: {
            for (int i = 0; i < e->as.array.item_count; i++) {
                if (!compile_expr(c, e->as.array.items[i])) return 0;
            }
            if (!emit_op(c, OP_ARRAY)) return 0;
            if (!emit_u16(c, (uint16_t)e->as.array.item_count)) return 0;
            return 1;
        }
        case AST_EXPR_DICT: {
            for (int i = 0; i < e->as.dict.entry_count; i++) {
                if (!compile_expr(c, e->as.dict.entries[i].key)) return 0;
                if (!compile_expr(c, e->as.dict.entries[i].value)) return 0;
            }
            if (!emit_op(c, OP_DICT)) return 0;
            if (!emit_u16(c, (uint16_t)e->as.dict.entry_count)) return 0;
            return 1;
        }
        case AST_EXPR_INDEX: {
            if (!compile_expr(c, e->as.index.target)) return 0;
            if (!compile_expr(c, e->as.index.index)) return 0;
            return emit_op(c, OP_INDEX);
        }
        case AST_EXPR_MEMBER: {
            if (!compile_expr(c, e->as.member.target)) return 0;
            int mem_idx = add_string_const(c, e->as.member.member ? e->as.member.member : "");
            if (mem_idx < 0 || mem_idx > 0xffff) return 0;
            if (!emit_op(c, OP_MEMBER)) return 0;
            if (!emit_u16(c, (uint16_t)mem_idx)) return 0;
            if (!emit_u8(c, (uint8_t)(e->as.member.is_optional_chain ? 1 : 0))) return 0;
            return 1;
        }
        case AST_EXPR_METHOD_CALL: {
            if (!compile_expr(c, e->as.method_call.target)) return 0;
            for (int i = 0; i < e->as.method_call.arg_count; i++) {
                if (!compile_expr(c, e->as.method_call.args[i])) return 0;
            }
            int name_idx = add_string_const(c, e->as.method_call.name ? e->as.method_call.name : "");
            if (name_idx < 0 || name_idx > 0xffff) return 0;
            if (!emit_op(c, OP_METHOD_CALL)) return 0;
            if (!emit_u16(c, (uint16_t)name_idx)) return 0;
            if (!emit_u8(c, (uint8_t)e->as.method_call.arg_count)) return 0;
            if (!emit_u8(c, (uint8_t)(e->as.method_call.is_optional_chain ? 1 : 0))) return 0;
            return 1;
        }
        default:
            printf("Error: Unsupported expr kind\n");
            return 0;
    }
}

static int push_loop(CompCtx* c, int continue_target) {
    if (c->loop_count + 1 > c->loop_capacity) {
        int new_cap = c->loop_capacity == 0 ? 4 : c->loop_capacity * 2;
        LoopCtx* nl = realloc(c->loops, sizeof(LoopCtx) * (size_t)new_cap);
        if (!nl) return 0;
        c->loops = nl;
        c->loop_capacity = new_cap;
    }

    LoopCtx* ctx = &c->loops[c->loop_count++];
    memset(ctx, 0, sizeof(*ctx));
    int_list_init(&ctx->breaks);
    int_list_init(&ctx->continues);
    ctx->continue_target = continue_target;
    return 1;
}

static LoopCtx* top_loop(CompCtx* c) {
    if (!c || c->loop_count <= 0) return NULL;
    return &c->loops[c->loop_count - 1];
}

static void pop_loop(CompCtx* c) {
    if (!c || c->loop_count <= 0) return;
    LoopCtx* ctx = &c->loops[c->loop_count - 1];
    int_list_free(&ctx->breaks);
    int_list_free(&ctx->continues);
    c->loop_count--;
}

static int compile_for_range(CompCtx* c, ASTStmtFor* st) {
    if (!c || !st || !st->range_expr) return 0;

    ASTExpr* re = st->range_expr;
    if (re->kind != AST_EXPR_CALL || !re->as.call.name || strcmp(re->as.call.name, "range") != 0 || re->as.call.arg_count != 1) {
        printf("Error: for currently only supports range(n)\n");
        return 0;
    }

    char tmp_name[64];
    snprintf(tmp_name, sizeof(tmp_name), "__$bc_for_lim_%d", c->for_tmp_counter++);

    if (!compile_expr(c, re->as.call.args[0])) return 0;
    {
        int name_idx = add_string_const(c, tmp_name);
        if (name_idx < 0 || name_idx > 0xffff) return 0;
        if (!emit_op(c, OP_DECL_VAR)) return 0;
        if (!emit_u16(c, (uint16_t)name_idx)) return 0;
        if (!emit_u8(c, (uint8_t)TYPE_INT)) return 0;
        if (!emit_u8(c, 1)) return 0;
    }

    {
        int zero_idx = add_int_const(c, 0);
        if (zero_idx < 0 || zero_idx > 0xffff) return 0;
        if (!emit_op(c, OP_CONSTANT)) return 0;
        if (!emit_u16(c, (uint16_t)zero_idx)) return 0;

        int name_idx = add_string_const(c, st->var_name ? st->var_name : "");
        if (name_idx < 0 || name_idx > 0xffff) return 0;
        if (!emit_op(c, OP_DECL_VAR_IF_MISSING)) return 0;
        if (!emit_u16(c, (uint16_t)name_idx)) return 0;
        if (!emit_u8(c, (uint8_t)TYPE_INT)) return 0;
        if (!emit_u8(c, 0)) return 0;

        int zero_idx2 = add_int_const(c, 0);
        if (zero_idx2 < 0 || zero_idx2 > 0xffff) return 0;
        if (!emit_op(c, OP_CONSTANT)) return 0;
        if (!emit_u16(c, (uint16_t)zero_idx2)) return 0;
        if (!emit_op(c, OP_STORE_VAR)) return 0;
        if (!emit_u16(c, (uint16_t)name_idx)) return 0;
    }

    int loop_start = c->chunk->count;

    {
        int i_idx = add_string_const(c, st->var_name ? st->var_name : "");
        int lim_idx = add_string_const(c, tmp_name);
        if (i_idx < 0 || i_idx > 0xffff || lim_idx < 0 || lim_idx > 0xffff) return 0;

        if (!emit_op(c, OP_LOAD_VAR)) return 0;
        if (!emit_u16(c, (uint16_t)i_idx)) return 0;
        if (!emit_op(c, OP_LOAD_VAR)) return 0;
        if (!emit_u16(c, (uint16_t)lim_idx)) return 0;
        if (!emit_op(c, OP_BINARY)) return 0;
        if (!emit_u8(c, (uint8_t)'<')) return 0;
    }

    int jfalse = emit_jump(c, OP_JUMP_IF_FALSE);
    if (jfalse < 0) return 0;

    if (!emit_op(c, OP_POP)) return 0;

    int inc_target_placeholder = -1;
    if (!push_loop(c, -1)) return 0;

    int loop_ctx_index = c->loop_count - 1;
    (void)loop_ctx_index;

    if (!compile_stmt_list(c, st->body)) {
        pop_loop(c);
        return 0;
    }

    int inc_start = c->chunk->count;
    top_loop(c)->continue_target = inc_start;

    {
        int i_idx = add_string_const(c, st->var_name ? st->var_name : "");
        int one_idx = add_int_const(c, 1);
        if (i_idx < 0 || i_idx > 0xffff || one_idx < 0 || one_idx > 0xffff) {
            pop_loop(c);
            return 0;
        }

        if (!emit_op(c, OP_LOAD_VAR)) {
            pop_loop(c);
            return 0;
        }
        if (!emit_u16(c, (uint16_t)i_idx)) {
            pop_loop(c);
            return 0;
        }
        if (!emit_op(c, OP_CONSTANT)) {
            pop_loop(c);
            return 0;
        }
        if (!emit_u16(c, (uint16_t)one_idx)) {
            pop_loop(c);
            return 0;
        }
        if (!emit_op(c, OP_BINARY)) {
            pop_loop(c);
            return 0;
        }
        if (!emit_u8(c, (uint8_t)'+')) {
            pop_loop(c);
            return 0;
        }
        if (!emit_op(c, OP_STORE_VAR)) {
            pop_loop(c);
            return 0;
        }
        if (!emit_u16(c, (uint16_t)i_idx)) {
            pop_loop(c);
            return 0;
        }
    }

    int loop_back_operand = emit_jump(c, OP_LOOP);
    if (loop_back_operand < 0) {
        pop_loop(c);
        return 0;
    }

    if (!patch_jump_backward(c, loop_back_operand, loop_start)) {
        pop_loop(c);
        return 0;
    }

    int false_exit = c->chunk->count;
    if (!patch_jump_forward(c, jfalse, false_exit)) {
        pop_loop(c);
        return 0;
    }

    if (!emit_op(c, OP_POP)) {
        pop_loop(c);
        return 0;
    }

    int loop_end = c->chunk->count;

    LoopCtx* lctx = top_loop(c);
    if (!lctx) {
        pop_loop(c);
        return 0;
    }

    for (int i = 0; i < lctx->breaks.count; i++) {
        if (!patch_jump_forward(c, lctx->breaks.items[i], loop_end)) {
            pop_loop(c);
            return 0;
        }
    }

    for (int i = 0; i < lctx->continues.count; i++) {
        if (!patch_jump_forward(c, lctx->continues.items[i], inc_start)) {
            pop_loop(c);
            return 0;
        }
    }

    pop_loop(c);
    (void)inc_target_placeholder;
    return 1;
}

static int compile_stmt_list(CompCtx* c, ASTStmtList* list) {
    if (!c || !list) return 1;
    for (ASTStmt* st = list->head; st; st = st->next) {
        switch (st->kind) {
            case AST_STMT_VAR_DECL: {
                if (!emit_trace_stmt(c, "var_decl")) return 0;
                if (!compile_expr(c, st->as.var_decl.init)) return 0;
                int name_idx = add_string_const(c, st->as.var_decl.var_name ? st->as.var_decl.var_name : "");
                if (name_idx < 0 || name_idx > 0xffff) return 0;
                if (!emit_op(c, OP_DECL_VAR)) return 0;
                if (!emit_u16(c, (uint16_t)name_idx)) return 0;
                if (!emit_u8(c, (uint8_t)st->as.var_decl.type)) return 0;
                if (!emit_u8(c, (uint8_t)(st->as.var_decl.is_const ? 1 : 0))) return 0;
                break;
            }
            case AST_STMT_VAR_ASSIGN: {
                if (!emit_trace_stmt(c, "var_assign")) return 0;
                if (!compile_expr(c, st->as.var_assign.value)) return 0;
                int name_idx = add_string_const(c, st->as.var_assign.var_name ? st->as.var_assign.var_name : "");
                if (name_idx < 0 || name_idx > 0xffff) return 0;
                if (!emit_op(c, OP_STORE_VAR)) return 0;
                if (!emit_u16(c, (uint16_t)name_idx)) return 0;
                break;
            }
            case AST_STMT_PRINT: {
                if (!emit_trace_stmt(c, "print")) return 0;
                if (!compile_expr(c, st->as.print.expr)) return 0;
                if (!emit_op(c, OP_PRINT)) return 0;
                break;
            }
            case AST_STMT_EXPR: {
                if (!emit_trace_stmt(c, "expr")) return 0;
                if (!compile_expr(c, st->as.expr.expr)) return 0;
                if (!emit_op(c, OP_POP)) return 0;
                break;
            }
            case AST_STMT_IF: {
                if (!emit_trace_stmt(c, "if")) return 0;
                if (!compile_expr(c, st->as.if_stmt.condition)) return 0;
                int jfalse = emit_jump(c, OP_JUMP_IF_FALSE);
                if (jfalse < 0) return 0;
                if (!emit_op(c, OP_POP)) return 0;

                if (!compile_stmt_list(c, st->as.if_stmt.then_branch)) return 0;
                int jend = emit_jump(c, OP_JUMP);
                if (jend < 0) return 0;

                int else_start = c->chunk->count;
                if (!patch_jump_forward(c, jfalse, else_start)) return 0;
                if (!emit_op(c, OP_POP)) return 0;
                if (st->as.if_stmt.else_branch) {
                    if (!compile_stmt_list(c, st->as.if_stmt.else_branch)) return 0;
                }
                int end = c->chunk->count;
                if (!patch_jump_forward(c, jend, end)) return 0;
                break;
            }
            case AST_STMT_WHILE: {
                if (!emit_trace_stmt(c, "while")) return 0;
                int loop_start = c->chunk->count;

                if (!compile_expr(c, st->as.while_stmt.condition)) return 0;
                int jfalse = emit_jump(c, OP_JUMP_IF_FALSE);
                if (jfalse < 0) return 0;
                if (!emit_op(c, OP_POP)) return 0;

                if (!push_loop(c, loop_start)) return 0;

                if (!compile_stmt_list(c, st->as.while_stmt.body)) {
                    pop_loop(c);
                    return 0;
                }

                int loop_back = emit_jump(c, OP_LOOP);
                if (loop_back < 0) {
                    pop_loop(c);
                    return 0;
                }
                if (!patch_jump_backward(c, loop_back, loop_start)) {
                    pop_loop(c);
                    return 0;
                }

                int false_exit = c->chunk->count;
                if (!patch_jump_forward(c, jfalse, false_exit)) {
                    pop_loop(c);
                    return 0;
                }
                if (!emit_op(c, OP_POP)) {
                    pop_loop(c);
                    return 0;
                }

                int loop_end = c->chunk->count;
                LoopCtx* lctx = top_loop(c);
                if (!lctx) {
                    pop_loop(c);
                    return 0;
                }

                for (int i = 0; i < lctx->breaks.count; i++) {
                    if (!patch_jump_forward(c, lctx->breaks.items[i], loop_end)) {
                        pop_loop(c);
                        return 0;
                    }
                }

                for (int i = 0; i < lctx->continues.count; i++) {
                    if (!patch_jump_forward(c, lctx->continues.items[i], loop_start)) {
                        pop_loop(c);
                        return 0;
                    }
                }

                pop_loop(c);
                break;
            }
            case AST_STMT_FOR: {
                if (!emit_trace_stmt(c, "for")) return 0;
                if (!compile_for_range(c, &st->as.for_stmt)) return 0;
                break;
            }
            case AST_STMT_BREAK: {
                if (!emit_trace_stmt(c, "break")) return 0;
                LoopCtx* l = top_loop(c);
                if (!l) {
                    printf("Error: break used outside loop\n");
                    return 0;
                }
                int j = emit_jump(c, OP_JUMP);
                if (j < 0) return 0;
                if (!int_list_push(&l->breaks, j)) return 0;
                break;
            }
            case AST_STMT_CONTINUE: {
                if (!emit_trace_stmt(c, "continue")) return 0;
                LoopCtx* l = top_loop(c);
                if (!l) {
                    printf("Error: continue used outside loop\n");
                    return 0;
                }
                int j = emit_jump(c, OP_JUMP);
                if (j < 0) return 0;
                if (!int_list_push(&l->continues, j)) return 0;
                break;
            }
            case AST_STMT_FUNC_DECL: {
                if (!emit_trace_stmt(c, "func_decl")) return 0;
                Function fn;
                memset(&fn, 0, sizeof(fn));
                fn.name = st->as.func_decl.name;
                fn.param_count = st->as.func_decl.param_count;
                fn.param_names = st->as.func_decl.param_names;
                fn.param_types = st->as.func_decl.param_types;
                fn.return_type = st->as.func_decl.return_type;
                fn.body = (void*)st->as.func_decl.body;
                fn.body_is_ast = 1;
                (void)register_function(&fn);
                break;
            }
            case AST_STMT_RETURN: {
                if (!emit_trace_stmt(c, "return")) return 0;
                if (!compile_expr(c, st->as.ret.expr)) return 0;
                if (!emit_op(c, OP_RETURN)) return 0;
                break;
            }
            default:
                printf("Error: Unsupported stmt kind\n");
                return 0;
        }
    }
    return 1;
}

int compile_program(ASTStmtList* program, BytecodeChunk* out_chunk) {
    if (!out_chunk) return 0;

    bc_chunk_init(out_chunk);

    CompCtx c;
    memset(&c, 0, sizeof(c));
    c.chunk = out_chunk;

    int ok = compile_stmt_list(&c, program);
    if (ok) ok = emit_op(&c, OP_END);

    for (int i = 0; i < c.loop_count; i++) {
        int_list_free(&c.loops[i].breaks);
        int_list_free(&c.loops[i].continues);
    }
    free(c.loops);

    if (!ok) {
        bc_chunk_free(out_chunk);
        return 0;
    }

    return 1;
}
