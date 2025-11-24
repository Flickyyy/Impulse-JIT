# Runtime & VM

## Current Implementation

The runtime now executes optimised SSA programs derived from the lowered IR:
- Loads modules, evaluates constant bindings, and caches the IR module
- Rebuilds SSA for each function invocation, then runs the optimiser (constant/copy propagation + dead assignment elimination) to a fixed point
- Interprets SSA instructions directly, maintaining a versioned value table instead of a raw operand stack
- Resolves control flow through block metadata (successors, phi nodes, dominance information) captured in SSA
- Supports direct function calls with lightweight activation records and recursive execution
- Stores numeric data as doubles (ints remain exact within the IEEE range)
- Hosts a managed heap with mark-sweep garbage collection for heap objects, keeping SSA values rooted via the locals map

### Supported Operations
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Logic: `&&`, `||`, `!`
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Variables: load/store locals, load globals
- Control: `return`, `branch`, `branch_if`, SSA phi resolution, loop headers
- Loops: lowered `while`/`for` using labels and conditional branches
- Calls: direct calls with value returns
- Value hygiene: SSA `drop` validates operand availability (no stack)
- Arrays: allocation, indexed loads/stores, and length queries through dedicated IR opcodes

## VM Structure

```
Vm {
  modules: LoadedModule[],
  heap: GcHeap,
  frames: ExecutionFrame[]
}

LoadedModule {
  name: string,
  globals: Map<string, Value>,
  module: ir::Module
}

ExecutionFrame {
  locals: Map<string, Value>,
  stack: Value[]
}
```

## Execution Model

1. Load a module via `Vm::load`, evaluating global bindings through the IR interpreter.
2. Look up the target function (default `main`) at run time.
3. Build SSA for the function, optimise it, seed parameter versions, and interpret SSA instructions block-by-block while resolving phi nodes based on predecessor edges.
4. Return the final value (if any) or report diagnostics when the SSA program is malformed.

Call frames stay lightweight: each invocation tracks local variables (including `$ssa:*` slots that root SSA values) and defers instruction storage to the SSA representation. Arguments flow through SSA value lookups before the `call` opcode dispatches to the callee.

## Managed Heap & Garbage Collection

Heap allocations (currently arrays) are represented by `GcObject` instances chained together inside `GcHeap`. The collector implements a classic mark-sweep algorithm:
- Allocation bumps `bytes_allocated` and schedules a collection when it crosses `next_gc_threshold`.
- `Vm::FrameGuard` records locals and operand stacks as roots. Every VM entry pushes a guard so GC can discover live values regardless of nested calls.
- `GcHeap::collect` marks reachable objects starting from the supplied roots, then sweeps the linked list and frees unmarked nodes, adjusting `bytes_allocated` and recalculating the next threshold.
- Thresholds are tunable at run time via `set_next_gc_threshold`, enabling stress tests and future heuristics.

Arrays initialise their storage with `nil` sentinel values. The runtime validates every index operation (`array_get`, `array_set`, `array_length`) to guarantee safety; violations raise `RuntimeError` results instead of corrupting memory. The frontend currently treats array elements as numeric (`float`) values, matching the runtime's use of doubles for computation.

## TODO

- Add tracing hooks for debugging and performance investigation
- Evaluate native code backends after interpreter semantics solidify
- Expose more heap-managed primitives (maps, strings) once semantics are defined



