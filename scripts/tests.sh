#!/bin/bash

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
      echo "Usage: $0 [--verbose] [--stop-on-error] [--test-type all|integration|llvm_backend]"
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

  while IFS= read -r -d '' test_file; do
    local base_name=$(basename "$test_file" .bread)
    local expected_file="$(dirname "$test_file")/$base_name.expected"

    if [ ! -f "$expected_file" ]; then
      echo -e "${YELLOW}Warning: No expected output file for $test_file${NC}"
      continue
    fi

    local output
    output=$("$BREADLANG" "$test_file" 2>&1 || true)
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
  local test_dir="$ROOT_DIR/tests/llvm_backend"

  if ! command -v llvm-config >/dev/null 2>&1; then
    echo -e "${YELLOW}Skipping LLVM backend tests - llvm-config not found${NC}"
    return
  fi

  if [ ! -d "$test_dir" ]; then
    echo -e "${YELLOW}Skipping LLVM backend tests - directory not found${NC}"
    return
  fi

  echo ""
  echo -e "${CYAN}Running LLVM backend tests...${NC}"
  echo -e "${CYAN}---${NC}"

  local tmp_dir
  tmp_dir=$(mktemp -d 2>/dev/null || mktemp -d -t breadlang_llvm_tests)

  while IFS= read -r -d '' test_file; do
    local base_name
    base_name=$(basename "$test_file" .bread)
    local expected_file="$(dirname "$test_file")/$base_name.expected"

    if [ ! -f "$expected_file" ]; then
      echo -e "${YELLOW}Warning: No expected output file for $test_file${NC}"
      continue
    fi

    local exe_path="$tmp_dir/$base_name"

    local compile_out
    compile_out=$("$BREADLANG" --emit-exe -o "$exe_path" "$test_file" 2>&1 || true)
    if [ ! -f "$exe_path" ]; then
      record_result "llvm_backend" "$base_name" "false" "LLVM compile failed: $compile_out"
      continue
    fi

    chmod +x "$exe_path" 2>/dev/null || true

    local output
    output=$("$exe_path" 2>&1 || true)
    local expected
    expected=$(cat "$expected_file")

    if [ "$output" = "$expected" ]; then
      record_result "llvm_backend" "$base_name" "true"
    else
      record_result "llvm_backend" "$base_name" "false" "Output mismatch"
    fi
  done < <(find "$test_dir" -name "*.bread" -print0)

  rm -rf "$tmp_dir" 2>/dev/null || true
}

# Main 
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}BreadLang Unified Test Suite${NC}"
echo -e "${CYAN}========================================${NC}"

case "$TEST_TYPE" in
  all)
    run_integration_tests
    run_llvm_backend_tests
    ;;
  integration)
    run_integration_tests
    ;;
  llvm_backend)
    run_llvm_backend_tests
    ;;
  *)
    echo -e "${RED}Unknown test type: $TEST_TYPE${NC}"
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

for category in integration llvm_backend; do
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
