# Impulse Grammar

## Keywords
```
module import export as
func struct interface
let var const
if else while for in return
true false panic
```

## Module Structure
```
Module      ::= "module" Path ";" Import* Decl*
Path        ::= Ident ("::" Ident)*
Import      ::= "import" Path ["as" Ident] ";"
Decl        ::= ["export"] (FuncDecl | StructDecl | InterfaceDecl | Binding)
```

## Declarations
```
FuncDecl    ::= "func" Ident "(" Params? ")" ["->" Type] Block
StructDecl  ::= "struct" Ident "{" Field* "}"
Interface   ::= "interface" Ident "{" Method* "}"
Binding     ::= ("let"|"const"|"var") Ident ":" Type "=" Expr ";"

Field       ::= Ident ":" Type ";"
Method      ::= "func" Ident "(" Params? ")" ["->" Type] ";"
Params      ::= Param ("," Param)*
Param       ::= Ident ":" Type
```

## Statements
```
Stmt        ::= Block | IfStmt | WhileStmt | ReturnStmt | Binding | ExprStmt
Block       ::= "{" Stmt* "}"
IfStmt      ::= "if" Expr Block ["else" Block]
WhileStmt   ::= "while" Expr Block
ReturnStmt  ::= "return" Expr? ";"
ExprStmt    ::= Expr ";"
```

## Expressions (precedence: low → high)
```
Expr        ::= LogicOr
LogicOr     ::= LogicAnd ("||" LogicAnd)*
LogicAnd    ::= Equality ("&&" Equality)*
Equality    ::= Relational (("=="|"!=") Relational)*
Relational  ::= Add (("<"|"<="|">"|">=") Add)*
Add         ::= Mul (("+"|"-") Mul)*
Mul         ::= Unary (("*"|"/"|"%") Unary)*
Unary       ::= ("!"|"-") Unary | Primary
Primary     ::= IntLit | FloatLit | BoolLit | Ident | "(" Expr ")"
```

## Literals
- Int: `0 | [1-9][0-9_]*`
- Float: `Digits.Digits [e[+-]?Digits]`
- Bool: `true | false`
- String: `"..."` (with `\n`, `\t`, `\"`, `\\`)

