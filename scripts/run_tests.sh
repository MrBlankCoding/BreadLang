#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BREADLANG="$ROOT_DIR/breadlang"
TEST_DIR="$ROOT_DIR/tests/integration"
PASSED=0
FAILED=0

echo "Running BreadLang tests..."

CC=clang
if ! command -v "$CC" >/dev/null 2>&1; then
  CC=cc
fi

LLVM_CFLAGS=""
LLVM_LDFLAGS=""
LLVM_LIBS=""
LLVM_DEFS=""
if command -v llvm-config >/dev/null 2>&1; then
  LLVM_CFLAGS="$(llvm-config --cflags 2>/dev/null || true)"
  LLVM_LDFLAGS="$(llvm-config --ldflags 2>/dev/null || true)"
  LLVM_LIBS="$(llvm-config --libs --system-libs 2>/dev/null || true)"
  LLVM_DEFS="-DBREAD_HAVE_LLVM=1"
fi

"$CC" -std=c11 -Wall -Wextra -O0 -g $LLVM_CFLAGS $LLVM_DEFS \
  "$ROOT_DIR"/src/ast.c \
  "$ROOT_DIR"/src/bytecode.c \
  "$ROOT_DIR"/src/compiler.c \
  "$ROOT_DIR"/src/bread_ir.c \
  "$ROOT_DIR"/src/expr.c \
  "$ROOT_DIR"/src/expr_ops.c \
  "$ROOT_DIR"/src/function.c \
  "$ROOT_DIR"/src/interpreter.c \
  "$ROOT_DIR"/src/llvm_backend.c \
  "$ROOT_DIR"/src/print.c \
  "$ROOT_DIR"/src/runtime.c \
  "$ROOT_DIR"/src/semantic.c \
  "$ROOT_DIR"/src/value.c \
  "$ROOT_DIR"/src/var.c \
  "$ROOT_DIR"/src/vm.c \
  -o "$BREADLANG" $LLVM_LDFLAGS $LLVM_LIBS -lm

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
