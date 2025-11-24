#include <cassert>
#include <cmath>
#include <string>
#include <unordered_map>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../ir/include/impulse/ir/interpreter.h"
#include "../runtime/include/impulse/runtime/runtime.h"

namespace {

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

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
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

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
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

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
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

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
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

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
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

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
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

auto runOperatorTests() -> int {
    testBooleanComparisons();
    testModuloOperator();
    testLogicalOperators();
    testUnaryOperators();
    testOperatorPrecedence();
    testModuloByZero();
    return 6;
}
