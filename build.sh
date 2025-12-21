#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BREADLANG="$ROOT_DIR/breadlang"

# Check if we're on Windows (WSL or similar)
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    echo "Warning: Building on Windows. Some features may not work as expected." >&2
fi

CC=clang
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "Error: clang not found. Please install clang." >&2
  exit 1
fi

# Check if LLVM is available (required for JIT)
if command -v llvm-config >/dev/null 2>&1; then
  echo "LLVM found, building with JIT support"
else
  echo "Error: llvm-config not found. LLVM is required for JIT compilation." >&2
  exit 1
fi

LLVM_CFLAGS="$(llvm-config --cflags 2>/dev/null || true)"
LLVM_LDFLAGS="$(llvm-config --ldflags 2>/dev/null || true)"
LLVM_LIBS="$(llvm-config --libs --system-libs 2>/dev/null || true)"
LLVM_DEFS="-DBREAD_HAVE_LLVM=1"

"$CC" -std=c11 -Wall -Wextra -O0 -g -I"$ROOT_DIR/include" $LLVM_CFLAGS $LLVM_DEFS \
  "$ROOT_DIR/src/main.c" \
  "$ROOT_DIR/src/core/function.c" \
  "$ROOT_DIR/src/core/value.c" \
  "$ROOT_DIR/src/core/var.c" \
  "$ROOT_DIR/src/compiler/ast/ast.c" \
  "$ROOT_DIR/src/compiler/ast/ast_memory.c" \
  "$ROOT_DIR/src/compiler/ast/ast_types.c" \
  "$ROOT_DIR/src/compiler/ast/ast_expr_parser.c" \
  "$ROOT_DIR/src/compiler/ast/ast_stmt_parser.c" \
  "$ROOT_DIR/src/compiler/ast/ast_dump.c" \
  "$ROOT_DIR/src/compiler/parser/expr.c" \
  "$ROOT_DIR/src/compiler/parser/expr_ops.c" \
  "$ROOT_DIR/src/compiler/analysis/semantic.c" \
  "$ROOT_DIR/src/compiler/analysis/type_stability.c" \
  "$ROOT_DIR/src/compiler/analysis/escape_analysis.c" \
  "$ROOT_DIR/src/compiler/optimization/optimization.c" \
  "$ROOT_DIR/src/backends/llvm_backend.c" \
  "$ROOT_DIR/src/codegen/codegen.c" \
  "$ROOT_DIR/src/codegen/optimized_codegen.c" \
  "$ROOT_DIR/src/runtime/print.c" \
  "$ROOT_DIR/src/runtime/runtime.c" \
  "$ROOT_DIR/src/runtime/error.c" \
  "$ROOT_DIR/src/runtime/string_ops.c" \
  "$ROOT_DIR/src/runtime/value_ops.c" \
  "$ROOT_DIR/src/runtime/operators.c" \
  "$ROOT_DIR/src/runtime/builtins.c" \
  "$ROOT_DIR/src/runtime/array_utils.c" \
  -o "$BREADLANG" $LLVM_LDFLAGS $LLVM_LIBS -lm

echo "Build successful! Executable at $BREADLANG"
