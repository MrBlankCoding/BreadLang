CC = clang
CFLAGS = -Wall -Wextra -std=c11 -I../../include -I. -g -O0
LDFLAGS = -lm

# Framework
FRAMEWORK_DIR = framework
FRAMEWORK_SOURCES = $(FRAMEWORK_DIR)/pbt_framework.c
FRAMEWORK_HEADERS = $(FRAMEWORK_DIR)/pbt_framework.h

# Test categories
CORE_TESTS = core/type_properties core/value_properties
RUNTIME_TESTS = runtime/string_properties runtime/memory_properties runtime/array_properties runtime/error_properties runtime/builtin_properties runtime/gc_properties
COMPILER_TESTS = compiler/parser_properties compiler/control_properties compiler/semantic_properties
INTEGRATION_TESTS = integration/collection_properties

ALL_TESTS = $(CORE_TESTS) $(RUNTIME_TESTS) $(COMPILER_TESTS) $(INTEGRATION_TESTS)

.PHONY: all clean test test-core test-runtime test-compiler test-integration

all: $(ALL_TESTS)

# Core tests
core/%: core/%.c $(FRAMEWORK_SOURCES) $(FRAMEWORK_HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORK_SOURCES) $(LDFLAGS)

# Runtime tests  
runtime/%: runtime/%.c $(FRAMEWORK_SOURCES) $(FRAMEWORK_HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORK_SOURCES) $(LDFLAGS)

# Compiler tests
compiler/%: compiler/%.c $(FRAMEWORK_SOURCES) $(FRAMEWORK_HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORK_SOURCES) $(LDFLAGS)

# Integration tests
integration/%: integration/%.c $(FRAMEWORK_SOURCES) $(FRAMEWORK_HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORK_SOURCES) $(LDFLAGS)

test: all
	@echo "Running all property-based tests..."
	@for test in $(ALL_TESTS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "All property tests passed!"

test-core: $(CORE_TESTS)
	@echo "Running core tests..."
	@for test in $(CORE_TESTS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

test-runtime: $(RUNTIME_TESTS)
	@echo "Running runtime tests..."
	@for test in $(RUNTIME_TESTS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

test-compiler: $(COMPILER_TESTS)
	@echo "Running compiler tests..."
	@for test in $(COMPILER_TESTS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

test-integration: $(INTEGRATION_TESTS)
	@echo "Running integration tests..."
	@for test in $(INTEGRATION_TESTS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

clean:
	rm -f $(ALL_TESTS)
	rm -rf *.dSYM core/*.dSYM runtime/*.dSYM compiler/*.dSYM integration/*.dSYM

# LLVM flags for parser_properties test
LLVM_CFLAGS = $(shell llvm-config --cflags 2>/dev/null || echo "")
LLVM_LDFLAGS = $(shell llvm-config --ldflags 2>/dev/null || echo "")
LLVM_LIBS = $(shell llvm-config --libs --system-libs 2>/dev/null || echo "")
LLVM_DEFS = -DBREAD_HAVE_LLVM=1 

SRC_DIR = ../../src
INCLUDE_DIR = ../../include


CORE_SOURCES = \
	$(SRC_DIR)/core/var.c \
	$(SRC_DIR)/core/value_core.c \
	$(SRC_DIR)/core/value_array.c \
	$(SRC_DIR)/core/value_dict.c \
	$(SRC_DIR)/core/value_optional.c \
	$(SRC_DIR)/core/value_struct.c \
	$(SRC_DIR)/core/value_class.c \
	$(SRC_DIR)/core/function.c \
	$(SRC_DIR)/runtime/runtime.c \
	$(SRC_DIR)/runtime/error.c \
	$(SRC_DIR)/compiler/parser/expr.c \
	$(SRC_DIR)/compiler/parser/expr_ops.c \
	$(SRC_DIR)/compiler/ast/ast.c \
	$(SRC_DIR)/compiler/ast/ast_memory.c \
	$(SRC_DIR)/compiler/ast/ast_types.c \
	$(SRC_DIR)/compiler/ast/ast_expr_parser.c \
	$(SRC_DIR)/compiler/ast/ast_stmt_parser.c \
	$(SRC_DIR)/compiler/ast/ast_dump.c \
	$(SRC_DIR)/compiler/analysis/semantic.c \
	$(SRC_DIR)/compiler/analysis/type_stability.c \
	$(SRC_DIR)/compiler/analysis/escape_analysis.c \
	$(SRC_DIR)/compiler/optimization/optimization.c \
	$(SRC_DIR)/backends/llvm_backend.c \
	$(SRC_DIR)/codegen/codegen.c \
	$(SRC_DIR)/codegen/optimized_codegen.c \
	$(SRC_DIR)/runtime/print.c \
	$(SRC_DIR)/runtime/memory.c \
	$(SRC_DIR)/runtime/string_ops.c \
	$(SRC_DIR)/runtime/value_ops.c \
	$(SRC_DIR)/runtime/operators.c \
	$(SRC_DIR)/runtime/builtins.c \
	$(SRC_DIR)/runtime/array_utils.c

PBT_SOURCES = pbt_framework.c
TESTS = type_properties string_properties array_properties parser_properties control_properties builtin_properties error_properties memory_properties collection_properties

.PHONY: all clean run

all: $(TESTS)

type_properties: type_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

string_properties: string_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

array_properties: array_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

parser_properties: parser_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

control_properties: control_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

builtin_properties: builtin_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

error_properties: error_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

memory_properties: memory_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

collection_properties: collection_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) $(LLVM_DEFS) -o $@ $^ $(LDFLAGS) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

run: $(TESTS)
	@echo "Running Property-Based Tests..."
	@echo "==============================="
	@for test in $(TESTS); do \
		echo "Running $$test..."; \
		./$$test; \
		echo ""; \
	done

clean:
	rm -f $(TESTS) *.o

run-type: type_properties
	./type_properties

run-string: string_properties
	./string_properties

run-array: array_properties
	./array_properties

run-parser: parser_properties
	./parser_properties

run-control: control_properties
	./control_properties

run-builtin: builtin_properties
	./builtin_properties

run-error: error_properties
	./error_properties

run-memory: memory_properties
	./memory_properties

run-collection: collection_properties
	./collection_properties