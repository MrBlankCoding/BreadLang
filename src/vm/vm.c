#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/vm.h"
#include "core/var.h"
#include "core/function.h"
#include "core/value.h"
#include "compiler/ast.h"
#include "runtime/runtime.h"

static void vm_stack_reset(VM* vm) {
    if (!vm) return;
    for (int i = 0; i < vm->stack_top; i++) {
        bread_value_release(&vm->stack[i]);
    }
    vm->stack_top = 0;
}

void vm_init(VM* vm) {
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));
}

void vm_free(VM* vm) {
    if (!vm) return;
    vm_stack_reset(vm);
}

static int vm_push(VM* vm, BreadValue v) {
    if (!vm) return 0;
    if (vm->stack_top >= (int)(sizeof(vm->stack) / sizeof(vm->stack[0]))) {
        printf("Error: VM stack overflow\n");
        bread_value_release(&v);
        vm->had_error = 1;
        return 0;
    }
    vm->stack[vm->stack_top++] = v;
    return 1;
}

static BreadValue vm_pop(VM* vm) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = TYPE_NIL;
    if (!vm || vm->stack_top <= 0) return v;
    vm->stack_top--;
    return vm->stack[vm->stack_top];
}

static BreadValue vm_peek(VM* vm) {
    BreadValue v;
    memset(&v, 0, sizeof(v));
    v.type = TYPE_NIL;
    if (!vm || vm->stack_top <= 0) return v;
    return vm->stack[vm->stack_top - 1];
}

static uint8_t vm_read_u8(VM* vm) {
    return *vm->ip++;
}

static uint16_t vm_read_u16(VM* vm) {
    uint16_t hi = (uint16_t)(*vm->ip++);
    uint16_t lo = (uint16_t)(*vm->ip++);
    return (uint16_t)((hi << 8) | lo);
}

static const char* vm_const_cstr(const BytecodeChunk* chunk, uint16_t idx) {
    if (!chunk) return "";
    if (idx >= (uint16_t)chunk->constants_count) return "";
    if (chunk->constants[idx].type != TYPE_STRING) return "";
    return bread_string_cstr(chunk->constants[idx].value.string_val);
}

static int vm_is_truthy(BreadValue v) {
    if (v.type != TYPE_BOOL) return 0;
    return v.value.bool_val ? 1 : 0;
}

static void vm_expr_release(ExprResult* r) {
    if (!r || r->is_error) return;
    BreadValue v = bread_value_from_expr_result(*r);
    bread_value_release(&v);
    memset(&r->value, 0, sizeof(r->value));
    r->type = TYPE_NIL;
}

static int vm_load_var(VM* vm, const char* name) {
    Variable* var = get_variable((char*)name);
    if (!var) {
        printf("Error: Unknown variable '%s'\n", name ? name : "");
        vm->had_error = 1;
        return 0;
    }

    BreadValue out;
    memset(&out, 0, sizeof(out));
    out.type = var->type;
    out.value = var->value;
    return vm_push(vm, bread_value_clone(out));
}

static int vm_store_var_from_value(VM* vm, const char* name, BreadValue v, int is_decl, VarType decl_type, int decl_is_const) {
    ExprResult r = bread_expr_result_from_value(bread_value_clone(v));

    int ok = 0;
    if (is_decl) {
        VarValue initial;
        memset(&initial, 0, sizeof(initial));
        ok = declare_variable_raw(name, decl_type, initial, decl_is_const);
        if (ok) {
            ok = bread_init_variable_from_expr_result(name, &r);
        }
    } else {
        ok = bread_assign_variable_from_expr_result(name, &r);
    }

    vm_expr_release(&r);
    bread_value_release(&v);

    if (!ok) {
        vm->had_error = 1;
        return 0;
    }

    return 1;
}

static int vm_binary_op(char op, BreadValue left, BreadValue right, BreadValue* out) {
    if (!out) {
        bread_value_release(&left);
        bread_value_release(&right);
        return 0;
    }

    if (op == '+') {
        BreadValue tmp;
        if (!bread_add(&left, &right, &tmp)) {
            bread_value_release(&left);
            bread_value_release(&right);
            return 0;
        }

        bread_value_release(&left);
        bread_value_release(&right);

        *out = tmp;
        return 1;
    }

    BreadValue l = left;
    BreadValue r = right;

    if (l.type == TYPE_INT && r.type == TYPE_DOUBLE) {
        l.type = TYPE_DOUBLE;
        l.value.double_val = (double)l.value.int_val;
    } else if (l.type == TYPE_DOUBLE && r.type == TYPE_INT) {
        r.type = TYPE_DOUBLE;
        r.value.double_val = (double)r.value.int_val;
    }

    if (op == '+' || op == '-' || op == '*' || op == '/' || op == '%') {
        if (l.type == TYPE_DOUBLE && r.type == TYPE_DOUBLE) {
            if (op == '/' && r.value.double_val == 0) {
                printf("Error: Division by zero\n");
                bread_value_release(&left);
                bread_value_release(&right);
                return 0;
            }
            if (op == '%') {
                printf("Error: Modulo operation not supported for floating point numbers\n");
                bread_value_release(&left);
                bread_value_release(&right);
                return 0;
            }
            double result_val = 0;
            switch (op) {
                case '+': result_val = l.value.double_val + r.value.double_val; break;
                case '-': result_val = l.value.double_val - r.value.double_val; break;
                case '*': result_val = l.value.double_val * r.value.double_val; break;
                case '/': result_val = l.value.double_val / r.value.double_val; break;
            }

            bread_value_release(&left);
            bread_value_release(&right);

            memset(out, 0, sizeof(*out));
            out->type = TYPE_DOUBLE;
            out->value.double_val = result_val;
            return 1;
        }

        if (l.type == TYPE_INT && r.type == TYPE_INT) {
            if (op == '/' && r.value.int_val == 0) {
                printf("Error: Division by zero\n");
                bread_value_release(&left);
                bread_value_release(&right);
                return 0;
            }
            if (op == '%' && r.value.int_val == 0) {
                printf("Error: Modulo by zero\n");
                bread_value_release(&left);
                bread_value_release(&right);
                return 0;
            }
            int result_val = 0;
            switch (op) {
                case '+': result_val = l.value.int_val + r.value.int_val; break;
                case '-': result_val = l.value.int_val - r.value.int_val; break;
                case '*': result_val = l.value.int_val * r.value.int_val; break;
                case '/': result_val = l.value.int_val / r.value.int_val; break;
                case '%': result_val = l.value.int_val % r.value.int_val; break;
            }

            bread_value_release(&left);
            bread_value_release(&right);

            memset(out, 0, sizeof(*out));
            out->type = TYPE_INT;
            out->value.int_val = result_val;
            return 1;
        }

        printf("Error: Invalid operand types for arithmetic operation\n");
        bread_value_release(&left);
        bread_value_release(&right);
        return 0;
    }

    if (op == '=' || op == '!' || op == '<' || op == '>') {
        int result_val = 0;
        if (l.type == TYPE_DOUBLE && r.type == TYPE_DOUBLE) {
            switch (op) {
                case '=': result_val = (l.value.double_val == r.value.double_val); break;
                case '!': result_val = (l.value.double_val != r.value.double_val); break;
                case '<': result_val = (l.value.double_val < r.value.double_val); break;
                case '>': result_val = (l.value.double_val > r.value.double_val); break;
            }
        } else if (l.type == TYPE_INT && r.type == TYPE_INT) {
            switch (op) {
                case '=': result_val = (l.value.int_val == r.value.int_val); break;
                case '!': result_val = (l.value.int_val != r.value.int_val); break;
                case '<': result_val = (l.value.int_val < r.value.int_val); break;
                case '>': result_val = (l.value.int_val > r.value.int_val); break;
            }
        } else if (l.type == TYPE_BOOL && r.type == TYPE_BOOL) {
            switch (op) {
                case '=': result_val = (l.value.bool_val == r.value.bool_val); break;
                case '!': result_val = (l.value.bool_val != r.value.bool_val); break;
                case '<': result_val = (l.value.bool_val < r.value.bool_val); break;
                case '>': result_val = (l.value.bool_val > r.value.bool_val); break;
            }
        } else if (l.type == TYPE_STRING && r.type == TYPE_STRING) {
            int cmp = bread_string_cmp(l.value.string_val, r.value.string_val);
            switch (op) {
                case '=': result_val = (cmp == 0); break;
                case '!': result_val = (cmp != 0); break;
                case '<': result_val = (cmp < 0); break;
                case '>': result_val = (cmp > 0); break;
            }
        } else {
            printf("Error: Cannot compare different types\n");
            bread_value_release(&left);
            bread_value_release(&right);
            return 0;
        }

        bread_value_release(&left);
        bread_value_release(&right);

        memset(out, 0, sizeof(*out));
        out->type = TYPE_BOOL;
        out->value.bool_val = result_val;
        return 1;
    }

    if (op == '&' || op == '|') {
        if (l.type != TYPE_BOOL || r.type != TYPE_BOOL) {
            printf("Error: Logical operations require boolean operands\n");
            bread_value_release(&left);
            bread_value_release(&right);
            return 0;
        }
        int result_val = 0;
        if (op == '&') result_val = (l.value.bool_val && r.value.bool_val);
        if (op == '|') result_val = (l.value.bool_val || r.value.bool_val);

        bread_value_release(&left);
        bread_value_release(&right);

        memset(out, 0, sizeof(*out));
        out->type = TYPE_BOOL;
        out->value.bool_val = result_val;
        return 1;
    }

    printf("Error: Unknown binary operator '%c'\n", op);
    bread_value_release(&left);
    bread_value_release(&right);
    return 0;
}

/* Removed unused helper `vm_print_simple_value` to avoid -Wunused-function warning. */

static int vm_print_value(BreadValue v) {
    bread_print(&v);
    return 1;
}

static int vm_call_function(VM* vm, const BytecodeChunk* chunk, const char* name, int argc) {
    (void)chunk;
    ExprResult* args = NULL;
    if (argc > 0) {
        args = malloc(sizeof(ExprResult) * (size_t)argc);
        if (!args) {
            printf("Error: Out of memory\n");
            vm->had_error = 1;
            return 0;
        }
    }

    for (int i = argc - 1; i >= 0; i--) {
        BreadValue v = vm_pop(vm);
        args[i] = bread_expr_result_from_value(v);
    }

    ExprResult out;
    memset(&out, 0, sizeof(out));
    out.is_error = 1;

    if (name && strcmp(name, "range") == 0) {
        if (argc != 1) {
            printf("Error: Function 'range' expected 1 args but got %d\n", argc);
            vm->had_error = 1;
        } else if (args[0].type != TYPE_INT) {
            printf("Error: range() expects Int\n");
            vm->had_error = 1;
        } else {
            out.is_error = 0;
            out.type = TYPE_INT;
            out.value.int_val = args[0].value.int_val;
        }
    } else {
        out = call_function_values(name, argc, args);
        if (out.is_error) vm->had_error = 1;
    }

    for (int i = 0; i < argc; i++) {
        vm_expr_release(&args[i]);
    }
    free(args);

    if (out.is_error) return 0;
    return vm_push(vm, bread_value_from_expr_result(out));
}

int vm_run(VM* vm, const BytecodeChunk* chunk, int is_function, ExprResult* out_return) {
    (void)is_function;

    if (!vm || !chunk) return -1;

    vm_stack_reset(vm);
    vm->chunk = chunk;
    vm->ip = chunk->code;
    vm->had_error = 0;

    for (;;) {
        uint8_t raw = vm_read_u8(vm);
        OpCode op = (OpCode)raw;

        switch (op) {
            case OP_CONSTANT: {
                uint16_t idx = vm_read_u16(vm);
                if (idx >= (uint16_t)chunk->constants_count) {
                    printf("Error: Bad constant index\n");
                    vm->had_error = 1;
                    return -1;
                }
                if (!vm_push(vm, bread_value_clone(chunk->constants[idx]))) return -1;
                break;
            }
            case OP_NIL: {
                BreadValue v;
                memset(&v, 0, sizeof(v));
                v.type = TYPE_NIL;
                if (!vm_push(vm, v)) return -1;
                break;
            }
            case OP_TRUE: {
                BreadValue v;
                memset(&v, 0, sizeof(v));
                v.type = TYPE_BOOL;
                v.value.bool_val = 1;
                if (!vm_push(vm, v)) return -1;
                break;
            }
            case OP_FALSE: {
                BreadValue v;
                memset(&v, 0, sizeof(v));
                v.type = TYPE_BOOL;
                v.value.bool_val = 0;
                if (!vm_push(vm, v)) return -1;
                break;
            }
            case OP_DUP: {
                BreadValue top = vm_peek(vm);
                if (!vm_push(vm, bread_value_clone(top))) return -1;
                break;
            }
            case OP_POP: {
                BreadValue v = vm_pop(vm);
                bread_value_release(&v);
                break;
            }
            case OP_TRACE: {
                uint16_t msg_idx = vm_read_u16(vm);
                if (bread_get_trace()) {
                    fprintf(stderr, "trace: %s\n", vm_const_cstr(chunk, msg_idx));
                }
                break;
            }
            case OP_LOAD_VAR: {
                uint16_t idx = vm_read_u16(vm);
                const char* name = vm_const_cstr(chunk, idx);
                if (!vm_load_var(vm, name)) return -1;
                break;
            }
            case OP_DECL_VAR: {
                uint16_t idx = vm_read_u16(vm);
                VarType t = (VarType)vm_read_u8(vm);
                int is_const = (int)vm_read_u8(vm);
                const char* name = vm_const_cstr(chunk, idx);
                BreadValue v = vm_pop(vm);
                if (!vm_store_var_from_value(vm, name, v, 1, t, is_const)) return -1;
                break;
            }
            case OP_DECL_VAR_IF_MISSING: {
                uint16_t idx = vm_read_u16(vm);
                VarType t = (VarType)vm_read_u8(vm);
                int is_const = (int)vm_read_u8(vm);
                const char* name = vm_const_cstr(chunk, idx);

                if (get_variable((char*)name)) {
                    BreadValue v = vm_pop(vm);
                    bread_value_release(&v);
                    break;
                }

                BreadValue v = vm_pop(vm);
                if (!vm_store_var_from_value(vm, name, v, 1, t, is_const)) return -1;
                break;
            }
            case OP_STORE_VAR: {
                uint16_t idx = vm_read_u16(vm);
                const char* name = vm_const_cstr(chunk, idx);
                BreadValue v = vm_pop(vm);
                if (!vm_store_var_from_value(vm, name, v, 0, TYPE_NIL, 0)) return -1;
                break;
            }
            case OP_PRINT: {
                BreadValue v = vm_pop(vm);
                (void)vm_print_value(v);
                bread_value_release(&v);
                break;
            }
            case OP_BINARY: {
                char bop = (char)vm_read_u8(vm);
                BreadValue right = vm_pop(vm);
                BreadValue left = vm_pop(vm);
                BreadValue out;
                memset(&out, 0, sizeof(out));
                out.type = TYPE_NIL;
                if (!vm_binary_op(bop, left, right, &out)) {
                    vm->had_error = 1;
                    return -1;
                }
                if (!vm_push(vm, out)) return -1;
                break;
            }
            case OP_NOT: {
                BreadValue v = vm_pop(vm);
                if (v.type != TYPE_BOOL) {
                    printf("Error: Logical NOT requires boolean operand\n");
                    bread_value_release(&v);
                    vm->had_error = 1;
                    return -1;
                }
                int b = !v.value.bool_val;
                bread_value_release(&v);
                BreadValue out;
                memset(&out, 0, sizeof(out));
                out.type = TYPE_BOOL;
                out.value.bool_val = b;
                if (!vm_push(vm, out)) return -1;
                break;
            }
            case OP_CALL: {
                uint16_t name_idx = vm_read_u16(vm);
                int argc = (int)vm_read_u8(vm);
                const char* name = vm_const_cstr(chunk, name_idx);
                if (!vm_call_function(vm, chunk, name, argc)) return -1;
                break;
            }
            case OP_ARRAY: {
                uint16_t count = vm_read_u16(vm);
                BreadArray* a = bread_array_new();
                if (!a) {
                    printf("Error: Out of memory\n");
                    vm->had_error = 1;
                    return -1;
                }

                BreadValue* items = NULL;
                if (count > 0) {
                    items = malloc(sizeof(BreadValue) * (size_t)count);
                    if (!items) {
                        printf("Error: Out of memory\n");
                        bread_array_release(a);
                        vm->had_error = 1;
                        return -1;
                    }
                }

                for (int i = (int)count - 1; i >= 0; i--) {
                    items[i] = vm_pop(vm);
                }

                for (int i = 0; i < (int)count; i++) {
                    if (!bread_array_append(a, items[i])) {
                        printf("Error: Out of memory\n");
                        for (int j = 0; j < (int)count; j++) bread_value_release(&items[j]);
                        free(items);
                        bread_array_release(a);
                        vm->had_error = 1;
                        return -1;
                    }
                    bread_value_release(&items[i]);
                }
                free(items);

                BreadValue out;
                memset(&out, 0, sizeof(out));
                out.type = TYPE_ARRAY;
                out.value.array_val = a;
                if (!vm_push(vm, out)) {
                    bread_array_release(a);
                    return -1;
                }
                break;
            }
            case OP_DICT: {
                uint16_t count = vm_read_u16(vm);
                BreadDict* d = bread_dict_new();
                if (!d) {
                    printf("Error: Out of memory\n");
                    vm->had_error = 1;
                    return -1;
                }

                for (int i = (int)count - 1; i >= 0; i--) {
                    BreadValue val = vm_pop(vm);
                    BreadValue key = vm_pop(vm);
                    if (key.type != TYPE_STRING) {
                        printf("Error: Dictionary keys must be strings\n");
                        bread_value_release(&key);
                        bread_value_release(&val);
                        bread_dict_release(d);
                        vm->had_error = 1;
                        return -1;
                    }
                    if (!bread_dict_set(d, bread_string_cstr(key.value.string_val), val)) {
                        printf("Error: Out of memory\n");
                        bread_value_release(&key);
                        bread_value_release(&val);
                        bread_dict_release(d);
                        vm->had_error = 1;
                        return -1;
                    }
                    bread_value_release(&key);
                    bread_value_release(&val);
                }

                BreadValue out;
                memset(&out, 0, sizeof(out));
                out.type = TYPE_DICT;
                out.value.dict_val = d;
                if (!vm_push(vm, out)) {
                    bread_dict_release(d);
                    return -1;
                }
                break;
            }
            case OP_INDEX: {
                BreadValue idx = vm_pop(vm);
                BreadValue target = vm_pop(vm);

                BreadValue real_target = target;
                int target_owned = 0;

                if (real_target.type == TYPE_OPTIONAL) {
                    BreadOptional* o = real_target.value.optional_val;
                    if (!o || !o->is_some) {
                        bread_value_release(&idx);
                        bread_value_release(&real_target);
                        BreadValue nil;
                        memset(&nil, 0, sizeof(nil));
                        nil.type = TYPE_NIL;
                        if (!vm_push(vm, nil)) return -1;
                        break;
                    }
                    BreadValue inner = bread_value_clone(o->value);
                    bread_value_release(&real_target);
                    real_target = inner;
                    target_owned = 1;
                }

                BreadValue out;
                memset(&out, 0, sizeof(out));
                out.type = TYPE_NIL;

                if (real_target.type == TYPE_ARRAY) {
                    if (idx.type != TYPE_INT) {
                        printf("Error: Array index must be Int\n");
                        bread_value_release(&idx);
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }
                    BreadValue* at = bread_array_get(real_target.value.array_val, idx.value.int_val);
                    if (at) out = bread_value_clone(*at);
                } else if (real_target.type == TYPE_DICT) {
                    if (idx.type != TYPE_STRING) {
                        printf("Error: Dictionary key must be String\n");
                        bread_value_release(&idx);
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }
                    BreadValue* v = bread_dict_get(real_target.value.dict_val, bread_string_cstr(idx.value.string_val));
                    if (v) out = bread_value_clone(*v);
                } else {
                    printf("Error: Type does not support indexing\n");
                    bread_value_release(&idx);
                    if (target_owned) bread_value_release(&real_target);
                    else bread_value_release(&target);
                    vm->had_error = 1;
                    return -1;
                }

                bread_value_release(&idx);
                if (target_owned) bread_value_release(&real_target);
                else bread_value_release(&target);

                if (!vm_push(vm, out)) {
                    bread_value_release(&out);
                    return -1;
                }
                break;
            }
            case OP_MEMBER: {
                uint16_t member_idx = vm_read_u16(vm);
                int is_opt = (int)vm_read_u8(vm);
                const char* member = vm_const_cstr(chunk, member_idx);

                BreadValue target = vm_pop(vm);
                BreadValue real_target = target;
                int target_owned = 0;

                if (is_opt) {
                    if (real_target.type == TYPE_NIL) {
                        bread_value_release(&real_target);
                        BreadValue nil;
                        memset(&nil, 0, sizeof(nil));
                        nil.type = TYPE_NIL;
                        if (!vm_push(vm, nil)) return -1;
                        break;
                    }
                    if (real_target.type == TYPE_OPTIONAL) {
                        BreadOptional* o = real_target.value.optional_val;
                        if (!o || !o->is_some) {
                            bread_value_release(&real_target);
                            BreadValue nil;
                            memset(&nil, 0, sizeof(nil));
                            nil.type = TYPE_NIL;
                            if (!vm_push(vm, nil)) return -1;
                            break;
                        }
                        BreadValue inner = bread_value_clone(o->value);
                        bread_value_release(&real_target);
                        real_target = inner;
                        target_owned = 1;
                    }
                }

                BreadValue out;
                memset(&out, 0, sizeof(out));
                out.type = TYPE_NIL;

                if (member && strcmp(member, "length") == 0) {
                    if (real_target.type == TYPE_ARRAY) {
                        out.type = TYPE_INT;
                        out.value.int_val = real_target.value.array_val ? real_target.value.array_val->count : 0;
                    } else if (real_target.type == TYPE_DICT) {
                        out.type = TYPE_INT;
                        out.value.int_val = real_target.value.dict_val ? real_target.value.dict_val->count : 0;
                    } else {
                        printf("Error: length is only supported on arrays and dictionaries\n");
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }
                } else if (real_target.type == TYPE_DICT) {
                    BreadValue* v = bread_dict_get(real_target.value.dict_val, member ? member : "");
                    if (v) out = bread_value_clone(*v);
                } else {
                    if (is_opt) {
                        out.type = TYPE_NIL;
                    } else {
                        printf("Error: Unsupported member access\n");
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }
                }

                if (target_owned) bread_value_release(&real_target);
                else bread_value_release(&target);

                if (!vm_push(vm, out)) {
                    bread_value_release(&out);
                    return -1;
                }
                break;
            }
            case OP_METHOD_CALL: {
                uint16_t name_idx = vm_read_u16(vm);
                int argc = (int)vm_read_u8(vm);
                int is_opt = (int)vm_read_u8(vm);
                const char* name = vm_const_cstr(chunk, name_idx);

                BreadValue* args = NULL;
                if (argc > 0) {
                    args = malloc(sizeof(BreadValue) * (size_t)argc);
                    if (!args) {
                        printf("Error: Out of memory\n");
                        vm->had_error = 1;
                        return -1;
                    }
                }
                for (int i = argc - 1; i >= 0; i--) {
                    args[i] = vm_pop(vm);
                }

                BreadValue target = vm_pop(vm);
                BreadValue real_target = target;
                int target_owned = 0;

                if (is_opt) {
                    if (real_target.type == TYPE_NIL) {
                        for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                        free(args);
                        bread_value_release(&real_target);
                        BreadValue nil;
                        memset(&nil, 0, sizeof(nil));
                        nil.type = TYPE_NIL;
                        if (!vm_push(vm, nil)) return -1;
                        break;
                    }
                    if (real_target.type == TYPE_OPTIONAL) {
                        BreadOptional* o = real_target.value.optional_val;
                        if (!o || !o->is_some) {
                            for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                            free(args);
                            bread_value_release(&real_target);
                            BreadValue nil;
                            memset(&nil, 0, sizeof(nil));
                            nil.type = TYPE_NIL;
                            if (!vm_push(vm, nil)) return -1;
                            break;
                        }
                        BreadValue inner = bread_value_clone(o->value);
                        bread_value_release(&real_target);
                        real_target = inner;
                        target_owned = 1;
                    }
                }

                if (name && strcmp(name, "append") == 0) {
                    if (real_target.type != TYPE_ARRAY) {
                        printf("Error: append() is only supported on arrays\n");
                        for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                        free(args);
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }
                    if (argc != 1) {
                        printf("Error: append() expects 1 argument\n");
                        for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                        free(args);
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }

                    if (!bread_array_append(real_target.value.array_val, args[0])) {
                        printf("Error: Out of memory\n");
                        for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                        free(args);
                        if (target_owned) bread_value_release(&real_target);
                        else bread_value_release(&target);
                        vm->had_error = 1;
                        return -1;
                    }

                    for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                    free(args);
                    if (target_owned) bread_value_release(&real_target);
                    else bread_value_release(&target);

                    BreadValue nil;
                    memset(&nil, 0, sizeof(nil));
                    nil.type = TYPE_NIL;
                    if (!vm_push(vm, nil)) return -1;
                    break;
                }

                for (int i = 0; i < argc; i++) bread_value_release(&args[i]);
                free(args);
                if (target_owned) bread_value_release(&real_target);
                else bread_value_release(&target);

                if (is_opt) {
                    BreadValue nil;
                    memset(&nil, 0, sizeof(nil));
                    nil.type = TYPE_NIL;
                    if (!vm_push(vm, nil)) return -1;
                    break;
                }

                printf("Error: Unsupported method call\n");
                vm->had_error = 1;
                return -1;
            }
            case OP_JUMP: {
                uint16_t off = vm_read_u16(vm);
                vm->ip += off;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t off = vm_read_u16(vm);
                BreadValue cond = vm_peek(vm);
                int is_false = !vm_is_truthy(cond);
                if (is_false) vm->ip += off;
                break;
            }
            case OP_LOOP: {
                uint16_t off = vm_read_u16(vm);
                vm->ip -= off;
                break;
            }
            case OP_RETURN: {
                BreadValue v = vm_pop(vm);
                if (out_return) {
                    *out_return = bread_expr_result_from_value(bread_value_clone(v));
                    out_return->is_error = 0;
                }
                bread_value_release(&v);
                return vm->had_error ? -1 : 0;
            }
            case OP_END: {
                if (out_return) {
                    memset(out_return, 0, sizeof(*out_return));
                    out_return->is_error = 0;
                    out_return->type = TYPE_NIL;
                }
                return vm->had_error ? -1 : 0;
            }

            default:
                printf("Error: Unimplemented opcode %d\n", (int)op);
                vm->had_error = 1;
                return -1;
        }
    }
}
