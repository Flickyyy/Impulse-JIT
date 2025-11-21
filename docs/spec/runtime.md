# Runtime & VM

## Current Implementation

Simple stack-based VM:
- Executes IR instructions sequentially
- Stack for operands
- Local variables storage
- Module loading with global bindings

### Supported Operations
- Arithmetic: +,-,*,/,%
- Logic: &&,||,!
- Comparison: ==,!=,<,<=,>,>=
- Variables: load/store locals and globals
- Control: return, branch, branch_if, labels
- Loops: while/for lowering using labels and conditional branches
- Calls: direct calls with value returns
- Stack: drop top value for expression statements

## VM Structure

```
VM {
  modules: LoadedModule[]
}

LoadedModule {
  name: string,
  globals: Map<string, double>,
  module: IR.Module
}
```

## Execution Model

1. Load module → evaluate global bindings
2. Find entry function (default "main")
3. Execute instructions on stack
4. Return result value

## TODO

- **Recursion semantics**: Define stack depth limits and tail call expectations
- **Loop control**: Add `break`/`continue` semantics and label resolution
- **GC**: Mark-and-sweep garbage collector
- **JIT**: Hot path compilation to native code
- **Call convention**: Proper ABI for cross-module calls
- **Heap**: Dynamic allocation beyond stack

