# Getting Started

## Installation

```bash
git clone <repository-url>
cd breadlang

make build

# Or build with CMake directly
mkdir build && cd build
cmake ..
make
```

## Your First Program

Create a file `hello.bread`:

```breadlang
print("Hello, World!")
```

## Running Programs

### Quick Execution (JIT)

```bash
# Using convenience script (recommended)
./bread run hello.bread

# Using Makefile
make run FILE=hello.bread

# Or using the binary directly
./build/breadlang --jit hello.bread
```

### Compile to Executable

```bash
# Using convenience script (recommended)
./bread compile hello.bread hello
./hello

# Using Makefile
make compile-exe FILE=hello.bread OUT=hello
./hello

# Or using the binary directly
./build/breadlang --emit-exe -o hello hello.bread
./hello
```

## Convenience Script

The `bread` script provides a simple interface to common operations:

```bash
./bread run <file>              # JIT execution
./bread compile <file> [output] # Compile to executable
./bread llvm <file> [output]    # Emit LLVM IR
./bread build                   # Build compiler
./bread help                    # Show help
```

## Makefile Targets

### Execution Targets
```bash
make run FILE=program.bread          # JIT execution
make jit FILE=program.bread          # JIT execution (alias)
```

### Compilation Targets
```bash
make compile-exe FILE=program.bread [OUT=output]     # Create executable
make compile-llvm FILE=program.bread [OUT=output.ll] # Emit LLVM IR
make compile-obj FILE=program.bread [OUT=output.o]   # Emit object file
```

### Build Targets
```bash
make build                           # Build the compiler
make clean                           # Clean build artifacts
make rebuild                         # Clean and rebuild
make test                            # Run tests
```