# Static Single Assignment (SSA) Plan

This document describes the roadmap for adding SSA form to the Impulse compiler. It defines the transformation pipeline, data structures, algorithms, and testing strategy required to build, validate, and consume SSA. The intent is to execute the work in milestones so the system never regresses while we expand capabilities.

## Goals

- Produce SSA form for every lowered function using the existing IR and CFG.
- Enable SSA-based optimizations (constant propagation, dead code elimination, copy propagation).
- Preserve existing interpreter execution while providing a path to SSA-aware backends (future JIT, optimizer).
- Provide clear extension points for dominance queries, data-flow analysis, and later register allocation.

## Milestones

- [x] **Infrastructure**
   - Extend IR module with SSA containers (blocks, values, phi nodes).
   - Introduce symbol/version tables to track assignments inside basic blocks.
   - Record dominance information (tree and frontiers) on top of the CFG builder.

- [x] **Transformation Pass**
   - Compute dominators and immediate dominators for every block.
   - Compute dominance frontiers to place phi nodes per variable.
   - Rename variables so each definition is unique, producing SSA form.
   - Materialize phi instructions in block headers with operand references.

- [ ] **Validation & Round-Trip**
   - [x] Add verification to ensure SSA invariants (single def per name, phi arguments from predecessors).
   - Add IR → SSA → IR round-trip for interpreter fallback (spill SSA back to linear stack form when needed).
   - Ensure constant evaluation and runtime execution still succeed for all existing tests.

- [ ] **SSA Consumers**
   - Implement baseline optimizations using SSA data (constant propagation, dead assignment elimination).
   - Expose SSA traces to future JIT backend.

## Role in the Pipeline

- **Input**: A function’s stack-based IR plus its control-flow graph (`ControlFlowGraph`). The SSA builder walks the flat instruction stream, reuses CFG block boundaries, and simulates evaluation to mint versioned values.
- **Output**: `SsaFunction`—a block-structured representation where every assignment is unique, phi nodes reconcile control-flow joins, and instructions reference explicit operands rather than the implicit stack.
- **Why it exists**: SSA eliminates the ambiguity of “which definition is live here?”, making downstream analyses (constant propagation, dead assignment elimination, liveness, register allocation) straightforward. It also defines the contract that any future code generator or optimisation pass will consume instead of raw stack instructions.

```
lowered IR ──► CFG ──► SSA ──► (future) DFG / analyses ──► optimisation & codegen
```

SSA therefore answers three questions for later stages:
1. Where are the control-flow merge points? (`phi` placement)
2. Which exact definition reaches each use? (versioned names)
3. In what order can we traverse the graph safely? (reverse postorder/DFS numbering captured during dominator computation)

## Data Structures

### SSA Value Identifiers

Each variable introduced in SSA receives a unique version: `name.version`. Internally we store an integer id per symbol combined with an increasing counter.

```
struct SsaValue {
    SymbolId symbol;
    uint32_t version;
};
```

### Phi Nodes

Phi nodes live at basic-block headers. They map predecessor block ids to SSA values.

```
struct PhiInput {
    BlockId predecessor;
    SsaValue value;
};

struct PhiNode {
    SymbolId symbol;
    std::vector<PhiInput> inputs;
};
```

### SSA Block State

Blocks maintain:
- Immediate dominator.
- Dominance frontier list.
- Vector of phi nodes.
- Sequence of SSA instructions (rewritten from stack IR).

## Algorithms

### Dominance Tree

1. Run iterative data-flow algorithm using reverse post-order to compute dominators.
2. Store immediate dominator for each block (except entry).
3. Build dominance tree adjacency lists for traversal and renaming.

> Implementation note: reverse postorder (RPO) numbering comes from a DFS over the CFG starting at the entry block. Using RPO keeps the iterative dominator algorithm and later data-flow passes stable even as new blocks are introduced.

### Dominance Frontier

Use standard method (Cytron et al.): for each block `b`, for every successor `s` that is not immediately dominated by `b`, add `s` to `DF(b)`.

### Phi Placement

1. For each variable assigned in a block, add block to `defsites[name]`.
2. For every `b` in `defsites[name]`, walk dominance frontiers and insert phi nodes where needed, ensuring uniqueness per block/name pair.

### Renaming Pass

Maintain per-symbol stacks of SSA versions. Traverse dominance tree depth-first:

1. On entry to block, push current version for each symbol defined by phi nodes.
2. Rename instructions: when reading a variable, use top version; when writing, increment counter and push new version.
3. Update phi operands in successor blocks to use the version live along that edge.
4. On exit, pop versions for symbols defined in block (including phi nodes).

### Stack IR → SSA Conversion

Current IR uses stack-based instructions. To generate SSA, we first convert block bodies into explicit assignments:

1. Simulate stack evaluation while assigning temporary names to intermediate results.
2. Each stack push yields a fresh temp (e.g., `%t0`, `%t1`).
3. Stack-consuming operations read temps instead of stack positions.
4. Store instructions produce definitions for user variables.

### SSA Verification

Implement validator to ensure:
- Every use references a defined SSA value reachable via control flow.
- Phi inputs exactly match predecessor count.
- No duplicate defs for the same symbol version.

## Testing Strategy

- Extend `tests/main.cpp` with SSA-focused suites:
  - Linear control flow (single block) renaming.
  - Diamond pattern (`if`/`else`) verifying phi placement.
  - Loop example ensuring phi nodes in headers for loop-carried variables.
- Add negative tests for verifier (missing phi input, undefined use).
- Keep interpreter tests running through SSA round-trip to guard regressions.

## Looking Ahead

- **Data-Flow Graph (DFG)**: Once SSA is in place, building an explicit DFG from versioned values becomes trivial. That graph will drive instruction scheduling, common subexpression elimination, and potential vectorisation work.
- **Liveness & Register Allocation**: SSA makes it straightforward to compute live-in/live-out sets. These analyses will feed a future register allocator when we target real hardware.
- **Code Generation**: With SSA + CFG (and eventually DFG) we can lower to machine code or optimised bytecode using standard techniques (instruction selection, scheduling, register assignment).

## Integration Plan

1. **Documentation Update**: Keep README, STATUS, and `docs/spec/toolchain.md` synchronized with SSA capabilities.
2. **Build Integration**: Register new SSA source files in `ir/CMakeLists.txt` and expose API through `ir/include` headers.
3. **CLI Exposure**: Add `--dump-ssa` flag once SSA production is stable.
4. **Performance Benchmarks**: After SSA + baseline optimizations, run factorial/sorting/prime programs to verify improvements.

## Roadmap Beyond SSA

- Data-flow analyses (liveness, reaching definitions) leveraging SSA naming.
- Register allocation heuristics for future native backend.
- Integration with planned GC (SSA will help identify roots).
- Hook SSA into planned JIT/pipeline for hot path compilation.
