#include <cassert>
#include <cmath>
#include <string>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../runtime/include/impulse/runtime/runtime.h"

namespace {

void testControlFlow() {
    const std::string source = R"(module demo;

func test_if() -> int {
    let x: int = 5;
    if x > 3 {
        return 10;
    }
    return 0;
}

func test_else() -> int {
    let x: int = 2;
    if x > 3 {
        return 10;
    } else {
        return 20;
    }
}

func test_while() -> int {
    let x: int = 0;
    while x < 5 {
        let x: int = x + 1;
    }
    return x;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);

    const auto result_if = vm.run("demo", "test_if");
    assert(result_if.status == impulse::runtime::VmStatus::Success);
    assert(result_if.has_value);
    assert(std::abs(result_if.value - 10.0) < 1e-9);

    const auto result_else = vm.run("demo", "test_else");
    assert(result_else.status == impulse::runtime::VmStatus::Success);
    assert(result_else.has_value);
    assert(std::abs(result_else.value - 20.0) < 1e-9);

    impulse::runtime::Vm vm2;
    assert(vm2.load(lowered).success);
    const auto result_while = vm2.run("demo", "test_while");
    assert(result_while.status == impulse::runtime::VmStatus::Success);
    assert(result_while.has_value);
    assert(std::abs(result_while.value - 5.0) < 1e-9);
}

void testForLoop() {
    const std::string source = R"(module demo;

func main() -> int {
    let sum: int = 0;
    for (let i: int = 0; i < 5; let i: int = i + 1;) {
        let sum: int = sum + i;
    }

    for (; sum < 20; let sum: int = sum + 5;) {
    }

    return sum;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);

    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 20.0) < 1e-9);
}

void testBreakContinueWhile() {
    const std::string source = R"(module demo;

func main() -> int {
    let i: int = 0;
    let sum: int = 0;
    while i < 10 {
        let i: int = i + 1;
        if i == 3 {
            continue;
        }
        if i == 6 {
            break;
        }
        let sum: int = sum + i;
    }
    return sum;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);

    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 12.0) < 1e-9);
}

void testBreakContinueFor() {
    const std::string source = R"(module demo;

func main() -> int {
    let total: int = 0;
    for (let i: int = 0; i < 5; let i: int = i + 1;) {
        if i % 2 == 0 {
            continue;
        }
        if i == 3 {
            break;
        }
        let total: int = total + i;
    }
    return total;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    assert(loadResult.success);

    const auto result = vm.run("demo", "main");
    assert(result.status == impulse::runtime::VmStatus::Success);
    assert(result.has_value);
    assert(std::abs(result.value - 1.0) < 1e-9);
}

}  // namespace

auto runControlFlowTests() -> int {
    testControlFlow();
    testForLoop();
    testBreakContinueWhile();
    testBreakContinueFor();
    return 4;
}
