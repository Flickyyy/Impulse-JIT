#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

#include "../frontend/include/impulse/frontend/lexer.h"

using impulse::frontend::Lexer;
using impulse::frontend::Token;
using impulse::frontend::TokenKind;

namespace {

void expectKinds(const std::string& source, const std::vector<TokenKind>& expected) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();

    std::vector<TokenKind> actual;
    actual.reserve(tokens.size());
    for (const auto& token : tokens) {
        actual.push_back(token.kind);
    }

    assert(actual == expected && "Token kinds mismatch");
}

void testModuleHeader() {
    const std::string source = R"(module math::core;
import std::io;
)";

    expectKinds(source,
                {
                    TokenKind::KwModule,
                    TokenKind::Identifier,
                    TokenKind::DoubleColon,
                    TokenKind::Identifier,
                    TokenKind::Semicolon,
                    TokenKind::KwImport,
                    TokenKind::Identifier,
                    TokenKind::DoubleColon,
                    TokenKind::Identifier,
                    TokenKind::Semicolon,
                    TokenKind::EndOfFile,
                });
}

void testNumericLiterals() {
    const std::string source = R"(let radius: float = 3.14;
let tiny: float = 0.5e-2;
let big: int = 1_000_000;
)";

    expectKinds(source,
                {
                    TokenKind::KwLet,      TokenKind::Identifier, TokenKind::Colon,       TokenKind::Identifier,
                    TokenKind::Equal,      TokenKind::FloatLiteral, TokenKind::Semicolon, TokenKind::KwLet,
                    TokenKind::Identifier, TokenKind::Colon,       TokenKind::Identifier, TokenKind::Equal,
                    TokenKind::FloatLiteral, TokenKind::Semicolon, TokenKind::KwLet,      TokenKind::Identifier,
                    TokenKind::Colon,      TokenKind::Identifier, TokenKind::Equal,      TokenKind::IntegerLiteral,
                    TokenKind::Semicolon,  TokenKind::EndOfFile,
                });
}

void testStringsAndComments() {
    const std::string source = R"(// single line ignored
let message: string = "panic\n"; /* keep calm */
panic(message);
)";

    Lexer lexer(source);
    auto tokens = lexer.tokenize();

    std::vector<TokenKind> expectedKinds = {
        TokenKind::KwLet,      TokenKind::Identifier, TokenKind::Colon,      TokenKind::Identifier,
        TokenKind::Equal,      TokenKind::StringLiteral, TokenKind::Semicolon, TokenKind::KwPanic,
        TokenKind::LParen,     TokenKind::Identifier, TokenKind::RParen,     TokenKind::Semicolon,
        TokenKind::EndOfFile,
    };

    std::vector<TokenKind> actualKinds;
    actualKinds.reserve(tokens.size());
    for (const auto& token : tokens) {
        actualKinds.push_back(token.kind);
    }
    assert(actualKinds == expectedKinds && "Kinds mismatch with comments/strings");

    const auto stringIt = std::find_if(tokens.begin(), tokens.end(), [](const Token& token) {
        return token.kind == TokenKind::StringLiteral;
    });
    assert(stringIt != tokens.end());
    assert(stringIt->lexeme == "panic\n");
}

}  // namespace

auto runLexerTests() -> int {
    testModuleHeader();
    testNumericLiterals();
    testStringsAndComments();
    return 3;
}
