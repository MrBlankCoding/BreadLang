#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Cleaning BreadLang build artifacts..."

rm -f "$ROOT_DIR/breadlang"
rm -rf "$ROOT_DIR"/*.dSYM

if [ -d "$ROOT_DIR/build" ]; then
    echo "Removing CMake build directory..."
    rm -rf "$ROOT_DIR/build"
fi

if [ -f "$ROOT_DIR/tests/property/Makefile" ]; then
    echo "Cleaning property tests..."
    cd "$ROOT_DIR/tests/property"
    make clean 2>/dev/null || true
fi

find "$ROOT_DIR" -name "*.o" -delete 2>/dev/null || true
find "$ROOT_DIR" -name "*.ll" -delete 2>/dev/null || true
find "$ROOT_DIR" -name "*.bc" -delete 2>/dev/null || true
find "$ROOT_DIR" -name "*.tmp" -delete 2>/dev/null || true

echo "Clean complete!"