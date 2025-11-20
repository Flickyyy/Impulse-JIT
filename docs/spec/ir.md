# IR & Bytecode

## Current Implementation

Simple stack-based IR with instructions:
- `Literal` - push constant
- `Reference` - load variable
- `Binary` - binary op (+,-,*,/,%,&&,||,==,!=,<,<=,>,>=)
- `Unary` - unary op (!,-)
- `Store` - save to variable
- `Return` - return from function
- `Comment` - annotation
- `Branch` - unconditional jump (TODO)
- `BranchIf` - conditional jump (TODO)
- `Label` - jump target (TODO)

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

