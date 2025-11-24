#include <cassert>
#include <string>

#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"

using impulse::frontend::BindingKind;
using impulse::frontend::Declaration;
using impulse::frontend::ParseResult;
using impulse::frontend::Parser;

namespace {

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
    assert(decl.kind == Declaration::Kind::Function);
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

void testStructFieldUnknownTypeDiagnostic() {
    const std::string source = R"(module demo;
struct Node {
    next: Missing;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);

    bool foundUnknownType = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Unknown type 'Missing' referenced in struct field") != std::string::npos) {
            foundUnknownType = true;
            break;
        }
    }
    assert(foundUnknownType && "Expected diagnostic for unknown struct field type");
}

void testStructFieldVoidTypeDiagnostic() {
    const std::string source = R"(module demo;
struct Holder {
    value: void;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);

    bool foundVoidField = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Field 'value' cannot have type 'void'") != std::string::npos) {
            foundVoidField = true;
            break;
        }
    }
    assert(foundVoidField && "Expected diagnostic for struct field declared void");
}

void testStructTypeNameConflictWithPrimitive() {
    const std::string source = R"(module demo;
struct int {
    value: int;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);

    bool foundConflict = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Type name 'int' conflicts with existing primitive type 'int'") !=
            std::string::npos) {
            foundConflict = true;
            break;
        }
    }
    assert(foundConflict && "Expected diagnostic for struct name clashing with primitive type");
}

void testInterfaceTypeConflicts() {
    const std::string source = R"(module demo;
struct Display {
    value: int;
}
interface Display {
    func show(self: Display);
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);

    bool foundTypeConflict = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Type name 'Display' conflicts with existing struct type 'Display'") !=
            std::string::npos) {
            foundTypeConflict = true;
            break;
        }
    }
    assert(foundTypeConflict && "Expected diagnostic for struct/interface name collision");
}

void testInterfaceTypeResolutionDiagnostics() {
    const std::string source = R"(module demo;
interface Renderer {
    func draw(self: Missing);
    func flush(self: Renderer) -> void;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);

    bool foundParamError = false;
    bool foundVoidReturn = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Unknown type 'Missing' referenced in interface method parameter") !=
            std::string::npos) {
            foundParamError = true;
        }
        if (diag.message.find("Interface method cannot explicitly return 'void'") != std::string::npos) {
            foundVoidReturn = true;
        }
    }
    assert(foundParamError && "Expected diagnostic for unknown interface parameter type");
    assert(foundVoidReturn && "Expected diagnostic for explicit void interface return type");
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

void testBindingTypeMismatchDiagnostic() {
    const std::string source = R"(module demo;
var text: string = 42;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Cannot assign expression of type") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected diagnostic for mismatched binding initializer");
}

void testFunctionReturnTypeMismatch() {
    const std::string source = R"(module demo;
func needsBool() -> bool {
    return 1.5;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Cannot return expression of type") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected diagnostic for return type mismatch");
}

void testUnknownIdentifierDiagnostic() {
    const std::string source = R"(module demo;
func mystery() -> int {
    return missing;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Unknown identifier 'missing'") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected diagnostic for unknown identifier usage");
}

void testLocalBindingResolvesWithinBlock() {
    const std::string source = R"(module demo;
func builder() -> int {
    let base: int = 2;
    if (base) {
        let base: int = 5;
        return base;
    }
    return 0;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(semantic.success);
    assert(semantic.diagnostics.empty());
}

void testBreakOutsideLoopDiagnostic() {
    const std::string source = R"(module demo;
func main() -> int {
    break;
    return 0;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("'break' statement") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected diagnostic for 'break' outside loop");
}

void testContinueOutsideLoopDiagnostic() {
    const std::string source = R"(module demo;
func main() -> int {
    continue;
    return 0;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("'continue' statement") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found && "Expected diagnostic for 'continue' outside loop");
}

void testLoopControlValid() {
    const std::string source = R"(module demo;
func main() -> int {
    while (true) {
        break;
    }
    for (; true; ) {
        if (true) {
            continue;
        }
        break;
    }
    return 0;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(semantic.success);
    assert(semantic.diagnostics.empty());
}

void testFunctionMissingReturn() {
    const std::string source = R"(module demo;
func main() -> int {
    let value: int = 42;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    assert(!semantic.success);

    bool foundDiagnostic = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Function 'main' may not return a value") != std::string::npos) {
            foundDiagnostic = true;
            break;
        }
    }
    assert(foundDiagnostic && "Expected diagnostic for missing return statement");
}

}  // namespace

auto runSemanticTests() -> int {
    testConstAndVarBindings();
    testFunctionDeclaration();
    testStructDeclaration();
    testInterfaceDeclaration();
    testStructDiagnostics();
    testStructFieldUnknownTypeDiagnostic();
    testStructFieldVoidTypeDiagnostic();
    testStructTypeNameConflictWithPrimitive();
    testInterfaceTypeConflicts();
    testInterfaceTypeResolutionDiagnostics();
    testSemanticDuplicates();
    testImportSemanticDiagnostics();
    testConstRequiresConstantInitializer();
    testConstDivisionByZeroDiagnostic();
    testBindingTypeMismatchDiagnostic();
    testFunctionReturnTypeMismatch();
    testUnknownIdentifierDiagnostic();
    testLocalBindingResolvesWithinBlock();
    testBreakOutsideLoopDiagnostic();
    testContinueOutsideLoopDiagnostic();
    testLoopControlValid();
    testFunctionMissingReturn();
    return 22;
}
