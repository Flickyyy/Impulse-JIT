#include <gtest/gtest.h>
#include <string>

#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"

using impulse::frontend::BindingKind;
using impulse::frontend::Declaration;
using impulse::frontend::ParseResult;
using impulse::frontend::Parser;

TEST(SemanticTest, ConstAndVarBindings) {
    const std::string source = R"(module demo;
const PI: float = 3.14;
var counter: int = 0;
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.module.declarations.size(), 2);

    const auto& constDecl = result.module.declarations[0].binding;
    EXPECT_EQ(constDecl.kind, BindingKind::Const);
    EXPECT_EQ(constDecl.name.value, "PI");
    EXPECT_EQ(constDecl.type_name.value, "float");

    const auto& varDecl = result.module.declarations[1].binding;
    EXPECT_EQ(varDecl.kind, BindingKind::Var);
    EXPECT_EQ(varDecl.name.value, "counter");
    EXPECT_EQ(varDecl.type_name.value, "int");
}

TEST(SemanticTest, FunctionDeclaration) {
    const std::string source = R"(module demo;
func add(a: int, b: int) -> int {
    return a + b;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.module.declarations.size(), 1);

    const auto& decl = result.module.declarations.front();
    EXPECT_EQ(decl.kind, Declaration::Kind::Function);
    const auto& func = decl.function;
    EXPECT_EQ(func.name.value, "add");
    EXPECT_EQ(func.parameters.size(), 2);
    EXPECT_EQ(func.parameters[0].name.value, "a");
    EXPECT_EQ(func.parameters[0].type_name.value, "int");
    ASSERT_TRUE(func.return_type.has_value());
    EXPECT_EQ(func.return_type->value, "int");
    EXPECT_NE(func.body.text.find("return"), std::string::npos);
}

TEST(SemanticTest, StructDeclaration) {
    const std::string source = R"(module demo;
struct Vec2 {
    x: float;
    y: float;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.module.declarations.size(), 1);

    const auto& decl = result.module.declarations.front();
    EXPECT_EQ(decl.kind, Declaration::Kind::Struct);
    const auto& structure = decl.structure;
    EXPECT_EQ(structure.name.value, "Vec2");
    EXPECT_EQ(structure.fields.size(), 2);
    EXPECT_EQ(structure.fields[0].name.value, "x");
    EXPECT_EQ(structure.fields[0].type_name.value, "float");
    EXPECT_EQ(structure.fields[1].name.value, "y");
    EXPECT_EQ(structure.fields[1].type_name.value, "float");
}

TEST(SemanticTest, InterfaceDeclaration) {
    const std::string source = R"(module demo;
interface Display {
    func toString(self: string) -> string;
    func reset(ctx: Display);
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.module.declarations.size(), 1);

    const auto& decl = result.module.declarations.front();
    EXPECT_EQ(decl.kind, Declaration::Kind::Interface);
    const auto& interfaceDecl = decl.interface_decl;
    EXPECT_EQ(interfaceDecl.name.value, "Display");
    EXPECT_EQ(interfaceDecl.methods.size(), 2);
    EXPECT_EQ(interfaceDecl.methods[0].name.value, "toString");
    EXPECT_EQ(interfaceDecl.methods[0].parameters.size(), 1);
    EXPECT_EQ(interfaceDecl.methods[0].parameters[0].type_name.value, "string");
    ASSERT_TRUE(interfaceDecl.methods[0].return_type.has_value());
    EXPECT_EQ(interfaceDecl.methods[0].return_type->value, "string");
    EXPECT_FALSE(interfaceDecl.methods[1].return_type.has_value());
}

TEST(SemanticTest, StructDiagnostics) {
    const std::string source = R"(module demo;
struct Broken {
    x: int
)
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(SemanticTest, StructFieldUnknownTypeDiagnostic) {
    const std::string source = R"(module demo;
struct Node {
    next: Missing;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);

    bool foundUnknownType = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Unknown type 'Missing' referenced in struct field") != std::string::npos) {
            foundUnknownType = true;
            break;
        }
    }
    EXPECT_TRUE(foundUnknownType) << "Expected diagnostic for unknown struct field type";
}

TEST(SemanticTest, StructFieldVoidTypeDiagnostic) {
    const std::string source = R"(module demo;
struct Holder {
    value: void;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);

    bool foundVoidField = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Field 'value' cannot have type 'void'") != std::string::npos) {
            foundVoidField = true;
            break;
        }
    }
    EXPECT_TRUE(foundVoidField) << "Expected diagnostic for struct field declared void";
}

TEST(SemanticTest, StructTypeNameConflictWithPrimitive) {
    const std::string source = R"(module demo;
struct int {
    value: int;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);

    bool foundConflict = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Type name 'int' conflicts with existing primitive type 'int'") !=
            std::string::npos) {
            foundConflict = true;
            break;
        }
    }
    EXPECT_TRUE(foundConflict) << "Expected diagnostic for struct name clashing with primitive type";
}

TEST(SemanticTest, InterfaceTypeConflicts) {
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
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);

    bool foundTypeConflict = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Type name 'Display' conflicts with existing struct type 'Display'") !=
            std::string::npos) {
            foundTypeConflict = true;
            break;
        }
    }
    EXPECT_TRUE(foundTypeConflict) << "Expected diagnostic for struct/interface name collision";
}

TEST(SemanticTest, InterfaceTypeResolutionDiagnostics) {
    const std::string source = R"(module demo;
interface Renderer {
    func draw(self: Missing);
    func flush(self: Renderer) -> void;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);

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
    EXPECT_TRUE(foundParamError) << "Expected diagnostic for unknown interface parameter type";
    EXPECT_TRUE(foundVoidReturn) << "Expected diagnostic for explicit void interface return type";
}

TEST(SemanticTest, SemanticDuplicates) {
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
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    EXPECT_FALSE(semantic.diagnostics.empty());
}

TEST(SemanticTest, ImportSemanticDiagnostics) {
    const std::string source = R"(module demo;
import std::io;
import std::io;
import std::fs as fs;
import std::net as fs;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    EXPECT_GE(semantic.diagnostics.size(), 2);
}

TEST(SemanticTest, ConstRequiresConstantInitializer) {
    const std::string source = R"(module demo;
let value: int = 1;
const answer: int = value;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("const binding 'answer'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected diagnostic for non-constant const initializer";
}

TEST(SemanticTest, ConstDivisionByZeroDiagnostic) {
    const std::string source = R"(module demo;
const broken: int = 1 / 0;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Division by zero") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected division-by-zero diagnostic";
}

TEST(SemanticTest, BindingTypeMismatchDiagnostic) {
    const std::string source = R"(module demo;
var text: string = 42;
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Cannot assign expression of type") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected diagnostic for mismatched binding initializer";
}

TEST(SemanticTest, FunctionReturnTypeMismatch) {
    const std::string source = R"(module demo;
func needsBool() -> bool {
    return 1.5;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Cannot return expression of type") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected diagnostic for return type mismatch";
}

TEST(SemanticTest, UnknownIdentifierDiagnostic) {
    const std::string source = R"(module demo;
func mystery() -> int {
    return missing;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Unknown identifier 'missing'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected diagnostic for unknown identifier usage";
}

TEST(SemanticTest, LocalBindingResolvesWithinBlock) {
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

TEST(SemanticTest, BreakOutsideLoopDiagnostic) {
    const std::string source = R"(module demo;
func main() -> int {
    break;
    return 0;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("'break' statement") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected diagnostic for 'break' outside loop";
}

TEST(SemanticTest, ContinueOutsideLoopDiagnostic) {
    const std::string source = R"(module demo;
func main() -> int {
    continue;
    return 0;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);
    bool found = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("'continue' statement") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected diagnostic for 'continue' outside loop";
}

TEST(SemanticTest, LoopControlValid) {
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

TEST(SemanticTest, FunctionMissingReturn) {
    const std::string source = R"(module demo;
func main() -> int {
    let value: int = 42;
}
)";

    Parser parser(source);
    ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_FALSE(semantic.success);

    bool foundDiagnostic = false;
    for (const auto& diag : semantic.diagnostics) {
        if (diag.message.find("Function 'main' may not return a value") != std::string::npos) {
            foundDiagnostic = true;
            break;
        }
    }
    EXPECT_TRUE(foundDiagnostic) << "Expected diagnostic for missing return statement";
}
