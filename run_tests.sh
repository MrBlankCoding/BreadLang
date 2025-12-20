#!/bin/bash

# Test runner for BreadLang

BREADLANG="./breadlang"
TEST_DIR="tests"
PASSED=0
FAILED=0

echo "Running BreadLang tests..."

# Build interpreter (keep this script self-contained)
cc -std=c11 -Wall -Wextra -O0 -g src/*.c -o breadlang -lm

for test_file in $(find $TEST_DIR -name "*.bread"); do
    base_name=$(basename "$test_file" .bread)
    expected_file="$(dirname "$test_file")/$base_name.expected"
    
    if [ ! -f "$expected_file" ]; then
        echo "Warning: No expected output file for $test_file"
        continue
    fi
    
    # Run the interpreter and capture output
    output=$($BREADLANG "$test_file" 2>&1)
    
    # Compare with expected
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