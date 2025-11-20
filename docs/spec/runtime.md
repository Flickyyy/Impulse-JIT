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
- Control: return (if/while TODO)

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

- **Control flow**: Branch/Label instructions for if/while
- **GC**: Mark-and-sweep garbage collector
- **JIT**: Hot path compilation to native code
- **Call convention**: Proper ABI for multi-function calls
- **Heap**: Dynamic allocation beyond stack

