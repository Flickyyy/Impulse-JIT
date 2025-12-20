# Testing Strategy & Acceptance Harness

Impulse uses layered validation that mirrors the compiler pipeline so regressions surface with precise context.

## Unit & Subsystem Tests

- **Lexer / Parser** – Grammar coverage, precedence, module declarations.
- **Semantic Analysis** – Bindings, control-flow rules, struct/interface validation.
- **IR & Optimiser** – Instruction sequencing, CFG reconstruction, SSA conversion, fixed-point passes.
- **Runtime** – SSA interpreter behaviour, call stacks, GC rooting, array primitives.

These suites live under `tests/*.cpp` and execute through the `impulse-tests` binary.

## Acceptance Harness (Phase 1)

A reusable harness now ships in `tests/acceptance/`. It runs each scenario end-to-end:

1. Tokenise `program.impulse` and dump the token stream.
2. Parse + dump the AST.
3. Run semantic analysis and lower to IR.
4. Emit IR, CFG, SSA (pre/post optimisation), and optimiser logs via shared dump utilities.
5. Execute the runtime with SSA tracing enabled.
6. Compare every artefact against golden files.

### Directory Layout

```
tests/acceptance/
├── harness.h / harness.cpp        # Pipeline driver + diff helpers
└── cases/
    └── <case-name>/
        ├── program.impulse
        ├── expected.tokens.txt
        ├── expected.ast.txt
        ├── expected.ir.txt
        ├── expected.cfg.txt
        ├── expected.ssa.txt
        ├── expected.optimisation.txt
        ├── expected.runtime-trace.txt
        ├── expected.runtime.txt
        ├── expected.diagnostics.txt
        └── stdin.txt (optional deterministic stdin payload)
```

Only directories containing `program.impulse` are treated as active acceptance cases. Missing expectation files are reported as actionable failures so goldens stay intentional.

Current canonical scenarios include:

- `arrays` – push/pop/join stack operations and formatting
- `assignment` – mutable variable reassignment with `x = expr;`
- `control_flow` – structured loops and branches
- `diagnostics` – semantic regression snapshot for error messaging
- `diagnostics_builtins` – semantic checks for builtin misuse (arity, type errors)
- `exam_factorial` – exam benchmark: factorial(20) = 2432902008176640000
- `exam_primes` – exam benchmark: count primes up to 1000 = 168
- `exam_sorting` – exam benchmark: quicksort 100 elements
- `runtime_demo` – recursion, arrays, and string utilities in the VM
- `runtime_io` – stdin plumbing through the CLI/runtime seam with deterministic fixtures
- `strings` – banner formatting with repeat/slice/trim/case helpers
- `strings_repeat_error` – runtime regression for invalid `string_repeat` counts
- `strings_slice_error` – runtime guardrail when slicing beyond bounds

### Regenerating Goldens

1. Build the tooling: `cmake --build build`.
2. Run the C++ CLI with the relevant dump flags, e.g.
   ```bash
   ./build/tools/cpp-cli/impulse-cpp \
     --file tests/acceptance/cases/control_flow/program.impulse \
     --dump-tokens tokens.tmp \
     --dump-ast ast.tmp \
     --dump-ir ir.tmp \
     --dump-cfg cfg.tmp \
     --dump-ssa ssa.tmp \
     --dump-optimisation-log opt.tmp \
     --trace-runtime runtime.tmp --run
   ```
3. Move artefacts into the case directory, renaming to the `expected.*.txt` scheme.
4. Capture diagnostics (if any) in `expected.diagnostics.txt` (leave empty for a clean run).

The CLI streams traces to stdout when no path is provided, making it easy to paste into the golden file.

### Failure Reporting

When a comparison fails the harness stops immediately, printing the label and the first differing line so the delta is obvious:

```
Acceptance case 'control_flow' failed:
  - runtime trace: line 42 differs
      expected: enter block 3
        actual: enter block 4
```

## Running the Suite

```bash
# Run unit + acceptance suites (reports per test group)
./build/tests/impulse-tests

# Or via CTest
ctest --test-dir build -R impulse-tests
```

## Inspection Tooling Recap

The CLI (and harness) rely on shared dump helpers exposed through the following flags:

- `--dump-tokens[=path]`
- `--dump-ast[=path]`
- `--dump-ir[=path]`
- `--dump-cfg[=path]`
- `--dump-ssa[=path]`
- `--dump-optimisation-log[=path]`
- `--trace-runtime[=path]` (implies `--run`)

Without an explicit path, output is written to stdout; otherwise the artefact is written to the specified file.

## Forward Plan

- **Phase 2 (✅ done)** – Canonical acceptance scenarios now cover control flow, optimiser behaviour, runtime recursion/arrays, and negative diagnostic baselines. The README documents a walkthrough for running the suite and refreshing goldens.
- **Phase 3 (✅ done)** – Runtime/core helpers (strings, arrays, stubbed I/O) ship with unit coverage and refreshed acceptance cases (`strings`, `arrays`, `runtime_io`, error baselines).
- **Phase 4 (✅ done)** – Diagnostic regression suite (`diagnostics`, `diagnostics_builtins`, targeted runtime failures) captures messaging for semantic and runtime guardrails.
- **Phase 5 (in progress)** – Structured diagnostics (error codes, hints) and optimisation planning (value numbering / loop awareness) building on the new CLI/runtime I/O plumbing.
