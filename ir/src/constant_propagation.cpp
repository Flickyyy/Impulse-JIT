#include "impulse/ir/constant_propagation.h"

#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace impulse::ir {
namespace {

enum class ConstState : std::uint8_t {
    Unknown,
    Constant,
    NonConstant,
};

struct ConstInfo {
    ConstState state = ConstState::Unknown;
    std::string value;
};

[[nodiscard]] auto make_constant(std::string value) -> ConstInfo {
    return ConstInfo{ConstState::Constant, std::move(value)};
}

[[nodiscard]] auto make_non_constant() -> ConstInfo {
    return ConstInfo{ConstState::NonConstant, {}};
}

[[nodiscard]] auto merge(const ConstInfo& current, const ConstInfo& incoming) -> std::pair<ConstInfo, bool> {
    if (incoming.state == ConstState::Unknown) {
        return {current, false};
    }

    if (current.state == ConstState::Unknown) {
        return {incoming, true};
    }

    if (current.state == ConstState::NonConstant) {
        return {current, false};
    }

    if (incoming.state == ConstState::NonConstant) {
        if (current.state == ConstState::NonConstant) {
            return {current, false};
        }
        return {make_non_constant(), true};
    }

    if (incoming.state == ConstState::Constant && current.state == ConstState::Constant) {
        if (incoming.value == current.value) {
            return {current, false};
        }
        return {make_non_constant(), true};
    }

    return {current, false};
}

[[nodiscard]] auto strip_numeric_separators(const std::string& literal) -> std::string {
    std::string sanitized;
    sanitized.reserve(literal.size());
    for (char ch : literal) {
        if (ch != '_') {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

[[nodiscard]] auto parse_literal(const std::string& literal) -> std::optional<double> {
    if (literal.empty()) {
        return std::nullopt;
    }
    if (literal == "true") {
        return 1.0;
    }
    if (literal == "false") {
        return 0.0;
    }
    try {
        const std::string sanitized = strip_numeric_separators(literal);
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

[[nodiscard]] auto format_constant(double value) -> std::string {
    if (!std::isfinite(value)) {
        return {};
    }
    const double rounded = std::round(value);
    if (std::abs(value - rounded) < 1e-9) {
        return std::to_string(static_cast<long long>(rounded));
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] auto evaluate_unary(const std::string& op, double operand) -> std::optional<std::string> {
    if (op == "!") {
        return operand == 0.0 ? std::string{"1"} : std::string{"0"};
    }
    if (op == "-") {
        return format_constant(-operand);
    }
    return std::nullopt;
}

[[nodiscard]] auto evaluate_binary(const std::string& op, double lhs, double rhs) -> std::optional<std::string> {
    constexpr double kEpsilon = 1e-12;
    if (op == "+") {
        return format_constant(lhs + rhs);
    }
    if (op == "-") {
        return format_constant(lhs - rhs);
    }
    if (op == "*") {
        return format_constant(lhs * rhs);
    }
    if (op == "/") {
        if (std::abs(rhs) < kEpsilon) {
            return std::nullopt;
        }
        return format_constant(lhs / rhs);
    }
    if (op == "%") {
        if (std::abs(rhs) < kEpsilon) {
            return std::nullopt;
        }
        const auto leftInt = static_cast<long long>(lhs);
        const auto rightInt = static_cast<long long>(rhs);
        if (rightInt == 0) {
            return std::nullopt;
        }
        return std::to_string(leftInt % rightInt);
    }
    if (op == "==") {
        return lhs == rhs ? std::string{"1"} : std::string{"0"};
    }
    if (op == "!=") {
        return lhs != rhs ? std::string{"1"} : std::string{"0"};
    }
    if (op == "<") {
        return lhs < rhs ? std::string{"1"} : std::string{"0"};
    }
    if (op == "<=") {
        return lhs <= rhs ? std::string{"1"} : std::string{"0"};
    }
    if (op == ">") {
        return lhs > rhs ? std::string{"1"} : std::string{"0"};
    }
    if (op == ">=") {
        return lhs >= rhs ? std::string{"1"} : std::string{"0"};
    }
    if (op == "&&") {
        const bool result = lhs != 0.0 && rhs != 0.0;
        return result ? std::string{"1"} : std::string{"0"};
    }
    if (op == "||") {
        const bool result = lhs != 0.0 || rhs != 0.0;
        return result ? std::string{"1"} : std::string{"0"};
    }
    return std::nullopt;
}

class ConstantPropagator {
public:
    explicit ConstantPropagator(SsaFunction& function) : function_(function) {}

    auto run() -> bool {
        bool mutated = false;
        if (function_.blocks.empty()) {
            return false;
        }

        bool changed = true;
        while (changed) {
            changed = false;

            for (auto& block : function_.blocks) {
                for (auto& phi : block.phi_nodes) {
                    const auto state = evaluate_phi(phi);
                    if (state.state == ConstState::Unknown) {
                        continue;
                    }
                    const auto key = phi.result.to_string();
                    const auto [merged, updated] = merge_state(key, state);
                    if (updated) {
                        changed = true;
                        value_states_[key] = merged;
                    }
                }

                for (auto& inst : block.instructions) {
                    const auto state = evaluate_instruction(inst, mutated);
                    if (state.state == ConstState::Unknown || !inst.result.has_value()) {
                        continue;
                    }
                    const auto key = inst.result->to_string();
                    const auto [merged, updated] = merge_state(key, state);
                    if (updated) {
                        changed = true;
                        value_states_[key] = merged;
                    }
                }
            }
        }

        return mutated;
    }

private:
    [[nodiscard]] auto get_state(const SsaValue& value) const -> ConstInfo {
        const auto key = value.to_string();
        const auto it = value_states_.find(key);
        if (it != value_states_.end()) {
            return it->second;
        }
        return {};
    }

    [[nodiscard]] auto merge_state(const std::string& key, ConstInfo incoming) -> std::pair<ConstInfo, bool> {
        const auto it = value_states_.find(key);
        if (it == value_states_.end()) {
            if (incoming.state == ConstState::Unknown) {
                return {incoming, false};
            }
            value_states_.emplace(key, incoming);
            return {incoming, true};
        }
        const auto [merged, changed] = merge(it->second, incoming);
        return {merged, changed};
    }

    [[nodiscard]] auto evaluate_phi(const PhiNode& phi) const -> ConstInfo {
        if (phi.inputs.empty()) {
            return make_non_constant();
        }

        bool saw_constant = false;
        std::string constant_value;
        for (const auto& input : phi.inputs) {
            if (!input.value.has_value()) {
                return make_non_constant();
            }
            const auto state = get_state(*input.value);
            if (state.state == ConstState::NonConstant) {
                return make_non_constant();
            }
            if (state.state == ConstState::Unknown) {
                return {};
            }
            if (!saw_constant) {
                constant_value = state.value;
                saw_constant = true;
                continue;
            }
            if (constant_value != state.value) {
                return make_non_constant();
            }
        }

        if (!saw_constant) {
            return {};
        }
        return make_constant(constant_value);
    }

    [[nodiscard]] auto evaluate_instruction(SsaInstruction& instruction, bool& mutated) -> ConstInfo {
        const auto& op = instruction.opcode;
        if (op == "literal") {
            if (!instruction.immediates.empty()) {
                return make_constant(instruction.immediates.front());
            }
            return {};
        }
        if (op == "unary") {
            return evaluate_unary_instruction(instruction, mutated);
        }
        if (op == "binary") {
            return evaluate_binary_instruction(instruction, mutated);
        }
        if (op == "assign") {
            if (instruction.arguments.empty() || !instruction.result.has_value()) {
                return make_non_constant();
            }
            return transfer_argument_state(instruction.arguments.front());
        }
        if (op == "call" || op == "array_make" || op == "array_get" || op == "array_set" || op == "array_length") {
            if (instruction.result.has_value()) {
                return make_non_constant();
            }
            return {};
        }
        if (op == "drop" || op == "return" || op == "branch" || op == "branch_if") {
            return {};
        }
        return {};
    }

    [[nodiscard]] auto evaluate_unary_instruction(SsaInstruction& instruction, bool& mutated) -> ConstInfo {
        if (instruction.arguments.empty()) {
            return make_non_constant();
        }
        const auto state = get_state(instruction.arguments.front());
        if (state.state == ConstState::NonConstant) {
            return make_non_constant();
        }
        if (state.state != ConstState::Constant) {
            return {};
        }
        const auto value = parse_literal(state.value);
        if (!value.has_value() || instruction.immediates.empty()) {
            return make_non_constant();
        }
        const auto folded = evaluate_unary(instruction.immediates.front(), *value);
        if (!folded.has_value()) {
            return make_non_constant();
        }
        rewrite_literal(instruction, *folded);
        mutated = true;
        return make_constant(*folded);
    }

    [[nodiscard]] auto evaluate_binary_instruction(SsaInstruction& instruction, bool& mutated) -> ConstInfo {
        if (instruction.arguments.size() < 2 || instruction.immediates.empty()) {
            return make_non_constant();
        }
        const auto lhs_state = get_state(instruction.arguments[0]);
        const auto rhs_state = get_state(instruction.arguments[1]);
        if (lhs_state.state == ConstState::NonConstant || rhs_state.state == ConstState::NonConstant) {
            return make_non_constant();
        }
        if (lhs_state.state != ConstState::Constant || rhs_state.state != ConstState::Constant) {
            return {};
        }
        const auto lhs = parse_literal(lhs_state.value);
        const auto rhs = parse_literal(rhs_state.value);
        if (!lhs.has_value() || !rhs.has_value()) {
            return make_non_constant();
        }
        const auto result = evaluate_binary(instruction.immediates.front(), *lhs, *rhs);
        if (!result.has_value()) {
            return make_non_constant();
        }
        rewrite_literal(instruction, *result);
        mutated = true;
        return make_constant(*result);
    }

    [[nodiscard]] auto transfer_argument_state(const SsaValue& argument) const -> ConstInfo {
        auto state = get_state(argument);
        if (state.state == ConstState::Unknown) {
            return {};
        }
        return state;
    }

    void rewrite_literal(SsaInstruction& instruction, const std::string& value) {
        instruction.opcode = "literal";
        instruction.arguments.clear();
        instruction.immediates.clear();
        instruction.immediates.push_back(value);
    }

    SsaFunction& function_;
    std::unordered_map<std::string, ConstInfo> value_states_;
};

}  // namespace

auto propagate_constants(SsaFunction& function) -> bool {
    ConstantPropagator propagator(function);
    return propagator.run();
}

}  // namespace impulse::ir
