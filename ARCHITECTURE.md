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
[VM / Interpreter] → Execution / Results
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

The runtime Virtual Machine executes IR code.

#### VM (`runtime.cpp`, `runtime.h`)
- **Purpose:** Execute IR functions
- **Features:**
  - Stack-based architecture
  - Local variable storage
  - Function call support with parameter passing
  - Control flow with program counter (PC)
  - Label resolution for jumps
  - Error handling and diagnostics

**Execution Model:**
1. Load IR module
2. Build label map (label name → instruction index)
3. Initialize locals from parameters
4. Execute instructions sequentially
5. Update PC for branches
6. Return final stack value

**Stack Machine Example:**
```
Instructions:           Stack State:
literal 5               [5]
literal 3               [5, 3]
binary +                [8]
return                  [] (returns 8)
```

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

## Testing Strategy

### Unit Tests (`tests/main.cpp`)
Organized into 9 groups (38 tests total):

1. **Lexer Tests** (3): Token recognition
2. **Parser Tests** (2): AST construction
3. **Semantic Tests** (9): Validation and diagnostics
4. **IR Tests** (8): IR generation, formatting, and CFG construction
5. **SSA Tests** (6): Dominators, phi placement, verifier success and failure paths
6. **Operator Tests** (6): All operators with precedence
7. **Control Flow Tests** (2): if/else, while, for-loop execution
8. **Runtime Tests** (3): VM execution, locals, expression statements
9. **Function Call Tests** (2): Parameter passing, nested calls, recursion

### Test Philosophy
- Test at each layer independently
- End-to-end tests through full pipeline
- Edge cases and error conditions
- Clear, readable test names

## Compiler Pipeline Overview

```
source ──► lexer ──► tokens ──► parser ──► AST ──► semantic checks ──► lowered IR
                │
                ▼
              control-flow graph (CFG)
                │
                ▼
               static single assignment (SSA)
                │
                ▼
              future: data-flow graph, liveness, regalloc, codegen
```

- **AST (Abstract Syntax Tree)** captures the language structure after parsing and is the entry point to the middle end.
- **Semantic checks** annotate the AST (name binding, duplicate detection) and only pass well-formed statements to lowering.
- **Lowered IR** represents each function as a flat instruction list that the VM can interpret immediately.
- The **CFG builder** re-groups that instruction list into basic blocks, discovers branch edges, and becomes the control substrate for later analyses.
- The **SSA builder** reads both the CFG and the original instructions to produce versioned values (e.g. `x.2`), enabling value-tracking analyses and optimisations.
- A future **DFG (data-flow graph)** can be derived from SSA when we need explicit producer/consumer relationships (common subexpression elimination, instruction scheduling).
- **Liveness analysis & register allocation** will consume SSA/DFG data once we target real hardware registers instead of the stack VM.
- **Code generation** will eventually turn the optimised representation into target-specific output (native code, optimised bytecode, etc.).

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
│       └── runtime.cpp             # VM implementation
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
    └── spec/                       # Language specification
```

## Future Architecture

### Planned Additions
1. **Type Checking**: Full semantic type validation
2. **Optimization**: Constant folding, dead code elimination
3. **SSA Form**: Static Single Assignment for advanced optimization
4. **JIT**: LLVM backend or custom x86-64 code generation
5. **GC**: Garbage collector for heap-allocated objects
6. **Stdlib**: Standard library (I/O, collections, math)

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
- **Better IR**: SSA for optimization
- **Memory management**: Arena allocation, object pooling
- **Parallelization**: Multi-threaded compilation

## Learning Resources

This compiler is designed for learning. Key concepts demonstrated:

- **Lexical Analysis**: Regular expressions to tokens
- **Parsing**: Recursive descent, operator precedence
- **AST Construction**: Tree representation of code
- **Semantic Analysis**: Type checking, scope resolution
- **IR Generation**: Lowering high-level constructs
- **VM Design**: Stack machines, instruction execution
- **Error Handling**: Diagnostics with source locations

## Conclusion

Impulse demonstrates a complete, working compiler pipeline from source to execution. The architecture is clean, modular, and extensible, making it suitable for both educational purposes and as a foundation for more advanced features.

For implementation details, see the source code and documentation in `docs/spec/`.
