#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"

#define INTERN_TABLE_SIZE 256
static BreadString* intern_table[INTERN_TABLE_SIZE];
static int intern_initialized = 0;

static BreadString* bread_string_alloc(size_t len) {
    size_t total = sizeof(BreadString) + len + 1;
    BreadString* s = (BreadString*)bread_alloc(total);
    if (!s) return NULL;
    s->header.kind = BREAD_OBJ_STRING;
    s->header.refcount = 1;
    s->len = (uint32_t)len;
    s->flags = (len <= BREAD_STRING_SMALL_MAX) ? BREAD_STRING_SMALL : 0;
    s->data[len] = '\0';
    return s;
}

static unsigned int bread_string_hash(const char* str, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)str[i];
    }
    return hash % INTERN_TABLE_SIZE;
}

void bread_string_intern_init(void) {
    if (intern_initialized) return;
    memset(intern_table, 0, sizeof(intern_table));
    intern_initialized = 1;
}

void bread_string_intern_cleanup(void) {
    if (!intern_initialized) return;
    for (int i = 0; i < INTERN_TABLE_SIZE; i++) {
        BreadString* s = intern_table[i];
        while (s) {
            BreadString* next = (BreadString*)(uintptr_t)s->header.refcount;
            s->header.refcount = 1;
            bread_string_release(s);
            s = next;
        }
        intern_table[i] = NULL;
    }
    intern_initialized = 0;
}

BreadString* bread_string_new_len(const char* data, size_t len) {
    BreadString* s = bread_string_alloc(len);
    if (!s) return NULL;
    if (len > 0 && data) {
        memcpy(s->data, data, len);
    }
    s->data[len] = '\0';
    return s;
}

BreadString* bread_string_new(const char* cstr) {
    if (!cstr) cstr = "";
    return bread_string_new_len(cstr, strlen(cstr));
}

BreadString* bread_string_new_literal(const char* cstr) {
    if (!cstr) cstr = "";
    size_t len = strlen(cstr);
    
    if (!intern_initialized) {
        bread_string_intern_init();
    }
    
    unsigned int hash = bread_string_hash(cstr, len);
    BreadString* s = intern_table[hash];
    while (s) {
        if (s->len == len && memcmp(s->data, cstr, len) == 0) {
            bread_string_retain(s);
            return s;
        }
        // Use a proper linked list traversal (this is simplified)
        break; // For now, just handle single entry per bucket
    }
    
    // Create new interned string
    s = bread_string_new_len(cstr, len);
    if (!s) return NULL;
    
    s->flags |= BREAD_STRING_INTERNED;
    
    // Add to intern table (simplified - just replace existing)
    if (intern_table[hash]) {
        bread_string_release(intern_table[hash]);
    }
    intern_table[hash] = s;
    bread_string_retain(s);
    
    return s;
}

const char* bread_string_cstr(const BreadString* s) {
    return s ? (const char*)s->data : "";
}

size_t bread_string_len(const BreadString* s) {
    return s ? (size_t)s->len : 0;
}

void bread_string_retain(BreadString* s) {
    if (s) s->header.refcount++;
}

void bread_string_release(BreadString* s) {
    if (!s) return;
    if (s->header.refcount == 0) return;
    s->header.refcount--;
    if (s->header.refcount > 0) return;
    bread_free(s);
}

BreadString* bread_string_concat(const BreadString* a, const BreadString* b) {
    size_t la = bread_string_len(a);
    size_t lb = bread_string_len(b);
    BreadString* out = bread_string_alloc(la + lb);
    if (!out) return NULL;
    if (la) memcpy(out->data, bread_string_cstr(a), la);
    if (lb) memcpy(out->data + la, bread_string_cstr(b), lb);
    out->data[la + lb] = '\0';
    return out;
}

int bread_string_eq(const BreadString* a, const BreadString* b) {
    size_t la = bread_string_len(a);
    size_t lb = bread_string_len(b);
    if (la != lb) return 0;
    if (la == 0) return 1;
    return memcmp(bread_string_cstr(a), bread_string_cstr(b), la) == 0;
}

int bread_string_cmp(const BreadString* a, const BreadString* b) {
    return strcmp(bread_string_cstr(a), bread_string_cstr(b));
}

char bread_string_get_char(const BreadString* s, size_t index) {
    if (!s) {
        BREAD_ERROR_SET_RUNTIME("String is null");
        return '\0';
    }
    if (index >= s->len) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "String index %zu out of bounds (string length: %zu)", index, s->len);
        BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
        return '\0';
    }
    return s->data[index];
}