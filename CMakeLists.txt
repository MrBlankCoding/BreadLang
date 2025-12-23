cmake_minimum_required(VERSION 3.16)
project(BreadLang VERSION 1.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
include_directories(${CMAKE_SOURCE_DIR}/include)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
add_definitions(-DBREAD_HAVE_LLVM=1)

# Sources
set(BREADLANG_SOURCES
    src/main.c
    
    src/core/function.c
    src/core/value_core.c
    src/core/value_array.c
    src/core/value_dict.c
    src/core/value_optional.c
    src/core/value_struct.c
    src/core/value_class.c
    src/core/var.c
    src/core/type_descriptor.c
    
    src/compiler/ast/ast.c
    src/compiler/ast/ast_memory.c
    src/compiler/ast/ast_types.c
    src/compiler/ast/ast_expr_parser.c
    src/compiler/ast/ast_stmt_parser.c
    src/compiler/ast/ast_dump.c
    src/compiler/parser/expr.c
    src/compiler/parser/expr_ops.c
    src/compiler/analysis/type_stability.c
    src/compiler/analysis/escape_analysis.c
    src/compiler/optimization/optimization.c
    
    src/backends/llvm_backend.c
    src/backends/llvm_backend_utils.c
    src/backends/llvm_backend_codegen.c
    src/backends/llvm_backend_emit.c
    src/backends/llvm_backend_link.c
    src/backends/llvm_backend_jit.c
    src/codegen/codegen.c
    src/codegen/codegen_helpers.c
    src/codegen/codegen_scope.c
    src/codegen/codegen_unboxing.c
    src/codegen/codegen_expr.c
    src/codegen/codegen_stmt.c
    src/codegen/codegen_semantic.c
    src/codegen/optimized_codegen.c
    
    src/runtime/array_utils.c
    src/runtime/builtins.c
    src/runtime/error.c
    src/runtime/memory.c
    src/runtime/operators.c
    src/runtime/print.c
    src/runtime/runtime.c
    src/runtime/string_ops.c
    src/runtime/value_ops.c
    
    src/codegen/codegen_runtime_bridge.c
)

# Exec for breadlang
add_executable(breadlang ${BREADLANG_SOURCES})
include_directories(
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

# Link librarys
find_library(LLVM_C_LIB NAMES LLVM-C PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
if (NOT LLVM_C_LIB)
    message(FATAL_ERROR "LLVM C API library not found. Please install llvm with C API support.")
endif()
target_link_libraries(breadlang 
    ${LLVM_C_LIB}
    m
    z
    ncurses
    pthread
    dl
)

target_link_directories(breadlang PRIVATE ${LLVM_LIBRARY_DIRS})
target_compile_definitions(breadlang PRIVATE -DBREAD_HAVE_LLVM=1)
install(TARGETS breadlang DESTINATION bin)

# Testing
enable_testing()
add_executable(bread_test_runner
    tests/bread_test_runner.c
)

add_dependencies(bread_test_runner breadlang)

file(GLOB BREADLANG_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tests/ctest/*.bread"
)

foreach(test_src ${BREADLANG_TEST_SOURCES})
    get_filename_component(test_name "${test_src}" NAME_WE)
    set(expected_file "${CMAKE_SOURCE_DIR}/tests/ctest/${test_name}.expected")

    if (EXISTS "${expected_file}")
        add_test(
            NAME "bread.${test_name}"
            COMMAND bread_test_runner
                "$<TARGET_FILE:breadlang>"
                "${test_src}"
                "${expected_file}"
        )
    endif()
endforeach()

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all BreadLang CTest tests"
)

add_custom_target(test-property
    COMMAND make -C tests/property test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running property-based tests"
)