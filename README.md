# Impulse JIT Compiler

# Main goal (Task)
- Static single assignment construction with dominance/phi insertion
 - executed apps must perform correctly
 - any technology is allowed, no restriction
- SSA verification and optimization passes (const propagation, dead code elimination, copy propagation)
  - Factorial calculation
  - Array sorting
  - Prime number generation

## Performance:
- Factorial of 20
- Sort array of 10000 elements
# C++ unit tests (38 tests in 9 groups)

## Marks:
- 4 – demonstrate benchmark execution
- 5 – develop benchmark (Task 4) in 2 hours timeframe, using your language and successfully execute it

### Task 1: Factorial Calculation (Recursive Function)

*Objective:* Implement a function that calculates the factorial of a given number using recursion.

*Purpose:* Test recursive function calls, stack management, and handling of large numbers.  

*Purpose:* Test array handling, loop operations, and element comparison.  
*Objective:* Implement an algorithm to generate prime numbers (e.g., Sieve of Eratosthenes).

*Purpose:* Test array manipulation, loops, and arithmetic operations.  
*Benchmark:* Measure the time it takes to generate all prime numbers up to 100,000.

## Overview

A modern, educational compiler implementation featuring a complete frontend (C++ parser + IR) with a bytecode VM, and a Go-based CLI interface. Designed for learning compiler construction while remaining practical and extensible.

## Features

✨ **Production-Ready Features:**
- Full expression parsing with operator precedence
- All operators: arithmetic (`+`, `-`, `*`, `/`, `%`), logical (`&&`, `||`, `!`), comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`), unary (`-`, `!`)
- Control flow: `if`/`else`, `while` loops
- `for` loops with initializer / condition / increment sections
- Function definitions with parameters and return values
- Function calls with argument passing (including nested calls)
- Recursive execution support (factorial, mutual recursion)
- Local variables with lexical scoping
- Constant expression evaluation at compile-time
- Stack-based IR with jump instructions (Branch, BranchIf, Label)
- Control-flow graph extraction for IR functions
- Static single assignment construction with dominance/phi insertion
- Runtime VM with complete execution support

🚀 **Active Roadmap:**
- SSA verification and optimization passes (const propagation, dead code elimination, copy propagation)
- Loop control statements (`break` / `continue`)
- Type checking and inference layer
- Garbage collector and improved runtime memory model
- JIT backend with native code generation

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
# C++ unit tests (38 tests in 9 groups)
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
├── tests/           # C++ Unit Tests (38 tests)
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
- **[docs/spec/cfg.md](docs/spec/cfg.md)** - CFG construction details
- **[docs/spec/runtime.md](docs/spec/runtime.md)** - VM behavior
- **[docs/spec/toolchain.md](docs/spec/toolchain.md)** - Development workflow
- **[docs/spec/types.md](docs/spec/types.md)** - Type system design
- **[docs/spec/ssa.md](docs/spec/ssa.md)** - SSA architecture and plan

## Implementation Status

✅ **Fully Working:**
- Lexer, Parser, Semantic Analysis
- All operators with correct precedence
✅ Control flow (if/else, while, for)
✅ Function calls with parameters (including recursion)
✅ IR generation and execution
✅ SSA construction with phi placement
✅ 35 comprehensive unit tests

🚧 **Planned:**
- SSA-driven optimization passes
- Loop controls (`break` / `continue`)
- Type checking and inference
- Standard library (print, file I/O)
- Garbage collector and runtime services
- JIT compilation pipeline

See [STATUS.md](STATUS.md) for detailed status.

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

Key areas for contribution:
- Implement SSA-based optimization passes
- Add type checking
- Improve error messages
- Add standard library functions
- Expand runtime and SSA test coverage

## License

[View LICENSE](LICENSE)

## Acknowledgments

Built following industry-standard compiler construction practices, drawing inspiration from LLVM, GCC, and modern language implementations.

