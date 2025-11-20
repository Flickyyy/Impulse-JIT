# Impulse JIT Compiler

A modern, educational compiler implementation featuring a complete frontend (C++ parser + IR) with a bytecode VM, and a Go-based CLI interface. Designed for learning compiler construction while remaining practical and extensible.

## Features

✨ **Production-Ready Features:**
- Full expression parsing with operator precedence
- All operators: arithmetic (`+`, `-`, `*`, `/`, `%`), logical (`&&`, `||`, `!`), comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`), unary (`-`, `!`)
- Control flow: `if`/`else`, `while` loops
- Function definitions with parameters and return values
- Function calls with argument passing (including nested calls)
- Local variables with lexical scoping
- Constant expression evaluation at compile-time
- Stack-based IR with jump instructions (Branch, BranchIf, Label)
- Runtime VM with complete execution support

## Quick Start

### Build from Source

```bash
# Build the C++ compiler core
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Build the Go CLI
cd cli && go build ./cmd/impulsec
```

### Run Tests

```bash
# C++ unit tests (29 tests in 8 groups)
./build/tests/impulse-tests

# Go CLI tests
cd cli && go test ./...
```

## Usage

```bash
# Compile and run a program
./cli/impulsec --file program.imp --run

# Emit IR for inspection
./cli/impulsec --file program.imp --emit-ir

# Syntax check only
./cli/impulsec --file program.imp --check

# Evaluate constant expressions
./cli/impulsec --file program.imp --evaluate
```

## Example Programs

### Control Flow
```impulse
module demo;

func factorial(n: int) -> int {
    let result: int = 1;
    let i: int = 1;
    while i <= n {
        let result: int = result * i;
        let i: int = i + 1;
    }
    return result;
}

func main() -> int {
    if factorial(5) == 120 {
        return 1;  // success
    } else {
        return 0;  // failure
    }
}
```

### Function Calls
```impulse
module math;

func add(a: int, b: int) -> int {
    return a + b;
}

func multiply(x: int, y: int) -> int {
    return x * y;
}

func main() -> int {
    return add(multiply(3, 4), multiply(2, 5));  // Returns 22
}
```

## Project Structure

```
Impulse-JIT/
├── frontend/        # C++ Parser, AST, Semantic Analysis
├── ir/              # Intermediate Representation
├── runtime/         # Stack-based Virtual Machine
├── cli/             # Go CLI Interface
├── tests/           # C++ Unit Tests (29 tests)
├── docs/spec/       # Language Specification
└── tools/           # Additional Tools
```

## Documentation

- **[README.md](README.md)** - This file
- **[STATUS.md](STATUS.md)** - Current implementation status
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Contribution guidelines
- **[docs/spec/grammar.md](docs/spec/grammar.md)** - Language grammar
- **[docs/spec/frontend.md](docs/spec/frontend.md)** - Parser implementation
- **[docs/spec/ir.md](docs/spec/ir.md)** - IR specification
- **[docs/spec/runtime.md](docs/spec/runtime.md)** - VM behavior
- **[docs/spec/toolchain.md](docs/spec/toolchain.md)** - Development workflow
- **[docs/spec/types.md](docs/spec/types.md)** - Type system design

## Implementation Status

✅ **Fully Working:**
- Lexer, Parser, Semantic Analysis
- All operators with correct precedence
- Control flow (if/else, while)
- Function calls with parameters
- IR generation and execution
- 29 comprehensive unit tests

🚧 **Planned:**
- For loops
- Type checking and inference
- Recursion support
- Standard library (print, file I/O)
- Optimization passes

See [STATUS.md](STATUS.md) for detailed status.

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

Key areas for contribution:
- Implement `for` loops
- Add type checking
- Improve error messages
- Add standard library functions
- Write more tests

## License

[View LICENSE](LICENSE)

## Acknowledgments

Built following industry-standard compiler construction practices, drawing inspiration from LLVM, GCC, and modern language implementations.

