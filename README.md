# Impulse Compiler

## Overview

Impulse is an educational compiler and virtual machine for a small, statically-typed language. The project demonstrates core compiler concepts: lexical analysis, parsing, semantic analysis, IR generation, SSA conversion, optimization passes, and runtime execution with garbage collection.

## Course Requirements

This project fulfills the following requirements:
- ✅ Basic arithmetic operations (`+`, `-`, `*`, `/`, `%`)
- ✅ Conditional operators (`if`/`else`)
- ✅ Loops (`for`, `while`)
- ✅ Recursion
- ✅ Garbage Collector (mark-sweep)
- ✅ JIT compiler (x86-64 code generation)

### Exam Benchmarks

| Task | Description | Target | Time |
|------|-------------|--------|------|
| Factorial | Recursive factorial | factorial(20) = 2432902008176640000 | <1ms |
| Sorting | Iterative Quicksort | 1000 / 10000 elements | ~60ms / ~800ms |
| Primes | Sieve of Eratosthenes | 100,000 (9592 primes) | ~180ms |
| NBody | Solar system simulation | Multiple iterations | ~680ms |

### JIT Performance

JIT provides **5-9x speedup** for numeric computations vs interpreter.

See `benchmarks/` for implementation.

## Quick Start

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run Tests

```bash
./build/tests/impulse-tests
```

### Run a Program

```bash
./build/tools/cpp-cli/impulse-cpp --file program.impulse --run
```

### Run Benchmarks

```bash
./build/tools/cpp-cli/impulse-cpp --file benchmarks/factorial.impulse --run
./build/tools/cpp-cli/impulse-cpp --file benchmarks/sorting.impulse --run
./build/tools/cpp-cli/impulse-cpp --file benchmarks/primes.impulse --run
```

## Language Features

### Types
- `int` - integers (stored as double)
- `float` - floating-point numbers
- `bool` - boolean values
- `string` - string values
- `array` - dynamic arrays (GC-managed)

### Control Flow
```impulse
module demo;

func factorial(n: int) -> int {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

func main() -> int {
    let res: int = factorial(10);
    println(res);
    return res;
}
```

### Arrays
```impulse
module demo;

func main() -> int {
    let arr: array = array(5);
    array_set(arr, 0, 42);
    return array_get(arr, 0);
}
```

## Project Structure

```
Impulse/
├── frontend/        # Lexer, Parser, AST, Semantic Analysis
├── ir/              # IR, CFG, SSA, Optimization Passes
├── jit/             # JIT Compiler (x86-64)
├── runtime/         # VM Interpreter + GC
├── tests/           # Unit & Acceptance Tests (Google Test)
├── benchmarks/      # Exam Benchmark Programs
├── tools/cpp-cli/   # Command-line Interface
└── docs/spec/       # Language Specification
```

## Architecture

```
Source (.impulse)
    ↓
[Lexer] → Tokens
    ↓
[Parser] → AST
    ↓
[Semantic Analysis] → Validated AST
    ↓
[IR Lowering] → Stack-based IR
    ↓
[CFG Builder] → Control Flow Graph
    ↓
[SSA Converter] → SSA Form
    ↓
[Optimizer] → Optimized SSA
    ↓
┌────────────────┬────────────────┐
│ [VM Interpreter]│ [JIT Compiler] │
│   (default)     │   (x86-64)     │
└────────────────┴────────────────┘
    ↓
Execution
```

### JIT Compiler (x86-64)

Features:
- CodeBuffer for machine code emission
- SSE instructions for floating-point arithmetic (addsd, subsd, mulsd, divsd)
- Full arithmetic operations (+, -, *, /, %)
- Comparison operations (<, >, ==, !=, <=, >=)
- Control flow: branch, branch_if instructions
- Enum-based opcode dispatch (~2x interpreter speedup)
- Compiled code caching for hot functions
- **5-9x speedup** for numeric-intensive code

### Runtime Features
- SSA-based interpreter
- Mark-sweep garbage collector
- Built-in functions (print, arrays, strings)
- Lightweight call frames

## CLI Usage

```bash
# Run program
./build/tools/cpp-cli/impulse-cpp --file program.impulse --run

# Dump intermediate representations
./build/tools/cpp-cli/impulse-cpp --file program.impulse \
    --dump-tokens tokens.txt \
    --dump-ast ast.txt \
    --dump-ir ir.txt \
    --dump-cfg cfg.txt \
    --dump-ssa ssa.txt
```

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Detailed architecture
- [STATUS.md](STATUS.md) - Implementation status
- [docs/spec/grammar.md](docs/spec/grammar.md) - Language grammar
- [docs/spec/ir.md](docs/spec/ir.md) - IR specification
- [docs/spec/runtime.md](docs/spec/runtime.md) - VM behavior

## License

[View LICENSE](LICENSE)

