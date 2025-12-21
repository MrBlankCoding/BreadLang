# Property-Based Testing Framework

This directory contains property-based tests for BreadLang using a custom testing framework.

## Structure

- `framework/` - Core testing framework
- `core/` - Tests for core language features (values, variables, functions)
- `runtime/` - Tests for runtime systems (memory, strings, arrays)
- `compiler/` - Tests for compiler components (parser, AST, semantic analysis)
- `integration/` - Cross-component property tests

## Running Tests

Use the Makefile to build and run all property tests:

```bash
make all
make test
make clean
```

Individual test categories can be run separately:

```bash
make test-core
make test-runtime
make test-compiler
```