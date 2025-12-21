CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../../include -g
LDFLAGS = 

# LLVM flags for parser_properties test
LLVM_CFLAGS = $(shell llvm-config --cflags 2>/dev/null || echo "")
LLVM_LDFLAGS = $(shell llvm-config --ldflags 2>/dev/null || echo "")
LLVM_LIBS = $(shell llvm-config --libs --system-libs 2>/dev/null || echo "")
LLVM_DEFS = -DBREAD_HAVE_LLVM=1 

SRC_DIR = ../../src
INCLUDE_DIR = ../../include


CORE_SOURCES = \
	$(SRC_DIR)/core/var.c \
	$(SRC_DIR)/core/value.c \
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
	$(SRC_DIR)/runtime/string_ops.c \
	$(SRC_DIR)/runtime/value_ops.c \
	$(SRC_DIR)/runtime/operators.c \
	$(SRC_DIR)/runtime/builtins.c \
	$(SRC_DIR)/runtime/array_utils.c

PBT_SOURCES = pbt_framework.c
TESTS = type_properties string_properties array_properties parser_properties control_properties builtin_properties error_properties

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