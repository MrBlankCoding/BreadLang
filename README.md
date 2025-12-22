# BreadLang Syntax & Learning Guide

BreadLang is a modern, statically-typed programming language with LLVM JIT compilation, designed for simplicity, readability, and native performance.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Language Fundamentals](#language-fundamentals)
3. [Types & Values](#types--values)
4. [Variables & Constants](#variables--constants)
5. [Operators & Expressions](#operators--expressions)
6. [Collections](#collections)
7. [Control Flow](#control-flow)
8. [Functions](#functions)
9. [Built-in Functions](#built-in-functions)
10. [Advanced Features](#advanced-features)

---

## Getting Started

### Installation

```bash
# Clone the repository
git clone <repository-url>
cd breadlang

# Build with CMake
mkdir build && cd build
cmake ..
make
```

### Your First Program

Create a file `hello.bread`:

```breadlang
print("Hello, World!")
```

Compile and run:

```bash
./build/breadlang -o hello hello.bread
./hello
```

### File Structure

- Source files use the `.bread` extension
- Statements are separated by newlines (`;` is also accepted)
- Code blocks are delimited with `{ ... }`
- Line comments start with `//`

---

## Language Fundamentals

### Comments

```breadlang
// This is a single-line comment
let x: Int = 42  // Comments can appear after code
```

### Basic Syntax Rules

- Whitespace is significant for statement separation
- Indentation is not significant (unlike Python)
- Blocks must use braces `{ }`
- Type annotations follow the pattern `name: Type`

---

## Types & Values

### Primitive Types

BreadLang supports the following primitive types:

| Type | Description | Example |
|------|-------------|---------|
| `Int` | Integer numbers | `42`, `-10` |
| `Double` | Double-precision floating-point | `3.14`, `2.718` |
| `Float` | Single-precision floating-point | `1.5` |
| `Bool` | Boolean values | `true`, `false` |
| `String` | Text strings | `"Hello"` |

### Special Values

```breadlang
let nothing: Int? = nil  // Represents absence of value
let isReady: Bool = true
let isError: Bool = false
```

### Literal Values

```breadlang
// Numbers
let integer: Int = 42
let decimal: Double = 3.14159

// Strings (with escape sequences)
let text: String = "Hello, World!"
let multiEscape: String = "Line 1\nLine 2\tTabbed"
let quote: String = "She said \"Hello\""
let backslash: String = "Path\\to\\file"
```

### Optional Types

Optionals represent values that might be absent:

```breadlang
let value: Int? = 42      // Optional with value
let empty: Int? = nil     // Optional without value
```

---

## Variables & Constants

### Declarations

```breadlang
// Mutable variable (can be reassigned)
let counter: Int = 0
counter = counter + 1

// Immutable constant (cannot be reassigned)
const PI: Double = 3.14159
const APP_NAME: String = "BreadLang"
```

**Best Practice:** Use `const` by default; use `let` only when you need to modify the value.

### Type Annotations

Type annotations are required for variable declarations:

```breadlang
let age: Int = 25
let name: String = "Alice"
let scores: [Int] = [95, 87, 92]
```

---

## Operators & Expressions

### Arithmetic Operators

```breadlang
let sum: Int = 10 + 5      // Addition: 15
let diff: Int = 10 - 5     // Subtraction: 5
let prod: Int = 10 * 5     // Multiplication: 50
let quot: Int = 10 / 5     // Division: 2
let rem: Int = 10 % 3      // Modulo: 1
```

### Comparison Operators

```breadlang
let isEqual: Bool = 5 == 5       // Equal to: true
let notEqual: Bool = 5 != 3      // Not equal to: true
let less: Bool = 3 < 5           // Less than: true
let greater: Bool = 5 > 3        // Greater than: true
let lessEq: Bool = 5 <= 5        // Less than or equal: true
let greaterEq: Bool = 5 >= 3     // Greater than or equal: true
```

### Logical Operators

```breadlang
let both: Bool = true && false   // Logical AND: false
let either: Bool = true || false // Logical OR: true
let inverted: Bool = !true       // Logical NOT: false
```

### Unary Operators

```breadlang
let negative: Int = -42          // Unary negation
let notTrue: Bool = !true        // Logical NOT
```

---

## Collections

### Arrays

Arrays are ordered, mutable collections of elements of the same type.

```breadlang
// Array declaration and initialization
let numbers: [Int] = [1, 2, 3, 4, 5]
let names: [String] = ["Alice", "Bob", "Charlie"]

// Empty array initialization
let empty: [Int] = []

// Nested arrays
let matrix: [[Int]] = [[1, 2], [3, 4], [5, 6]]
```

#### Array Operations

```breadlang
// Accessing elements (zero-indexed)
let first: Int = numbers[0]      // 1
let last: Int = numbers[-1]      // 5 (negative indexing)

// Modifying elements
let items: [Int] = [10, 20, 30]
items[1] = 99  // items is now [10, 99, 30]

// Adding elements
items.append(40)  // items is now [10, 99, 30, 40]

// Array length
let count: Int = items.length    // 4
```

### Dictionaries

Dictionaries are unordered collections of key-value pairs.

```breadlang
// Dictionary declaration (keys are typically strings)
let ages: [String: Int] = ["Alice": 25, "Bob": 30]
let config: [String: String] = ["host": "localhost", "port": "8080"]

// Empty dictionary initialization
let empty_dict: [String: Int] = [:]

// Accessing values
let aliceAge: Int = ages["Alice"]

// Modifying values
ages["Alice"] = 26
ages["Charlie"] = 35  // Adding new key-value pair

// Dictionary length
let count: Int = ages.length  // 3
```

#### Dictionary Member Access Sugar

You can access dictionary values using dot notation:

```breadlang
let user: [String: Int] = ["age": 25, "score": 100]
print(user.age)    // Same as user["age"]
print(user.score)  // Same as user["score"]
```

### String Operations

```breadlang
// String indexing returns a String of length 1
let greeting: String = "Hello"
let first: String = greeting[0]    // "H"
let last: String = greeting[-1]    // "o"

// String length
let len: Int = greeting.length     // 5

// String concatenation
let full: String = "Hello" + " " + "World"
```

---

## Control Flow

### If / Else If / Else

```breadlang
let score: Int = 85

if score >= 90 {
    print("A grade")
} else if score >= 80 {
    print("B grade")
} else if score >= 70 {
    print("C grade")
} else if score >= 60 {
    print("D grade")
} else {
    print("F grade")
}
```

**Note:** You can chain multiple `else if` blocks for multi-way conditionals.

### While Loops

```breadlang
let countdown: Int = 5
while countdown > 0 {
    print(countdown)
    countdown = countdown - 1
}
print("Liftoff!")
```

### For-In Loops

Iterate over any iterable expression:

```breadlang
// Iterate over a range
for i in range(5) {
    print(i)  // Prints 0, 1, 2, 3, 4
}

// Iterate over an array
let fruits: [String] = ["apple", "banana", "orange"]
for fruit in fruits {
    print(fruit)
}

// Iterate over nested structures
let matrix: [[Int]] = [[1, 2], [3, 4], [5, 6]]
for row in matrix {
    for value in row {
        print(value)
    }
}
```

---

## Functions

### Function Declaration

```breadlang
// Basic function
def add(a: Int, b: Int) -> Int {
    return a + b
}

// Alternative syntax
def multiply(x: Int, y: Int) -> Int {
    return x * y
}
```

### Calling Functions

```breadlang
let sum: Int = add(10, 5)       // 15
let product: Int = multiply(4, 7) // 28
print(sum)
```

### Default Parameter Values

Functions can have default parameter values:

```breadlang
def greet(name: String = "World") -> String {
    return "Hello, " + name + "!"
}

print(greet())           // "Hello, World!"
print(greet("Alice"))    // "Hello, Alice!"
```

### Return Values

All code paths in a function must return a value of the declared type:

```breadlang
def absolute(x: Int) -> Int {
    if x < 0 {
        return -x
    } else {
        return x
    }
}
```

---

## Built-in Functions

BreadLang provides several built-in functions for common operations:

### Type Introspection & Conversion

```breadlang
// Get the type of a value
let t: String = type(42)        // "int"
let t2: String = type("hello")  // "string"

// Convert to string
let s: String = str(42)         // "42"
let s2: String = str(true)      // "true"

// Convert to integer
let i: Int = int("123")         // 123
let i2: Int = int(3.14)         // 3

// Convert to float
let f: Double = float("3.14")   // 3.14
let f2: Double = float(42)      // 42.0
```

### Length Function

```breadlang
// Get length of collections
let arr_len: Int = len([1, 2, 3])           // 3
let str_len: Int = len("Hello")             // 5
let dict_len: Int = len(["a": 1, "b": 2])   // 2
```

### Printing Output

```breadlang
print("Hello, World!")
print(42)
print(true)

let name: String = "Alice"
print("Hello, " + name)
```

---

## Advanced Features

### Optional Chaining

Use `?.` to safely access members or call methods on optional values:

```breadlang
let maybeValue: Int? = 42

// Safe member access (returns nil if maybeValue is nil)
let result: Int? = maybeValue?.toString()
```

If the target is `nil`, the entire expression evaluates to `nil`.

### Method Calls

Some types support method calls:

```breadlang
// Array methods
let items: [Int] = [1, 2, 3]
items.append(4)  // items is now [1, 2, 3, 4]

// Conversion methods
let numStr: String = 123.toString()      // "123"
let boolStr: String = true.toString()    // "true"
```

### Range Function

The `range(n)` function generates a sequence from 0 to n-1:

```breadlang
for i in range(10) {
    print(i)  // Prints 0 through 9
}
```

### Negative Indexing

Arrays and strings support negative indices to count from the end:

```breadlang
let items: [Int] = [10, 20, 30, 40, 50]
let lastItem: Int = items[-1]    // 50
let secondLast: Int = items[-2]  // 40

let text: String = "Hello"
let lastChar: String = text[-1]  // "o"
```

---

## Complete Example Programs

### Example 1: FizzBuzz

```breadlang
for i in range(1, 101) {
    if i % 15 == 0 {
        print("FizzBuzz")
    } else if i % 3 == 0 {
        print("Fizz")
    } else if i % 5 == 0 {
        print("Buzz")
    } else {
        print(i)
    }
}
```

### Example 2: Temperature Converter

```breadlang
def celsiusToFahrenheit(celsius: Double) -> Double {
    return celsius * 9.0 / 5.0 + 32.0
}

def fahrenheitToCelsius(fahrenheit: Double) -> Double {
    return (fahrenheit - 32.0) * 5.0 / 9.0
}

let tempC: Double = 25.0
let tempF: Double = celsiusToFahrenheit(tempC)
print("25°C is " + str(tempF) + "°F")
```

### Example 3: Working with Collections

```breadlang
// Create a grade book
let grades: [String: Int] = [:]
grades["Alice"] = 95
grades["Bob"] = 87
grades["Charlie"] = 92

// Calculate average
let total: Int = 0
let count: Int = grades.length

for name in grades {
    total = total + grades[name]
}

let average: Double = float(total) / float(count)
print("Average grade: " + str(average))
```

---

## Testing Your Code

Run the test suite to verify your installation:

```bash
# Build the project
cd build
make

# Run all tests
cd ..
./run_tests.sh

# Run individual tests
./build/bread_test_runner
./build/breadlang tests/ctest/basic_print.bread tests/ctest/basic_print.expected
```

---

## Structs

BreadLang supports simple struct types with named fields.

```breadlang
struct Point {
    x: Int
    y: Int
}

let p: Point = Point{x: 10, y: 20}
print(p)  // Point { x: 10, y: 20 }