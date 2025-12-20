# BreadLang

This repository contains a small C interpreter for **BreadLang**.

## Build

From the repo root:

```sh
cc -std=c11 -Wall -Wextra -O0 -g src/*.c -o breadlang -lm
```

This produces a `./breadlang` binary.

## Run

```sh
./breadlang <file.bread>
```

## Command-line flags

```sh
./breadlang [--dump-ast] [--trace] <file.bread>
```

- **`--dump-ast`**
  - Parses the input and prints a structured dump of the parsed statement list.
  - Note: expressions are currently stored as strings, so the dump will show fields like `expr=...`.
  - Does not execute the program.

- **`--trace`**
  - Executes the program but prints a simple step-by-step trace to `stderr`.
  - Current trace format: `trace: <stmt_kind>` (example: `trace: var_decl`, `trace: print`).

## Tests

There is a simple test runner script that builds and runs all `.bread` files under `tests/` and compares output against the matching `.expected` file.

```sh
./run_tests.sh
```

## Repo layout

- `src/`
  - Interpreter implementation (parser + evaluator)
  - Entry point: `src/interpreter.c`
- `include/`
  - Public headers
- `tests/`
  - `.bread` programs and `.expected` outputs

## Notes for extending

- Parsing currently produces a `StmtList` (statements are structured), but expressions are evaluated from expression strings (`evaluate_expression`).
- Variable/function state is currently global; Phase 0 work aims to stabilize semantics and then make error handling/state passing more explicit.
