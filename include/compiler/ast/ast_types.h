#ifndef AST_TYPES_H
#define AST_TYPES_H

#include "core/var.h"

// Type parsing functions
int parse_type_string(const char** code, char* out_buf, size_t out_sz);
int parse_type_token(const char** code, VarType* out_type);

#endif