#include <gtest/gtest.h>
#include <string>

#include "../frontend/include/impulse/frontend/parser.h"

using impulse::frontend::Parser;
using impulse::frontend::ParseResult;

TEST(ParserTest, HappyPath) {
    const std::string source = R"(module math::core;
import std::io as io;

let value: int = 1 + 2;
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.module.decl.path.size(), 2);
    EXPECT_EQ(result.module.imports.size(), 1);
    EXPECT_EQ(result.module.declarations.size(), 1);

    const auto& importDecl = result.module.imports.front();
    EXPECT_EQ(importDecl.path.size(), 2);
    ASSERT_TRUE(importDecl.alias.has_value());
    EXPECT_EQ(importDecl.alias->value, "io");

    const auto& letDecl = result.module.declarations.front().binding;
    EXPECT_EQ(letDecl.name.value, "value");
    EXPECT_EQ(letDecl.type_name.value, "int");
    EXPECT_EQ(letDecl.initializer.text, "1 + 2");
}

TEST(ParserTest, Diagnostics) {
    const std::string source = R"(module bad
let missing: int = 0)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(ParserTest, AssignmentStatement) {
    const std::string source = R"(module test;

func main() -> int {
    let x: int = 5;
    x = x + 1;
    return x;
}
)";

    Parser parser(source);
    ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success) << "Parse should succeed";
    ASSERT_EQ(result.module.declarations.size(), 1);
    
    const auto& func = result.module.declarations[0].function;
    EXPECT_EQ(func.name.value, "main");
    ASSERT_EQ(func.parsed_body.statements.size(), 3);
    
    // Second statement should be assignment
    const auto& assignStmt = func.parsed_body.statements[1];
    EXPECT_EQ(assignStmt.kind, impulse::frontend::Statement::Kind::Assign);
    EXPECT_EQ(assignStmt.assign_target.value, "x");
}
