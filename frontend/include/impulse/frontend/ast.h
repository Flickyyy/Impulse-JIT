#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace impulse::frontend {

struct SourceLocation {
    size_t line = 0;
    size_t column = 0;
};

struct Snippet {
    std::string text;
    SourceLocation location;
};

struct Identifier {
    std::string value;
    SourceLocation location;
};

struct Diagnostic {
    SourceLocation location;
    std::string message;
};

struct ModuleDecl {
    std::vector<Identifier> path;
};

struct ImportDecl {
    std::vector<Identifier> path;
    std::optional<Identifier> alias;
};

enum class BindingKind : std::uint8_t {
    Let,
    Const,
    Var,
};

struct Expression {
    enum class Kind : std::uint8_t {
        Literal,
        Identifier,
        Binary,
        Unary,
    };

    Kind kind = Kind::Literal;
    enum class BinaryOperator : std::uint8_t {
        Add,
        Subtract,
        Multiply,
        Divide,
        Modulo,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        LogicalAnd,
        LogicalOr,
    };

    enum class UnaryOperator : std::uint8_t {
        LogicalNot,
        Negate,
    };

    SourceLocation location;
    std::string literal_value;
    Identifier identifier;
    BinaryOperator binary_operator = BinaryOperator::Add;
    UnaryOperator unary_operator = UnaryOperator::LogicalNot;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
    std::unique_ptr<Expression> operand;
};

struct Parameter {
    Identifier name;
    Identifier type_name;
};

struct InterfaceMethod {
    Identifier name;
    std::vector<Parameter> parameters;
    std::optional<Identifier> return_type;
};

struct BindingDecl {
    BindingKind kind = BindingKind::Let;
    Identifier name;
    Identifier type_name;
    Snippet initializer;
    std::unique_ptr<Expression> initializer_expr;
};

struct Statement {
    enum class Kind : std::uint8_t {
        Return,
        Binding,
    } kind = Kind::Return;

    SourceLocation location;
    std::unique_ptr<Expression> return_expression;
    BindingDecl binding;
};

struct FunctionBody {
    std::vector<Statement> statements;
};

struct FieldDecl {
    Identifier name;
    Identifier type_name;
};

struct StructDecl {
    Identifier name;
    std::vector<FieldDecl> fields;
};

struct InterfaceDecl {
    Identifier name;
    std::vector<InterfaceMethod> methods;
};

struct FunctionDecl {
    Identifier name;
    std::vector<Parameter> parameters;
    std::optional<Identifier> return_type;
    Snippet body;
    FunctionBody parsed_body;
};

struct Declaration {
    enum class Kind : std::uint8_t {
        Binding,
        Function,
        Struct,
        Interface,
    } kind = Kind::Binding;

    bool exported = false;
    BindingDecl binding;
    FunctionDecl function;
    StructDecl structure;
    InterfaceDecl interface_decl;
};

struct Module {
    ModuleDecl decl;
    std::vector<ImportDecl> imports;
    std::vector<Declaration> declarations;
};

struct ParseResult {
    Module module;
    std::vector<Diagnostic> diagnostics;
    bool success = true;
};

}  // namespace impulse::frontend
