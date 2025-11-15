#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "../frontend/include/impulse/frontend/lexer.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/semantic.h"

using impulse::frontend::Lexer;
using impulse::frontend::ParseResult;
using impulse::frontend::Parser;
using impulse::frontend::BindingKind;
using impulse::frontend::Token;
using impulse::frontend::TokenKind;
using impulse::frontend::Declaration;

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

void testStructDeclaration() {
    const std::string source = R"(module demo;
struct Vec2 {
    x: float;
    y: float;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.declarations.size() == 1);

    const auto& decl = result.module.declarations.front();
    assert(decl.kind == Declaration::Kind::Struct);
    const auto& structure = decl.structure;
    assert(structure.name.value == "Vec2");
    assert(structure.fields.size() == 2);
    assert(structure.fields[0].name.value == "x");
    assert(structure.fields[0].type_name.value == "float");
    assert(structure.fields[1].name.value == "y");
    assert(structure.fields[1].type_name.value == "float");
}

void testInterfaceDeclaration() {
    const std::string source = R"(module demo;
interface Display {
    func toString(self: string) -> string;
    func reset(ctx: Display);
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.declarations.size() == 1);

    const auto& decl = result.module.declarations.front();
    assert(decl.kind == Declaration::Kind::Interface);
    const auto& interfaceDecl = decl.interface_decl;
    assert(interfaceDecl.name.value == "Display");
    assert(interfaceDecl.methods.size() == 2);
    assert(interfaceDecl.methods[0].name.value == "toString");
    assert(interfaceDecl.methods[0].parameters.size() == 1);
    assert(interfaceDecl.methods[0].parameters[0].type_name.value == "string");
    assert(interfaceDecl.methods[0].return_type.has_value());
    assert(interfaceDecl.methods[0].return_type->value == "string");
    assert(!interfaceDecl.methods[1].return_type.has_value());
}

void testStructDiagnostics() {
    const std::string source = R"(module demo;
struct Broken {
    x: int
)
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(!result.success);
    assert(!result.diagnostics.empty());
}

void testEmitIrText() {
    const std::string source = R"(module demo;
func main() -> int {
    return 0;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("^entry") != std::string::npos);
    assert(irText.find("return") != std::string::npos || irText.find('#') != std::string::npos);
}

void testInterfaceIrEmission() {
    const std::string source = R"(module demo;
interface Display {
    func show(self: Display) -> string;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("interface Display") != std::string::npos);
    assert(irText.find("func show") != std::string::npos);
}

void testExportedDeclarations() {
    const std::string source = R"(module demo;
export let value: int = 1;
export func main() -> int {
    return value;
}
export struct Point {
    x: int;
}
export interface Display {
    func show(self: Display);
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.declarations.size() == 4);
    for (const auto& decl : result.module.declarations) {
        assert(decl.exported);
    }

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("export let value") != std::string::npos);
    assert(irText.find("export func main") != std::string::npos);
    assert(irText.find("export struct Point") != std::string::npos);
    assert(irText.find("export interface Display") != std::string::npos);
}

void testSemanticDuplicates() {
    const std::string source = R"(module demo;
let value: int = 1;
let value: int = 2;
struct Vec2 {
    x: float;
    x: float;
}
interface Display {
    func show(self: Display);
    func show(self: Display);
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    assert(!semantic.diagnostics.empty());
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
    testStructDeclaration();
    testInterfaceDeclaration();
    testStructDiagnostics();
    testEmitIrText();
    testInterfaceIrEmission();
    testExportedDeclarations();
    testSemanticDuplicates();

    std::cout << "All frontend tests passed\n";
    return 0;
}
