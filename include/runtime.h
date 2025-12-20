#ifndef RUNTIME_H
#define RUNTIME_H

#include <stddef.h>
#include <stdint.h>

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
    char data[];
} BreadString;

void* bread_alloc(size_t size);
void* bread_realloc(void* ptr, size_t new_size);
void bread_free(void* ptr);

BreadString* bread_string_new(const char* cstr);
BreadString* bread_string_new_len(const char* data, size_t len);
const char* bread_string_cstr(const BreadString* s);
size_t bread_string_len(const BreadString* s);

void bread_string_retain(BreadString* s);
void bread_string_release(BreadString* s);

BreadString* bread_string_concat(const BreadString* a, const BreadString* b);
int bread_string_eq(const BreadString* a, const BreadString* b);
int bread_string_cmp(const BreadString* a, const BreadString* b);

struct BreadValue;

int bread_add(const struct BreadValue* left, const struct BreadValue* right, struct BreadValue* out);
int bread_eq(const struct BreadValue* left, const struct BreadValue* right, int* out_bool);
void bread_print(const struct BreadValue* v);

#endif
