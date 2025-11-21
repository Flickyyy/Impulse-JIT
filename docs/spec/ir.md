# IR & Bytecode

## Current Implementation

Simple stack-based IR with instructions:
- `Literal` - push constant
- `Reference` - load variable
- `Binary` - binary op (+,-,*,/,%,&&,||,==,!=,<,<=,>,>=)
- `Unary` - unary op (!,-)
- `Store` - save to variable
- `Drop` - discard top of stack
- `Return` - return from function
- `Comment` - annotation
- `Branch` - unconditional jump
- `BranchIf` - conditional jump comparing top of stack
- `Label` - jump target
- `Call` - invoke function with N arguments (arguments pushed left-to-right)

`BranchIf` pops a single value off the stack and compares it to an immediate operand. When the values match, execution jumps to the named label. Lowering currently emits a comparison against `0` for falsy checks.

## IR Structure

```
Module {
  path: string[],
  bindings: Binding[],
  functions: Function[],
  structs: Struct[],
  interfaces: Interface[]
}

Function {
  name: string,
  params: Parameter[],
  return_type?: string,
  blocks: BasicBlock[]
}

BasicBlock {
  label: string,
  instructions: Instruction[]
}
```

## Text Format

```
module test

let x: int = (1 + 2);  # = 3
  {
    literal 1
    literal 2
    binary +
    store x
  }

func main() -> int {
  ^entry:
    reference x
    return
}
```

## TODO: Full SSA

Future improvements:
- Phi nodes for SSA form
- CFG optimization
- Type information in IR
- Binary bytecode format

