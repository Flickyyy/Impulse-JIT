#include "../include/impulse/frontend/lexer.h"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>

namespace impulse::frontend {
namespace {

[[nodiscard]] auto isIdentifierStart(char c) -> bool {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) != 0 || c == '_';
}

[[nodiscard]] auto isIdentifierBody(char c) -> bool {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

[[nodiscard]] auto isDigit(char c) -> bool {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

[[nodiscard]] auto keywordTable() -> const std::unordered_map<std::string, TokenKind>& {
    static const std::unordered_map<std::string, TokenKind> table = {
    {"module", TokenKind::KwModule},   {"import", TokenKind::KwImport},   {"as", TokenKind::KwAs},
    {"export", TokenKind::KwExport},
        {"func", TokenKind::KwFunc},       {"struct", TokenKind::KwStruct},   {"interface", TokenKind::KwInterface},
        {"let", TokenKind::KwLet},         {"var", TokenKind::KwVar},         {"const", TokenKind::KwConst},
        {"if", TokenKind::KwIf},           {"else", TokenKind::KwElse},       {"while", TokenKind::KwWhile},
        {"for", TokenKind::KwFor},         {"in", TokenKind::KwIn},           {"return", TokenKind::KwReturn},
        {"break", TokenKind::KwBreak},     {"continue", TokenKind::KwContinue},
        {"match", TokenKind::KwMatch},     {"option", TokenKind::KwOption},   {"result", TokenKind::KwResult},
        {"panic", TokenKind::KwPanic},
    };
    return table;
}

[[nodiscard]] auto classifyIdentifier(const std::string& lexeme) -> TokenKind {
    if (lexeme == "true" || lexeme == "false") {
        return TokenKind::BooleanLiteral;
    }
    if (const auto it = keywordTable().find(lexeme); it != keywordTable().end()) {
        return it->second;
    }
    return TokenKind::Identifier;
}

}  // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

auto Lexer::tokenize() -> std::vector<Token> {
    std::vector<Token> tokens;
    tokens.reserve(source_.size() / 4 + 8);

    while (true) {
        skipTrivia();
        if (isAtEnd()) {
            break;
        }

        const SourceLocation location{line_, column_};
        const size_t startIndex = current_;
        const char c = advance();

        if (isIdentifierStart(c)) {
            lexIdentifier(tokens, location, startIndex);
            continue;
        }

        if (isDigit(c)) {
            lexNumber(tokens, location, startIndex);
            continue;
        }

        switch (c) {
            case '"':
                lexString(tokens, location);
                break;
            case '(':
                pushToken(tokens, TokenKind::LParen, "(", location);
                break;
            case ')':
                pushToken(tokens, TokenKind::RParen, ")", location);
                break;
            case '{':
                pushToken(tokens, TokenKind::LBrace, "{", location);
                break;
            case '}':
                pushToken(tokens, TokenKind::RBrace, "}", location);
                break;
            case '[':
                pushToken(tokens, TokenKind::LBracket, "[", location);
                break;
            case ']':
                pushToken(tokens, TokenKind::RBracket, "]", location);
                break;
            case ',':
                pushToken(tokens, TokenKind::Comma, ",", location);
                break;
            case ';':
                pushToken(tokens, TokenKind::Semicolon, ";", location);
                break;
            case ':':
                if (match(':')) {
                    pushToken(tokens, TokenKind::DoubleColon, "::", location);
                } else {
                    pushToken(tokens, TokenKind::Colon, ":", location);
                }
                break;
            case '.':
                pushToken(tokens, TokenKind::Dot, ".", location);
                break;
            case '-':
                if (match('>')) {
                    pushToken(tokens, TokenKind::Arrow, "->", location);
                } else {
                    pushToken(tokens, TokenKind::Minus, "-", location);
                }
                break;
            case '+':
                pushToken(tokens, TokenKind::Plus, "+", location);
                break;
            case '*':
                pushToken(tokens, TokenKind::Star, "*", location);
                break;
            case '/':
                pushToken(tokens, TokenKind::Slash, "/", location);
                break;
            case '%':
                pushToken(tokens, TokenKind::Percent, "%", location);
                break;
            case '&':
                if (match('&')) {
                    pushToken(tokens, TokenKind::AmpersandAmpersand, "&&", location);
                } else {
                    pushToken(tokens, TokenKind::Ampersand, "&", location);
                }
                break;
            case '|':
                if (match('|')) {
                    pushToken(tokens, TokenKind::PipePipe, "||", location);
                } else {
                    pushToken(tokens, TokenKind::Pipe, "|", location);
                }
                break;
            case '!':
                if (match('=')) {
                    pushToken(tokens, TokenKind::BangEqual, "!=", location);
                } else {
                    pushToken(tokens, TokenKind::Bang, "!", location);
                }
                break;
            case '=':
                if (match('=')) {
                    pushToken(tokens, TokenKind::EqualEqual, "==", location);
                } else {
                    pushToken(tokens, TokenKind::Equal, "=", location);
                }
                break;
            case '<':
                if (match('=')) {
                    pushToken(tokens, TokenKind::LessEqual, "<=", location);
                } else {
                    pushToken(tokens, TokenKind::Less, "<", location);
                }
                break;
            case '>':
                if (match('=')) {
                    pushToken(tokens, TokenKind::GreaterEqual, ">=", location);
                } else {
                    pushToken(tokens, TokenKind::Greater, ">", location);
                }
                break;
            default:
                pushToken(tokens, TokenKind::Error, std::string{c}, location);
                break;
        }
    }

    tokens.push_back(Token{TokenKind::EndOfFile, "", line_, column_});
    return tokens;
}

[[nodiscard]] auto Lexer::peek() const -> char {
    if (isAtEnd()) {
        return '\0';
    }
    return source_[current_];
}

[[nodiscard]] auto Lexer::peekNext() const -> char {
    if (current_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[current_ + 1];
}

auto Lexer::advance() -> char {
    const char c = source_[current_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

[[nodiscard]] auto Lexer::match(char expected) -> bool {
    if (isAtEnd() || source_[current_] != expected) {
        return false;
    }
    advance();
    return true;
}

[[nodiscard]] auto Lexer::isAtEnd() const -> bool { return current_ >= source_.size(); }

void Lexer::skipTrivia() {
    bool consumed = true;
    while (consumed && !isAtEnd()) {
        consumed = false;
        switch (peek()) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                advance();
                consumed = true;
                break;
            case '/':
                if (peekNext() == '/') {
                    while (!isAtEnd() && peek() != '\n') {
                        advance();
                    }
                    consumed = true;
                } else if (peekNext() == '*') {
                    advance();  // '/'
                    advance();  // '*'
                    while (!isAtEnd()) {
                        if (peek() == '*' && peekNext() == '/') {
                            advance();
                            advance();
                            break;
                        }
                        advance();
                    }
                    consumed = true;
                }
                break;
            default:
                break;
        }
    }
}

void Lexer::lexIdentifier(std::vector<Token>& tokens, SourceLocation location, size_t startIndex) {
    while (isIdentifierBody(peek())) {
        advance();
    }
    auto lexeme = slice(startIndex);
    const TokenKind kind = classifyIdentifier(lexeme);
    pushToken(tokens, kind, std::move(lexeme), location);
}

void Lexer::lexNumber(std::vector<Token>& tokens, SourceLocation location, size_t startIndex) {
    const auto consumeDigits = [this]() {
        while (!isAtEnd()) {
            const char next = peek();
            if (isDigit(next) || next == '_') {
                advance();
            } else {
                break;
            }
        }
    };

    consumeDigits();
    bool isFloat = false;

    if (peek() == '.' && isDigit(peekNext())) {
        isFloat = true;
        advance();  // consume '.'
        consumeDigits();
    }

    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }

        const size_t exponentDigitsStart = current_;
        consumeDigits();

        if (exponentDigitsStart == current_) {
            pushToken(tokens, TokenKind::Error, "Invalid exponent", location);
            return;
        }
    }

    auto lexeme = slice(startIndex);
    const TokenKind kind = isFloat ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral;
    pushToken(tokens, kind, std::move(lexeme), location);
}

void Lexer::lexString(std::vector<Token>& tokens, SourceLocation location) {
    std::string value;
    value.reserve(16);
    bool terminated = false;

    while (!isAtEnd()) {
        const char c = advance();
        if (c == '"') {
            terminated = true;
            break;
        }

        if (c == '\\') {
            if (isAtEnd()) {
                break;
            }
            const char escape = advance();
            switch (escape) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                default:
                    value.push_back(escape);
                    break;
            }
            continue;
        }

        value.push_back(c);
    }

    if (!terminated) {
        pushToken(tokens, TokenKind::Error, "Unterminated string literal", location);
        return;
    }

    pushToken(tokens, TokenKind::StringLiteral, std::move(value), location);
}

void Lexer::pushToken(std::vector<Token>& tokens, TokenKind kind, std::string lexeme, SourceLocation location) {
    tokens.push_back(Token{kind, std::move(lexeme), location.line, location.column});
}

[[nodiscard]] auto Lexer::slice(size_t startIndex) const -> std::string {
    return source_.substr(startIndex, current_ - startIndex);
}

}  // namespace impulse::frontend
