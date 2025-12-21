# BreadLang Syntax & Learning Guide

This document describes **what the current compiler/runtime in this repo supports today** (based on the parser, semantic pass, runtime operators, and tests).

## Files, whitespace, and comments

- Source files use the `.bread` extension.
- Statements are separated by newlines; `;` is also accepted in many places.
- Blocks are delimited with `{ ... }`.
- Comments:
  - Line comment: `// ...`

## Values and literals

### Nil and booleans

- `nil`
- `true`, `false`

### Numbers

- Integers: `42`
- Floating-point literals are parsed as `Double`: `3.14`

### Strings

- Double-quoted strings: `"hello"`
- Supports common escapes like `\n`, `\t`, `\"`, `\\`.

### Arrays

- Array literals:

```breadlang
let xs: [Int] = [1, 2, 3]
let empty: [Int] = []
```

Nested arrays are allowed:

```breadlang
let m: [[Int]] = [[1, 2], [3, 4]]
```

### Dictionaries

- Dictionary literals use the same brackets as arrays and are distinguished by `:` entries:

```breadlang
let d: [String: Int] = ["a": 1, "b": 2]
```

In practice, indexing and runtime helpers assume dictionary keys are strings.

## Types

Type tokens currently recognized:

- `Int`, `Bool`, `Float`, `Double`, `String`
- Arrays: `[T]` (e.g. `[Int]`, `[[Int]]`)
- Dictionaries: `[K: V]` (e.g. `[String: Int]`)
- Optionals: `T?` (e.g. `Int?`, `[String: Int]?`)

Important nuance: for arrays/dicts, the runtime representation is still “array”/“dict” — element/key/value types are primarily for the front-end and may not be fully enforced everywhere.

## Variables

### Declarations

```breadlang
let x: Int = 10
const z: Double = 3.14159
```

Notes:

- `const` declares an immutable binding.
- `let` declares a mutable binding.
- The type annotation is written as `name: Type`.

### Assignment

```breadlang
let counter: Int = 0
counter = counter + 1
```

### Index assignment

Index assignment is supported for arrays and dictionaries:

```breadlang
let arr: [Int] = [1, 2, 3]
arr[1] = 99

let dict: [String: Int] = ["a": 1]
dict["a"] = 2
```

## Expressions

### Operators

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparisons: `<`, `>`, `<=`, `>=`, `==`, `!=`
- Logic: `&&`, `||`
- Unary: `!`, unary `-`

### Indexing

```breadlang
let s: String = "Hello"
print(s[0])
print(s[-1])

let xs: [Int] = [10, 20, 30]
print(xs[1])
print(xs[-1])

let d: [String: Int] = ["a": 1]
print(d["a"])
```

Notes:

- String indexing returns a **String** of length 1.
- Negative indices are supported for strings and arrays.

### Member access

BreadLang supports member access via `.` and optional chaining via `?.`.

#### Common property: `.length`

Works on:

- `String`
- Arrays
- Dictionaries

```breadlang
print("Hello".length)
print([1, 2, 3].length)
print(["a": 1].length)
```

#### Dictionary member sugar

If `obj` is a dictionary, `obj.name` behaves like `obj["name"]`.

### Method calls

Supported method-call syntax:

```breadlang
let xs: [Int] = [1, 2]
xs.append(3)

print(123.toString())
print(true.toString())
```

Known methods implemented in the runtime operator layer:

- `append(value)` on arrays (returns `nil`)
- `toString()` on `Int`, `Bool`, `Float`, `Double`, `String`

### Optional chaining (`?.`)

- `target?.member` and `target?.method()` will yield `nil` if `target` is `nil` or an empty optional.
- Indexing also has optional-unwrapping behavior in the runtime (`optional[index]` yields `nil` if empty).

Note: the language has `T?` types, but the surface syntax for *constructing* optionals is currently minimal; optional-chaining is still useful for values returned as optionals from runtime operations.

## Ranges

BreadLang supports ranges via the `range(n)` function.

A function-style range that is used heavily in tests and examples:

```breadlang
for i in range(5) {
    print(i)
}
```

## Control flow

### If / else

```breadlang
if x > 0 {
    print(1)
} else {
    print(2)
}
```

### While

```breadlang
let i: Int = 0
while i < 3 {
    print(i)
    i = i + 1
}
```

### For-in loops

BreadLang supports iterating over an iterable expression:

```breadlang
for i in range(3) {
    print(i)
}

let matrix: [[Int]] = [[1, 2], [3, 4]]
for row in matrix {
    for val in row {
        print(val)
    }
}
```

## Functions

### Declaration

```breadlang
func add(a: Int, b: Int) -> Int {
    return a + b
}

fn square(x: Int) -> Int {
    return x * x
}
```

### Calls

```breadlang
print(add(1, 2))
```

### Default parameter values

The parser supports default parameter expressions:

```breadlang
func f(x: Int = 10) -> Int {
    return x
}
```

Whether defaults are fully honored is implementation-dependent (see hiccups document).

## Built-in functions

The runtime registers several built-in functions by name:

- `len(x) -> Int`
- `type(x) -> String`
- `str(x) -> String`
- `int(x) -> Int`
- `float(x) -> Double`

`print(expr)` is a statement form in the parser.
