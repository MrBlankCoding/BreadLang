# BreadLang

This repository contains a small C implementation of **BreadLang** with both:
- an AST interpreter (debug/fallback)
- a bytecode compiler + stack-based VM (default execution)

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

By default, BreadLang runs the pipeline:

- Parse program into AST
- Semantic analysis
- Compile AST to bytecode (`BytecodeChunk`)
- Execute bytecode on the VM

## Command-line flags

```sh
./breadlang [--dump-ast] [--trace] [--use-ast] <file.bread>
```

- **`--dump-ast`**
  - Parses the input and prints a structured dump of the parsed statement list.
  - Note: expressions are currently stored as strings, so the dump will show fields like `expr=...`.
  - Does not execute the program.

- **`--trace`**
  - Executes the program but prints a simple step-by-step trace to `stderr`.
  - Current trace format: `trace: <stmt_kind>` (example: `trace: var_decl`, `trace: print`).

- **`--use-ast`**
  - Runs the legacy AST interpreter instead of the bytecode VM.
  - Useful as a correctness fallback while the VM/compiler evolves.

## Tests

There is a simple test runner script that builds and runs all `.bread` files under `tests/` and compares output against the matching `.expected` file.

```sh
./run_tests.sh
```

CLI flag tests:

```sh
bash scripts/cli_flags_tests.sh
```

## Repo layout

- `src/`
  - Parser + semantic analysis + AST interpreter
  - Bytecode compiler (`compiler.c`) and VM runtime (`vm.c`)
  - Entry point: `src/interpreter.c`
- `include/`
  - Public headers
- `tests/`
  - `.bread` programs and `.expected` outputs

## Notes for extending

- Expressions in the AST are stored as structured nodes (`ASTExpr`), and bytecode compilation walks the AST.
- Function declarations are registered during compilation; user-defined function bodies are currently still executed via the AST interpreter when called.
