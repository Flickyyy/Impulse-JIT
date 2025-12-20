# Runtime & VM

## Current Implementation

The runtime executes SSA programs derived from the lowered IR:
- Loads modules, evaluates constant bindings, and caches the IR module
- Rebuilds SSA for each function invocation
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
- Builtins: `print`, `println`, `string_length`, `string_equals`, `string_concat`, `string_repeat`, `string_slice`,
  `string_lower`, `string_upper`, `string_trim`, `array`, `array_get`, `array_set`, `array_length`, `array_fill`,
  `array_push`, `array_pop`, `array_join`, `array_sum`, `read_line`

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
3. Build SSA for the function, seed parameter versions, and interpret SSA instructions block-by-block while resolving phi nodes based on predecessor edges.
4. Return the final value (if any) or report diagnostics when the SSA program is malformed.

Call frames stay lightweight: each invocation tracks local variables (including `$ssa:*` slots that root SSA values) and defers instruction storage to the SSA representation. Arguments flow through SSA value lookups before the `call` opcode dispatches to the callee.

## Managed Heap & Garbage Collection

Heap allocations (currently arrays) are represented by `GcObject` instances chained together inside `GcHeap`. The collector implements a classic mark-sweep algorithm:
- Allocation bumps `bytes_allocated` and schedules a collection when it crosses `next_gc_threshold`.
- `Vm::FrameGuard` records locals and operand stacks as roots. Every VM entry pushes a guard so GC can discover live values regardless of nested calls.
- `GcHeap::collect` marks reachable objects starting from the supplied roots, then sweeps the linked list and frees unmarked nodes, adjusting `bytes_allocated` and recalculating the next threshold.
- Thresholds are tunable at run time via `set_next_gc_threshold`, enabling stress tests and future heuristics.

Arrays initialise their storage with `nil` sentinel values. The runtime validates every index operation (`array_get`,
`array_set`, `array_length`) to guarantee safety; violations raise `RuntimeError` results instead of corrupting memory.
Higher-level helpers like `array_fill`, `array_push`, `array_pop`, `array_join`, and `array_sum` sit on top of that base
behaviour and surface descriptive errors when inputs are malformed (e.g., popping from an empty array, joining objects).
The frontend currently treats array elements as numeric (`float`) values, matching the runtime's use of doubles for
computation, but the helpers accept strings and `nil` values so acceptance coverage can exercise formatting paths.

String helpers now span length queries, equality checks, concatenation, repetition, slicing, case conversion, and
whitespace trimming. `string_repeat` and `string_slice` defensively validate their numeric arguments before allocating
results so runaway calls surface as runtime errors instead of exhausting memory.

`read_line` now consumes data through the VM's pluggable input surface. The CLI wires this to process stdin by default,
while the acceptance harness feeds deterministic `stdin.txt` fixtures so regression cases stay hermetic.

## TODO

- Add richer tracing/diagnostic hooks (toggleable builtin logging)
- Expand structured diagnostics (IDs, hints) emitted by the runtime
- Integrate JIT compiler for hot functions
- Expose more heap-managed primitives (maps, strings) once semantics are defined



