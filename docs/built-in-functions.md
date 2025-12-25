# Built-in Functions

BreadLang provides several built-in functions for common operations.

## Input Functions

### input() Function

Read a line of input from the user (stdin). It takes a prompt string.

```breadlang
let name: String = input("What is your name? ")
print("Hello " + name)
```

## Type Introspection

### type() Function

Get the type of a value as a string:

```breadlang
let t1: String = type(42)           // "int"
let t2: String = type("hello")      // "string"
let t3: String = type(true)         // "bool"
let t4: String = type([1, 2, 3])    // "array"
let t5: String = type(["a": 1])     // "dict"
```

## Type Conversion

### str() Function

Convert values to string representation:

```breadlang
let s1: String = str(42)            // "42"
let s2: String = str(true)          // "true"
let s3: String = str(3.14)          // "3.14"
let s4: String = str([1, 2, 3])     // "[1, 2, 3]"
```

### int() Function

Convert values to integers:

```breadlang
let i1: Int = int("123")            // 123
let i2: Int = int(3.14)             // 3 (truncates)
let i3: Int = int(true)             // 1
let i4: Int = int(false)            // 0
```

**Note:** `int()` truncates decimal values, it does not round.

### float() Function

Convert values to double-precision floating-point:

```breadlang
let f1: Double = float("3.14")      // 3.14
let f2: Double = float(42)          // 42.0
let f3: Double = float(true)        // 1.0
let f4: Double = float(false)       // 0.0
```

## Length Functions

### len() Function

Get the length of collections:

```breadlang
let arr_len: Int = len([1, 2, 3])              // 3
let str_len: Int = len("Hello")                // 5
let dict_len: Int = len(["a": 1, "b": 2])      // 2
```

### .length Property

Alternative syntax for getting length:

```breadlang
let arr: [Int] = [1, 2, 3]
let len1: Int = len(arr)     // Function call
let len2: Int = arr.length   // Property access (equivalent)

let text: String = "Hello"
let textLen: Int = text.length  // 5

let ages: [String: Int] = ["Alice": 25, "Bob": 30]
let dictSize: Int = ages.length  // 2
```

## Range Function

### range() Function

Generate sequences of numbers for iteration:

```breadlang
// range(n) generates 0 to n-1
for i in range(5) {
    print(i)  // 0, 1, 2, 3, 4
}

// range(start, end) generates start to end-1
for i in range(2, 7) {
    print(i)  // 2, 3, 4, 5, 6
}
```

**Note:** Range is exclusive of the end value.

## Conversion Examples

### Safe Conversions

```breadlang
def safeStringToInt(s: String) -> Int {
    // Note: No built-in error handling for invalid conversions
    // Invalid strings may cause runtime errors
    return int(s)
}

def boolToString(b: Bool) -> String {
    if b {
        return "true"
    } else {
        return "false"
    }
}
```

### Type Checking and Conversion

```breadlang
def processValue(value: Int) -> String {
    let valueType: String = type(value)
    
    if valueType == "int" {
        return "Integer: " + str(value)
    } else {
        return "Not an integer"
    }
}
```

## Practical Examples

### Working with Collections

```breadlang
def printArrayInfo(arr: [Int]) -> String {
    let size: Int = len(arr)
    let sizeStr: String = str(size)
    
    if size == 0 {
        return "Empty array"
    } else {
        return "Array with " + sizeStr + " elements"
    }
}

def printDictInfo(dict: [String: Int]) -> String {
    let count: Int = dict.length
    return "Dictionary has " + str(count) + " entries"
}
```

### String Processing

```breadlang
def analyzeText(text: String) -> String {
    let length: Int = text.length
    let lengthStr: String = str(length)
    
    return "Text '" + text + "' has " + lengthStr + " characters"
}
```

### Numeric Processing

```breadlang
def formatNumber(num: Int) -> String {
    let numStr: String = str(num)
    let numType: String = type(num)
    
    return "Number " + numStr + " (type: " + numType + ")"
}

def convertAndFormat(floatVal: Double) -> String {
    let intVal: Int = int(floatVal)  // Truncates
    let backToFloat: Double = float(intVal)
    
    return str(floatVal) + " -> " + str(intVal) + " -> " + str(backToFloat)
}
```