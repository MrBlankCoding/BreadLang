# BreadLang

BreadLang is a custom programming language implemented in C. It features a complete pipeline from parsing to execution, supporting both a bytecode virtual machine and an experimental LLVM native code backend.

## Features

- **Dual Execution Engines**:
  - **Bytecode VM**: Stack-based virtual machine for portable execution (default).
  - **LLVM Backend**: Compiles BreadLang code to optimized native executables (experimental).
- **Core Language Support**:
  - Arithmetic operations and expressions.
  - Variable declarations and assignments.
  - Control flow (`if`/`else`, `while` loops).
  - Function definitions and calls.
  - Standard output via `print`.
- **Developer Tools**:
  - AST Dumper for debugging parsing.
  - Execution tracer for the VM.

## Build

### Prerequisites

- A C compiler (GCC/Clang) supporting C11.
- `make` (optional, for manual build steps).
- **LLVM** (optional): Required only for the LLVM backend features (`llvm-config` must be in your PATH).

### Building

Use the provided build script to compile the project:

```sh
./build.sh
```

This will produce the `breadlang` executable in the project root.

## Usage

Run a BreadLang program using the bytecode VM:

```sh
./breadlang examples/hello.bread
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `--dump-ast` | Parses the code and prints the Abstract Syntax Tree structure. |
| `--trace` | Runs the program with instruction-level execution tracing. |
| `--use-ast` | Uses the legacy AST interpreter instead of the bytecode VM. |
| `--emit-exe` | Compiles the program to a native executable using LLVM. |

## Language Syntax

BreadLang supports a C-like syntax.

**Variables and Math:**
```bread
var x = 10;
var y = 20;
print x + y;
```

**Control Flow:**
```bread
var a = 5;
if (a > 3) {
    print 1;
} else {
    print 0;
}

var i = 0;
while (i < 5) {
    print i;
    i = i + 1;
}
```

**Functions:**
```bread
func add(a, b) {
    return a + b;
}

print add(10, 5);
```

## Testing

The project maintains a robust test suite to ensure stability across both execution engines.

### Running Tests

Use the unified test runner script:

```sh
# Run all tests (Integration + LLVM)
./scripts/tests.sh

# Run only integration tests
./scripts/tests.sh -t integration

# Run only LLVM backend tests
./scripts/tests.sh -t llvm_backend
```

### Test Categories

- **Integration Tests** (`tests/integration/`): Verifies core language features (math, variables, control flow, functions) running on the Bytecode VM.
- **LLVM Backend Tests** (`tests/llvm_backend/`): Verifies that code compiles correctly to native executables and produces expected output.

## Project Structure

- `src/`: Source code.
  - `compiler/`: Lexer, parser, and bytecode compiler.
  - `vm/`: Stack-based virtual machine implementation.
  - `backends/`: LLVM code generation logic.
- `include/`: Header files.
- `tests/`: Test cases (`.bread` source and `.expected` output).
- `scripts/`: Build and test automation scripts.
