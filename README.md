# BreadLang

BreadLang is a custom programming language implemented in C with LLVM JIT compilation. It features a complete pipeline from parsing to native code execution using LLVM's Just-In-Time compiler.

## Features

- **LLVM JIT Compilation**: Direct compilation to native code using LLVM's JIT engine for optimal performance
- **Core Language Support**:
  - Arithmetic operations and expressions
  - Variable declarations and assignments
  - Control flow (`if`/`else`, `while` loops, `for` loops)
  - Function definitions and calls
  - Arrays and dictionaries
  - Standard output via `print`
- **Developer Tools**:
  - AST Dumper for debugging parsing
  - LLVM IR emission for inspection
  - Native executable generation

## Build

### Prerequisites

- A C compiler (GCC/Clang) supporting C11
- **LLVM**: Required for JIT compilation (`llvm-config` must be in your PATH)

### Building

Use the provided build script to compile the project:

```sh
./build.sh
```

This will produce the `breadlang` executable in the project root.

## Usage

Run a BreadLang program using LLVM JIT compilation:

```sh
./breadlang examples/hello.bread
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `--dump-ast` | Parses the code and prints the Abstract Syntax Tree structure |
| `--trace` | Runs the program with execution tracing |
| `--emit-llvm` | Emits LLVM IR to a `.ll` file instead of executing |
| `--emit-obj` | Compiles to an object file |
| `--emit-exe` | Compiles to a native executable |
| `-o <file>` | Specifies output file for emit operations |

## Language Syntax

BreadLang supports a modern, expressive syntax.

**Variables and Math:**
```bread
let x: Int = 10
let y: Int = 20
print(x + y)
```

**Control Flow:**
```bread
let a: Int = 5
if a > 3 {
    print(1)
} else {
    print(0)
}

let i: Int = 0
while i < 5 {
    print(i)
    i = i + 1
}

for i in range(5) {
    print(i)
}
```

**Functions:**
```bread
func add(a: Int, b: Int) -> Int {
    return a + b
}

print(add(10, 5))
```

**Arrays and Dictionaries:**
```bread
let arr: [Int] = [1, 2, 3, 4, 5]
print(arr[0])

let dict: [String: Int] = ["hello": 1, "world": 2]
print(dict["hello"])
```

## Testing

The project maintains a comprehensive test suite to ensure stability.

### Running Tests

Use the unified test runner script:

```sh
# Run all tests
./scripts/tests.sh

# Run only integration tests
./scripts/tests.sh -t integration

# Run only LLVM backend tests
./scripts/tests.sh -t llvm_backend
```

### Test Categories

- **Integration Tests** (`tests/integration/`): Verifies core language features using JIT compilation
- **LLVM Backend Tests** (`tests/llvm_backend/`): Verifies native code generation and execution

## Project Structure

- `src/`: Source code
  - `compiler/`: Lexer, parser, and semantic analysis
  - `backends/`: LLVM JIT compilation logic
  - `codegen/`: LLVM IR code generation
  - `runtime/`: Runtime support functions
- `include/`: Header files
- `tests/`: Test cases (`.bread` source and `.expected` output)
- `scripts/`: Build and test automation scripts

## Architecture

BreadLang uses a modern compilation pipeline:

1. **Parsing**: Source code → Abstract Syntax Tree (AST)
2. **Semantic Analysis**: Type checking and validation
3. **Code Generation**: AST → LLVM IR
4. **JIT Compilation**: LLVM IR → Native machine code
5. **Execution**: Direct native code execution
