#include <stdio.h>
#include <stdlib.h>

#include "compiler/ast/ast.h"
#include "compiler/ast/ast_dump.h"

static void ast_dump_expr(const ASTExpr* e, FILE* out) {
    if (!out) return;
    if (!e) {
        fprintf(out, "<null>");
        return;
    }

    switch (e->kind) {
        case AST_EXPR_NIL:
            fprintf(out, "nil");
            break;
        case AST_EXPR_BOOL:
            fprintf(out, e->as.bool_val ? "true" : "false");
            break;
        case AST_EXPR_INT:
            fprintf(out, "%d", e->as.int_val);
            break;
        case AST_EXPR_DOUBLE:
            fprintf(out, "%lf", e->as.double_val);
            break;
        case AST_EXPR_STRING:
            fprintf(out, "\"%s\"", e->as.string_val ? e->as.string_val : "");
            break;
        case AST_EXPR_STRING_LITERAL:
            fprintf(out, "\"%s\"", e->as.string_literal.value ? e->as.string_literal.value : "");
            break;
        case AST_EXPR_VAR:
            fprintf(out, "%s", e->as.var_name ? e->as.var_name : "");
            break;
        case AST_EXPR_BINARY:
            fprintf(out, "(");
            ast_dump_expr(e->as.binary.left, out);
            fprintf(out, " %c ", e->as.binary.op);
            ast_dump_expr(e->as.binary.right, out);
            fprintf(out, ")");
            break;
        case AST_EXPR_UNARY:
            fprintf(out, "(%c", e->as.unary.op);
            ast_dump_expr(e->as.unary.operand, out);
            fprintf(out, ")");
            break;
        case AST_EXPR_CALL:
            fprintf(out, "%s(", e->as.call.name ? e->as.call.name : "");
            for (int i = 0; i < e->as.call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.call.args[i], out);
            }
            fprintf(out, ")");
            break;
        case AST_EXPR_ARRAY:
            fprintf(out, "[");
            for (int i = 0; i < e->as.array.item_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.array.items[i], out);
            }
            fprintf(out, "]");
            break;
        case AST_EXPR_ARRAY_LITERAL:
            fprintf(out, "[");
            for (int i = 0; i < e->as.array_literal.element_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.array_literal.elements[i], out);
            }
            fprintf(out, "]");
            break;
        case AST_EXPR_DICT:
            fprintf(out, "[");
            for (int i = 0; i < e->as.dict.entry_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.dict.entries[i].key, out);
                fprintf(out, ": ");
                ast_dump_expr(e->as.dict.entries[i].value, out);
            }
            fprintf(out, "]");
            break;
        case AST_EXPR_INDEX:
            ast_dump_expr(e->as.index.target, out);
            fprintf(out, "[");
            ast_dump_expr(e->as.index.index, out);
            fprintf(out, "]");
            break;
        case AST_EXPR_MEMBER:
            ast_dump_expr(e->as.member.target, out);
            fprintf(out, "%s%s", e->as.member.is_optional_chain ? "?." : ".", e->as.member.member ? e->as.member.member : "");
            break;
        case AST_EXPR_METHOD_CALL:
            ast_dump_expr(e->as.method_call.target, out);
            fprintf(out, "%s%s(", e->as.method_call.is_optional_chain ? "?." : ".", e->as.method_call.name ? e->as.method_call.name : "");
            for (int i = 0; i < e->as.method_call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                ast_dump_expr(e->as.method_call.args[i], out);
            }
            fprintf(out, ")");
            break;
        default:
            fprintf(out, "<expr>");
            break;
    }
}

void ast_dump_stmt_list(const ASTStmtList* stmts, FILE* out) {
    if (!out || !stmts) return;
    
    ASTStmt* cur = stmts->head;
    while (cur) {
        switch (cur->kind) {
            case AST_STMT_VAR_DECL:
                fprintf(out, "%s %s: %s = ", 
                    cur->as.var_decl.is_const ? "const" : "let",
                    cur->as.var_decl.var_name ? cur->as.var_decl.var_name : "",
                    cur->as.var_decl.type_str ? cur->as.var_decl.type_str : "");
                ast_dump_expr(cur->as.var_decl.init, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_VAR_ASSIGN:
                fprintf(out, "%s = ", cur->as.var_assign.var_name ? cur->as.var_assign.var_name : "");
                ast_dump_expr(cur->as.var_assign.value, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_PRINT:
                fprintf(out, "print(");
                ast_dump_expr(cur->as.print.expr, out);
                fprintf(out, ")\n");
                break;
            case AST_STMT_EXPR:
                ast_dump_expr(cur->as.expr.expr, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_IF:
                fprintf(out, "if ");
                ast_dump_expr(cur->as.if_stmt.condition, out);
                fprintf(out, " {\n");
                ast_dump_stmt_list(cur->as.if_stmt.then_branch, out);
                fprintf(out, "}");
                if (cur->as.if_stmt.else_branch) {
                    fprintf(out, " else {\n");
                    ast_dump_stmt_list(cur->as.if_stmt.else_branch, out);
                    fprintf(out, "}");
                }
                fprintf(out, "\n");
                break;
            case AST_STMT_WHILE:
                fprintf(out, "while ");
                ast_dump_expr(cur->as.while_stmt.condition, out);
                fprintf(out, " {\n");
                ast_dump_stmt_list(cur->as.while_stmt.body, out);
                fprintf(out, "}\n");
                break;
            case AST_STMT_FOR:
                fprintf(out, "for %s in ", cur->as.for_stmt.var_name ? cur->as.for_stmt.var_name : "");
                ast_dump_expr(cur->as.for_stmt.range_expr, out);
                fprintf(out, " {\n");
                ast_dump_stmt_list(cur->as.for_stmt.body, out);
                fprintf(out, "}\n");
                break;
            case AST_STMT_FOR_IN:
                fprintf(out, "for %s in ", cur->as.for_in_stmt.var_name ? cur->as.for_in_stmt.var_name : "");
                ast_dump_expr(cur->as.for_in_stmt.iterable, out);
                fprintf(out, " {\n");
                ast_dump_stmt_list(cur->as.for_in_stmt.body, out);
                fprintf(out, "}\n");
                break;
            case AST_STMT_FUNC_DECL:
                fprintf(out, "func %s(", cur->as.func_decl.name ? cur->as.func_decl.name : "");
                for (int i = 0; i < cur->as.func_decl.param_count; i++) {
                    if (i > 0) fprintf(out, ", ");
                    fprintf(out, "%s", cur->as.func_decl.param_names[i] ? cur->as.func_decl.param_names[i] : "");
                    if (cur->as.func_decl.param_defaults && cur->as.func_decl.param_defaults[i]) {
                        fprintf(out, " = ");
                        ast_dump_expr(cur->as.func_decl.param_defaults[i], out);
                    }
                }
                fprintf(out, ") {\n");
                ast_dump_stmt_list(cur->as.func_decl.body, out);
                fprintf(out, "}\n");
                break;
            case AST_STMT_RETURN:
                fprintf(out, "return ");
                ast_dump_expr(cur->as.ret.expr, out);
                fprintf(out, "\n");
                break;
            case AST_STMT_BREAK:
                fprintf(out, "break\n");
                break;
            case AST_STMT_CONTINUE:
                fprintf(out, "continue\n");
                break;
            default:
                fprintf(out, "<stmt>\n");
                break;
        }
        cur = cur->next;
    }
}