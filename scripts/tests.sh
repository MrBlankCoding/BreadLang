#!/bin/bash

# Default C compiler
: ${CC:=clang}

#./unified_tests.sh                          Run all tests
#./unified_tests.sh --verbose                Run with detailed output
#./unified_tests.sh -t integration           Run only integration tests
#./unified_tests.sh --stop-on-error          Stop on first failure

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BREADLANG="$ROOT_DIR/breadlang"

VERBOSE=false
STOP_ON_ERROR=false
TEST_TYPE="all"

# cmd line args
while [[ $# -gt 0 ]]; do
  case $1 in
    --verbose|-v)
      VERBOSE=true
      shift
      ;;
    --stop-on-error|-s)
      STOP_ON_ERROR=true
      shift
      ;;
    --test-type|-t)
      TEST_TYPE="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--verbose] [--stop-on-error] [--test-type all|integration|llvm_backend|property]"
      exit 1
      ;;
  esac
done

# results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS_COUNT=0
INTEGRATION_TOTAL=0
INTEGRATION_PASSED=0
INTEGRATION_FAILED=0
LLVM_BACKEND_TOTAL=0
LLVM_BACKEND_PASSED=0
LLVM_BACKEND_FAILED=0
PROPERTY_TOTAL=0
PROPERTY_PASSED=0
PROPERTY_FAILED=0

FAILED_TESTS=()

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' 

# bob the BUILDER
echo -e "${CYAN}Building BreadLang...${NC}"
"$ROOT_DIR/build.sh"

if [ ! -f "$BREADLANG" ]; then
  echo -e "${RED}Error: breadlang not found at $BREADLANG${NC}"
  exit 1
fi

record_result() {
  local category="$1"
  local test_name="$2"
  local success="$3"
  local message="${4:-}"

  local total_var="${category}_TOTAL"
  local passed_var="${category}_PASSED"
  local failed_var="${category}_FAILED"
  
  # init to 0 if not explicity set
  [ -z "${!total_var:-}" ] && eval "$total_var=0"
  [ -z "${!passed_var:-}" ] && eval "$passed_var=0"
  [ -z "${!failed_var:-}" ] && eval "$failed_var=0"
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  eval "$total_var=\$(( ${!total_var} + 1 ))"

  if [ "$success" = "true" ]; then
    echo -e "${GREEN}✓ PASS: $category/$test_name${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    eval "$passed_var=\$(( ${!passed_var} + 1 ))"
  else
    echo -e "${RED}✗ FAIL: $category/$test_name${NC}"
    if [ "$VERBOSE" = "true" ] && [ -n "$message" ]; then
      echo -e "${YELLOW}  $message${NC}"
    fi
    FAILED_TESTS_COUNT=$((FAILED_TESTS_COUNT + 1))
    eval "$failed_var=\$(( ${!failed_var} + 1 ))"
    FAILED_TESTS+=("$category/$test_name")
    
    if [ "$STOP_ON_ERROR" = "true" ]; then
      exit 1
    fi
  fi
}

# integation tests?
run_integration_tests() {
  local test_dir="$ROOT_DIR/tests/integration"
  
  if [ ! -d "$test_dir" ]; then
    echo -e "${YELLOW}Skipping integration tests - directory not found${NC}"
    return
  fi

  echo ""
  echo -e "${CYAN}Running integration tests...${NC}"
  echo -e "${CYAN}---${NC}"

  # Compile the test runner with all core sources
  local test_runner="$ROOT_DIR/tests/integration/run_test.c"
  local test_binary="$ROOT_DIR/tests/integration/run_test"
  
  # Create a simple test runner
  cat > "$test_runner" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/core/function.h"
#include "../../include/core/value.h"
#include "../../include/core/var.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_file.bread>\n", argv[0]);
        return 1;
    }
    
    // Initialize core components
    value_init();
    
    // Run the test file
    FILE* file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open test file");
        return 1;
    }
    
    // Read and execute the test file
    // (This is a simplified version - you'll need to adapt it to your actual execution model)
    // For now, we'll just print the file contents
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file)) {
        printf("%s", buffer);
    }
    
    fclose(file);
    return 0;
}
EOF

  # Compile the test runner with all core sources
  $CC -std=c11 -Wall -Wextra -I"$ROOT_DIR/include" -o "$test_binary" \
    "$test_runner" \
    "$ROOT_DIR/src/core/function.c" \
    "$ROOT_DIR/src/core/value.c" \
    "$ROOT_DIR/src/core/var.c" \
    -lm  # Link with math library if needed
    
  while IFS= read -r -d '' test_file; do
    local base_name=$(basename "$test_file" .bread)
    local expected_file="$(dirname "$test_file")/$base_name.expected"

    if [ ! -f "$expected_file" ]; then
      echo -e "${YELLOW}Warning: No expected output file for $test_file${NC}"
      continue
    fi

    local output
    # Use the compiled test runner instead of direct breadlang interpreter
    output=$("$test_binary" "$test_file" 2>&1 || true)
    local expected
    expected=$(cat "$expected_file")

    if [ "$output" = "$expected" ]; then
      record_result "integration" "$base_name" "true"
    else
      record_result "integration" "$base_name" "false" "Output mismatch"
    fi
  done < <(find "$test_dir" -name "*.bread" -print0)
}

run_llvm_backend_tests() {
  local category="llvm_backend"
  local test_dir="$ROOT_DIR/tests/llvm_backend"
  local pattern="*.bread"
  
  if [ ! -d "$test_dir" ]; then
    echo -e "${YELLOW}Warning: LLVM backend test directory not found at $test_dir${NC}"
    return
  fi
  
  echo -e "\n${CYAN}Running LLVM Backend Tests...${NC}"
  
  local tests=($(find "$test_dir" -name "$pattern" | sort))
  LLVM_BACKEND_TOTAL=${#tests[@]}
  
  if [ $LLVM_BACKEND_TOTAL -eq 0 ]; then
    echo -e "${YELLOW}No LLVM backend tests found${NC}"
    return
  fi
  
  for test_file in "${tests[@]}"; do
    local test_name=$(basename "$test_file" .bread)
    local expected_file="${test_file%.*}.expected"
    
    if [ ! -f "$expected_file" ]; then
      echo -e "${YELLOW}Warning: Expected output file not found for $test_name${NC}"
      continue
    fi
    
    local output_file=$(mktemp)
    local exit_code=0
    
    if $VERBOSE; then
      echo -e "${BLUE}Running test: $test_name${NC}"
      $BREADLANG --llvm "$test_file" > "$output_file" 2>&1 || exit_code=$?
    else
      $BREADLANG --llvm "$test_file" > "$output_file" 2>&1 || exit_code=$?
    fi
    
    if [ $exit_code -ne 0 ]; then
      record_result "$category" "$test_name" 1 "Non-zero exit code ($exit_code)" "$output_file"
    else
      local diff_output=$(diff -u "$expected_file" "$output_file" 2>&1)
      if [ -n "$diff_output" ]; then
        record_result "$category" "$test_name" 1 "Output does not match expected" "$output_file" "$expected_file"
      else
        record_result "$category" "$test_name" 0 "" "$output_file" "$expected_file"
      fi
    fi
    
    rm -f "$output_file"
    
    if [ $FAILED_TESTS_COUNT -gt 0 ] && $STOP_ON_ERROR; then
      break
    fi
  done
}

run_property_tests() {
  local category="property"
  local test_dir="$ROOT_DIR/tests/property"
  
  if [ ! -d "$test_dir" ]; then
    echo -e "${YELLOW}Warning: Property test directory not found at $test_dir${NC}"
    return
  fi
  
  echo -e "\n${CYAN}Running Property-Based Tests...${NC}"
  
  # Count the number of property test targets from the Makefile
  PROPERTY_TOTAL=$(grep -E '^[a-z_]+:' "$test_dir/Makefile" | grep -v '^\s*#' | wc -l | tr -d ' ')
  
  if [ $PROPERTY_TOTAL -eq 0 ]; then
    echo -e "${YELLOW}No property tests found${NC}"
    return
  fi
  
  # Run the tests
  local output_file=$(mktemp)
  local exit_code=0
  
  pushd "$test_dir" > /dev/null
  
  if $VERBOSE; then
    echo -e "${BLUE}Running property tests...${NC}"
    make test > "$output_file" 2>&1 || exit_code=$?
  else
    make test > "$output_file" 2>&1 || exit_code=$?
  fi
  
  if [ $exit_code -eq 0 ]; then
    PROPERTY_PASSED=$PROPERTY_TOTAL
    PROPERTY_FAILED=0
    record_result "$category" "property_tests" 0 "All $PROPERTY_TOTAL property tests passed" "$output_file"
  else
    PROPERTY_PASSED=0
    PROPERTY_FAILED=$PROPERTY_TOTAL
    record_result "$category" "property_tests" 1 "Property tests failed" "$output_file"
  fi
  
  popd > /dev/null
  rm -f "$output_file"
  
  if [ $FAILED_TESTS_COUNT -gt 0 ] && $STOP_ON_ERROR; then
    return 1
  fi
  
  return 0
}

# Main 
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}BreadLang Unified Test Suite${NC}"
echo -e "${CYAN}========================================${NC}"

echo -e "Test type: $TEST_TYPE"

case "$TEST_TYPE" in
  "all")
    run_integration_tests
    run_llvm_backend_tests
    run_property_tests
    ;;
  "integration")
    run_integration_tests
    ;;
  "llvm_backend")
    run_llvm_backend_tests
    ;;
  "property")
    run_property_tests
    ;;
  *)
    echo "Unknown test type: $TEST_TYPE"
    echo "Valid types: all, integration, llvm_backend, property"
    exit 1
    ;;
esac

# sum
echo ""
echo -e "${CYAN}========================================${NC}"

echo -e "${CYAN}Test Results Summary:${NC}"
echo -e "${CYAN}========================================${NC}"

get_category_value() {
  local category=$1
  local type=$2
  case "${category}_${type}" in
    integration_TOTAL) echo "$INTEGRATION_TOTAL" ;;
    integration_PASSED) echo "$INTEGRATION_PASSED" ;;
    integration_FAILED) echo "$INTEGRATION_FAILED" ;;
    llvm_backend_TOTAL) echo "$LLVM_BACKEND_TOTAL" ;;
    llvm_backend_PASSED) echo "$LLVM_BACKEND_PASSED" ;;
    llvm_backend_FAILED) echo "$LLVM_BACKEND_FAILED" ;;
  esac
}

for category in integration llvm_backend property; do
  total=$(get_category_value $category TOTAL)
  if [ "$total" -gt 0 ]; then
    echo ""
    echo -e "${CYAN}${category^^}:${NC}"
    echo "  Total:  $total"
    passed=$(get_category_value $category PASSED)
    failed=$(get_category_value $category FAILED)
    echo -e "  Passed: ${GREEN}${passed}${NC}"
    echo -e "  Failed: ${RED}${failed}${NC}"
  fi
done

echo ""
echo -e "${CYAN}OVERALL RESULTS:${NC}"
echo "  Total:  $TOTAL_TESTS"
echo -e "  Passed: ${GREEN}${PASSED_TESTS}${NC}"
echo -e "  Failed: ${RED}${FAILED_TESTS_COUNT}${NC}"

if [ ${FAILED_TESTS_COUNT} -gt 0 ]; then
  echo ""
  echo -e "${RED}Failed Tests Details:${NC}"
  for failed in "${FAILED_TESTS[@]}"; do
    echo -e "  ${RED}✗ $failed${NC}"
  done
  echo ""
  echo -e "${YELLOW}Run with --verbose for more details${NC}"
fi

echo ""

# Exit with appropriate code
if [ $FAILED_TESTS_COUNT -gt 0 ]; then
  exit 1
else
  exit 0
fi
