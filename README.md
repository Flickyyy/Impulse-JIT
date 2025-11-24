# Impulse JIT Compiler

# Current Focus
- Keep the SSA-based runtime and optimiser stable across the full regression suite
- Improve diagnostics, documentation, and developer tooling around the pipeline
- Grow example programs and CLI ergonomics for day-to-day experimentation

## Overview

Impulse is a compact educational compiler for a small, statically scoped language. Source code flows through the C++ lexer, parser, semantic analyser, and IR lowering. The resulting stack-based IR is lifted to SSA, optimised, and executed by a lightweight runtime. A Go-based CLI (`impulsec`) wraps the C++ core for quick experiments.

## Features

### What Works Today
- Lexer, parser, and semantic analysis for modules, functions, structs, interfaces, and structured control flow
- Lowering to stack IR, CFG reconstruction, SSA conversion, and optimiser passes (constant propagation, copy propagation, dead assignment elimination)
- Runtime VM that interprets optimised SSA with lightweight call frames, numeric values, and GC-managed arrays
- Comprehensive regression tests covering frontend, IR, SSA, runtime, and GC behaviour

### What Comes Next
- Broader semantic diagnostics and richer error recovery
- Minimal standard library helpers plus additional runtime primitives
- Extra SSA passes (value numbering, loop-aware rewrites) and performance instrumentation
- Investigation of native code generation backends once interpreter profiling stabilises

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
./build/tests/impulse-tests
```

## Usage

```bash
# Compile and run a program
./cli/impulsec --file program.imp --run

# Emit IR for inspection
./cli/impulsec --file program.imp --emit-ir

# Syntax / semantic check only
./cli/impulsec --file program.imp --check

# Evaluate constant expressions without running the VM
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
├── runtime/         # SSA interpreter + GC runtime
├── cli/             # Go CLI Interface
├── tests/           # C++ unit tests grouped by subsystem
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
- **[docs/spec/ssa.md](docs/spec/ssa.md)** - SSA representation and optimisation passes

See [STATUS.md](STATUS.md) for a detailed, regularly updated implementation snapshot.

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

Key areas for contribution:
- Enhance diagnostics and error reporting in the frontend
- Flesh out the runtime with basic I/O helpers and more heap primitives
- Extend the SSA optimiser (value numbering, loop passes, inlining)
- Experiment with native code generation once the interpreter matures

## License

[View LICENSE](LICENSE)

## Acknowledgments

Built following industry-standard compiler construction practices, drawing inspiration from LLVM, GCC, and modern language implementations.

