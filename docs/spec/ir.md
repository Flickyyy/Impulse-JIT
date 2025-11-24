# Intermediate Representation

Impulse now emits a single stack-based IR that mirrors the parser’s evaluation order. The representation is deliberately simple: instructions operate on an implicit operand stack and basic blocks are linked by explicit branch instructions. This IR feeds both the constant evaluator and the runtime interpreter.

## Stack IR Instruction Set

The stack IR is a linear sequence of instructions grouped into labelled basic blocks. Operations consume/produce values on an implicit operand stack.

- `Literal <value>` – push a numeric or boolean constant
- `Reference <name>` – load a binding or temporary onto the stack
- `Binary <op>` – pop two operands, apply `<op>` (`+`, `-`, `*`, `/`, `%`, `&&`, `||`, `==`, `!=`, `<`, `<=`, `>`, `>=`)
- `Unary <op>` – pop one operand, apply `<op>` (`!`, `-`)
- `Store <name>` – pop value and assign to `<name>`
- `Drop` – discard the top of the stack (used for expression statements)
- `Return` – pop the optional return value and exit the current function
- `Comment` – textual annotation for diagnostics / debugging
- `Branch <label>` – jump to the target basic block
- `BranchIf <label> <sentinel>` – pop a value; jump to `<label>` when it equals `<sentinel>` (lowering emits comparisons against `0` for falsy checks)
- `Label <name>` – mark the beginning of a basic block
- `Call <symbol>` – call another function with N previously pushed arguments

`Call` leaves its return value on the stack. All other instructions either mutate control flow or the stack directly. The IR is the canonical output of semantic lowering and is used for constant evaluation and execution inside the VM.

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

## Future Work

- Re-introduce an SSA layer once the simplified pipeline has comprehensive tests
- Add metadata for debugging and profiling
- Explore typed IR variants for native backends

