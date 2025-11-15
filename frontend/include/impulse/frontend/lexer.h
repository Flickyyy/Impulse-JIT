#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace impulse::frontend {

enum class TokenKind : std::uint8_t {
    // literals / identifiers
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    BooleanLiteral,

    // keywords
    KwModule,
    KwImport,
    KwAs,
    KwExport,
    KwFunc,
    KwStruct,
    KwInterface,
    KwLet,
    KwVar,
    KwConst,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwIn,
    KwReturn,
    KwBreak,
    KwContinue,
    KwMatch,
    KwOption,
    KwResult,
    KwPanic,

    // punctuation / operators
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    DoubleColon,
    Dot,
    Arrow,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Ampersand,
    AmpersandAmpersand,
    Pipe,
    PipePipe,
    Bang,
    BangEqual,
    Equal,
    EqualEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    // diagnostics
    Error,
    EndOfFile,
};

struct Token {
    TokenKind kind;
    std::string lexeme;
    size_t line;
    size_t column;
};

class Lexer {
   public:
    explicit Lexer(std::string source);
    [[nodiscard]] auto tokenize() -> std::vector<Token>;

   private:
    struct SourceLocation {
        size_t line;
        size_t column;
    };

    [[nodiscard]] auto peek() const -> char;
    [[nodiscard]] auto peekNext() const -> char;
    auto advance() -> char;
    [[nodiscard]] auto match(char expected) -> bool;
    [[nodiscard]] auto isAtEnd() const -> bool;
    void skipTrivia();
    void lexIdentifier(std::vector<Token>& tokens, SourceLocation location, size_t startIndex);
    void lexNumber(std::vector<Token>& tokens, SourceLocation location, size_t startIndex);
    void lexString(std::vector<Token>& tokens, SourceLocation location);
    void pushToken(std::vector<Token>& tokens, TokenKind kind, std::string lexeme, SourceLocation location);
    [[nodiscard]] auto slice(size_t startIndex) const -> std::string;

    std::string source_;
    size_t current_ = 0;
    size_t line_ = 1;
    size_t column_ = 1;
};

}  // namespace impulse::frontend
