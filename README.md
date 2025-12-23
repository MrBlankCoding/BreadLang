# BreadLang

BreadLang is a small programming language and compiler.

- It has a simple, readable syntax.
- It is fully statically typed.
- It uses LLVM to JIT-run programs and to compile programs to native code.

If you want the full language guide (syntax, types, examples), see the `docs/` folder.

## What is in this repo?

- `src/` and `include/`: the BreadLang compiler + runtime (written in C).
- `build/breadlang`: the compiler executable after you build.
- `bread`: a helper script that runs common commands.
- `tests/`: tests for the language/compiler.

## Build BreadLang (developer build)

### Requirements

You need:

- CMake
- Clang
- LLVM (with the C API library)

### Build

From the repo root:

```bash
make build
```

This creates:

- `./build/breadlang`

## Run a program

### JIT run (fastest)

```bash
./build/breadlang --jit hello.bread
```

Or using the helper script:

```bash
./bread run hello.bread
```

### Compile to a native executable

```bash
./build/breadlang --emit-exe -o hello hello.bread
./hello
```

Or using the helper script:

```bash
./bread compile hello.bread hello
./hello
```

### Emit LLVM IR or an object file

```bash
./build/breadlang --emit-llvm -o hello.ll hello.bread
./build/breadlang --emit-obj  -o hello.o  hello.bread
```

## Run tests

```bash
make test
```

## Make a macOS release bundle (no Homebrew/LLVM required on the target Mac)

Normal builds (`make build`) are for development and do NOT bundle LLVM.

For a release-style bundle, use one of the packaging targets below. The output is a folder you can zip and copy to another Mac.

### Package for your current Mac architecture (fast)

```bash
make package-macos
```

### Package a universal binary (Apple Silicon + Intel)

```bash
make package-macos-universal
```

Output:

- `dist/macos-universal/breadlang`
- `dist/macos-universal/lib/*.dylib`

Run it from inside that folder:

```bash
./dist/macos-universal/breadlang --jit hello.bread
```

## Help / usage

```bash
./build/breadlang --help
```

Or:

```bash
./bread help
```
