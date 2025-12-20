#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/print.h"
#include "../include/var.h"
#include "../include/stmt.h"
#include "../include/function.h"

#define MAX_FILE_SIZE 65536

char* trim_main(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    
    FILE* file = fopen(argv[1], "r");
    if (!file) {
        printf("Error: Could not open file '%s'\n", argv[1]);
        return 1;
    }
    
    // Read entire file
    char* code = malloc(MAX_FILE_SIZE);
    if (!code) {
        printf("Error: Out of memory\n");
        fclose(file);
        return 1;
    }
    size_t bytes_read = fread(code, 1, MAX_FILE_SIZE - 1, file);
    code[bytes_read] = '\0';
    fclose(file);
    
    init_variables();
    init_functions();
    
    // Parse and execute statements
    StmtList* stmts = parse_statements(code);
    if (stmts) {
        (void)execute_statements(stmts, NULL);
        free_stmt_list(stmts);
    }
    
    free(code);
    cleanup_functions();
    cleanup_variables();
    return 0;
}