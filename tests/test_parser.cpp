#include <cassert>
#include <string>

#include "../frontend/include/impulse/frontend/parser.h"

using impulse::frontend::Parser;
using impulse::frontend::ParseResult;

namespace {

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

}  // namespace

auto runParserTests() -> int {
    testParserHappyPath();
    testParserDiagnostics();
    return 2;
}
