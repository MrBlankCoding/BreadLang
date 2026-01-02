#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#elif defined(__linux__)
#endif

#include "backends/llvm_backend_utils.h"

int write_text_file(const char* path, const char* data) {
    if (!path || !data) return 0;
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    size_t n = strlen(data);
    if (n > 0) {
        if (fwrite(data, 1, n, f) != n) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

int bread_is_project_root_dir(const char* dir) {
    if (!dir) return 0;
    char src_dir[PATH_MAX];
    char inc_dir[PATH_MAX];
    char marker[PATH_MAX];
    snprintf(src_dir, sizeof(src_dir), "%s/breadlang/src", dir);
    snprintf(inc_dir, sizeof(inc_dir), "%s/breadlang/include", dir);
    snprintf(marker, sizeof(marker), "%s/breadlang/src/runtime/runtime.c", dir);
    if (access(src_dir, F_OK) != 0) return 0;
    if (access(inc_dir, F_OK) != 0) return 0;
    if (access(marker, R_OK) != 0) return 0;
    return 1;
}

int bread_find_project_root_from_exe_dir(const char* exe_dir, char* out_root, size_t cap) {
    if (!exe_dir || !out_root || cap == 0) return 0;

    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    snprintf(candidate, sizeof(candidate), "%s/..", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    snprintf(candidate, sizeof(candidate), "%s/../..", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    snprintf(candidate, sizeof(candidate), "%s/../../..", exe_dir);
    if (bread_is_project_root_dir(candidate)) {
        snprintf(out_root, cap, "%s", candidate);
        return 1;
    }

    return 0;
}

int bread_get_exe_dir(char* out_dir, size_t cap) {
    if (!out_dir || cap == 0) return 0;

#if defined(__APPLE__)
    uint32_t size = (uint32_t)cap;
    if (_NSGetExecutablePath(out_dir, &size) != 0) {
        return 0;
    }
    out_dir[cap - 1] = '\0';
    char* d = dirname(out_dir);
    if (!d) return 0;
    if (d != out_dir) {
        size_t n = strlen(d);
        if (n + 1 > cap) return 0;
        memmove(out_dir, d, n + 1);
    }
    return 1;
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", out_dir, cap - 1);
    if (n <= 0) return 0;
    out_dir[n] = '\0';
    char* d = dirname(out_dir);
    if (!d) return 0;
    if (d != out_dir) {
        size_t dn = strlen(d);
        if (dn + 1 > cap) return 0;
        memmove(out_dir, d, dn + 1);
    }
    return 1;
#else
    (void)out_dir;
    (void)cap;
    return 0;
#endif
}