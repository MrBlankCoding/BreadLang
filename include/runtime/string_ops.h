#ifndef STRING_OPS_H
#define STRING_OPS_H

#include "runtime/runtime.h"

BreadString* bread_string_new(const char* cstr);
BreadString* bread_string_new_len(const char* data, size_t len);
BreadString* bread_string_new_literal(const char* cstr);
const char* bread_string_cstr(const BreadString* s);
size_t bread_string_len(const BreadString* s);

void bread_string_retain(BreadString* s);
void bread_string_release(BreadString* s);

BreadString* bread_string_concat(const BreadString* a, const BreadString* b);
int bread_string_eq(const BreadString* a, const BreadString* b);
int bread_string_cmp(const BreadString* a, const BreadString* b);
char bread_string_get_char(const BreadString* s, size_t index);

void bread_string_intern_init(void);
void bread_string_intern_cleanup(void);

#endif // STRING_OPS_H