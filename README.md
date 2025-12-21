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

- **Clang**: Required C compiler supporting C11 (GCC is not supported)
- **CMake**: Build system generator (version 3.16 or higher)
- **LLVM**: Required for JIT compilation (LLVM 14+)
  - On macOS: `brew install llvm cmake`
  - On Ubuntu: `sudo apt install llvm-dev cmake`

### Building

1. Configure and build the project:
   ```sh
   mkdir -p build && cd build
   cmake ..
   cmake --build .
   ```

The `breadlang` executable will be available in the `build` directory.

### Running Tests

After building, you can run the test suite:

```sh
cd build
ctest --output-on-failure
```

Or run specific test suites:
```sh
ctest -R integration  # Run integration tests
ctest -R llvm         # Run LLVM backend tests
```

## Usage

Run a BreadLang program using LLVM JIT compilation:

```sh
./build/breadlang program.bread
```

To build a native executable from a BreadLang program:

```sh
./build/breadlang --output program program.bread
./program
```

Run inline BreadLang code from the command line:

```sh
./build/breadlang -c 'print(1 + 2)'
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

BreadLang maintains a comprehensive test suite covering all language features and implementation details.

### Test Categories

- **Integration Tests** (`tests/integration/`): End-to-end language feature testing
- **LLVM Backend Tests** (`tests/llvm_backend/`): Code generation, optimization, and performance
- **Property-Based Tests** (`tests/property/`): Automated testing with systematic coverage
  - `core/`: Core language features (values, types, variables)
  - `runtime/`: Runtime systems (memory, strings, arrays, GC)
  - `compiler/`: Compiler components (parser, semantic analysis)
  - `integration/`: Cross-component property tests

### Running Tests

#### All Tests
```sh
./scripts/tests.sh
```

#### Specific Categories
```sh
# Integration tests only
./scripts/tests.sh -t integration

# LLVM backend tests only
./scripts/tests.sh -t llvm_backend

# Property-based tests
cd tests/property
make test
```

#### Using CMake
```sh
cd build
make test-all          # All tests
make test-integration  # Integration tests
make test-llvm         # LLVM backend tests
make test-property     # Property-based tests
```

All tests use LLVM JIT compilation, ensuring the test environment matches production behavior.

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
