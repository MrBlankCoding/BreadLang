#ifndef RUNTIME_H
#define RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include "core/var.h"

typedef enum {
    BREAD_OBJ_STRING = 1,
    BREAD_OBJ_ARRAY = 2,
    BREAD_OBJ_DICT = 3,
    BREAD_OBJ_OPTIONAL = 4
} BreadObjKind;

typedef struct {
    uint32_t kind;
    uint32_t refcount;
} BreadObjHeader;

typedef struct BreadString {
    BreadObjHeader header;
    uint32_t len;
    uint32_t flags;
    char data[];
} BreadString;

#define BREAD_STRING_INTERNED   0x01
#define BREAD_STRING_SMALL      0x02
#define BREAD_STRING_SMALL_MAX  15

void* bread_alloc(size_t size);
void* bread_realloc(void* ptr, size_t new_size);
void bread_free(void* ptr);

BreadString* bread_string_new(const char* cstr);
BreadString* bread_string_new_len(const char* data, size_t len);
BreadString* bread_string_new_literal(const char* cstr);  // For string literals (interned)
const char* bread_string_cstr(const BreadString* s);
size_t bread_string_len(const BreadString* s);

void bread_string_retain(BreadString* s);
void bread_string_release(BreadString* s);

BreadString* bread_string_concat(const BreadString* a, const BreadString* b);
int bread_string_eq(const BreadString* a, const BreadString* b);
int bread_string_cmp(const BreadString* a, const BreadString* b);
char bread_string_get_char(const BreadString* s, size_t index);  // For string indexing
void bread_string_intern_init(void);
void bread_string_intern_cleanup(void);

struct BreadValue;
struct BreadArray;
struct BreadDict;
struct BreadOptional;

typedef struct BreadValue BreadValue;

int bread_add(const struct BreadValue* left, const struct BreadValue* right, struct BreadValue* out);
int bread_eq(const struct BreadValue* left, const struct BreadValue* right, int* out_bool);
void bread_print(const struct BreadValue* v);

void bread_value_set_nil(struct BreadValue* out);
void bread_value_set_bool(struct BreadValue* out, int v);
void bread_value_set_int(struct BreadValue* out, int v);
void bread_value_set_float(struct BreadValue* out, float v);
void bread_value_set_double(struct BreadValue* out, double v);
void bread_value_set_string(struct BreadValue* out, const char* cstr);
void bread_value_set_array(struct BreadValue* out, struct BreadArray* a);
void bread_value_set_dict(struct BreadValue* out, struct BreadDict* d);
void bread_value_set_optional(struct BreadValue* out, struct BreadOptional* o);

size_t bread_value_size(void);

void bread_value_copy(const struct BreadValue* in, struct BreadValue* out);
void bread_value_release_value(struct BreadValue* v);

int bread_is_truthy(const BreadValue* v);

int bread_unary_not(const BreadValue* in, BreadValue* out);
int bread_binary_op(char op, const BreadValue* left, const BreadValue* right, BreadValue* out);

int bread_coerce_value(VarType target, const BreadValue* in, BreadValue* out);

int bread_index_op(const BreadValue* target, const BreadValue* idx, BreadValue* out);
int bread_member_op(const BreadValue* target, const char* member, int is_opt, BreadValue* out);
int bread_method_call_op(const BreadValue* target, const char* name, int argc, const BreadValue* args, int is_opt, BreadValue* out);

int bread_dict_set_value(struct BreadDict* d, const BreadValue* key, const BreadValue* val);
int bread_array_append_value(struct BreadArray* a, const BreadValue* v);
int bread_array_set_value(struct BreadArray* a, int index, const BreadValue* v);

int bread_var_decl(const char* name, VarType type, int is_const, const BreadValue* init);
int bread_var_decl_if_missing(const char* name, VarType type, int is_const, const BreadValue* init);
int bread_var_assign(const char* name, const BreadValue* value);
int bread_var_load(const char* name, BreadValue* out);

void bread_push_scope(void);
void bread_pop_scope(void);

// Built-in range functions
struct BreadArray* bread_range_create(int start, int end, int step);
struct BreadArray* bread_range(int n);

// LLVM backend wrapper functions
int bread_array_get_value(struct BreadArray* a, int idx, struct BreadValue* out);
int bread_value_array_get(struct BreadValue* array_val, int idx, struct BreadValue* out);
int bread_value_array_length(struct BreadValue* array_val);

#endif
