# BreadLang Complete Learning Guide

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
10. [Object-Oriented Programming](#object-oriented-programming)
11. [Syntax Quirks & Common Pitfalls](#syntax-quirks--common-pitfalls)

---

## Getting Started

### Installation

```bash
# Clone the repository
git clone <repository-url>
cd breadlang

# Build with Make (recommended)
make build

# Or build with CMake directly
mkdir build && cd build
cmake ..
make
```

### Your First Program

Create a file `hello.bread`:

```breadlang
print("Hello, World!")
```

#### Quick Execution (JIT)

```bash
# Using convenience script (recommended)
./bread run hello.bread

# Using Makefile
make run FILE=hello.bread

# Or using the binary directly
./build/breadlang --jit hello.bread
```

#### Compile to Executable

```bash
# Using convenience script (recommended)
./bread compile hello.bread hello
./hello

# Using Makefile
make compile-exe FILE=hello.bread OUT=hello
./hello

# Or using the binary directly
./build/breadlang --emit-exe -o hello hello.bread
./hello
```

### Convenience Script

The `bread` script provides a simple interface to common operations:

```bash
./bread run <file>              # JIT execution
./bread compile <file> [output] # Compile to executable
./bread llvm <file> [output]    # Emit LLVM IR
./bread build                   # Build compiler
./bread help                    # Show help
```

### Makefile Targets

The enhanced Makefile provides convenient targets for common operations:

#### Execution Targets
```bash
make run FILE=program.bread          # JIT execution
make jit FILE=program.bread          # JIT execution (alias)
```

#### Compilation Targets
```bash
make compile-exe FILE=program.bread [OUT=output]     # Create executable
make compile-llvm FILE=program.bread [OUT=output.ll] # Emit LLVM IR
make compile-obj FILE=program.bread [OUT=output.o]   # Emit object file
```

#### Build Targets
```bash
make build                           # Build the compiler
make clean                           # Clean build artifacts
make rebuild                         # Clean and rebuild
make test                            # Run tests
```

### File Structure

- Source files use the `.bread` extension
- Statements are separated by newlines (`;` is also accepted but optional)
- Code blocks are delimited with `{ ... }`
- Line comments start with `//`
- No semicolons required at end of lines

---

## Language Fundamentals

### Comments

```breadlang
// This is a single-line comment
let x: Int = 42  // Comments can appear after code

// BreadLang does not support multi-line comments
// Use multiple single-line comments instead
```

### Basic Syntax Rules

- **Whitespace**: Newlines separate statements; indentation is not significant
- **Blocks**: Always use braces `{ }` for code blocks
- **Type annotations**: Required and follow the pattern `name: Type`
- **Case sensitivity**: All identifiers are case-sensitive
- **Naming conventions**: Use camelCase for variables/functions, PascalCase for types

**Important:** Unlike Python, indentation has no syntactic meaning. Unlike C/JavaScript, semicolons are optional.

---

## Types & Values

### Primitive Types

| Type | Description | Size | Example |
|------|-------------|------|---------|
| `Int` | Integer numbers | Platform-dependent | `42`, `-10`, `0` |
| `Double` | Double-precision float | 64-bit | `3.14`, `2.718` |
| `Float` | Single-precision float | 32-bit | `1.5` |
| `Bool` | Boolean values | 1-bit | `true`, `false` |
| `String` | Text strings | Variable | `"Hello"` |

### Type System Characteristics

- **Static typing**: All types must be known at compile time
- **Type inference**: Limited; explicit type annotations are required for declarations
- **No implicit conversions**: Must explicitly convert between types
- **Nullable types**: Use optional syntax `Type?` for nullable values

### Literal Values

```breadlang
// Integer literals
let decimal: Int = 42
let negative: Int = -100
let zero: Int = 0

// Floating-point literals (require decimal point)
let pi: Double = 3.14159
let scientific: Double = 1.5  // No scientific notation support yet

// String literals with escape sequences
let simple: String = "Hello"
let newline: String = "Line 1\nLine 2"
let tab: String = "Column1\tColumn2"
let quote: String = "She said \"Hello\""
let backslash: String = "C:\\Users\\path"

// Boolean literals
let isTrue: Bool = true
let isFalse: Bool = false
```

### Supported Escape Sequences

| Sequence | Meaning |
|----------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\\` | Backslash |
| `\"` | Double quote |

### Optional Types

Optionals represent values that might be absent:

```breadlang
let value: Int? = 42      // Optional with value
let empty: Int? = nil     // Optional without value

// Working with optionals
if value != nil {
    print("Has value")
}

// Optional chaining (returns nil if optional is nil)
let result: String? = value?.toString()
```

**Quirk:** Dereferencing a `nil` optional will cause a runtime error. Always check before accessing.

---

## Variables & Constants

### Declaration Syntax

```breadlang
// Mutable variable (can be reassigned)
let counter: Int = 0
counter = counter + 1  // OK

// Immutable constant (cannot be reassigned)
const PI: Double = 3.14159
// PI = 3.14  // ERROR: Cannot reassign constant
```

### Important Rules

1. **Type annotations are mandatory**: You cannot omit the type annotation
   ```breadlang
   let x = 42        // ERROR: Type annotation required
   let x: Int = 42   // OK
   ```

2. **Initialization required**: Variables must be initialized at declaration
   ```breadlang
   let x: Int        // ERROR: Must initialize
   let x: Int = 0    // OK
   ```

3. **Scope rules**: Variables are block-scoped
   ```breadlang
   if true {
       let x: Int = 10
   }
   // print(x)  // ERROR: x not in scope
   ```

4. **No shadowing in same scope**: Cannot redeclare in same scope
   ```breadlang
   let x: Int = 10
   // let x: Int = 20  // ERROR: Already declared
   ```

**Best Practice:** Use `const` by default; use `let` only when you need to modify the value.

---

## Operators & Expressions

### Arithmetic Operators

```breadlang
let sum: Int = 10 + 5       // Addition: 15
let diff: Int = 10 - 5      // Subtraction: 5
let prod: Int = 10 * 5      // Multiplication: 50
let quot: Int = 10 / 5      // Division: 2
let rem: Int = 10 % 3       // Modulo: 1
```

**Type rules for arithmetic:**
- Both operands must be the same type
- No automatic type promotion
- Integer division truncates (10 / 3 = 3)
- Modulo works only on integers

```breadlang
let a: Int = 10
let b: Double = 5.0
// let c = a + b  // ERROR: Type mismatch

// Must explicitly convert
let c: Double = float(a) + b  // OK
```

### Comparison Operators

```breadlang
let isEqual: Bool = 5 == 5        // Equal: true
let notEqual: Bool = 5 != 3       // Not equal: true
let less: Bool = 3 < 5            // Less than: true
let greater: Bool = 5 > 3         // Greater than: true
let lessEq: Bool = 5 <= 5         // Less or equal: true
let greaterEq: Bool = 5 >= 3      // Greater or equal: true
```

**Type rules for comparisons:**
- Both operands must be the same type
- Strings can be compared (lexicographic order)
- Collections cannot be compared directly

### Logical Operators

```breadlang
let both: Bool = true && false    // Logical AND: false
let either: Bool = true || false  // Logical OR: true
let inverted: Bool = !true        // Logical NOT: false
```

**Short-circuit evaluation:**
- `&&` stops evaluating if first operand is false
- `||` stops evaluating if first operand is true

```breadlang
// Safe: division doesn't occur if x is 0
if x != 0 && 10 / x > 2 {
    print("Safe division")
}
```

### Unary Operators

```breadlang
let negative: Int = -42           // Unary negation
let positive: Int = +42           // Unary plus (identity)
let notTrue: Bool = !true         // Logical NOT
```

### Operator Precedence (Highest to Lowest)

1. Unary operators: `-`, `+`, `!`
2. Multiplicative: `*`, `/`, `%`
3. Additive: `+`, `-`
4. Comparison: `<`, `<=`, `>`, `>=`
5. Equality: `==`, `!=`
6. Logical AND: `&&`
7. Logical OR: `||`

**Quirk:** Use parentheses to make precedence explicit when mixing operators.

---

## Collections

### Arrays

Arrays are ordered, mutable, dynamically-sized collections of elements of the same type.

```breadlang
// Array declaration and initialization
let numbers: [Int] = [1, 2, 3, 4, 5]
let names: [String] = ["Alice", "Bob", "Charlie"]

// Empty array initialization
let empty: [Int] = []

// Nested arrays (2D arrays)
let matrix: [[Int]] = [[1, 2], [3, 4], [5, 6]]
```

#### Array Operations

```breadlang
// Accessing elements (zero-indexed)
let first: Int = numbers[0]       // 1
let second: Int = numbers[1]      // 2

// Negative indexing (counts from end)
let last: Int = numbers[-1]       // 5
let secondLast: Int = numbers[-2] // 4

// Modifying elements
let items: [Int] = [10, 20, 30]
items[1] = 99  // items is now [10, 99, 30]

// Adding elements
items.append(40)  // items is now [10, 99, 30, 40]

// Array length
let count: Int = items.length     // 4
```

#### Array Quirks

1. **Index bounds**: Out-of-bounds access causes runtime error
   ```breadlang
   let arr: [Int] = [1, 2, 3]
   // let x = arr[10]  // RUNTIME ERROR
   ```

2. **Negative indices**: Must be within bounds
   ```breadlang
   let arr: [Int] = [1, 2, 3]
   let x: Int = arr[-1]   // OK: 3
   // let y = arr[-10]    // RUNTIME ERROR
   ```

3. **Type homogeneity**: All elements must be same type
   ```breadlang
   // let mixed = [1, "two", 3.0]  // ERROR: Mixed types
   ```

### Dictionaries

Dictionaries are unordered collections of key-value pairs with string keys.

```breadlang
// Dictionary declaration
let ages: [String: Int] = ["Alice": 25, "Bob": 30]
let config: [String: String] = ["host": "localhost", "port": "8080"]

// Empty dictionary initialization
let empty_dict: [String: Int] = [:]

// Accessing values
let aliceAge: Int = ages["Alice"]

// Modifying values
ages["Alice"] = 26        // Update existing
ages["Charlie"] = 35      // Add new pair

// Dictionary length
let count: Int = ages.length  // Number of key-value pairs
```

#### Dictionary Member Access (Syntactic Sugar)

```breadlang
let user: [String: Int] = ["age": 25, "score": 100]

// Both are equivalent:
print(user["age"])    // Bracket notation
print(user.age)       // Dot notation (sugar)

// This also works for setting values:
user.age = 26         // Same as user["age"] = 26
```

**Limitation:** Dot notation only works when the key is a valid identifier (no spaces, special characters).

#### Dictionary Quirks

1. **Keys must be strings**: No other key types supported currently
   ```breadlang
   // let intKeys: [Int: String] = [1: "one"]  // ERROR
   ```

2. **Missing keys**: Accessing non-existent key causes runtime error
   ```breadlang
   let dict: [String: Int] = ["a": 1]
   // let x = dict["missing"]  // RUNTIME ERROR
   ```

3. **Insertion order not preserved**: Dictionaries are unordered

### String Operations

```breadlang
// String indexing returns a String of length 1
let greeting: String = "Hello"
let first: String = greeting[0]     // "H"
let last: String = greeting[-1]     // "o"

// String length
let len: Int = greeting.length      // 5

// String concatenation (only with + operator)
let full: String = "Hello" + " " + "World"

// String repetition not supported
// let repeated = "Hi" * 3  // ERROR: Not supported
```

#### String Quirks

1. **Indexing returns String**: Not a character type
   ```breadlang
   let s: String = "Hi"
   let c: String = s[0]  // Type is String, value is "H"
   ```

2. **No string interpolation**: Must concatenate manually
   ```breadlang
   let name: String = "Alice"
   // let msg = "Hello, \(name)"  // ERROR: No interpolation
   let msg: String = "Hello, " + name  // OK
   ```

3. **Immutable**: Cannot modify individual characters
   ```breadlang
   let word: String = "Hello"
   // word[0] = "J"  // ERROR: Strings are immutable
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

**Important rules:**
- Condition must be of type `Bool` (no truthy/falsy values)
- Braces are required even for single statements
- `else if` can be chained indefinitely
- Final `else` is optional

```breadlang
// ERROR: Condition must be Bool
let x: Int = 5
// if x { }  // ERROR: Int is not Bool
if x != 0 { }  // OK: comparison returns Bool
```

### While Loops

```breadlang
let countdown: Int = 5
while countdown > 0 {
    print(countdown)
    countdown = countdown - 1
}
print("Liftoff!")
```

**Characteristics:**
- Condition evaluated before each iteration
- Infinite loops possible if condition never becomes false
- No `do-while` variant
- Use `break` to exit early (if supported)

### For-In Loops

Iterate over any iterable expression:

```breadlang
// Iterate over a range
for i in range(5) {
    print(i)  // Prints 0, 1, 2, 3, 4
}

// Range with start and end
for i in range(1, 6) {
    print(i)  // Prints 1, 2, 3, 4, 5
}

// Iterate over an array
let fruits: [String] = ["apple", "banana", "orange"]
for fruit in fruits {
    print(fruit)
}

// Iterate over dictionary (iterates over keys)
let ages: [String: Int] = ["Alice": 25, "Bob": 30]
for name in ages {
    print(name + " is " + str(ages[name]))
}

// Nested iteration
let matrix: [[Int]] = [[1, 2], [3, 4], [5, 6]]
for row in matrix {
    for value in row {
        print(value)
    }
}
```

#### For-Loop Quirks

1. **Loop variable is immutable**: Cannot reassign loop variable
   ```breadlang
   for i in range(5) {
       // i = i + 1  // ERROR: Loop variable is immutable
   }
   ```

2. **Dictionary iteration**: Iterates over keys, not key-value pairs
   ```breadlang
   let dict: [String: Int] = ["a": 1, "b": 2]
   for key in dict {
       let value: Int = dict[key]  // Must access value separately
   }
   ```

3. **Range is exclusive**: `range(5)` gives 0-4, not 0-5

---

## Functions

### Function Declaration

```breadlang
// Basic function
def add(a: Int, b: Int) -> Int {
    return a + b
}

// Function with multiple statements
def greet(name: String) -> String {
    let greeting: String = "Hello, " + name
    return greeting + "!"
}

// Function with no parameters
def getCurrentTime() -> String {
    return "12:00"
}
```

### Function Syntax Rules

1. **`def` keyword required**: All functions start with `def`
2. **Parameter types mandatory**: Each parameter needs a type annotation
3. **Return type mandatory**: Must specify return type after `->`
4. **Return statement required**: All code paths must return a value

```breadlang
// ERROR: Missing return in some code path
def absolute(x: Int) -> Int {
    if x < 0 {
        return -x
    }
    // ERROR: No return in else path
}

// OK: All paths return
def absolute(x: Int) -> Int {
    if x < 0 {
        return -x
    } else {
        return x
    }
}

// OK: Unconditional return at end
def absolute(x: Int) -> Int {
    if x < 0 {
        return -x
    }
    return x
}
```

### Calling Functions

```breadlang
// Positional arguments only (no named arguments)
let sum: Int = add(10, 5)
let product: Int = multiply(4, 7)

// Function calls as expressions
let result: Int = add(5, multiply(2, 3))

// Functions must be called with correct number of arguments
// add(5)  // ERROR: Missing argument
// add(5, 10, 15)  // ERROR: Too many arguments
```

### Default Parameter Values

```breadlang
def greet(name: String = "World") -> String {
    return "Hello, " + name + "!"
}

print(greet())           // "Hello, World!"
print(greet("Alice"))    // "Hello, Alice!"

// Multiple defaults
def createUser(name: String, age: Int = 18, country: String = "USA") -> String {
    return name + ", " + str(age) + ", " + country
}

print(createUser("Bob"))              // "Bob, 18, USA"
print(createUser("Alice", 25))        // "Alice, 25, USA"
print(createUser("Charlie", 30, "UK")) // "Charlie, 30, UK"
```

**Rules for defaults:**
- Parameters with defaults must come after required parameters
- Cannot skip parameters (no named arguments)
- Supply arguments left-to-right only

### Function Quirks

1. **No function overloading**: Cannot have multiple functions with same name
   ```breadlang
   def add(a: Int, b: Int) -> Int { return a + b }
   // def add(a: Double, b: Double) -> Double { }  // ERROR: Redefinition
   ```

2. **No variadic functions**: Cannot have variable number of arguments
   ```breadlang
   // def sum(values: ...Int) -> Int { }  // Not supported
   ```

3. **No return type inference**: Must explicitly specify return type
   ```breadlang
   // def add(a: Int, b: Int) { return a + b }  // ERROR: Missing return type
   ```

4. **Functions are not first-class**: Cannot assign functions to variables or pass as arguments

---

## Built-in Functions

BreadLang provides several built-in functions for common operations:

### Type Introspection

```breadlang
// Get the type of a value as a string
let t1: String = type(42)           // "int"
let t2: String = type("hello")      // "string"
let t3: String = type(true)         // "bool"
let t4: String = type([1, 2, 3])    // "array"
let t5: String = type(["a": 1])     // "dict"
```

### Type Conversion

```breadlang
// Convert to string
let s1: String = str(42)            // "42"
let s2: String = str(true)          // "true"
let s3: String = str(3.14)          // "3.14"

// Convert to integer (truncates decimals)
let i1: Int = int("123")            // 123
let i2: Int = int(3.14)             // 3
let i3: Int = int(true)             // 1

// Convert to float/double
let f1: Double = float("3.14")      // 3.14
let f2: Double = float(42)          // 42.0
let f3: Double = float(true)        // 1.0
```

**Conversion quirks:**
- Invalid string conversions may cause runtime errors
- `int()` truncates, doesn't round
- Boolean conversions: `true` → 1, `false` → 0

### Length Function

```breadlang
// Get length of collections
let arr_len: Int = len([1, 2, 3])              // 3
let str_len: Int = len("Hello")                // 5
let dict_len: Int = len(["a": 1, "b": 2])      // 2

// Also works via property syntax
let arr: [Int] = [1, 2, 3]
let len1: Int = len(arr)     // Function call
let len2: Int = arr.length   // Property access (equivalent)
```

### Range Function

```breadlang
// range(n) generates 0 to n-1
for i in range(5) {
    print(i)  // 0, 1, 2, 3, 4
}

// range(start, end) generates start to end-1
for i in range(2, 7) {
    print(i)  // 2, 3, 4, 5, 6
}

// range with step (if supported)
// for i in range(0, 10, 2) { }  // May not be supported
```

### Print Function

```breadlang
// Print to standard output
print("Hello, World!")
print(42)
print(true)
print([1, 2, 3])

// Concatenate before printing
let name: String = "Alice"
print("Hello, " + name)

// Note: print() adds newline automatically
```

**Print quirks:**
- No printf-style formatting
- No multiple arguments: `print(x, y)` may not work
- Always adds newline (no built-in way to suppress)

---

## Object-Oriented Programming

### Structs

Simple data containers with named fields (no methods):

```breadlang
struct Point {
    x: Int
    y: Int
}

struct Person {
    name: String
    age: Int
}

// Creating struct instances
let p: Point = Point{x: 10, y: 20}
let person: Person = Person{name: "Alice", age: 25}

// Accessing fields
print(p.x)           // 10
print(person.name)   // "Alice"

// Modifying fields (if struct variable is mutable)
let origin: Point = Point{x: 0, y: 0}
origin.x = 5        // OK if origin declared with 'let'

// Printing structs shows all fields
print(p)  // Point { x: 10, y: 20 }
```

### Classes

Classes support inheritance, fields, and methods:

```breadlang
// Base class
class Animal {
    name: String
    age: Int

    // Initializer
    def init(name: String, age: Int) {
        self.name = name
        self.age = age
    }

    def speak() -> String {
        return "Some sound"
    }

    def getInfo() -> String {
        return name + " is " + str(age) + " years old"
    }
}

// Derived class with inheritance
class Dog extends Animal {
    breed: String

    // Initializer (must call super.init)
    def init(name: String, age: Int, breed: String) {
        super.init(name, age)
        self.breed = breed
    }

    // Override parent method
    def speak() -> String {
        return "Woof!"
    }

    // New method specific to Dog
    def wagTail() -> String {
        return name + " is wagging tail"
    }
}

// Creating instances
let animal: Animal = Animal("Generic", 5)
let dog: Dog = Dog("Buddy", 3, "Golden Retriever")

// Calling methods
print(animal.speak())    // "Some sound"
print(dog.speak())       // "Woof!"
print(dog.wagTail())     // "Buddy is wagging tail"

// Accessing fields
print(dog.name)          // "Buddy"
print(dog.breed)         // "Golden Retriever"
```

### OOP Characteristics

1. **Field initialization**: All fields must be initialized when creating instances
2. **Method access**: Methods can access instance fields directly
3. **Method overriding**: Child classes can override parent methods
4. **Single inheritance**: Classes can extend only one parent class
5. **No constructors**: Use initialization syntax `ClassName{field: value}`

### OOP Quirks and Limitations

1. **No access modifiers**: All fields and methods are public
   ```breadlang
   class Example {
       // No private, protected, or public keywords
       field: Int
   }
   ```

2. **No constructor methods**: Cannot define `__init__` or constructor logic
   ```breadlang
   class Point {
       x: Int
       y: Int
       
       // No constructor - use initialization syntax instead
       // def __init__(x: Int, y: Int) { }  // Not supported
   }
   ```

3. **Method overriding without keyword**: No `override` keyword required
   ```breadlang
   class Parent {
       def method() -> String { return "parent" }
   }
   
   class Child extends Parent {
       // Implicitly overrides, no 'override' keyword needed
       def method() -> String { return "child" }
   }
   ```

4. **No interface or protocol support**: Only single inheritance

5. **Type checking**: Type is reported as "Class" for all class instances
   ```breadlang
   let dog: Dog = Dog{name: "Rex", age: 2, breed: "Husky"}
   print(type(dog))  // "Class" (not "Dog")
   ```

---

## Syntax Quirks & Common Pitfalls

### 1. Type Annotations Are Mandatory

```breadlang
// ERROR: Missing type
// let x = 42

// OK: Explicit type
let x: Int = 42

// ERROR: Cannot infer type even from obvious context
// let numbers = [1, 2, 3]

// OK: Explicit array type
let numbers: [Int] = [1, 2, 3]
```

### 2. No Type Inference or Type Coercion

```breadlang
let a: Int = 10
let b: Double = 5.0

// ERROR: Cannot mix types
// let c = a + b

// OK: Explicit conversion
let c: Double = float(a) + b

// ERROR: No automatic widening
// let d: Double = a

// OK: Explicit conversion
let d: Double = float(a)
```

### 3. Conditions Must Be Boolean

```breadlang
let x: Int = 5

// ERROR: Int is not Bool
// if x { }

// OK: Explicit comparison
if x != 0 { }
if x > 0 { }

// OK: Boolean variable
let isReady: Bool = true
if isReady { }
```

### 4. String Concatenation Only

```breadlang
let name: String = "Alice"
let age: Int = 25

// ERROR: Cannot concatenate String and Int
// let msg = "Age: " + age

// OK: Convert to string first
let msg: String = "Age: " + str(age)

// No string interpolation
// let msg = "Hello, \(name)"  // ERROR

// Must concatenate manually
let msg2: String = "Hello, " + name
```

### 5. Array and Dictionary Access Can Fail

```breadlang
let arr: [Int] = [1, 2, 3]

// RUNTIME ERROR: Index out of bounds
// let x = arr[10]

// Always check bounds or know your data
if 10 < arr.length {
    let x: Int = arr[10]
}

let dict: [String: Int] = ["a": 1]

// RUNTIME ERROR: Key doesn't exist
// let y = dict["missing"]

// No built-in way to check key existence safely
```

### 6. Loop Variables Are Immutable

```breadlang
for i in range(10) {
    // ERROR: Cannot reassign loop variable
    // i = i + 1
    
    // OK: Use loop variable as-is
    print(i)
}
```

### 7. Functions Must Return on All Code Paths

```breadlang
// ERROR: Missing return in else branch
// def absolute(x: Int) -> Int {
//     if x < 0 {
//         return -x
//     }
// }

// OK: All paths return
def absolute(x: Int) -> Int {
    if x < 0 {
        return -x
    }
    return x
}
```

### 8. No Implicit Bool Conversions

```breadlang
let count: Int = 5

// ERROR: Int cannot be used as Bool
// while count {
//     count = count - 1
// }

// OK: Explicit comparison
while count > 0 {
    count = count - 1
}
```

### 9. Dictionary Iteration Gives Keys Only

```breadlang
let scores: [String: Int] = ["Alice": 95, "Bob": 87]

for name in scores {
    // 'name' is the key (String)
    let score: Int = scores[name]  // Access value separately
    print(name + ": " + str(score))
}
```

### 10. No Function Overloading or First-Class Functions

```breadlang
// ERROR: Cannot overload functions
// def add(a: Int, b: Int) -> Int { return a + b }
// def add(a: Double, b: Double) -> Double { return a + b }

// ERROR: Cannot assign functions to variables
// let operation = add
// let result = operation(5, 3)
```

---