# Toolchain & Development Process

## 1. Components

### Current Implementation
- **`frontend/`** — C++ lexer, parser, semantic analysis, and lowering to IR
- **`ir/`** — Stack-based IR with interpreter for constant evaluation and function execution
- **`runtime/`** — VM executing IR instructions with control flow support
- **`cli/`** — Go CLI wrapper (`impulsec`) communicating with C++ via CGO
- **`tests/`** — Unit tests covering parser, IR, and runtime

### Future Components
- **`tools/`** — Scripts for code generation, benchmarks, profiling
- **`stdlib/`** — Standard library modules (print, math, collections, io)

## 2. Build System

### Current Setup
```bash
# Build C++ components
cmake -S . -B build
cmake --build build

# Build CLI
cd cli && go build ./cmd/impulsec

# Run tests
cd build && ctest
cd ../cli && go test ./...
```

### Targets
- `impulse-ir` — IR library (printer, builder, interpreter)
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
source ──► lexer ──► tokens ──► parser ──► AST ──► semantic checks ──► IR lowering
								│
								▼
							Control Flow Graph
								│
								▼
						   Static Single Assignment
								│
								▼
			     Future: Data-Flow Graph, liveness, register allocation, codegen
```

- **AST ➜ IR**: `frontend::lower_to_ir` translates well-typed syntax into stack-based IR.
- **IR ➜ CFG**: `ir::build_control_flow_graph` groups the flat instruction stream into basic blocks so we know which edges exist.
- **CFG ➜ SSA**: `ir::build_ssa` relies on the CFG topology to insert `phi` nodes and mint versioned variables.
- **SSA ➜ Analyses**: optimisations (dead code elimination, common subexpression elimination, loop transforms), liveness, register allocation, and future code generators all build on top of SSA (and any derived DFG).

Documenting these boundaries keeps the repo approachable and makes it clear how a new pass should plug in.

## 5. Current Limitations & Roadmap

### What Works Now
✅ Full expression parsing and evaluation  
✅ Control flow (if/else, while)  
✅ Function definitions and calls  
✅ Local variables  
✅ All operators  
✅ For loops with initializer / condition / increment sections  
✅ Control-flow graph construction for IR functions  
✅ Static single assignment construction and validation (dominance, phi insertion, renaming, verifier)  

### Next Priorities
1. **SSA optimizations** — Constant propagation, dead assignment elimination
2. **SSA round-trip tooling** — IR ↔ SSA conversions for diagnostics
3. **Type checking** — Semantic verification beyond syntax
4. **Standard library** — Built-in print, file I/O
5. **Loop control** — `break`/`continue` statements and loop scoping

### Future Work
- **GC** — Memory management for heap-allocated objects
- **JIT** — Native code generation for hot paths
- **Optimization** — Dead code elimination, constant folding
- **Modules** — Import/export system
- **Debugging** — Source maps, breakpoints

## 6. Testing Strategy

### Current Tests (38 total)
- Module header parsing
- Numeric literals and strings
- Expression parsing and precedence
- Semantic validation (duplicates, const rules)
- Operator functionality (arithmetic, logical, comparison)
- Control flow execution (if/else, while)
- For loop execution (initializer / condition / increment)
- Control-flow graph construction
- SSA renaming, phi validation, and verifier coverage
- Constant evaluation
- Function call execution
- Expression statements with value dropping
- Recursive execution paths

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
- Interpretation: Suitable for scripts and testing
- No optimization yet

### Future Optimization
- JIT compilation for hot functions
- Inline caching for common operations
- Constant folding and propagation
- Dead code elimination

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
