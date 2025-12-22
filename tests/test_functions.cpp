#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include <string>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../runtime/include/impulse/runtime/runtime.h"

TEST(FunctionCallTest, FunctionCalls) {
    const std::string source = R"(module demo;

func identity(x: int) -> int {
    return x;
}

func add(a: int, b: int) -> int {
    return a + b;
}

func multiply(x: int, y: int) -> int {
    return x * y;
}

func test_identity() -> int {
    return identity(42);
}

func test_add() -> int {
    return add(10, 20);
}

func test_multiply() -> int {
    return multiply(5, 7);
}

func test_nested() -> int {
    return add(multiply(2, 3), identity(4));
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result_identity = vm.run("demo", "test_identity");
    if (result_identity.status != impulse::runtime::VmStatus::Success) {
        std::cout << "test_identity status=" << static_cast<int>(result_identity.status)
                  << " message='" << result_identity.message << "'\n";
    }
    EXPECT_EQ(result_identity.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result_identity.has_value);
    EXPECT_LT(std::abs(result_identity.value - 42.0), 1e-9);

    const auto result_add = vm.run("demo", "test_add");
    if (result_add.status != impulse::runtime::VmStatus::Success) {
        std::cout << "test_add status=" << static_cast<int>(result_add.status)
                  << " message='" << result_add.message << "'\n";
    }
    EXPECT_EQ(result_add.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result_add.has_value);
    EXPECT_LT(std::abs(result_add.value - 30.0), 1e-9);

    const auto result_multiply = vm.run("demo", "test_multiply");
    if (result_multiply.status != impulse::runtime::VmStatus::Success) {
        std::cout << "test_multiply status=" << static_cast<int>(result_multiply.status)
                  << " message='" << result_multiply.message << "'\n";
    }
    EXPECT_EQ(result_multiply.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result_multiply.has_value);
    EXPECT_LT(std::abs(result_multiply.value - 35.0), 1e-9);

    const auto result_nested = vm.run("demo", "test_nested");
    if (result_nested.status != impulse::runtime::VmStatus::Success) {
        std::cout << "test_nested status=" << static_cast<int>(result_nested.status)
                  << " message='" << result_nested.message << "'\n";
    }
    EXPECT_EQ(result_nested.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result_nested.has_value);
    EXPECT_LT(std::abs(result_nested.value - 10.0), 1e-9);
}

TEST(FunctionCallTest, Recursion) {
    const std::string source = R"(module demo;

func fact(n: int) -> int {
    if n <= 1 {
        return 1;
    } else {
        return n * fact(n - 1);
    }
}

func main() -> int {
    return fact(5);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value - 120.0), 1e-9);
}
