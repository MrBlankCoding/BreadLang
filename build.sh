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

# Check if LLVM is available
if command -v llvm-config >/dev/null 2>&1; then
  echo "LLVM found, enabling LLVM backend support"
else
  echo "Warning: llvm-config not found. LLVM backend will be disabled." >&2
  LLVM_DEFS="-DBREAD_NO_LLVM=1"
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

"$CC" -std=c11 -Wall -Wextra -O0 -g -I"$ROOT_DIR/include" $LLVM_CFLAGS $LLVM_DEFS \
  "$ROOT_DIR/src/main.c" \
  "$ROOT_DIR/src/core/function.c" \
  "$ROOT_DIR/src/core/value.c" \
  "$ROOT_DIR/src/core/var.c" \
  "$ROOT_DIR/src/compiler/ast.c" \
  "$ROOT_DIR/src/compiler/compiler.c" \
  "$ROOT_DIR/src/compiler/expr.c" \
  "$ROOT_DIR/src/compiler/expr_ops.c" \
  "$ROOT_DIR/src/compiler/semantic.c" \
  "$ROOT_DIR/src/compiler/type_stability.c" \
  "$ROOT_DIR/src/compiler/escape_analysis.c" \
  "$ROOT_DIR/src/compiler/optimization.c" \
  "$ROOT_DIR/src/vm/bytecode.c" \
  "$ROOT_DIR/src/vm/vm.c" \
  "$ROOT_DIR/src/ir/bread_ir.c" \
  "$ROOT_DIR/src/backends/llvm_backend.c" \
  "$ROOT_DIR/src/codegen/codegen.c" \
  "$ROOT_DIR/src/codegen/optimized_codegen.c" \
  "$ROOT_DIR/src/runtime/print.c" \
  "$ROOT_DIR/src/runtime/runtime.c" \
  -o "$BREADLANG" $LLVM_LDFLAGS $LLVM_LIBS -lm

echo "Build successful! Executable at $BREADLANG"
