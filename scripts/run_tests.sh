#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BREADLANG="$ROOT_DIR/breadlang"
TEST_DIR="$ROOT_DIR/tests/integration"
PASSED=0
FAILED=0

echo "Running BreadLang tests..."

cc -std=c11 -Wall -Wextra -O0 -g \
  "$ROOT_DIR"/src/ast.c \
  "$ROOT_DIR"/src/expr.c \
  "$ROOT_DIR"/src/expr_ops.c \
  "$ROOT_DIR"/src/function.c \
  "$ROOT_DIR"/src/interpreter.c \
  "$ROOT_DIR"/src/print.c \
  "$ROOT_DIR"/src/semantic.c \
  "$ROOT_DIR"/src/value.c \
  "$ROOT_DIR"/src/var.c \
  -o "$BREADLANG" -lm

for test_file in $(find "$TEST_DIR" -name "*.bread"); do
    base_name=$(basename "$test_file" .bread)
    expected_file="$(dirname "$test_file")/$base_name.expected"

    if [ ! -f "$expected_file" ]; then
        echo "Warning: No expected output file for $test_file"
        continue
    fi

    output=$("$BREADLANG" "$test_file" 2>&1)

    if [ "$output" = "$(cat "$expected_file")" ]; then
        echo "PASS: $base_name"
        ((PASSED++))
    else
        echo "FAIL: $base_name"
        echo "Expected:"
        cat "$expected_file"
        echo "Got:"
        echo "$output"
        ((FAILED++))
    fi
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
