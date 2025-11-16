#include "impulse/ir/interpreter.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace impulse::ir {

namespace {

constexpr double kEpsilon = 1e-12;

[[nodiscard]] auto strip_numeric_separators(std::string_view literal) -> std::string {
    std::string sanitized;
    sanitized.reserve(literal.size());
    for (char ch : literal) {
        if (ch != '_') {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

[[nodiscard]] auto parse_literal(const std::string& operand) -> std::optional<double> {
    if (operand.empty()) {
        return std::nullopt;
    }
    try {
        const std::string sanitized = strip_numeric_separators(operand);
        if (sanitized.empty()) {
            return std::nullopt;
        }
        size_t processed = 0;
        const double value = std::stod(sanitized, &processed);
        if (processed != sanitized.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] auto make_error(std::string message) -> BindingEvalResult {
    return BindingEvalResult{
        .status = EvalStatus::Error,
        .value = std::nullopt,
        .message = std::move(message),
    };
}

}  // namespace

auto interpret_binding(const Binding& binding, const std::unordered_map<std::string, double>& environment)
    -> BindingEvalResult {
    if (binding.initializer_instructions.empty()) {
        return BindingEvalResult{
            .status = EvalStatus::NonConstant,
            .value = std::nullopt,
            .message = "no initializer instructions",
        };
    }

    std::vector<double> stack;
    stack.reserve(binding.initializer_instructions.size());

    for (const auto& inst : binding.initializer_instructions) {
        switch (inst.kind) {
            case InstructionKind::Literal: {
                if (inst.operands.empty()) {
                    return make_error("literal instruction missing operand");
                }
                const auto parsed = parse_literal(inst.operands.front());
                if (!parsed.has_value()) {
                    return make_error("unable to parse literal operand '" + inst.operands.front() + "'");
                }
                stack.push_back(*parsed);
                break;
            }
            case InstructionKind::Reference: {
                if (inst.operands.empty()) {
                    return make_error("reference instruction missing operand");
                }
                const auto it = environment.find(inst.operands.front());
                if (it == environment.end()) {
                    return BindingEvalResult{
                        .status = EvalStatus::NonConstant,
                        .value = std::nullopt,
                        .message = "reference to unknown binding '" + inst.operands.front() + "'",
                    };
                }
                stack.push_back(it->second);
                break;
            }
            case InstructionKind::Binary: {
                if (inst.operands.empty()) {
                    return make_error("binary instruction missing operator");
                }
                if (stack.size() < 2) {
                    return make_error("binary instruction requires two operands");
                }
                const double right = stack.back();
                stack.pop_back();
                const double left = stack.back();
                stack.pop_back();
                const std::string& op = inst.operands.front();
                if (op == "+") {
                    stack.push_back(left + right);
                } else if (op == "-") {
                    stack.push_back(left - right);
                } else if (op == "*") {
                    stack.push_back(left * right);
                } else if (op == "/") {
                    if (std::abs(right) < kEpsilon) {
                        return make_error("division by zero during interpretation");
                    }
                    stack.push_back(left / right);
                } else {
                    return make_error("unsupported binary operator '" + op + "'");
                }
                break;
            }
            case InstructionKind::Store: {
                if (stack.empty()) {
                    return make_error("store instruction requires a value on the stack");
                }
                const double value = stack.back();
                stack.pop_back();
                return BindingEvalResult{
                    .status = EvalStatus::Success,
                    .value = value,
                    .message = inst.operands.empty() ? std::string{} : inst.operands.front(),
                };
            }
            case InstructionKind::Comment:
            case InstructionKind::Return:
                break;
        }
    }

    if (!stack.empty()) {
        return BindingEvalResult{
            .status = EvalStatus::Success,
            .value = stack.back(),
            .message = "implicit result",
        };
    }

    return BindingEvalResult{
        .status = EvalStatus::NonConstant,
        .value = std::nullopt,
        .message = "no store encountered",
    };
}

}  // namespace impulse::ir
