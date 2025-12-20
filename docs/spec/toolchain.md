# Toolchain & Development Process

## 1. Components

### Current Implementation
- **`frontend/`** — C++ lexer, parser, semantic analysis, and lowering to stack IR
- **`ir/`** — Stack-based IR utilities (builder, printer, CFG, SSA with dominators, rename, instruction materialisation)
- **`runtime/`** — SSA-aware interpreter with GC-backed arrays
- **`jit/`** — x86-64 JIT compiler infrastructure (CodeBuffer, machine code emission)
- **`cli/`** — Go CLI wrapper (`impulsec`) communicating with C++ via CGO (mirrors the C++ `impulse-cpp` tool)
- **`tests/`** — Unit tests covering lexer, parser, semantics, IR, and runtime execution
```
source ──► lexer ──► tokens ──► parser ──► AST ──► semantic checks ──► IR lowering ──► SSA optimise ──► interpreter
			│				│
				▼			▼
	   Control Flow Graph ────────────────► SSA (dominators, phi nodes, renamed values)
```

- **AST ➜ IR**: `frontend::lower_to_ir` translates well-typed syntax into the stack IR.
- **IR ➜ CFG**: `ir::build_control_flow_graph` groups the flat instruction stream into basic blocks for analysis and SSA construction.
- **IR ➜ SSA**: `ir::build_ssa` mirrors CFG layout, records dominance data, places phi nodes, and materialises SSA instructions.
- **SSA ➨ Optimiser**: `ir::optimize_ssa` is a stub for future optimization passes.
- **SSA ➨ Runtime**: the VM evaluates the SSA program directly while maintaining GC rooting.

## 2. Build & Test

```
source ──► lexer ──► tokens ──► parser ──► AST ──► semantic checks ──► IR lowering ──► SSA optimise ──► runtime
			│				│
				▼			▼
	   Control Flow Graph ────────────────► SSA (dominators, phi nodes, renamed values)
```

- **AST ➜ IR**: `frontend::lower_to_ir` translates well-typed syntax into the stack IR.
- **IR ➜ CFG**: `ir::build_control_flow_graph` groups the flat instruction stream into basic blocks for analysis, SSA construction, and testing.
- **IR ➜ SSA**: `ir::build_ssa` mirrors CFG layout, records dominator data, inserts phi nodes, and performs renaming.
- **SSA ➜ Runtime**: the VM executes the optimised SSA program directly.

Future passes (optimisation, codegen) will build on the SSA representation already in place.
- `impulse-ir` — IR library (printer, builder, interpreter, CFG)
- `impulse-runtime` — VM runtime
- `impulse-frontend` — Parser and lowering
- `impulse-tests` — Unit test suite
- `impulse-cpp` — C++ CLI (optional)

## 3. Code Style & Quality

### Standards
- **C++**: Modern C++17, Google C++ Style Guide
- **Go**: `gofmt`, standard Go conventions
- **Comments**: Document public APIs and complex logic

### Compiler Flags
- C++: `-Wall -Wextra -std=c++17`
- Go: Standard flags, `-race` for concurrent code

### Testing Requirements
- All new features must have unit tests
- Parser tests verify AST structure
- Runtime tests verify execution correctness
- Edge cases (division by zero, empty inputs) must be tested

## 4. Development Workflow

### Making Changes
1. Write failing test first (TDD)
2. Implement feature
3. Verify tests pass
4. Update documentation if public API changes
5. Run full test suite before committing

### Code Organization
- Keep files focused (single responsibility)
- Separate concerns: parsing ≠ evaluation ≠ execution
- Use namespaces consistently
- Minimize dependencies between modules

### Layer Interactions

```
source ──► lexer ──► tokens ──► parser ──► AST ──► semantic checks ──► IR lowering ──► interpreter
				│					│
				▼					▼
		   Control Flow Graph ────────────────► SSA scaffold (structure + symbols)
```

- **AST ➜ IR**: `frontend::lower_to_ir` translates well-typed syntax into the stack IR.
- **IR ➜ CFG**: `ir::build_control_flow_graph` groups the flat instruction stream into basic blocks for analysis, SSA, and testing.
- **IR ➜ SSA**: `ir::build_ssa` currently mirrors CFG layout, records dominator data, and inserts placeholder phi nodes ahead of the full SSA rename pass.
- **IR ➜ Runtime**: the interpreter executes the flattened instruction list directly.

Future passes (optimisation, codegen) will build on the SSA representation once the transformation is complete.

## 5. Current Limitations & Roadmap

### What Works Now
✅ Full expression parsing and lowering  
✅ Control flow (`if`/`else`, `while`, `for`)  
✅ Function definitions, calls, recursion  
✅ Local bindings with constant folding  
✅ Control-flow graph construction for IR functions  
✅ SSA-aware interpreter executes optimised programs  

### Next Priorities
1. Expose end-to-end I/O through the CLI/runtime boundary (replace deterministic stubs, surface errors cleanly)
2. Extend the SSA optimiser with additional passes (value numbering, loop-aware rewrites) and capture optimisation metrics
3. Improve observability: richer tracing/logging toggles, diagnostic IDs, and CLI ergonomics for inspection
4. Investigate native backend options after the expanded optimisation stack stabilises

### Future Work
- **Optimization** — Loop optimizations, inlining, register allocation
- **Modules** — Import/export system
- **Debugging** — Source maps, breakpoints
- **Pattern matching** — Exhaustiveness checks and lowering

## 6. Testing Strategy

Refer to [`docs/spec/testing.md`](testing.md) for the detailed acceptance matrix, demo programs, and inspection tooling plan.

### Current Tests
- Lexer and parser suites (grammar coverage, precedence, modules)
- Semantic validation (bindings, scopes, returns, struct/interface declarations)
- Operator behaviour (arithmetic, logic, comparison, error cases)
- Control-flow execution (`if`/`else`, `while`, `for`, `break`/`continue`)
- Function call recursion scenarios
- IR construction and CFG building
- Runtime execution through the SSA interpreter (GC + array stress coverage)
- Acceptance harness (`tests/acceptance/`) exercising the entire pipeline and diffing tokens, AST, IR, SSA, optimiser logs, and runtime traces against goldens

### Test Coverage Goals
- Parser: All grammar productions
- Evaluator: All operators and edge cases
- VM: All instructions and control flow paths
- Semantic: All error conditions

## 7. Documentation Standards

### Required Documentation
- **README.md** — Quick start, basic usage
- **STATUS.md** — Current implementation status
- **Grammar spec** — Language syntax reference
- **IR spec** — Instruction set and semantics
- **CFG spec** — Control-flow graph representation
- **Runtime spec** — VM behavior and calling convention
- **Type system** — Type rules and checking (when implemented)

### Code Documentation
- Public functions: Purpose, parameters, return value, exceptions
- Complex algorithms: High-level explanation
- Non-obvious code: Why, not what

## 8. Contributing Guidelines

### Before Starting Work
1. Check STATUS.md for current state
2. Review relevant spec docs
3. Ensure build environment is set up
4. Run existing tests to establish baseline

### While Working
- Commit frequently with clear messages
- Keep changes focused (one feature per PR)
- Update tests alongside code
- Maintain backward compatibility when possible

### Before Submitting
- All tests pass
- Code follows style guidelines
- Documentation updated
- No compiler warnings
- Manual testing of new features

## 9. Performance Considerations

### Current Performance
- Parsing: Fast enough for interactive use
- Interpretation: SSA interpreter handles regression suite comfortably
- JIT: Infrastructure ready for native code generation

### Future Optimization
- JIT compilation for hot functions
- Inline caching or specialised dispatch for common operations
- Loop-aware SSA passes (value numbering, strength reduction, LICM)
- Escape analysis and allocation sinking

## 10. Release Process (Future)

### Version Numbering
- 0.x.y for pre-1.0 releases
- x.y.z for stable releases (semantic versioning)

### Release Artifacts
- Compiler binary (`impulsec`)
- Runtime library
- Standard library modules
- Documentation
- Example programs

### Quality Bar for Releases
- All tests pass
- No known critical bugs
- Documentation up to date
- Example programs work
- Performance acceptable
