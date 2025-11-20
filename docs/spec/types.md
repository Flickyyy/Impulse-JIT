# Type System

## 1. Design Principles

### Core Philosophy
- **Static typing** — All types known at compile time
- **Type safety** — No implicit conversions that lose information
- **Simplicity** — Easy to understand and reason about
- **Expressiveness** — Support common programming patterns

### Current State
⚠️ **Type checking not yet implemented**. Parser accepts type annotations but doesn't verify them. All values are currently treated as `double` in the runtime.

## 2. Primitive Types

### Numeric Types
```
int     — Integer numbers (currently interpreted as double)
float   — Floating-point numbers
```

**Planned behavior:**
- `int`: 64-bit signed integer
- `float`: 64-bit double precision
- Automatic promotion: `int` → `float` when needed
- Explicit conversion: `int(x)`, `float(x)`

### Boolean Type
```
bool    — true or false
```

**Current behavior:**
- `true` → 1.0
- `false` → 0.0
- Any non-zero value is truthy

### String Type (Future)
```
string  — UTF-8 text
```

**Planned:**
- Immutable by default
- Built-in operations: concatenation, slicing, comparison
- Escape sequences: `\n`, `\t`, `\"`, `\\`

## 3. Composite Types

### Structs
```impulse
struct Point {
    x: int;
    y: int;
}

let p: Point = Point { x: 10, y: 20 };
```

**Current state:** Parsed but not executable  
**Needed:** Memory layout, field access, construction

### Arrays (Future)
```impulse
let numbers: [int] = [1, 2, 3, 4, 5];
let matrix: [[int]] = [[1, 2], [3, 4]];
```

**Planned:**
- Fixed-size arrays
- Dynamic arrays (vector/slice)
- Bounds checking
- Iteration support

### Tuples (Future)
```impulse
let pair: (int, string) = (42, "answer");
let triple: (int, int, int) = (1, 2, 3);
```

## 4. Function Types

### Current Syntax
```impulse
func add(a: int, b: int) -> int {
    return a + b;
}
```

**Working:**
- Function declarations with typed parameters
- Return type annotation
- Function calls (basic)

**Needed:**
- Type checking parameter types
- Return type verification
- Function pointers/closures

### Higher-Order Functions (Future)
```impulse
func map(f: func(int) -> int, xs: [int]) -> [int] {
    // Apply f to each element
}
```

## 5. Type Inference (Future)

### Local Variable Inference
```impulse
let x = 42;              // infer: int
let y = 3.14;            // infer: float
let z = x + 1;           // infer: int
let w = true && false;   // infer: bool
```

### Function Return Type Inference
```impulse
func identity(x: int) {
    return x;  // infer return type: int
}
```

## 6. Type Checking Rules

### Assignment Compatibility
**Rule:** Expression type must match declared type
```impulse
let x: int = 42;        // OK
let y: int = 3.14;      // ERROR: float assigned to int
let z: float = 42;      // OK: int promotes to float
```

### Operator Type Rules

#### Arithmetic Operators (+, -, *, /, %)
```
int    OP int    → int
float  OP float  → float
int    OP float  → float
```

#### Comparison Operators (<, <=, >, >=, ==, !=)
```
int    CMP int    → bool
float  CMP float  → bool
int    CMP float  → bool
```

#### Logical Operators (&&, ||, !)
```
bool   AND bool   → bool
bool   OR  bool   → bool
NOT    bool       → bool
```

### Control Flow Type Rules
```impulse
if condition {  // condition must be bool
    // ...
}

while condition {  // condition must be bool
    // ...
}
```

## 7. Interfaces (Future)

### Declaration
```impulse
interface Drawable {
    func draw() -> void;
    func move(dx: int, dy: int) -> void;
}
```

### Implementation
```impulse
struct Circle {
    x: int;
    y: int;
    radius: int;
}

impl Drawable for Circle {
    func draw() -> void {
        // Implementation
    }
    
    func move(dx: int, dy: int) -> void {
        let x: int = x + dx;
        let y: int = y + dy;
    }
}
```

**Planned features:**
- Structural typing (duck typing)
- Multiple interface implementation
- Interface composition

## 8. Generics (Future)

### Generic Functions
```impulse
func swap<T>(a: T, b: T) -> (T, T) {
    return (b, a);
}

let pair = swap<int>(1, 2);
```

### Generic Types
```impulse
struct Box<T> {
    value: T;
}

let int_box: Box<int> = Box { value: 42 };
let str_box: Box<string> = Box { value: "hello" };
```

### Constraints
```impulse
interface Numeric {
    func add(other: Self) -> Self;
    func mul(other: Self) -> Self;
}

func sum<T: Numeric>(values: [T]) -> T {
    // T must implement Numeric
}
```

## 9. Option and Result Types (Future)

### Option for Nullable Values
```impulse
option<int>  // Some(value) or None

func find(xs: [int], target: int) -> option<int> {
    // Return Some(index) or None
}
```

### Result for Error Handling
```impulse
result<int, string>  // Ok(value) or Err(error)

func divide(a: int, b: int) -> result<int, string> {
    if b == 0 {
        return Err("division by zero");
    }
    return Ok(a / b);
}
```

## 10. Type System Implementation Plan

### Phase 1: Basic Type Checking (Next Priority)
- ✅ Parse type annotations (DONE)
- ⬜ Symbol table with types
- ⬜ Type checking for expressions
- ⬜ Type checking for statements
- ⬜ Function signature verification

### Phase 2: Type Inference
- ⬜ Local variable type inference
- ⬜ Return type inference
- ⬜ Generic function instantiation

### Phase 3: Advanced Features
- ⬜ Struct types and field access
- ⬜ Array types and indexing
- ⬜ Interface types and implementations
- ⬜ Generic types and constraints

### Phase 4: Refinement
- ⬜ Better error messages
- ⬜ Type aliases
- ⬜ Newtype pattern
- ⬜ Exhaustiveness checking

## 11. Type Error Messages

### Good Error Messages (Goal)
```
error: type mismatch
  --> example.imp:5:12
   |
5  |     let x: int = "hello";
   |            ^^^   ^^^^^^^ expected int, found string
   |            |
   |            variable declared as int
   |
help: convert string to int with int() function
```

### Current State
⚠️ No type checking yet — parser accepts all syntactically valid programs

## 12. Type System Soundness

### Goals
- **No undefined behavior** — Type errors caught at compile time
- **Memory safety** — No invalid memory access through types
- **Predictable conversions** — Clear rules for type coercion

### Guarantees (Future)
- Well-typed programs don't crash due to type errors
- No implicit narrowing conversions
- Generic code is type-safe at instantiation

## 13. Interoperability

### FFI Types (Future)
```impulse
extern func printf(format: string, ...) -> int;
extern func malloc(size: int) -> pointer;
```

**Required:**
- C type mappings
- Calling convention compatibility
- Memory management at boundaries

## 14. Performance Considerations

### Type Representation
- Primitives: Machine native types
- Structs: Packed in memory
- Arrays: Contiguous allocation
- Interfaces: Vtable dispatch

### Optimization Opportunities
- Monomorphization of generics
- Devirtualization of interface calls
- Escape analysis for stack allocation
- Inline type-specialized functions
