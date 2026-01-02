#ifndef LLVM_BACKEND_UTILS_H
#define LLVM_BACKEND_UTILS_H

#include <stddef.h>

int write_text_file(const char* path, const char* data);
int bread_is_project_root_dir(const char* dir);
int bread_find_project_root_from_exe_dir(const char* exe_dir, char* out_root, size_t cap);
int bread_get_exe_dir(char* out_dir, size_t cap);

#endif