# Impulse Compiler Architecture

This document describes the high-level architecture and design decisions of the Impulse JIT compiler.

## Overview

Impulse is a multi-stage compiler with a clean separation of concerns:

```
Source Code (.imp)
    ↓
[Lexer] → Tokens
    ↓
[Parser] → Abstract Syntax Tree (AST)
    ↓
[Semantic Analysis] → Validated AST
    ↓
[IR Lowering] → Intermediate Representation
  ↓
[IR] → Stack Machine
    ↓
[SSA Conversion + Optimiser] → SSA
    ↓
[SSA Interpreter] → Execution / Results
```

## Components

### 1. Frontend (C++)

**Location:** `frontend/`

The frontend is responsible for transforming source code into a validated Abstract Syntax Tree.

#### Lexer (`lexer.cpp`, `lexer.h`)
- **Input:** Raw source code (string)
- **Output:** Token stream
- **Features:**
  - Keywords: `module`, `import`, `func`, `let`, `const`, `if`, `while`, etc.
  - Operators: `+`, `-`, `*`, `/`, `%`, `&&`, `||`, `!`, `==`, `!=`, `<`, `<=`, `>`, `>=`
  - Literals: integers, floats, booleans, strings
  - Identifiers and comments

#### Parser (`parser.cpp`, `parser.h`)
- **Input:** Token stream
- **Output:** Abstract Syntax Tree (AST)
- **Strategy:** Recursive descent with operator precedence parsing
- **Features:**
  - Module declarations with imports/exports
  - Function, struct, and interface declarations
  - Expression parsing with proper precedence
  - Statement parsing (if/else, while, let/const/var, return)
  - Postfix expressions (function calls)
  - Unary expressions (logical NOT, negation)

**Operator Precedence (lowest to highest):**
- `||` (5)
- `&&` (8)
- `==`, `!=` (10)
- `<`, `<=`, `>`, `>=` (15)
- `+`, `-` (20)
- `*`, `/`, `%` (30)
- Unary `!`, `-` (highest)

#### AST (`ast.h`)
- **Purpose:** Represents the structure of the program
- **Key Types:**
  - `Module`: Top-level container
  - `Declaration`: Functions, structs, interfaces, bindings
  - `Statement`: Control flow, bindings, returns
  - `Expression`: Literals, identifiers, binary/unary ops, function calls
  - `Type`: Type annotations (int, float, bool, string, custom types)

#### Semantic Analysis (`semantic.cpp`, `semantic.h`)
- **Input:** AST
- **Output:** Validated AST with diagnostics
- **Checks:**
  - Duplicate declarations
  - Const requires constant initializers
  - Division/modulo by zero (in constant expressions)
  - Import validation
  - Scope resolution

#### Expression Evaluator (`expression_eval.cpp`, `expression_eval.h`)
- **Purpose:** Compile-time constant expression evaluation
- **Features:**
  - All arithmetic, logical, and comparison operators
  - Type coercion (int to float when needed)
  - Error detection (division by zero, invalid operations)
  - Used for const initializers

### 2. IR Layer (C++)

**Location:** `ir/`

The Intermediate Representation is a stack-based instruction set designed for simple interpretation and future JIT compilation.

#### IR Builder (`builder.cpp`, `builder.h`)
- **Purpose:** Construct IR modules and functions
- **Features:** Fluent API for building IR programmatically

#### IR Instructions (`ir.h`)
- **Literal**: Push constant onto stack
- **Reference**: Push variable value onto stack
- **Store**: Pop value, store in variable
- **Binary**: Pop 2 values, compute operation, push result
- **Unary**: Pop value, compute operation, push result
- **Call**: Pop N arguments, call function, push result
- **Branch**: Unconditional jump to label
- **BranchIf**: Conditional jump (pop condition, jump if true)
- **Label**: Jump target marker
- **Return**: Pop value, return from function
- **Drop**: Remove the top of stack (expression statements)
- **Comment**: No-op for debugging

#### Lowering (`lowering.cpp`, `lowering.h`)
- **Input:** Validated AST
- **Output:** IR Module
- **Process:**
  - Each function becomes an IR function
  - Statements lower to instruction sequences
  - Expressions lower to stack operations
  - Control flow generates Branch/BranchIf/Label instructions
  - Function calls generate Call instructions with argument count

**Example Lowering:**
```impulse
func add(a: int, b: int) -> int {
    return a + b;
}
```
↓
```
func add(a: int, b: int) -> int {
  ^entry:
    reference a    # Push a onto stack
    reference b    # Push b onto stack
    binary +       # Pop b, pop a, push a+b
    return         # Pop result, return it
}
```

#### IR Printer (`printer.cpp`, `printer.h`)
- **Purpose:** Human-readable IR formatting
- **Features:** Pretty-printed output for debugging and inspection

#### IR Interpreter (`interpreter.cpp`, `interpreter.h`)
- **Purpose:** Execute IR for constant bindings
- **Features:**
  - Stack-based evaluation
  - Label resolution for jumps
  - Used during semantic analysis for constant evaluation

### 3. Runtime (C++)

**Location:** `runtime/`

The runtime rebuilds SSA per function invocation, applies optimiser passes, and interprets the optimised SSA program.

#### VM (`runtime.cpp`, `runtime.h`)
- **Purpose:** Execute SSA functions with lightweight activation records and GC integration
- **Features:**
  - Rebuilds SSA from the function’s IR and runs the optimisation pipeline to a fixed point
  - Interprets SSA instructions block-by-block, honouring phi nodes, control-flow metadata, and value versions
  - Seeds parameter and global values into the SSA value cache to mirror semantic scope rules
  - Provides direct function calls, recursion, and array primitives backed by a mark-sweep heap
  - Reports structured errors for malformed SSA (missing operands, invalid control flow, type mismatches)

### 4. CLI (Go)

**Location:** `cli/`

The Command-Line Interface provides user-friendly access to the compiler.

#### Frontend Interface (`internal/frontend/`)
- **Purpose:** CGO bridge to C++ compiler
- **Features:** Call C++ functions from Go

#### Commands (`cmd/impulsec/`)
- `--file`: Input source file
- `--emit-ir`: Output IR text
- `--check`: Syntax/semantic check only
- `--evaluate`: Evaluate constant expressions
- `--run`: Compile and execute program

#### Design Philosophy
- Keep Go layer thin
- Heavy lifting in C++
- Simple, predictable CLI

## Design Decisions

### Why Stack-Based IR?
- **Simplicity**: Easy to generate, easy to interpret
- **Portability**: Platform-independent
- **Foundation**: Can be converted to SSA or native code later

### Why C++ Frontend?
- **Performance**: Fast parsing and compilation
- **Education**: Learn C++ and compiler techniques
- **Industry standard**: Most production compilers use C++

### Why Go CLI?
- **Ergonomics**: Easy CLI development
- **Cross-compilation**: Simple to build for multiple platforms
- **CGO**: Clean C++ interop

### Why Not JIT Yet?
- **Incremental**: Build working interpreter first
- **Foundation**: JIT requires solid IR and type system
- **Future**: LLVM or custom JIT planned

### Testing Strategy

### Unit Tests (`tests/main.cpp`)
Organised into 7 groups:

1. **Lexer Tests**: Token recognition
2. **Parser Tests**: AST construction
3. **Semantic Tests**: Validation and diagnostics
4. **IR Tests**: IR generation, formatting, CFG construction
5. **Operator Tests**: Arithmetic, logic, comparison behaviour
6. **Control Flow Tests**: `if`/`else`, `while`, `for`, `break`, `continue`
7. **Function Call Tests**: Parameter passing, nested calls, recursion

### Test Philosophy
- Test at each layer independently
- End-to-end tests through full pipeline
- Edge cases and error conditions
- Clear, readable test names

## Compiler Pipeline Overview

```
source ──► lexer ──► tokens ──► parser ──► AST ──► semantic checks ──► lowered IR ──► SSA optimise ──► interpreter
      │
      ▼
     control-flow graph (analysis + SSA metadata)
```

- **AST (Abstract Syntax Tree)** captures the language structure after parsing and is the entry point to the middle end.
- **Semantic checks** annotate the AST (name binding, duplicate detection) and only pass well-formed statements to lowering.
- **Lowered IR** represents each function as a flat instruction list that feeds the CFG and SSA builders.
- The **CFG builder** re-groups that instruction list into basic blocks, discovers branch edges, and annotates dominance data for SSA.
- **SSA + Optimiser** version values, resolves phi nodes, and applies constant/copy propagation plus dead assignment elimination before execution.

Every arrow corresponds to a documented boundary in `docs/spec/`: grammar, IR, CFG, SSA. This separation makes it easy to slot in educational passes like dead code elimination, loop optimisations, or register allocation without destabilising earlier layers.

## Code Organization

```
Impulse-JIT/
├── frontend/
│   ├── include/impulse/frontend/  # Public headers
│   │   ├── ast.h                   # AST node definitions
│   │   ├── lexer.h                 # Lexer interface
│   │   ├── parser.h                # Parser interface
│   │   ├── semantic.h              # Semantic analysis
│   │   ├── lowering.h              # IR lowering
│   │   └── expression_eval.h       # Constant evaluation
│   └── src/                        # Implementation files
│
├── ir/
│   ├── include/impulse/ir/
│   │   ├── ir.h                    # IR types and structures
│   │   ├── builder.h               # IR builder API
│   │   ├── printer.h               # IR formatting
│   │   └── interpreter.h           # IR interpreter
│   └── src/                        # Implementation files
│
├── runtime/
│   ├── include/impulse/runtime/
│   │   └── runtime.h               # VM interface
│   └── src/
│       └── runtime.cpp             # SSA interpreter + GC runtime
│
├── cli/
│   ├── cmd/impulsec/               # CLI entry point
│   ├── internal/frontend/          # CGO interface
│   └── go.mod                      # Go dependencies
│
├── tests/
│   └── main.cpp                    # Unit test suite
│
└── docs/
  └── spec/                       # Language & backend specifications
```

## Future Architecture

### Planned Additions
1. **Type Checking**: Full semantic type validation and diagnostics
2. **GC**: Mark/sweep collector integrated with VM frames and heap
3. **Optimization Pipeline**: Loop optimisations, inlining, pass manager infrastructure
4. **Native Backend**: LLVM-based or custom x86-64 code generation
5. **Stdlib & Benchmarks**: Foundational runtime modules and performance suites

### Extensibility Points
- **New IR instructions**: Add to `ir.h`, implement in `interpreter.cpp` and `runtime.cpp`
- **New operators**: Add to lexer tokens, parser precedence, and evaluator
- **New statements**: Add to parser, lowering, and VM
- **New optimizations**: Implement as IR → IR passes

## Performance Considerations

### Current Focus
- **Correctness first**: Get semantics right
- **Readable code**: Clear, maintainable implementation
- **Good error messages**: Help users debug

### Future Optimization
- **JIT compilation**: Hot path native code generation
- **Deeper SSA**: Value numbering, loop optimisations, inlining, register-friendly form
- **Memory management**: Arena allocation, object pooling
- **Parallelization**: Multi-threaded compilation

## Learning Resources

This compiler is designed for learning. Key concepts demonstrated:

- **Lexical Analysis**: Regular expressions to tokens
- **Parsing**: Recursive descent, operator precedence
- **AST Construction**: Tree representation of code
- **Semantic Analysis**: Type checking, scope resolution
- **IR Generation**: Lowering high-level constructs
- **VM Design**: SSA interpreter, control-flow evaluation
- **Error Handling**: Diagnostics with source locations

## Conclusion

Impulse demonstrates a complete, working compiler pipeline from source to execution with a deliberately small runtime. The architecture remains clean, modular, and extensible, making it suitable for both educational purposes and as a foundation for more advanced features.

For implementation details, see the source code and documentation in `docs/spec/`.
