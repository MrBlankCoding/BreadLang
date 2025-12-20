#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BREADLANG="$ROOT_DIR/breadlang"
TEST_DIR="$ROOT_DIR/tests/cli_flags"

PASSED=0
FAILED=0

echo "Running BreadLang CLI flag tests..."

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

run_case() {
  local name="$1"
  local flags="$2"
  local bread_file="$3"
  local expected_file="$4"

    local output
    output=$("$BREADLANG" $flags "$bread_file" 2>&1)

    local expected
    expected=$(cat "$expected_file")

    if [ "$output" = "$expected" ]; then
        echo "PASS: $name"
        PASSED=$((PASSED + 1))
    else
        echo "FAIL: $name"
        echo "Expected:"
        cat "$expected_file"
        echo "Got:"
        echo "$output"
        FAILED=$((FAILED + 1))
    fi
}

run_case "dump_ast_simple" "--dump-ast" "$TEST_DIR/dump_ast_simple.bread" "$TEST_DIR/dump_ast_simple.expected"
run_case "trace_simple" "--trace" "$TEST_DIR/trace_simple.bread" "$TEST_DIR/trace_simple.expected"
run_case "use_ast_simple" "--use-ast" "$TEST_DIR/use_ast_simple.bread" "$TEST_DIR/use_ast_simple.expected"
run_case "trace_use_ast_simple" "--trace --use-ast" "$TEST_DIR/trace_use_ast_simple.bread" "$TEST_DIR/trace_use_ast_simple.expected"

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
