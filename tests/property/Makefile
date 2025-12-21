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
	$(SRC_DIR)/compiler/expr.c \
	$(SRC_DIR)/compiler/expr_ops.c \
	$(SRC_DIR)/compiler/ast.c \
	$(SRC_DIR)/compiler/semantic.c \
	$(SRC_DIR)/compiler/type_stability.c \
	$(SRC_DIR)/compiler/escape_analysis.c \
	$(SRC_DIR)/compiler/optimization.c \
	$(SRC_DIR)/backends/llvm_backend.c \
	$(SRC_DIR)/codegen/codegen.c \
	$(SRC_DIR)/codegen/optimized_codegen.c \
	$(SRC_DIR)/runtime/print.c

PBT_SOURCES = pbt_framework.c
TESTS = type_properties string_properties array_properties parser_properties

.PHONY: all clean run

all: $(TESTS)

type_properties: type_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

string_properties: string_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

array_properties: array_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

parser_properties: parser_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
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