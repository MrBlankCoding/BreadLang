#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ir/bread_ir.h"

static int bread_ir_program_reserve(BreadIRProgram* p, size_t needed) {
    if (!p) return 0;
    if (p->capacity >= needed) return 1;
    size_t new_cap = p->capacity ? p->capacity : 16;
    while (new_cap < needed) new_cap *= 2;
    BreadIRInst* next = (BreadIRInst*)realloc(p->insts, new_cap * sizeof(BreadIRInst));
    if (!next) return 0;
    p->insts = next;
    p->capacity = new_cap;
    return 1;
}

static int bread_ir_program_push(BreadIRProgram* p, BreadIRInst inst) {
    if (!p) return 0;
    if (!bread_ir_program_reserve(p, p->count + 1)) return 0;
    p->insts[p->count++] = inst;
    return 1;
}

void bread_ir_program_init(BreadIRProgram* p) {
    if (!p) return;
    p->insts = NULL;
    p->count = 0;
    p->capacity = 0;
}

void bread_ir_program_free(BreadIRProgram* p) {
    if (!p) return;
    free(p->insts);
    p->insts = NULL;
    p->count = 0;
    p->capacity = 0;
}

static int lower_expr_to_value(const ASTExpr* e, BreadIRValue* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));

    if (!e) {
        out->kind = BREAD_IR_VAL_NONE;
        return 1;
    }

    switch (e->kind) {
        case AST_EXPR_NIL:
            out->kind = BREAD_IR_VAL_NONE;
            return 1;
        case AST_EXPR_BOOL:
            out->kind = BREAD_IR_VAL_BOOL;
            out->as.bool_val = e->as.bool_val;
            return 1;
        case AST_EXPR_INT:
            out->kind = BREAD_IR_VAL_INT;
            out->as.int_val = e->as.int_val;
            return 1;
        case AST_EXPR_DOUBLE:
            out->kind = BREAD_IR_VAL_DOUBLE;
            out->as.double_val = e->as.double_val;
            return 1;
        case AST_EXPR_STRING:
            out->kind = BREAD_IR_VAL_STRING;
            out->as.string_val = e->as.string_val ? e->as.string_val : "";
            return 1;
        default:
            return 0;
    }
}

int bread_ir_lower_program(const ASTStmtList* program, BreadIRProgram* out) {
    if (!program || !out) return 0;

    bread_ir_program_init(out);

    for (ASTStmt* st = program->head; st; st = st->next) {
        if (st->kind == AST_STMT_PRINT) {
            BreadIRValue v;
            if (!lower_expr_to_value(st->as.print.expr, &v)) {
                printf("Error: LLVM backend currently supports only literal print expressions\n");
                bread_ir_program_free(out);
                return 0;
            }
            BreadIRInst inst;
            memset(&inst, 0, sizeof(inst));
            inst.op = BREAD_IR_OP_PRINT;
            inst.a = v;
            if (!bread_ir_program_push(out, inst)) {
                printf("Error: Out of memory\n");
                bread_ir_program_free(out);
                return 0;
            }
            continue;
        }

        if (st->kind == AST_STMT_EXPR || st->kind == AST_STMT_VAR_DECL || st->kind == AST_STMT_VAR_ASSIGN || st->kind == AST_STMT_IF || st->kind == AST_STMT_WHILE || st->kind == AST_STMT_FOR || st->kind == AST_STMT_BREAK || st->kind == AST_STMT_CONTINUE || st->kind == AST_STMT_FUNC_DECL || st->kind == AST_STMT_RETURN) {
            printf("Error: LLVM backend currently supports only print statements\n");
            bread_ir_program_free(out);
            return 0;
        }
    }

    BreadIRInst ret;
    memset(&ret, 0, sizeof(ret));
    ret.op = BREAD_IR_OP_RET;
    if (!bread_ir_program_push(out, ret)) {
        printf("Error: Out of memory\n");
        bread_ir_program_free(out);
        return 0;
    }

    return 1;
}
