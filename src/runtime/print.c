#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "runtime/print.h"
#include "core/var.h"
#include "compiler/parser/expr.h"
#include "core/value.h"
#include "runtime/runtime.h"

#define MAX_LINE 1024

char* trim(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void execute_print(char* line) {
    char* start = strstr(line, "print(");
    if (!start) return;
    
    start += 6;
    char* end = strrchr(start, ')');
    if (!end) {
        printf("Error: Missing closing parenthesis\n");
        return;
    }
    
    int len = end - start;
    char content[MAX_LINE];
    strncpy(content, start, len);
    content[len] = '\0';
    
    char* trimmed = trim(content);
    
    ExprResult result = evaluate_expression(trimmed);
    if (result.is_error) {
        return;
    }
    
    // Print the result
    switch (result.type) {
        case TYPE_STRING:
            printf("%s\n", bread_string_cstr(result.value.string_val));
            break;
        case TYPE_INT:
            printf("%lld\n", result.value.int_val);
            break;
        case TYPE_BOOL:
            printf("%s\n", result.value.bool_val ? "true" : "false");
            break;
        case TYPE_FLOAT:
            printf("%f\n", result.value.float_val);
            break;
        case TYPE_DOUBLE:
            printf("%lf\n", result.value.double_val);
            break;
        case TYPE_NIL:
            printf("nil\n");
            break;
        case TYPE_OPTIONAL: {
            BreadOptional* o = result.value.optional_val;
            if (!o || !o->is_some) {
                printf("nil\n");
            } else {
                ExprResult inner = bread_expr_result_from_value(bread_value_clone(o->value));
                // Recurse via printing logic (without re-evaluating)
                switch (inner.type) {
                    case TYPE_STRING:
                        printf("%s\n", bread_string_cstr(inner.value.string_val));
                        break;
                    case TYPE_INT:
                        printf("%lld\n", inner.value.int_val);
                        break;
                    case TYPE_BOOL:
                        printf("%s\n", inner.value.bool_val ? "true" : "false");
                        break;
                    case TYPE_FLOAT:
                        printf("%f\n", inner.value.float_val);
                        break;
                    case TYPE_DOUBLE:
                        printf("%lf\n", inner.value.double_val);
                        break;
                    case TYPE_NIL:
                        printf("nil\n");
                        break;
                    default:
                        // fall back to generic below
                        break;
                }
                BreadValue v = bread_value_from_expr_result(inner);
                bread_value_release(&v);
            }
            break;
        }
        case TYPE_ARRAY: {
            BreadArray* a = result.value.array_val;
            printf("[");
            int n = a ? a->count : 0;
            for (int i = 0; i < n; i++) {
                if (i > 0) printf(", ");
                BreadValue item = bread_value_clone(a->items[i]);
                ExprResult inner = bread_expr_result_from_value(item);
                switch (inner.type) {
                    case TYPE_STRING:
                        printf("\"%s\"", bread_string_cstr(inner.value.string_val));
                        break;
                    case TYPE_INT:
                        printf("%lld", inner.value.int_val);
                        break;
                    case TYPE_BOOL:
                        printf("%s", inner.value.bool_val ? "true" : "false");
                        break;
                    case TYPE_FLOAT:
                        printf("%f", inner.value.float_val);
                        break;
                    case TYPE_DOUBLE:
                        printf("%lf", inner.value.double_val);
                        break;
                    case TYPE_NIL:
                        printf("nil");
                        break;
                    default:
                        printf("nil");
                        break;
                }
                BreadValue iv = bread_value_from_expr_result(inner);
                bread_value_release(&iv);
            }
            printf("]\n");
            break;
        }
        case TYPE_DICT: {
            BreadDict* d = result.value.dict_val;
            printf("[");
            int n = d ? d->count : 0;
            int printed = 0;
            for (int i = 0; i < d->capacity && printed < n; i++) {
                if (d->entries[i].is_occupied && !d->entries[i].is_deleted) {
                    if (printed > 0) printf(", ");
                    if (d->entries[i].key.type == TYPE_STRING) {
                        printf("\"%s\": ", bread_string_cstr(d->entries[i].key.value.string_val));
                    } else {
                        printf("key: ");
                    }
                    BreadValue item = bread_value_clone(d->entries[i].value);
                    ExprResult inner = bread_expr_result_from_value(item);
                    switch (inner.type) {
                        case TYPE_STRING:
                            printf("\"%s\"", bread_string_cstr(inner.value.string_val));
                        break;
                    case TYPE_INT:
                        printf("%lld", inner.value.int_val);
                        break;
                    case TYPE_BOOL:
                        printf("%s", inner.value.bool_val ? "true" : "false");
                        break;
                    case TYPE_FLOAT:
                        printf("%f", inner.value.float_val);
                        break;
                    case TYPE_DOUBLE:
                        printf("%lf", inner.value.double_val);
                        break;
                    case TYPE_NIL:
                        printf("nil");
                        break;
                    default:
                        printf("nil");
                        break;
                }
                BreadValue iv = bread_value_from_expr_result(inner);
                bread_value_release(&iv);
                printed++;
                }
            }
            printf("]\n");
            break;
        }
        default:
            printf("Error: Unsupported type for print\n");
    }

    BreadValue v = bread_value_from_expr_result(result);
    bread_value_release(&v);
}