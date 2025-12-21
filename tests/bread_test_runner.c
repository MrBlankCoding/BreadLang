#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

static char* read_entire_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return NULL;
    }

    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static int files_equal_normalized_newlines(const char* a, const char* b) {
    size_t la = 0, lb = 0;
    char* ca = read_entire_file(a, &la);
    char* cb = read_entire_file(b, &lb);
    if (!ca || !cb) {
        free(ca);
        free(cb);
        return 0;
    }

    // Normalize CRLF -> LF in-place.
    size_t wa = 0;
    for (size_t i = 0; i < la; i++) {
        if (ca[i] != '\r') ca[wa++] = ca[i];
    }
    ca[wa] = '\0';

    size_t wb = 0;
    for (size_t i = 0; i < lb; i++) {
        if (cb[i] != '\r') cb[wb++] = cb[i];
    }
    cb[wb] = '\0';

    int eq = (wa == wb) && (memcmp(ca, cb, wa) == 0);
    free(ca);
    free(cb);
    return eq;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <breadlang_bin> <test.bread> <expected.txt>\n", argv[0]);
        return 2;
    }

    const char* breadlang_bin = argv[1];
    const char* test_src = argv[2];
    const char* expected = argv[3];

    const char* base = strrchr(test_src, '/');
    base = base ? (base + 1) : test_src;
    char name[512];
    snprintf(name, sizeof(name), "%s", base);
    char* dot = strrchr(name, '.');
    if (dot) *dot = '\0';

    char exe_path[1024];
    snprintf(exe_path, sizeof(exe_path), "bread_test_%s_exe", name);

    // Remove any previous artifacts
    remove(exe_path);

    // Compile test into a native executable
    // Note: using system() for portability/simplicity; file paths with spaces are not supported here.
    char compile_cmd[4096];
    snprintf(compile_cmd, sizeof(compile_cmd), "%s -o %s %s", breadlang_bin, exe_path, test_src);
    int compile_rc = system(compile_cmd);
    if (compile_rc != 0 || access(exe_path, X_OK) != 0) {
        fprintf(stderr, "BreadLang test compile failed: %s\n", test_src);
        remove(exe_path);
        return 1;
    }

    // Produce an output file in the build dir to keep the source tree clean.
    char tmp_out[1024];
    snprintf(tmp_out, sizeof(tmp_out), "bread_test_%s_output.tmp", name);
    remove(tmp_out);

    char run_cmd[4096];
    snprintf(run_cmd, sizeof(run_cmd), "./%s > %s 2>&1", exe_path, tmp_out);
    int run_rc = system(run_cmd);
    (void)run_rc;

    if (!files_equal_normalized_newlines(expected, tmp_out)) {
        fprintf(stderr, "BreadLang test failed: %s\n", test_src);

        size_t elen = 0, olen = 0;
        char* e = read_entire_file(expected, &elen);
        char* o = read_entire_file(tmp_out, &olen);
        if (e) {
            fprintf(stderr, "--- expected ---\n%.*s\n", (int)elen, e);
        }
        if (o) {
            fprintf(stderr, "--- actual ---\n%.*s\n", (int)olen, o);
        }
        free(e);
        free(o);

        remove(tmp_out);
        remove(exe_path);
        return 1;
    }

    remove(tmp_out);
    remove(exe_path);
    return 0;
}
