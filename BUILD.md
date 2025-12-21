# Building BreadLang

BreadLang is designed to compile to native binaries using LLVM and Clang. This document describes the build process and requirements.

## Requirements

### Compiler
- **Clang**: BreadLang requires Clang as the C compiler (GCC is not supported)
- Minimum version: Clang 10.0 or later
- C11 standard support required

### LLVM
- **LLVM Development Libraries**: Required for JIT compilation
- Minimum version: LLVM 14.0 or later
- `llvm-config` must be in your PATH

### Platform Support
- **macOS**: Fully supported (primary development platform)
- **Linux**: Fully supported
- **Windows**: Not supported (use WSL2 with Linux environment)

## Installation of Dependencies

### macOS
```bash
brew install llvm
# Add LLVM to PATH if needed
export PATH="/usr/local/opt/llvm/bin:$PATH"
```

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install clang llvm-dev cmake
```

### Arch Linux
```bash
sudo pacman -S clang llvm cmake
```

## Build Methods

### Method 1: Quick Build with build.sh

The simplest way to build BreadLang:

```bash
./build.sh
```

This creates the `breadlang` executable in the project root directory.

### Method 2: CMake Build (Recommended for Development)

CMake provides better IDE integration and build management:

```bash
# Create build directory
mkdir build
cd build

# Configure
cmake ..

# Build
make

# Optional: Install system-wide
sudo make install
```

The executable will be at `build/breadlang`.

#### CMake Build Types

```bash
# Debug build (with symbols, no optimization)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Building Standalone Executables

BreadLang can compile your programs to standalone native executables:

```bash
# Compile a BreadLang program to executable
./breadlang --emit-exe -o myprogram program.bread

# Run the standalone executable
./myprogram
```

The generated executable includes:
- Compiled native machine code (via LLVM)
- BreadLang runtime library
- No external dependencies (except system libraries)

## Intermediate Outputs

### LLVM IR
```bash
./breadlang --emit-llvm -o program.ll program.bread
```

### Object File
```bash
./breadlang --emit-obj -o program.o program.bread
```

## Testing

### Run All Tests
```bash
./scripts/tests.sh
```

### Run Specific Test Categories
```bash
# Integration tests
./scripts/tests.sh -t integration

# LLVM backend tests
./scripts/tests.sh -t llvm_backend

# Property-based tests
cd tests/property
make test
```

### Using CMake for Testing
```bash
cd build
make test-all          # All tests
make test-integration  # Integration tests
make test-llvm         # LLVM backend tests
make test-property     # Property-based tests
```

## Troubleshooting

### LLVM Not Found
If CMake or build.sh cannot find LLVM:

```bash
# Check if llvm-config is in PATH
which llvm-config

# If not, add LLVM to PATH (macOS example)
export PATH="/usr/local/opt/llvm/bin:$PATH"

# Or specify LLVM location to CMake
cmake -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm ..
```

### Clang Not Found
```bash
# Install clang
# macOS
brew install llvm

# Ubuntu
sudo apt install clang

# Verify installation
clang --version
```

### Build Errors
1. Ensure you have the latest LLVM development libraries
2. Check that clang supports C11
3. Verify all source files are present
4. Try a clean build:
   ```bash
   rm -rf build
   mkdir build
   cd build
   cmake ..
   make
   ```

## Cross-Compilation

BreadLang executables are platform-specific. To create executables for different platforms:

1. Build BreadLang on the target platform
2. Use the native `breadlang` compiler to create executables
3. The resulting binaries will be optimized for that platform

## Performance Optimization

For maximum performance in generated executables:

```bash
# Use release build
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Generate optimized executable
./breadlang --emit-exe -o myprogram program.bread
```

LLVM automatically applies optimizations including:
- Dead code elimination
- Constant folding
- Inline expansion
- Loop optimizations
- Register allocation

## Development Workflow

Recommended workflow for BreadLang development:

1. Use CMake for building during development
2. Enable debug symbols: `cmake -DCMAKE_BUILD_TYPE=Debug ..`
3. Run tests frequently: `make test-all`
4. Use `--dump-ast` to debug parsing issues
5. Use `--emit-llvm` to inspect generated IR

## Clean Build

```bash
# Clean CMake build
rm -rf build

# Clean build.sh artifacts
rm -f breadlang
rm -rf *.dSYM

# Clean test artifacts
cd tests/property
make clean
```