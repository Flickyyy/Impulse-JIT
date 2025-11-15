#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "../frontend/include/impulse/frontend/lexer.h"
#include "../frontend/include/impulse/frontend/parser.h"

using impulse::frontend::Lexer;
using impulse::frontend::ParseResult;
using impulse::frontend::Parser;
using impulse::frontend::BindingKind;
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

void testParserHappyPath() {
    const std::string source = R"(module math::core;
import std::io as io;

let value: int = 1 + 2;
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.decl.path.size() == 2);
    assert(result.module.imports.size() == 1);
    assert(result.module.declarations.size() == 1);

    const auto& importDecl = result.module.imports.front();
    assert(importDecl.path.size() == 2);
    assert(importDecl.alias.has_value());
    assert(importDecl.alias->value == "io");

    const auto& letDecl = result.module.declarations.front().binding;
    assert(letDecl.name.value == "value");
    assert(letDecl.type_name.value == "int");
    assert(letDecl.initializer.text == "1 + 2");
}

void testParserDiagnostics() {
    const std::string source = R"(module bad
let missing: int = 0)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(!result.success);
    assert(!result.diagnostics.empty());
}

void testConstAndVarBindings() {
    const std::string source = R"(module demo;
const PI: float = 3.14;
var counter: int = 0;
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.declarations.size() == 2);

    const auto& constDecl = result.module.declarations[0].binding;
    assert(constDecl.kind == BindingKind::Const);
    assert(constDecl.name.value == "PI");
    assert(constDecl.type_name.value == "float");

    const auto& varDecl = result.module.declarations[1].binding;
    assert(varDecl.kind == BindingKind::Var);
    assert(varDecl.name.value == "counter");
    assert(varDecl.type_name.value == "int");
}

void testFunctionDeclaration() {
    const std::string source = R"(module demo;
func add(a: int, b: int) -> int {
    return a + b;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.declarations.size() == 1);

    const auto& decl = result.module.declarations.front();
    assert(decl.kind == impulse::frontend::Declaration::Kind::Function);
    const auto& func = decl.function;
    assert(func.name.value == "add");
    assert(func.parameters.size() == 2);
    assert(func.parameters[0].name.value == "a");
    assert(func.parameters[0].type_name.value == "int");
    assert(func.return_type.has_value());
    assert(func.return_type->value == "int");
    assert(func.body.text.find("return") != std::string::npos);
}

}  // namespace

auto main() -> int {
    testModuleHeader();
    testNumericLiterals();
    testStringsAndComments();
    testParserHappyPath();
    testParserDiagnostics();
    testConstAndVarBindings();
    testFunctionDeclaration();

    std::cout << "All frontend tests passed\n";
    return 0;
}
