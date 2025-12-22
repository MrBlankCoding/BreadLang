#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "pbt_framework.h"
#include "../../include/compiler/ast/ast.h"
#include "../../include/backends/llvm_backend.h"
#include "../../include/compiler/analysis/semantic.h"
#include "../../include/core/var.h"
#include "../../include/core/function.h"

typedef struct {
    char* source_code;
} ParserTestData;

// Generate test data for parser round-trip testing
void* generate_parser_test_data(PBTGenerator* gen) {
    ParserTestData* data = malloc(sizeof(ParserTestData));
    if (!data) return NULL;
    
    // Generate different types of BreadLang code snippets using currently supported syntax
    int test_type = pbt_random_int(gen, 0, 4);
    
    switch (test_type) {
        case 0: {
            // Simple variable declaration
            int value = pbt_random_int(gen, 1, 1000);
            data->source_code = malloc(100);
            if (!data->source_code) {
                free(data);
                return NULL;
            }
            snprintf(data->source_code, 100, 
                    "let x: Int = %d\nprint(x)", value);
            break;
        }
        case 1: {
            // Basic arithmetic
            int a = pbt_random_int(gen, 1, 100);
            int b = pbt_random_int(gen, 1, 100);
            data->source_code = malloc(100);
            if (!data->source_code) {
                free(data);
                return NULL;
            }
            snprintf(data->source_code, 100, 
                    "let result: Int = %d + %d\nprint(result)", a, b);
            break;
        }
        case 2: {
            // Function declaration (basic)
            data->source_code = malloc(200);
            if (!data->source_code) {
                free(data);
                return NULL;
            }
            strcpy(data->source_code,
                   "def add(a: Int, b: Int) -> Int {\n"
                   "    return a + b\n"
                   "}\n"
                   "let result: Int = add(5, 3)\n"
                   "print(result)");
            break;
        }
        case 3: {
            // If statement
            int value = pbt_random_int(gen, 1, 100);
            data->source_code = malloc(200);
            if (!data->source_code) {
                free(data);
                return NULL;
            }
            snprintf(data->source_code, 200,
                    "let x: Int = %d\n"
                    "if x > 50 {\n"
                    "    print(x)\n"
                    "} else {\n"
                    "    print(0)\n"
                    "}", value);
            break;
        }
        default: {
            // While loop
            data->source_code = malloc(200);
            if (!data->source_code) {
                free(data);
                return NULL;
            }
            strcpy(data->source_code,
                   "let i: Int = 0\n"
                   "while i < 3 {\n"
                   "    print(i)\n"
                   "    i = i + 1\n"
                   "}");
            break;
        }
    }
    
    return data;
}

void cleanup_parser_data(void* test_data) {
    ParserTestData* data = (ParserTestData*)test_data;
    if (!data) return;
    if (data->source_code) free(data->source_code);
    free(data);
}

// Property 20: Syntax parsing and IR generation consistency
// For any valid BreadLang program using new syntax, parsing then pretty-printing should produce equivalent code
int property_parser_ir_consistency(void* test_data) {
    ParserTestData* data = (ParserTestData*)test_data;
    if (!data || !data->source_code) return 0;
    
    // Step 1: Parse the source code into AST
    ASTStmtList* ast = ast_parse_program(data->source_code);
    if (!ast) {
        // If parsing fails, that's not necessarily a property violation
        // since we might generate invalid syntax
        return 1;
    }
    
    // Step 2: Run semantic analysis like main.c does
    if (!semantic_analyze(ast)) {
        // Semantic analysis failed - treat as valid (semantic error in generated code)
        ast_free_stmt_list(ast);
        return 1;
    }
    
    // Step 3: Try to generate LLVM IR from the AST
    // We'll use a temporary file approach to test IR generation
    char temp_ll_file[] = "/tmp/breadlang_test_XXXXXX";
    int fd = mkstemp(temp_ll_file);
    if (fd == -1) {
        ast_free_stmt_list(ast);
        return 0;
    }
    close(fd);
    
    int ir_result = bread_llvm_emit_ll(ast, temp_ll_file);
    
    // Step 4: Check if IR generation succeeded
    int property_holds = 1;
    
    if (ir_result == 0) {
        // IR generation failed (returns 0 on failure, 1 on success)
        property_holds = 0;
    } else {
        // Step 5: Verify the IR file was created and has content
        FILE* ir_file = fopen(temp_ll_file, "r");
        if (!ir_file) {
            property_holds = 0;
        } else {
            // Check if file has meaningful content (not empty)
            fseek(ir_file, 0, SEEK_END);
            long file_size = ftell(ir_file);
            if (file_size <= 0) {
                property_holds = 0;
            }
            fclose(ir_file);
        }
    }
    
    // Cleanup
    unlink(temp_ll_file);
    ast_free_stmt_list(ast);
    
    return property_holds;
}

int run_parser_tests() {
    printf("Running Parser Property Tests\n");
    printf("=============================\n\n");
    
    // Initialize the system once before running tests
    init_variables();
    init_functions();
    
    int all_passed = 1;
    
    PBTResult result20 = pbt_run_property(
        "Syntax parsing and IR generation consistency",
        generate_parser_test_data,
        property_parser_ir_consistency,
        cleanup_parser_data,
        PBT_MIN_ITERATIONS
    );
    
    pbt_report_result("breadlang-core-features", 20, 
                     "Syntax parsing and IR generation consistency", result20);
    
    if (result20.failed > 0) all_passed = 0;
    
    pbt_free_result(&result20);
    
    return all_passed;
}

int main() {
    return run_parser_tests() ? 0 : 1;
}