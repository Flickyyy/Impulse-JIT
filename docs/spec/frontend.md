# Frontend plan (incremental parser)

## Current milestone scope

For the first parser increments we only need to support the top-level constructs required to build and test toolchain plumbing:

1. **Module header** – `module <ident {:: ident}>;`
2. **Import list** – `import <ident {:: ident}> [as ident];`
3. **Bindings** – `let|const|var name: Type = expr;`
4. **Function declarations** – `func name(params) -> Type { ... }` with a minimal statement list (return + let/const/var)
5. **Struct declarations** – `struct Name { field: Type; ... }` with field lists captured as identifiers
6. **Interface declarations** – `interface Name { func method(params) -> Type; }` signatures only (no bodies)
7. **Exports** – optional `export` modifier that can precede any top-level declaration.

Everything else (structs, functions, pattern matching, etc.) will be added iteratively once we have a solid scanner+parser pipeline and AST carriers.

## AST surface (v0)

```text
Module
 ├─ name: vector<Identifier>
 ├─ imports: vector<Import>
 └─ decls: vector<Declaration>

Import
 ├─ path: vector<Identifier>
 └─ alias: optional<Identifier>

Declaration
 ├─ exported: bool
 ├─ BindingDecl (let/const/var)
 ├─ FunctionDecl
 ├─ StructDecl
 └─ InterfaceDecl
 └─ exported: bool

BindingDecl
 ├─ keyword: BindingKind
 ├─ name/type: Identifier
 └─ initializer: Snippet

FunctionDecl
 ├─ name: Identifier
 ├─ params: vector<Parameter>
 ├─ return_type: optional<Identifier>
 ├─ body: Snippet (raw tokens between braces)
 └─ parsed_body: FunctionBody { statements: vector<Statement> }

Statement (function-local)
 ├─ Return: `return expr;`
 └─ Binding: `let|const|var name: Type = expr;`

Expressions (v0)
 ├─ Literals: numeric + `true`/`false`
 └─ Binary operators: `+ - * / == != < <= > >=`

StructDecl
 ├─ name: Identifier
 └─ fields: vector<FieldDecl>

FieldDecl
 ├─ name: Identifier
 └─ type_name: Identifier

InterfaceDecl
 ├─ name: Identifier
 └─ methods: vector<InterfaceMethod>

InterfaceMethod
 ├─ name: Identifier
 ├─ params: vector<Parameter>
 └─ return_type: optional<Identifier>
```

Identifiers capture both the string value and source location (line/column). Expressions remain opaque blobs until the expression parser lands; the lexer slice is enough to keep tooling moving.

## Error handling

- Parser collects `Diagnostic` entries with `location` and `message`.
- On first fatal issue we stop parsing to avoid cascades, but the API still returns partial AST plus diagnostics.
- Later we can add recovery hooks per production.

## Deliverables for this iteration

- `frontend/include/impulse/frontend/ast.h` – structs listed above.
- `frontend/include/impulse/frontend/parser.h` – parser API (`parse_module(std::string_view)` returning `ParseResult`).
- `frontend/src/parser.cpp` – implementation using the existing lexer.
- `frontend/include/impulse/frontend/lowering.h` + `frontend/src/lowering.cpp` – упрощённый lowering в текстовый IR для CLI.
- `frontend/include/impulse/frontend/semantic.h` + `frontend/src/semantic.cpp` – семантические проверки (дубликаты имён, поля структур, методы интерфейсов, повторные import'ы и alias'ы).
- Unit tests under `tests/` verifying module/import/binding/function parsing and representative diagnostics.

This keeps the repository shippable (build + tests stay green) while giving us a concrete foundation for the next parser features.
