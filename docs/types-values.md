# Types & Values

## Primitive Types

| Type | Description | Size | Example |
|------|-------------|------|---------|
| `Int` | Integer numbers | Platform-dependent | `42`, `-10`, `0` |
| `Double` | Double-precision float | 64-bit | `3.14`, `2.718` |
| `Float` | Single-precision float | 32-bit | `1.5` |
| `Bool` | Boolean values | 1-bit | `true`, `false` |
| `String` | Text strings | Variable | `"Hello"` |

## Type System Characteristics

- **Static typing**: All types must be known at compile time
- **Type inference**: Limited; explicit type annotations are required for declarations
- **No implicit conversions**: Must explicitly convert between types
- **Nullable types**: Use optional syntax `Type?` for nullable values

## Literal Values

### Integer Literals
```breadlang
let decimal: Int = 42
let negative: Int = -100
let zero: Int = 0
```

### Floating-Point Literals
```breadlang
// Require decimal point
let pi: Double = 3.14159
let scientific: Double = 1.5  // No scientific notation support yet
```

### String Literals
```breadlang
let simple: String = "Hello"
let newline: String = "Line 1\nLine 2"
let tab: String = "Column1\tColumn2"
let quote: String = "She said \"Hello\""
let backslash: String = "C:\\Users\\path"
```

### Boolean Literals
```breadlang
let isTrue: Bool = true
let isFalse: Bool = false
```

## Escape Sequences

| Sequence | Meaning |
|----------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\\` | Backslash |
| `\"` | Double quote |

## Optional Types

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

**Warning:** Dereferencing a `nil` optional will cause a runtime error. Always check before accessing.

## Type Checking

Use the `type()` function to check types at runtime:

```breadlang
let x: Int = 42
let t: String = type(x)  // "int"

print(type(42))          // "int"
print(type("hello"))     // "string"
print(type(true))        // "bool"
print(type([1, 2, 3]))   // "array"
print(type(["a": 1]))    // "dict"
```

## Type Conversion

Explicit conversion is required between types:

```breadlang
let i: Int = 42
let d: Double = float(i)    // Convert to Double
let s: String = str(i)      // Convert to String

// No automatic conversion
// let result = i + 3.14    // ERROR: Type mismatch
let result: Double = float(i) + 3.14  // OK
```