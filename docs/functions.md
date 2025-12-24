# Functions

## Function Declaration

### Basic Function Syntax

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
// def absolute(x: Int) -> Int {
//     if x < 0 {
//         return -x
//     }
//     // ERROR: No return in else path
// }

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

## Calling Functions

### Basic Function Calls

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

### Function Call Examples

```breadlang
def calculate(x: Int, y: Int, operation: String) -> Int {
    if operation == "add" {
        return x + y
    } else if operation == "subtract" {
        return x - y
    } else if operation == "multiply" {
        return x * y
    } else {
        return 0
    }
}

let result1: Int = calculate(10, 5, "add")       // 15
let result2: Int = calculate(10, 5, "subtract")  // 5
let result3: Int = calculate(10, 5, "multiply")  // 50
```

## Default Parameter Values

### Functions with Defaults

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

### Default Parameter Rules

- Parameters with defaults must come after required parameters
- Cannot skip parameters (no named arguments)
- Supply arguments left-to-right only

```breadlang
// ERROR: Required parameter after default
// def badFunction(name: String = "default", age: Int) -> String { }

// OK: Defaults at end
def goodFunction(age: Int, name: String = "default") -> String {
    return name + " is " + str(age)
}
```

## Function Examples

### Mathematical Functions

```breadlang
def factorial(n: Int) -> Int {
    if n <= 1 {
        return 1
    } else {
        return n * factorial(n - 1)
    }
}

def max(a: Int, b: Int) -> Int {
    if a > b {
        return a
    } else {
        return b
    }
}

def isPrime(n: Int) -> Bool {
    if n < 2 {
        return false
    }
    
    for i in range(2, n) {
        if n % i == 0 {
            return false
        }
    }
    
    return true
}
```

### String Processing Functions

```breadlang
def capitalize(text: String) -> String {
    if text.length == 0 {
        return text
    }
    
    let first: String = text[0]
    let rest: String = ""
    
    for i in range(1, text.length) {
        rest = rest + text[i]
    }
    
    return first + rest  // Note: No built-in uppercase conversion
}

def repeat(text: String, times: Int) -> String {
    let result: String = ""
    for i in range(times) {
        result = result + text
    }
    return result
}
```

### Array Processing Functions

```breadlang
def sum(numbers: [Int]) -> Int {
    let total: Int = 0
    for num in numbers {
        total = total + num
    }
    return total
}

def contains(arr: [Int], target: Int) -> Bool {
    for value in arr {
        if value == target {
            return true
        }
    }
    return false
}

def reverse(arr: [Int]) -> [Int] {
    let result: [Int] = []
    for i in range(arr.length) {
        let index: Int = arr.length - 1 - i
        result.append(arr[index])
    }
    return result
}
```

## Function Limitations

### No Function Overloading

```breadlang
def add(a: Int, b: Int) -> Int { return a + b }
// def add(a: Double, b: Double) -> Double { }  // ERROR: Redefinition
```

### No Variadic Functions

```breadlang
// def sum(values: ...Int) -> Int { }  // Not supported
```

### No Return Type Inference

```breadlang
// def add(a: Int, b: Int) { return a + b }  // ERROR: Missing return type
def add(a: Int, b: Int) -> Int { return a + b }  // OK
```

### Functions Are Not First-Class

```breadlang
// Cannot assign functions to variables
// let operation = add  // ERROR

// Cannot pass functions as arguments
// def apply(func, x: Int, y: Int) -> Int { }  // ERROR
```