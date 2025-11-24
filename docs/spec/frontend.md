# Frontend Implementation

## Current State

Parser supports:
1. Module header: `module path::to::mod;`
2. Imports: `import path::to::mod [as alias];`  
3. Bindings: `let|const|var name: Type = expr;`
4. Functions: `func name(params) -> Type { stmts }`
5. Structs: `struct Name { fields }`
6. Interfaces: `interface Name { methods }`
7. Control flow: `if/else`, `while`, `for (init; cond; incr)`, `break`, `continue`
8. Expressions: all operators with precedence

## AST Structure

```
Module → (ModuleDecl, Import*, Declaration*)
Declaration → Binding | Function | Struct | Interface
Statement → Return | Binding | If | While | For | Break | Continue | Expr
Expression → Literal | Identifier | Binary | Unary
```

## Error Handling

- Parser collects Diagnostic{location, message}
- Stops on first fatal error
- Partial AST returned with diagnostics

