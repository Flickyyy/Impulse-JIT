# Спецификация синтаксиса Impulse

Документ фиксирует грамматику языка, на основе которой строятся parser и проверки типов. Грамматика записана в расширенной BNF; ключевые слова выделены капсом.

## 1. Цели
- Статическая грамматика без скрытой магии и неявных преобразований.
- Читаемый C-подобный синтаксис с минимумом пунктуации.
- Возможность однозначного парсинга LL(1)/LR(1) генераторами.

## 2. Лексические элементы

| Категория | Правило |
| --- | --- |
| Идентификатор | `[A-Za-z_][A-Za-z0-9_]*` (unicode в планах) |
| Разделители | пробелы, табы, перевод строки |
| Комментарии | `// line` до конца строки, `/* block */` без вложенности (в будущем — вложенные) |
| Целый литерал | `0` &#124; `[1-9][0-9_]*` |
| Вещественный литерал | `{IntLit}``.`{Digits} (опционально `e[+-]?{Digits}`) |
| Булевый литерал | `true`, `false` |
| Строка | `" ... "` с escape-последовательностями `\n`, `\t`, `\"`, `\\` |

Ключевые слова (зарезервированы, не могут быть идентификаторами):
```
module import export func struct interface let var const if else while for in return break continue match true false option result panic
```

## 3. Структура модуля

Каждый файл описывает один модуль. Первой строчкой обязан быть `module <Ident>;`. Импорты идут после объявления модуля, затем блок деклараций.

```
Module      ::= ModuleHeader ImportList DeclList
ModuleHeader::= "module" Ident ";"
ImportList  ::= { Import }
Import      ::= "import" ImportPath ["as" Ident] ";"
ImportPath  ::= Ident {"::" Ident}
DeclList    ::= { Decl }
```

В дальнейшем допустимы `export` перед объявлениями для явной видимости.

## 4. Декларации верхнего уровня

```
Decl        ::= FuncDecl | StructDecl | InterfaceDecl | ConstDecl | LetDecl | VarDecl

FuncDecl    ::= "func" Ident GenericParams? "(" ParamList? ")" RetType Block
GenericParams ::= "<" GenericParam {"," GenericParam} ">"
GenericParam  ::= Ident [":" TypeBound]
ParamList   ::= Param {"," Param}
Param       ::= Pattern ":" Type
RetType     ::= "->" Type | ε

StructDecl  ::= "struct" Ident GenericParams? StructBody
StructBody  ::= "{" FieldDecl* "}"
FieldDecl   ::= Ident ":" Type ";"

InterfaceDecl ::= "interface" Ident InterfaceBody
InterfaceBody  ::= "{" InterfaceMethod* "}"
InterfaceMethod ::= Ident "(" ParamList? ")" RetType ";"

ConstDecl   ::= "const" Binding
LetDecl     ::= "let" Binding
VarDecl     ::= "var" Binding
Binding     ::= Pattern ":" Type "=" Expr ";"
```

`Pattern` пока ограничен идентификатором; в roadmap — кортежные паттерны.

## 5. Операторы и конструкции

### Управляющие конструкции
```
Stmt        ::= Block | LetStmt | VarStmt | IfStmt | WhileStmt | ForStmt | MatchStmt |
                ReturnStmt | BreakStmt | ContinueStmt | ExprStmt

Block       ::= "{" Stmt* "}"
IfStmt      ::= "if" Expr Block ["else" (Block | IfStmt)]
WhileStmt   ::= "while" Expr Block
ForStmt     ::= "for" LoopHead Block
LoopHead    ::= Ident "in" Expr | Expr? ";" Expr? ";" Expr?
MatchStmt   ::= "match" Expr "{" MatchArm+ "}"
MatchArm    ::= Pattern "=>" (Expr | Block) ","
ReturnStmt  ::= "return" Expr? ";"
LetStmt     ::= "let" Binding
VarStmt     ::= "var" Binding
ExprStmt    ::= Expr ";"
```

### Выражения и приоритеты

```
Expr          ::= Assign
Assign        ::= LogicOr {"=" Assign}
LogicOr       ::= LogicAnd {"||" LogicAnd}
LogicAnd      ::= Equality {"&&" Equality}
Equality      ::= Relational {("==" | "!=") Relational}
Relational    ::= Add {("<" | "<=" | ">" | ">=") Add}
Add           ::= Mul {("+" | "-") Mul}
Mul           ::= Unary {("*" | "/" | "%") Unary}
Unary         ::= ("!" | "-" | "&" | "*") Unary | Postfix
Postfix       ::= Primary {PostfixSuffix}
PostfixSuffix ::= CallSuffix | IndexSuffix | MemberSuffix
CallSuffix    ::= "(" ArgList? ")"
ArgList       ::= Expr {"," Expr}
IndexSuffix   ::= "[" Expr "]"
MemberSuffix  ::= "." Ident
Primary       ::= Literal | Ident | "(" Expr ")" | Lambda
Lambda        ::= "func" "(" ParamList? ")" RetType Block
```

Оператор присваивания праворекурсивный: допускаются цепочки `a = b = c` с правой ассоциативностью. `&` и `*` в `Unary` возвращают ссылку/разыменование дескрипторов (без C-пойнтеров, но синтаксис заложен).

## 6. Примеры

### Полный модуль
```impulse
module math::factorial;

import std::io;

func factorial<T: Numeric>(n: T) -> T {
    if n <= 1 { return 1; }
    return n * factorial(n - 1);
}

func main() -> int {
    let value: int = factorial(20);
    print(value);
    return 0;
}
```

### Pattern matching
```impulse
match optionValue {
    Some(x) => print(x),
    None    => panic("empty"),
}
```

---

Детальная таблица приоритетов и расширенные паттерны будут добавлены по мере эволюции языка.
