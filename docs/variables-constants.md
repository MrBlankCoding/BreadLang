# Variables & Constants

## Declaration Syntax

```breadlang
// Mutable variable (can be reassigned)
let counter: Int = 0
counter = counter + 1  // OK

// Immutable constant (cannot be reassigned)
const PI: Double = 3.14159
// PI = 3.14  // ERROR: Cannot reassign constant
```

## Declaration Rules

### Type Annotations Are Mandatory
You cannot omit the type annotation:
```breadlang
let x = 42        // ERROR: Type annotation required
let x: Int = 42   // OK
```

### Initialization Required
Variables must be initialized at declaration:
```breadlang
let x: Int        // ERROR: Must initialize
let x: Int = 0    // OK
```

### Scope Rules
Variables are block-scoped:
```breadlang
if true {
    let x: Int = 10
}
// print(x)  // ERROR: x not in scope
```

### No Shadowing in Same Scope
Cannot redeclare in same scope:
```breadlang
let x: Int = 10
// let x: Int = 20  // ERROR: Already declared
```

## Variable vs Constant

### Use `let` for Mutable Variables
```breadlang
let count: Int = 0
let items: [String] = []

count = count + 1
items.append("new item")
```

### Use `const` for Immutable Values
```breadlang
const MAX_USERS: Int = 100
const APP_NAME: String = "MyApp"
const DEFAULT_CONFIG: [String: String] = ["host": "localhost"]

// These cannot be reassigned:
// MAX_USERS = 200  // ERROR
// APP_NAME = "NewApp"  // ERROR
```

## Scope Examples

### Block Scope
```breadlang
let outer: Int = 1

if true {
    let inner: Int = 2
    print(outer)  // OK: can access outer scope
    print(inner)  // OK: in same scope
}

// print(inner)  // ERROR: inner not accessible here
```

### Function Scope
```breadlang
def example() -> Int {
    let local: Int = 42
    return local
}

// print(local)  // ERROR: local not accessible outside function
```

### Loop Scope
```breadlang
for i in range(5) {
    let temp: Int = i * 2
    print(temp)
}

// print(i)     // ERROR: loop variable not accessible
// print(temp)  // ERROR: temp not accessible
```
