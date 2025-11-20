#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../frontend/include/impulse/frontend/lexer.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../ir/include/impulse/ir/interpreter.h"
#include "../runtime/include/impulse/runtime/runtime.h"

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

void testBindingExpressionLowering() {
    const std::string source = R"(module demo;
let value: int = 1 + 2 * 3;
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    assert(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("let value: int = (1 + (2 * 3));  # = 7") != std::string::npos);
    assert(irText.find("literal 1") != std::string::npos);
    assert(irText.find("literal 2") != std::string::npos);
    assert(irText.find("literal 3") != std::string::npos);
    assert(irText.find("binary +") != std::string::npos);
    assert(irText.find("binary *") != std::string::npos);
    assert(irText.find("store value") != std::string::npos);
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

void testImportSemanticDiagnostics() {
    const std::string source = R"(module demo;
import std::io;
import std::io;
import std::fs as fs;
import std::net as fs;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    assert(semantic.diagnostics.size() >= 2);
}

void testConstRequiresConstantInitializer() {
    const std::string source = R"(module demo;
let value: int = 1;
const answer: int = value;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("const binding 'answer'") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected diagnostic for non-constant const initializer");
}

void testConstDivisionByZeroDiagnostic() {
    const std::string source = R"(module demo;
const broken: int = 1 / 0;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Division by zero") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected division-by-zero diagnostic");
}

void testIrBindingInterpreter() {
    const std::string source = R"(module demo;
let a: int = 2;
let b: int = a * 5 + 3;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    bool foundB = false;

    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
        if (binding.name == "b") {
            assert(eval.status == impulse::ir::EvalStatus::Success);
            assert(eval.value.has_value());
            assert(std::abs(*eval.value - 13.0) < 1e-9);
            foundB = true;
        }
    }

    assert(foundB && "Interpreter did not evaluate binding 'b'");
}

void testRuntimeVmExecution() {
    const std::string source = R"(module demo;
let seed: int = 10;
func main() -> int {
    return seed + 5;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    if (!loadResult.success) {
        for (const auto& diag : loadResult.diagnostics) {
            std::cerr << "runtime load error: " << diag << '\n';
        }
    }
    assert(loadResult.success);

    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 15.0) < 1e-9);
}

void testFunctionLocals() {
    const std::string source = R"(module demo;
let seed: int = 2;
func main() -> int {
    let base: int = seed * 3;
    let extra: int = base + 4;
    return extra;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    std::unordered_map<std::string, double> environment;
    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
    }

    assert(!lowered.functions.empty());
    const auto evalFunction = impulse::ir::interpret_function(lowered.functions.front(), environment, {});
    assert(evalFunction.status == impulse::ir::EvalStatus::Success);
    assert(evalFunction.value.has_value());
    assert(std::abs(*evalFunction.value - 10.0) < 1e-9);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);
    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 10.0) < 1e-9);
}

void testBooleanComparisons() {
    const std::string source = R"(module demo;
let eq: int = true == false;
let neq: int = 1 != 2;
let lt: int = 1 < 2;
let ge: int = 3 >= 2;
func main() -> int {
    return (eq + neq) + (lt + ge) + (1 == 1) + (2 > 3);
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto irText = impulse::frontend::emit_ir_text(parseResult.module);
    assert(irText.find("literal true") != std::string::npos);
    assert(irText.find("binary ==") != std::string::npos);
    assert(irText.find("binary >=") != std::string::npos);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
    }

    assert(!lowered.functions.empty());
    const auto evalFunction = impulse::ir::interpret_function(lowered.functions.front(), environment, {});
    assert(evalFunction.status == impulse::ir::EvalStatus::Success);
    assert(evalFunction.value.has_value());
    assert(std::abs(*evalFunction.value - 4.0) < 1e-9);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);
    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 4.0) < 1e-9);
}

void testModuloOperator() {
    const std::string source = R"(module demo;
let a: int = 10 % 3;
let b: int = 17 % 5;
func main() -> int {
    let c: int = 15 % 4;
    return a + b + c;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto irText = impulse::frontend::emit_ir_text(parseResult.module);
    assert(irText.find("binary %") != std::string::npos);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
    }
    assert(environment["a"] == 1.0);
    assert(environment["b"] == 2.0);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);
    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 6.0) < 1e-9);
}

void testLogicalOperators() {
    const std::string source = R"(module demo;
let and_true: int = true && true;
let and_false: int = true && false;
let or_true: int = false || true;
let or_false: int = false || false;
func main() -> int {
    let a: int = (1 < 2) && (3 > 1);
    let b: int = (1 > 2) || (3 > 1);
    return and_true + and_false + or_true + or_false + a + b;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto irText = impulse::frontend::emit_ir_text(parseResult.module);
    assert(irText.find("binary &&") != std::string::npos);
    assert(irText.find("binary ||") != std::string::npos);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
    }
    assert(environment["and_true"] == 1.0);
    assert(environment["and_false"] == 0.0);
    assert(environment["or_true"] == 1.0);
    assert(environment["or_false"] == 0.0);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);
    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 4.0) < 1e-9);
}

void testUnaryOperators() {
    const std::string source = R"(module demo;
let not_true: int = !true;
let not_false: int = !false;
let neg_five: int = -5;
let neg_expr: int = -(3 + 2);
func main() -> int {
    let a: int = !0;
    let b: int = !(1 > 2);
    let c: int = -10;
    return not_true + not_false + neg_five + neg_expr + a + b + c;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto irText = impulse::frontend::emit_ir_text(parseResult.module);
    assert(irText.find("unary !") != std::string::npos);
    assert(irText.find("unary -") != std::string::npos);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
    }
    assert(environment["not_true"] == 0.0);
    assert(environment["not_false"] == 1.0);
    assert(environment["neg_five"] == -5.0);
    assert(environment["neg_expr"] == -5.0);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);
    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - (-17.0)) < 1e-9);
}

void testOperatorPrecedence() {
    const std::string source = R"(module demo;
let a: int = 2 + 3 * 4;
let b: int = 10 % 3 + 2;
let c: int = 1 < 2 && 3 > 1;
let d: int = 0 || 1 && 0;
let e: int = !0 + 1;
let f: int = -2 * 3;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
    }
    
    assert(std::abs(environment["a"] - 14.0) < 1e-9);
    assert(std::abs(environment["b"] - 3.0) < 1e-9);
    assert(std::abs(environment["c"] - 1.0) < 1e-9);
    assert(std::abs(environment["d"] - 0.0) < 1e-9);
    assert(std::abs(environment["e"] - 2.0) < 1e-9);
    assert(std::abs(environment["f"] - (-6.0)) < 1e-9);
}

void testModuloByZero() {
    const std::string source = R"(module demo;
const broken: int = 10 % 0;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("odulo by zero") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected modulo-by-zero diagnostic");
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
    testBindingExpressionLowering();
    testExportedDeclarations();
    testSemanticDuplicates();
    testImportSemanticDiagnostics();
    testConstRequiresConstantInitializer();
    testConstDivisionByZeroDiagnostic();
    testIrBindingInterpreter();
    testRuntimeVmExecution();
    testFunctionLocals();
    testBooleanComparisons();
    testModuloOperator();
    testLogicalOperators();
    testUnaryOperators();
    testOperatorPrecedence();
    testModuloByZero();

    std::cout << "All frontend tests passed\n";
    return 0;
}
