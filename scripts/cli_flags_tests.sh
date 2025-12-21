#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BREADLANG="$ROOT_DIR/breadlang"
TEST_DIR="$ROOT_DIR/tests/cli_flags"

PASSED=0
FAILED=0

echo "Running BreadLang CLI flag tests..."

"$ROOT_DIR/build.sh"

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
