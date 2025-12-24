# Collections

## Arrays

Arrays are ordered, mutable, dynamically-sized collections of elements of the same type.

### Array Declaration

```breadlang
let numbers: [Int] = [1, 2, 3, 4, 5]
let names: [String] = ["Alice", "Bob", "Charlie"]
let empty: [Int] = []

// 2d Array
let matrix: [[Int]] = [[1, 2], [3, 4], [5, 6]]
```

### Array Operations

```breadlang
let first: Int = numbers[0]       // 1
let second: Int = numbers[1]      // 2
// Count from end
let last: Int = numbers[-1]       // 5
let secondLast: Int = numbers[-2] // 4

let items: [Int] = [10, 20, 30]
items[1] = 99  // items is now [10, 99, 30]

items.append(40)  // items is now [10, 99, 30, 40]

// Array length
let count: Int = items.length     // 4
```

### Array Limitations

1. **Index bounds**: Out-of-bounds access causes runtime error
2. **Negative indices**: Must be within bounds
3. **Type homogeneity**: All elements must be same type

```breadlang
let arr: [Int] = [1, 2, 3]
// let x = arr[10]     // RUNTIME ERROR
// let y = arr[-10]    // RUNTIME ERROR
// let mixed = [1, "two", 3.0]  // ERROR: Mixed types
```

## Dictionaries

Dictionaries are unordered collections of key-value pairs with string keys.

### Dictionary Declaration

```breadlang
// Dictionary declaration
let ages: [String: Int] = ["Alice": 25, "Bob": 30]
let config: [String: String] = ["host": "localhost", "port": "8080"]

// Empty dictionary initialization
let empty_dict: [String: Int] = [:]
```

### Dictionary Operations

```breadlang
let aliceAge: Int = ages["Alice"]

ages["Alice"] = 26        // Update existing
ages["Charlie"] = 35      // Add new pair

let count: Int = ages.length  // Number of key-value pairs
```

### Dictionary Member Access

Dot notation provides syntactic sugar for string keys:

```breadlang
let user: [String: Int] = ["age": 25, "score": 100]

print(user["age"])    // Bracket notation
print(user.age)       // Dot notation (sugar)

// This also works for setting values:
user.age = 26         // Same as user["age"] = 26
```

### Dictionary Limitations

1. **Keys must be strings**: No other key types supported
2. **Missing keys**: Accessing non-existent key causes runtime error
3. **Insertion order not preserved**: Dictionaries are unordered

```breadlang
// let intKeys: [Int: String] = [1: "one"]  // ERROR
let dict: [String: Int] = ["a": 1]
// let x = dict["missing"]  // RUNTIME ERROR
```

## Strings

Strings are immutable sequences of characters.

### String Operations

```breadlang
let greeting: String = "Hello"
let first: String = greeting[0]     // "H"
let last: String = greeting[-1]     // "o"

// String length
let len: Int = greeting.length      // 5

let full: String = "Hello" + " " + "World"
```

### String Limitations

1. **Indexing returns String**: Not a character type
2. **No string interpolation**: Must concatenate manually
3. **Immutable**: Cannot modify individual characters

```breadlang
let s: String = "Hi"
let c: String = s[0]  // Type is String, value is "H"

let name: String = "Alice"
// let msg = "Hello, \(name)"  // ERROR: No interpolation
let msg: String = "Hello, " + name  // OK

let word: String = "Hello"
// word[0] = "J"  // ERROR: Strings are immutable
```

## Iteration

### Array Iteration

```breadlang
let fruits: [String] = ["apple", "banana", "orange"]

for fruit in fruits {
    print(fruit)
}
```

### Dictionary Iteration

```breadlang
let ages: [String: Int] = ["Alice": 25, "Bob": 30]

for name in ages {
    print(name + " is " + str(ages[name]))
}
```

### String Iteration

```breadlang
let word: String = "Hello"

for char in word {
    print(char)  // Each char is a String of length 1
}
```

## Common Patterns

### Safe Access

```breadlang
// Check array bounds
let arr: [Int] = [1, 2, 3]
let index: Int = 5

if index >= 0 && index < arr.length {
    let value: Int = arr[index]
}

// Must handle potential runtime errors
```

### Building Collections

```breadlang
let numbers: [Int] = []
for i in range(10) {
    numbers.append(i * 2)
}

let squares: [String: Int] = [:]
for i in range(5) {
    squares[str(i)] = i * i
}
```