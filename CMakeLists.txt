cmake_minimum_required(VERSION 3.16)
project(BreadLang VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set compiler to clang
set(CMAKE_C_COMPILER clang)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${LLVM_INCLUDE_DIRS})

# LLVM definitions
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
add_definitions(-DBREAD_HAVE_LLVM=1)

# Source files
set(BREADLANG_SOURCES
    src/main.c
    src/core/function.c
    src/core/value.c
    src/core/var.c
    src/compiler/ast/ast.c
    src/compiler/ast/ast_memory.c
    src/compiler/ast/ast_types.c
    src/compiler/ast/ast_expr_parser.c
    src/compiler/ast/ast_stmt_parser.c
    src/compiler/ast/ast_dump.c
    src/compiler/parser/expr.c
    src/compiler/parser/expr_ops.c
    src/compiler/analysis/semantic.c
    src/compiler/analysis/type_stability.c
    src/compiler/analysis/escape_analysis.c
    src/compiler/optimization/optimization.c
    src/backends/llvm_backend.c
    src/codegen/codegen.c
    src/codegen/optimized_codegen.c
    src/runtime/print.c
    src/runtime/runtime.c
    src/runtime/memory.c
    src/runtime/error.c
    src/runtime/string_ops.c
    src/runtime/value_ops.c
    src/runtime/operators.c
    src/runtime/builtins.c
    src/runtime/array_utils.c
)

# Create executable
add_executable(breadlang ${BREADLANG_SOURCES})

# Link LLVM libraries
llvm_map_components_to_libnames(llvm_libs support core irreader executionengine interpreter mcjit native)
target_link_libraries(breadlang ${llvm_libs} m)

# Install target
install(TARGETS breadlang DESTINATION bin)

# Testing
enable_testing()

# Add custom target for running tests
add_custom_target(test-all
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/tests.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running all BreadLang tests"
)

# Property-based tests
add_custom_target(test-property
    COMMAND make -C tests/property test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running property-based tests"
)

# Integration tests
add_custom_target(test-integration
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/tests.sh -t integration
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running integration tests"
)

# LLVM backend tests
add_custom_target(test-llvm
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/tests.sh -t llvm_backend
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running LLVM backend tests"
)