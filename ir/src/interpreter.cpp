#include "impulse/ir/interpreter.h"

#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>
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
    if (operand == "true") {
        return 1.0;
    }
    if (operand == "false") {
        return 0.0;
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

[[nodiscard]] auto make_function_error(std::string message) -> FunctionEvalResult {
    return FunctionEvalResult{
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
                    } else if (op == "%") {
                        if (std::abs(right) < kEpsilon) {
                            return make_error("modulo by zero during interpretation");
                        }
                        const int leftInt = static_cast<int>(left);
                        const int rightInt = static_cast<int>(right);
                        stack.push_back(static_cast<double>(leftInt % rightInt));
                    } else if (op == "==") {
                        stack.push_back((left == right) ? 1.0 : 0.0);
                    } else if (op == "!=") {
                        stack.push_back((left != right) ? 1.0 : 0.0);
                    } else if (op == "<") {
                        stack.push_back((left < right) ? 1.0 : 0.0);
                    } else if (op == "<=") {
                        stack.push_back((left <= right) ? 1.0 : 0.0);
                    } else if (op == ">") {
                        stack.push_back((left > right) ? 1.0 : 0.0);
                    } else if (op == ">=") {
                        stack.push_back((left >= right) ? 1.0 : 0.0);
                    } else if (op == "&&") {
                        stack.push_back((left != 0.0 && right != 0.0) ? 1.0 : 0.0);
                    } else if (op == "||") {
                        stack.push_back((left != 0.0 || right != 0.0) ? 1.0 : 0.0);
                    } else {
                        return make_error("unsupported binary operator '" + op + "'");
                    }
                break;
            }
            case InstructionKind::Unary: {
                if (inst.operands.empty()) {
                    return make_error("unary instruction missing operator");
                }
                if (stack.empty()) {
                    return make_error("unary instruction requires one operand");
                }
                const double operand = stack.back();
                stack.pop_back();
                const std::string& op = inst.operands.front();
                if (op == "!") {
                    stack.push_back((operand == 0.0) ? 1.0 : 0.0);
                } else if (op == "-") {
                    stack.push_back(-operand);
                } else {
                    return make_error("unsupported unary operator '" + op + "'");
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

auto interpret_function(const Function& function, const std::unordered_map<std::string, double>& environment,
                        const std::unordered_map<std::string, double>& parameters) -> FunctionEvalResult {
    if (function.blocks.empty()) {
        return FunctionEvalResult{
            .status = EvalStatus::NonConstant,
            .value = std::nullopt,
            .message = "function has no basic blocks",
        };
    }

    std::vector<double> stack;
    std::unordered_map<std::string, double> locals;
    
    std::vector<Instruction> all_instructions;
    for (const auto& block : function.blocks) {
        all_instructions.insert(all_instructions.end(), block.instructions.begin(), block.instructions.end());
    }
    
    std::unordered_map<std::string, size_t> labels;
    for (size_t i = 0; i < all_instructions.size(); ++i) {
        if (all_instructions[i].kind == InstructionKind::Label && !all_instructions[i].operands.empty()) {
            labels[all_instructions[i].operands.front()] = i;
        }
    }
    
    size_t pc = 0;
    while (pc < all_instructions.size()) {
        const auto& inst = all_instructions[pc];
        
        switch (inst.kind) {
            case InstructionKind::Literal: {
                if (inst.operands.empty()) {
                    return make_function_error("literal instruction missing operand");
                }
                const auto parsed = parse_literal(inst.operands.front());
                if (!parsed.has_value()) {
                    return make_function_error("unable to parse literal operand '" + inst.operands.front() + "'");
                }
                stack.push_back(*parsed);
                break;
            }
            case InstructionKind::Reference: {
                    if (inst.operands.empty()) {
                        return make_function_error("reference instruction missing operand");
                    }
                    const std::string& name = inst.operands.front();
                    const auto localIt = locals.find(name);
                    if (localIt != locals.end()) {
                        stack.push_back(localIt->second);
                        break;
                    }
                    const auto paramIt = parameters.find(name);
                    if (paramIt != parameters.end()) {
                        stack.push_back(paramIt->second);
                        break;
                    }
                    const auto envIt = environment.find(name);
                    if (envIt == environment.end()) {
                        return FunctionEvalResult{
                            .status = EvalStatus::NonConstant,
                            .value = std::nullopt,
                            .message = "reference to unknown symbol '" + name + "'",
                        };
                    }
                    stack.push_back(envIt->second);
                    break;
                }
                case InstructionKind::Binary: {
                    if (inst.operands.empty()) {
                        return make_function_error("binary instruction missing operator");
                    }
                    if (stack.size() < 2) {
                        return make_function_error("binary instruction requires two operands");
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
                            return make_function_error("division by zero during interpretation");
                        }
                        stack.push_back(left / right);
                    } else if (op == "%") {
                        if (std::abs(right) < kEpsilon) {
                            return make_function_error("modulo by zero during interpretation");
                        }
                        const int leftInt = static_cast<int>(left);
                        const int rightInt = static_cast<int>(right);
                        stack.push_back(static_cast<double>(leftInt % rightInt));
                    } else if (op == "==") {
                        stack.push_back((left == right) ? 1.0 : 0.0);
                    } else if (op == "!=") {
                        stack.push_back((left != right) ? 1.0 : 0.0);
                    } else if (op == "<") {
                        stack.push_back((left < right) ? 1.0 : 0.0);
                    } else if (op == "<=") {
                        stack.push_back((left <= right) ? 1.0 : 0.0);
                    } else if (op == ">") {
                        stack.push_back((left > right) ? 1.0 : 0.0);
                    } else if (op == ">=") {
                        stack.push_back((left >= right) ? 1.0 : 0.0);
                    } else if (op == "&&") {
                        stack.push_back((left != 0.0 && right != 0.0) ? 1.0 : 0.0);
                    } else if (op == "||") {
                        stack.push_back((left != 0.0 || right != 0.0) ? 1.0 : 0.0);
                    } else {
                        return make_function_error("unsupported binary operator '" + op + "'");
                    }
                    break;
                }
                case InstructionKind::Unary: {
                    if (inst.operands.empty()) {
                        return make_function_error("unary instruction missing operator");
                    }
                    if (stack.empty()) {
                        return make_function_error("unary instruction requires one operand");
                    }
                    const double operand = stack.back();
                    stack.pop_back();
                    const std::string& op = inst.operands.front();
                    if (op == "!") {
                        stack.push_back((operand == 0.0) ? 1.0 : 0.0);
                    } else if (op == "-") {
                        stack.push_back(-operand);
                    } else {
                        return make_function_error("unsupported unary operator '" + op + "'");
                    }
                    break;
                }
                case InstructionKind::Return: {
                    if (stack.empty()) {
                        return make_function_error("return instruction requires a value");
                    }
                    const double value = stack.back();
                    stack.pop_back();
                    return FunctionEvalResult{
                        .status = EvalStatus::Success,
                        .value = value,
                        .message = inst.operands.empty() ? std::string{} : inst.operands.front(),
                    };
                }
                case InstructionKind::Store: {
                    if (inst.operands.empty()) {
                        return make_function_error("store instruction missing operand");
                    }
                    if (stack.empty()) {
                        return make_function_error("store instruction requires a value");
                    }
                    const double value = stack.back();
                    stack.pop_back();
                    locals[inst.operands.front()] = value;
                    break;
                }
                case InstructionKind::Branch: {
                    if (inst.operands.empty()) {
                        return make_function_error("branch instruction missing label");
                    }
                    const auto it = labels.find(inst.operands.front());
                    if (it == labels.end()) {
                        return make_function_error("branch to undefined label '" + inst.operands.front() + "'");
                    }
                    pc = it->second;
                    continue;
                }
                case InstructionKind::BranchIf: {
                    if (inst.operands.size() < 2) {
                        return make_function_error("branch_if instruction requires label and condition");
                    }
                    if (stack.empty()) {
                        return make_function_error("branch_if requires a condition on stack");
                    }
                    const double condition = stack.back();
                    stack.pop_back();
                    const double compare_val = std::stod(inst.operands[1]);
                    if (std::abs(condition - compare_val) < kEpsilon) {
                        const auto it = labels.find(inst.operands.front());
                        if (it == labels.end()) {
                            return make_function_error("branch_if to undefined label '" + inst.operands.front() + "'");
                        }
                        pc = it->second;
                        continue;
                    }
                    break;
                }
                case InstructionKind::Label:
                case InstructionKind::Comment:
                    break;
            }
        ++pc;
    }

    return FunctionEvalResult{
        .status = EvalStatus::NonConstant,
        .value = std::nullopt,
        .message = "no return encountered",
    };
}

}  // namespace impulse::ir
