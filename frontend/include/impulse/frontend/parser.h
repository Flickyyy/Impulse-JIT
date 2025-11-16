#pragma once

#include <string>
#include <memory>

#include "impulse/frontend/ast.h"
#include "impulse/frontend/lexer.h"

namespace impulse::frontend {

class Parser {
   public:
    explicit Parser(std::string source);
    [[nodiscard]] auto parseModule() -> ParseResult;

   private:
    auto advance() -> const Token&;
    [[nodiscard]] auto peek() const -> const Token&;
    [[nodiscard]] auto previous() const -> const Token&;
    [[nodiscard]] auto isAtEnd() const -> bool;
    [[nodiscard]] auto check(TokenKind kind) const -> bool;
    [[nodiscard]] auto match(TokenKind kind) -> bool;
    [[nodiscard]] auto consume(TokenKind kind, const char* message) -> const Token*;

    struct TokenRange {
        size_t start;
        size_t end;
    };

    [[nodiscard]] auto parseModuleDecl() -> ModuleDecl;
    [[nodiscard]] auto parseImportList() -> std::vector<ImportDecl>;
    [[nodiscard]] auto parseDeclaration() -> std::optional<Declaration>;
    [[nodiscard]] auto parseBindingDecl(BindingKind kind) -> BindingDecl;
    [[nodiscard]] auto parseFunctionDecl() -> FunctionDecl;
    [[nodiscard]] auto parseStructDecl() -> StructDecl;
    [[nodiscard]] auto parseFieldDecl() -> std::optional<FieldDecl>;
    [[nodiscard]] auto parseInterfaceDecl() -> InterfaceDecl;
    [[nodiscard]] auto parseInterfaceMethod() -> std::optional<InterfaceMethod>;
    [[nodiscard]] auto parseParameterList() -> std::vector<Parameter>;
    [[nodiscard]] auto parsePath(const char* context) -> std::vector<Identifier>;
    [[nodiscard]] auto parseExpression() -> std::unique_ptr<Expression>;
    [[nodiscard]] auto parseBinaryExpression(int minPrecedence) -> std::unique_ptr<Expression>;
    [[nodiscard]] auto parsePrimaryExpression() -> std::unique_ptr<Expression>;
    [[nodiscard]] static auto binaryPrecedence(TokenKind kind) -> int;
    [[nodiscard]] static auto toBinaryOperator(TokenKind kind) -> Expression::BinaryOperator;

    void reportError(const Token& token, std::string message);
    [[nodiscard]] auto makeIdentifier(const Token& token) const -> Identifier;
    [[nodiscard]] auto makeSnippet(TokenRange range) const -> Snippet;

    std::string source_;
    std::vector<Token> tokens_;
    size_t current_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace impulse::frontend
