# Commands

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

## Run tests

```sh
ctest --test-dir build --output-on-failure
```

## Write and compile a BreadLang program

Create `hello.bread`:

```breadlang
print("Hello, World!")
```

Compile to a native executable:

```sh
./build/breadlang -o hello hello.bread
```

Run it:

```sh
./hello
```
