CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../../include -g
LDFLAGS = 

SRC_DIR = ../../src
INCLUDE_DIR = ../../include


CORE_SOURCES = \
	$(SRC_DIR)/core/var.c \
	$(SRC_DIR)/core/value.c \
	$(SRC_DIR)/runtime/runtime.c \
	$(SRC_DIR)/compiler/expr.c \
	$(SRC_DIR)/compiler/expr_ops.c

PBT_SOURCES = pbt_framework.c
TESTS = type_properties string_properties array_properties

.PHONY: all clean run

all: $(TESTS)

type_properties: type_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

string_properties: string_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

array_properties: array_properties.c $(PBT_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

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