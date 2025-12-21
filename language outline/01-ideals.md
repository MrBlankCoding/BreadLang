# BreadLang Ideals (Technical Vision)

## What BreadLang is trying to be

BreadLang is a **statically typed**, **LLVM-first** language implemented in C, where the default execution model is:

- Parse source into an AST
- Perform semantic checks / basic type tagging
- Generate LLVM IR
- JIT compile via LLVM
- Execute native code (no AST interpreter in the hot path)

The language’s “center of gravity” is therefore the compiler + runtime boundary, not an interpreter.

## Core pillars

### 1) LLVM as the execution engine
BreadLang aims to treat LLVM as the authoritative backend:

- The language should have a single semantics that is valid under LLVM codegen.
- Any non-LLVM execution path (debug interpreter / constant folding evaluator) should be strictly aligned with the LLVM semantics.
- “Fast enough by default”: the implementation choice of LLVM JIT is meant to make BreadLang feel like a compiled language even during interactive/dev runs.

### 2) Integrated compilation pipeline (NEW)
BreadLang uses a unified approach where semantic analysis and code generation happen together:

- **Single-phase compilation**: Parse → LLVM Codegen (with integrated semantic analysis) → Execute
- **Consistent symbol management**: One symbol table shared between analysis and codegen
- **Immediate error detection**: Semantic errors are caught at the point of code generation
- **No phase drift**: Analysis and codegen cannot become inconsistent since they're the same phase

### 3) A small, explicit type system
BreadLang’s types are intended to be:

- **Simple to read/write** (Swift-ish / TypeScript-ish surface syntax)
- **Easy to compile** (straightforward lowering to runtime `BreadValue` operations)
- **Strong enough to prevent common mistakes** (e.g. mismatched arithmetic, misuse of collection keys)

Current type tokens in the language include:

- `Int`, `Bool`, `Float`, `Double`, `String`
- Arrays: `[T]` (including nested arrays like `[[Int]]`)
- Dictionaries: `[K: V]` (currently practical `K` is `String`)
- Optional: `T?`

### 3) “Runtime values” as the ABI
A unifying architectural constraint is that all values are represented in the runtime as a `BreadValue` (tag + payload). The compiler then lowers high-level constructs into calls such as:

- `bread_binary_op` for arithmetic/comparisons
- `bread_index_op` / `bread_index_set_op` for indexing and index assignment
- `bread_member_op` / `bread_method_call_op` for property + method surface syntax
- `bread_print` for output

This design lets you evolve language features without having to re-encode the entire ABI; it also centralizes tricky behaviors (truthiness, optional unwrapping, indexing rules) in one place.

### 5) Modern-but-minimal surface syntax
BreadLang’s surface syntax aims to feel modern, while remaining easy to parse:

- `let` / `const` declarations with type annotations
- C/Swift-style block braces `{ ... }`
- `func name(params) -> ReturnType { ... }`
- `if` / `else`, `while`, and `for x in iterable { ... }`

The language intentionally avoids “too much magic”; the goal is to keep grammar and lowering predictable.

### 6) A test-driven language design loop
BreadLang’s language design should be driven by:

- Backend-focused tests (`tests/llvm_backend/*.bread`)
- Property tests for runtime invariants (`tests/property/*`)

This keeps feature work honest: the syntax guide should match what the compiler accepts, and “ideals” should be regularly reconciled with the implementation.

## Long-term direction (practical goals)

- **Semantic consistency**: interpreter/debug evaluation, semantic analysis, and LLVM codegen should agree.
- **Better type-checking**: more precise tagging of expression types; fewer “everything is double” fallbacks.
- **Ergonomic standard library**: built-ins and methods that are compiled efficiently (not placeholders).
- **Quality errors**: precise file/line/column errors across parser, semantic analysis, and runtime.

