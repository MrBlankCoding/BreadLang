# BreadLang Tests

This directory contains integration tests for the BreadLang interpreter.

## Test Files

Each test consists of:
- `.bread` file: The BreadLang source code to execute
- `.expected` file: The expected output from running the interpreter

## Running Tests

To run all tests:

```bash
./run_tests.sh
```

This will compile the interpreter if needed and run each test, comparing the output to the expected results.

## Adding New Tests

1. Create a `.bread` file with your test code
2. Run the interpreter on it: `./breadlang tests/your_test.bread`
3. Capture the output and create `your_test.expected` with that output

Make sure to include trailing newlines in expected files if the output has them.