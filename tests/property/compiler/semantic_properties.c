#include "../framework/pbt_framework.h"
#include "compiler/analysis/semantic.h"
#include "compiler/ast/ast.h"
#include "runtime/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Property: Semantic analysis should catch type mismatches
static bool test_type_mismatch_detection(void) {
    bread_error_init();
    
    // Parse invalid code with type mismatch
    const char* invalid_code = "let x: Int = \"string\"";
    ASTStmtList* program = ast_parse_program(invalid_code);
    
    if (!program) {
        bread_error_cleanup();
        return true; // Should fail to parse or analyze
    }
    
    bool result = !semantic_analyze(program);
    
    ast_free_stmt_list(program);
    bread_error_cleanup();
    return result;
}

// Property: Empty container literals must be explicitly typed
static bool test_empty_container_literals_require_type(void) {
    bread_error_init();

    const char* invalid_code =
        "let a = []\n"
        "let d = [:]\n";

    ASTStmtList* program = ast_parse_program(invalid_code);
    if (!program) {
        bread_error_cleanup();
        return true;
    }

    bool result = !semantic_analyze(program);

    ast_free_stmt_list(program);
    bread_error_cleanup();
    return result;
}

// Property: Indexing/member access must be type-correct
static bool test_invalid_index_member_rejected(void) {
    bread_error_init();

    const char* invalid_code =
        "let x: Int = 1\n"
        "let y = x[0]\n";

    ASTStmtList* program = ast_parse_program(invalid_code);
    if (!program) {
        bread_error_cleanup();
        return true;
    }

    bool result = !semantic_analyze(program);

    ast_free_stmt_list(program);
    bread_error_cleanup();
    return result;
}

// Property: Valid programs should pass semantic analysis
static bool test_valid_program_analysis(void) {
    bread_error_init();
    
    const char* valid_code = 
        "let x: Int = 42\n"
        "let y: String = \"hello\"\n"
        "def add(a: Int, b: Int) -> Int {\n"
        "    return a + b\n"
        "}\n"
        "let result: Int = add(x, 10)\n";
    
    ASTStmtList* program = ast_parse_program(valid_code);
    
    if (!program) {
        bread_error_cleanup();
        return false;
    }
    
    bool result = semantic_analyze(program);
    
    ast_free_stmt_list(program);
    bread_error_cleanup();
    return result;
}

// Property: Undefined variable usage should be caught
static bool test_undefined_variable_detection(void) {
    bread_error_init();
    
    const char* invalid_code = "print(undefined_variable)";
    ASTStmtList* program = ast_parse_program(invalid_code);
    
    if (!program) {
        bread_error_cleanup();
        return true; // Should fail
    }
    
    bool result = !semantic_analyze(program);
    
    ast_free_stmt_list(program);
    bread_error_cleanup();
    return result;
}

// Property: Function signature mismatches should be caught
static bool test_function_signature_validation(void) {
    bread_error_init();
    
    const char* invalid_code = 
        "def test(x: Int) -> Int {\n"
        "    return x\n"
        "}\n"
        "let result: Int = test(\"string\")\n";
    
    ASTStmtList* program = ast_parse_program(invalid_code);
    
    if (!program) {
        bread_error_cleanup();
        return true; // Should fail
    }
    
    bool result = !semantic_analyze(program);
    
    ast_free_stmt_list(program);
    bread_error_cleanup();
    return result;
}

int main(void) {
    pbt_init("Semantic Analysis Properties");
    
    pbt_property("Type mismatch detection", test_type_mismatch_detection);
    pbt_property("Valid program analysis", test_valid_program_analysis);
    pbt_property("Undefined variable detection", test_undefined_variable_detection);
    pbt_property("Function signature validation", test_function_signature_validation);
    pbt_property("Empty containers require type", test_empty_container_literals_require_type);
    pbt_property("Invalid index/member rejected", test_invalid_index_member_rejected);
    
    return pbt_run();
}