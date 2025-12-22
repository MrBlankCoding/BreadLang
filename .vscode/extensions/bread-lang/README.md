# Bread Language Support for VS Code

This extension provides language support for the BreadLang programming language, including syntax highlighting, autocomplete, and other helpful features.

## Features

- **Syntax Highlighting**: Full syntax highlighting for Bread language constructs
- **Auto-completion**: Intelligent suggestions for keywords, types, and built-in functions
- **Hover Information**: Helpful tooltips when hovering over language elements
- **Signature Help**: Function parameter hints while typing
- **Code Snippets**: Quick templates for common patterns
- **Bracket Matching**: Automatic bracket pairing and indentation
- **Build Integration**: One-click build and run buttons in the editor toolbar

## Language Features Supported

### Keywords
- `let`, `const` - Variable declarations
- `func`, `fn` - Function declarations
- `if`, `else` - Conditional statements
- `while`, `for`, `in` - Loops
- `return`, `break`, `continue` - Control flow
- `true`, `false`, `nil` - Literals

### Types
- `Int`, `Bool`, `Float`, `Double`, `String` - Primitive types
- `[Type]` - Arrays
- `[KeyType: ValueType]` - Dictionaries
- `Type?` - Optional types

### Built-in Functions
- `print(value)` - Print to console
- `len(collection)` - Get length
- `type(value)` - Get type name
- `str(value)` - Convert to string
- `int(value)` - Convert to integer
- `float(value)` - Convert to float
- `range(count)` - Create iteration range

### Methods and Properties
- `.length` - Length property for collections
- `.append(value)` - Add to arrays
- `.toString()` - Convert to string

## Build Integration

When editing a `.bread` file, you'll see build buttons in the top-right corner of the editor:

- **🔨 Build** - Compile the current Bread file
- **▶️ Build & Run** - Compile and execute the current Bread file

The extension intelligently detects your build system:
1. **CMake** - If `CMakeLists.txt` exists, uses `cmake` and `make`
2. **Makefile** - If `Makefile` exists, uses `make`
3. **Direct** - Attempts to use `breadlang` compiler directly

Build output appears in both the terminal and a dedicated "Bread Build" output channel.

## Installation

1. Copy this extension folder to your VS Code extensions directory
2. Restart VS Code
3. Open any `.bread` file to activate the extension

## Usage

Create a new file with the `.bread` extension and start coding! The extension will automatically provide syntax highlighting and language features.

When editing a `.bread` file, you'll see two build buttons in the top-right corner:
- **🔧 Build Bread File** - Compile the current file
- **▶️ Build & Run Bread File** - Compile and execute the current file

### Example Bread Code

```bread
// Simple Bread program
let message: String = "Hello, Bread!"
let x: Int = 42

print(message)
print(x)

for i in range(3) {
    print(i)
}
```

## Development

To modify this extension:

1. Install dependencies: `npm install`
2. Compile TypeScript: `npm run compile`
3. Press F5 in VS Code to launch a new Extension Development Host window

## Contributing

This extension is part of the BreadLang project. Contributions are welcome!