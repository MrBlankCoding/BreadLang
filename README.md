# BreadLang

BreadLang is a modern programming language implemented in C with LLVM JIT compilation. It compiles directly to native machine code using LLVM's Just-In-Time compiler for optimal performance, eliminating the need for traditional interpretation.

## Features

- **Pure LLVM JIT Compilation**: All code execution goes through LLVM's JIT engine - no AST interpretation
- **Native Performance**: Direct compilation to optimized machine code
- **Core Language Support**:
  - Arithmetic operations and expressions
  - Variable declarations and assignments (`let`, `var`, `const`)
  - Control flow (`if`/`else`, `while` loops, Python-style `for i in range(n)` loops)
  - Function definitions and calls with type annotations
  - Arrays and dictionaries with literal syntax
  - String operations and indexing
  - Standard output via `print`
- **Advanced Features**:
  - Type inference and checking
  - Memory management with reference counting
  - Optimized code generation
  - LLVM IR emission for inspection
  - Native executable generation

## Build

### Prerequisites

- A C compiler (GCC/Clang) supporting C11
- **LLVM**: Required for JIT compilation (`llvm-config` must be in your PATH)
  - Tested with LLVM 14+
  - On macOS: `brew install llvm`
  - On Ubuntu: `sudo apt install llvm-dev`

### Building

Use the provided build script to compile the project:

```sh
./build.sh
```

This will produce the `breadlang` executable in the project root.

## Usage

Run a BreadLang program using LLVM JIT compilation:

```sh
./breadlang program.bread
```

Run inline BreadLang code from the command line:

```sh
./breadlang -c 'print(1 + 2)'
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `--dump-ast` | Parses the code and prints the Abstract Syntax Tree structure |
| `--emit-llvm` | Emits LLVM IR to a `.ll` file instead of executing |
| `--emit-obj` | Compiles to an object file |
| `--emit-exe` | Compiles to a native executable |
| `-o <file>` | Specifies output file for emit operations |
| `-h`, `--help` | Shows usage and available options |
| `-c <code>`, `--eval <code>` | Executes BreadLang code passed directly on the command line |

## Language Syntax

BreadLang supports a modern, expressive syntax with strong typing.

**Variables and Math:**
```breadlang
let x: Int = 10
let y: Int = 20
print(x + y)  // Output: 30

const PI: Double = 3.14159
var counter: Int = 0
```

**Control Flow:**
```breadlang
let a: Int = 5
if a > 3 {
    print("Greater than 3")
} else {
    print("Less than or equal to 3")
}

// While loops
let i: Int = 0
while i < 5 {
    print(i)
    i = i + 1
}

// Python-style range loops
for i in range(5) {
    print(i)  // Prints 0, 1, 2, 3, 4
}

// Nested loops work perfectly
for i in range(3) {
    for j in range(2) {
        print(i + j)
    }
}
```

**Functions:**
```breadlang
func add(a: Int, b: Int) -> Int {
    return a + b
}

func greet(name: String) -> String {
    return "Hello, " + name + "!"
}

print(add(10, 5))        // Output: 15
print(greet("World"))    // Output: Hello, World!
```

**Arrays and Dictionaries:**
```breadlang
// Arrays with type inference
let numbers: [Int] = [1, 2, 3, 4, 5]
print(numbers[0])        // Output: 1
print(numbers.length)    // Output: 5

// Dictionaries
let scores: [String: Int] = ["Alice": 95, "Bob": 87, "Charlie": 92]
print(scores["Alice"])   // Output: 95

// Array operations
let fruits: [String] = ["apple", "banana"]
fruits.append("orange")
print(fruits.length)     // Output: 3
```

**String Operations:**
```breadlang
let message: String = "Hello, BreadLang!"
print(message[0])        // Output: H
print(message.length)    // Output: 16

let greeting: String = "Hello" + ", " + "World"
print(greeting)          // Output: Hello, World
```

## Testing

The project maintains a comprehensive test suite covering all language features.

### Running Tests

Use the unified test runner script:

```sh
# Run all tests
./scripts/tests.sh

# Run with verbose output
./scripts/tests.sh --verbose

# Run only integration tests
./scripts/tests.sh -t integration

# Run only LLVM backend tests
./scripts/tests.sh -t llvm_backend
```

### Test Categories

- **Integration Tests** (`tests/integration/`): Core language features and syntax
- **LLVM Backend Tests** (`tests/llvm_backend/`): Performance, optimization, and code generation
- **Property-Based Tests** (`tests/property/`): Automated testing with random inputs

All tests use LLVM JIT compilation for execution, ensuring the test environment matches production.

## Project Structure

```
├── src/
│   ├── compiler/          # Frontend: parsing, AST, semantic analysis
│   ├── backends/          # LLVM JIT compilation backend
│   ├── codegen/           # LLVM IR code generation
│   ├── runtime/           # Runtime support and built-in functions
│   ├── core/              # Core data structures (values, arrays, etc.)
│   └── main.c             # Entry point and CLI handling
├── include/               # Header files
├── tests/                 # Test cases (.bread source and .expected output)
├── scripts/               # Build and test automation
└── build.sh              # Main build script
```

## Architecture

BreadLang uses a modern compilation pipeline optimized for performance:

1. **Lexing & Parsing**: Source code → Abstract Syntax Tree (AST)
2. **Semantic Analysis**: Type checking, variable resolution, and validation
3. **Code Generation**: AST → LLVM IR with optimizations
4. **JIT Compilation**: LLVM IR → Optimized native machine code
5. **Execution**: Direct native code execution (no interpretation)
