# Backend Roadmap

This document tracks the remaining work required to move from the current interpreter-centric pipeline to a complete managed backend with native code generation.

## 1. Runtime Execution Model
- [x] Design call frame layout (parameters, locals, temporaries, return slot, caller PC)
- [x] Implement frame stack in `impulse::runtime::Vm`
- [x] Expose SSA-aware executor that can run optimized SSA directly
- [x] Maintain compatibility with stack IR fallback (via SSA → IR lowering)
- [x] Add diagnostics and tracing hooks for execution state

## 2. Memory Management & GC
- [x] Define heap object model (headers, type tags, payload layout)
- [ ] Introduce allocation intrinsics in IR/SSA and runtime helper APIs
- [ ] Track roots (globals, VM stack, SSA temporaries, native handles)
- [ ] Implement mark-and-sweep collector and integrate with runtime
- [ ] Add stress tests that force collections and validate liveness

## 3. Static Type Checker
- [ ] Build symbol/type tables during semantic analysis
- [ ] Enforce expression and statement typing rules
- [ ] Validate function signatures and return paths
- [ ] Produce rich diagnostics and recovery where possible
- [ ] Gate backend features on type correctness (e.g., heap layouts)

## 4. SSA Optimization Pipeline
- [ ] Add passes for loop-invariant code motion and basic inlining
- [ ] Collect metadata (loop info, dominance summaries) for backend consumers
- [ ] Establish pass manager with diagnostics/metrics reporting
- [ ] Document pass ordering and interactions

## 5. Native Code Generation
- [ ] Choose initial target (e.g., x86-64 System V)
- [ ] Map SSA instructions to machine-level IR
- [ ] Implement register allocation (linear scan to start)
- [ ] Emit machine code and link with runtime support stubs
- [ ] Provide CLI switches for JIT/AOT modes and benchmarking

## 6. Standard Library & Benchmarks
- [ ] Implement core stdlib (print, math helpers, collections)
- [ ] Provide runtime support for array/struct operations
- [ ] Ship benchmark programs (factorial, sort, primes) and automated timing harness

## Tracking & Documentation
- Keep `STATUS.md` and this roadmap in sync after each milestone
- Document new APIs/specs as they are introduced (`docs/spec/*`)
- Ensure every feature lands with dedicated tests and CLI coverage
