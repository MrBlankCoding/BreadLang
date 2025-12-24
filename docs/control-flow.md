# Control Flow

## If / Else If / Else

### Basic If Statement

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

### If Statement Rules

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

### Nested If Statements

```breadlang
let age: Int = 25
let hasLicense: Bool = true

if age >= 18 {
    if hasLicense {
        print("Can drive")
    } else {
        print("Need license")
    }
} else {
    print("Too young to drive")
}
```

## While Loops

### Basic While Loop

```breadlang
let countdown: Int = 5
while countdown > 0 {
    print(countdown)
    countdown = countdown - 1
}
print("Liftoff!")
```

### While Loop Characteristics

- Condition evaluated before each iteration
- Infinite loops possible if condition never becomes false
- No `do-while` variant available

```breadlang
while true {
    print("Forever")
}

// Controlled version
let running: Bool = true
let counter: Int = 0

while running {
    counter = counter + 1
    if counter >= 10 {
        running = false
    }
}
```

## For-In Loops

### Range Iteration

```breadlang
for i in range(5) {
    print(i)  // Prints 0, 1, 2, 3, 4
}

// Range with start and end
for i in range(1, 6) {
    print(i)  // Prints 1, 2, 3, 4, 5
}
```

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

### Nested Loops

```breadlang
let matrix: [[Int]] = [[1, 2], [3, 4], [5, 6]]
for row in matrix {
    for value in row {
        print(value)
    }
}
```

## Loop Limitations

### Loop Variable Immutability

```breadlang
for i in range(10) {
    // ERROR: Cannot reassign loop variable
    // i = i + 1
    
    // OK: Use loop variable as-is
    print(i)
}
```

### Dictionary Iteration Details

```breadlang
let dict: [String: Int] = ["a": 1, "b": 2]
for key in dict {
    // 'key' is the dictionary key (String)
    let value: Int = dict[key]  // Must access value separately
    print(key + ": " + str(value))
}
```

### Range Exclusivity

```breadlang
// range(5) gives 0, 1, 2, 3, 4 (not including 5)
for i in range(5) {
    print(i)  // 0 through 4
}

// range(1, 6) gives 1, 2, 3, 4, 5 (not including 6)
for i in range(1, 6) {
    print(i)  // 1 through 5
}
```

## Control Flow Patterns

### Early Exit Pattern

```breadlang
def findValue(arr: [Int], target: Int) -> Bool {
    for value in arr {
        if value == target {
            return true  // Early exit from function
        }
    }
    return false
}
```

### Conditional Processing

```breadlang
let numbers: [Int] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

for num in numbers {
    if num % 2 == 0 {
        print(str(num) + " is even")
    } else {
        print(str(num) + " is odd")
    }
}
```

### Accumulation Pattern

```breadlang
let numbers: [Int] = [1, 2, 3, 4, 5]
let sum: Int = 0

for num in numbers {
    sum = sum + num
}

print("Sum: " + str(sum))
```

## Common Pitfalls

### Boolean Conditions

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

### Missing Return Paths

```breadlang
// ERROR: Missing return in some code path
// def checkValue(x: Int) -> String {
//     if x > 0 {
//         return "positive"
//     }
//     // Missing return for x <= 0
// }

// OK: All paths return
def checkValue(x: Int) -> String {
    if x > 0 {
        return "positive"
    } else {
        return "non-positive"
    }
}
```