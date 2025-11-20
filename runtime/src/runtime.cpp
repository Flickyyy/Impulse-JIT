#include "impulse/runtime/runtime.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "impulse/ir/interpreter.h"

namespace impulse::runtime {

namespace {

constexpr double kEpsilon = 1e-12;

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

[[nodiscard]] auto make_result(VmStatus status, std::string message) -> VmResult {
    VmResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] auto join_path(const std::vector<std::string>& segments) -> std::string {
    if (segments.empty()) {
        return "<anonymous>";
    }
    std::ostringstream builder;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) {
            builder << "::";
        }
        builder << segments[i];
    }
    return builder.str();
}

}  // namespace

auto Vm::load(ir::Module module) -> VmLoadResult {
    VmLoadResult result;
    LoadedModule loaded;
    loaded.name = normalize_module_name(module);
    loaded.module = std::move(module);

    std::unordered_map<std::string, double> environment;
    for (const auto& binding : loaded.module.bindings) {
        const auto eval = ir::interpret_binding(binding, environment);
        if (eval.status == ir::EvalStatus::Success && eval.value.has_value()) {
            environment[binding.name] = *eval.value;
            loaded.globals[binding.name] = *eval.value;
            continue;
        }

        result.success = false;
        if (!eval.message.empty()) {
            result.diagnostics.push_back("binding '" + binding.name + "': " + eval.message);
        } else {
            result.diagnostics.push_back("binding '" + binding.name + "' failed to evaluate");
        }
    }

    if (result.success) {
        const auto existing = std::find_if(modules_.begin(), modules_.end(), [&](const LoadedModule& candidate) {
            return candidate.name == loaded.name;
        });
        if (existing != modules_.end()) {
            *existing = std::move(loaded);
        } else {
            modules_.push_back(std::move(loaded));
        }
    }

    return result;
}

[[nodiscard]] auto Vm::run(const std::string& module_name, const std::string& entry) const -> VmResult {
    const auto moduleIt = std::find_if(modules_.begin(), modules_.end(), [&](const LoadedModule& module) {
        return module.name == module_name;
    });

    if (moduleIt == modules_.end()) {
        return make_result(VmStatus::MissingSymbol, "module '" + module_name + "' not loaded");
    }

    const auto functionIt = std::find_if(moduleIt->module.functions.begin(), moduleIt->module.functions.end(),
                                         [&](const ir::Function& function) { return function.name == entry; });
    if (functionIt == moduleIt->module.functions.end()) {
        return make_result(VmStatus::MissingSymbol, "function '" + entry + "' not found in module '" + module_name + "'");
    }

    std::unordered_map<std::string, double> parameters;
    for (const auto& parameter : functionIt->parameters) {
        parameters.emplace(parameter.name, 0.0);
    }

    return execute_function(*moduleIt, *functionIt, parameters);
}

[[nodiscard]] auto Vm::execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, double>& parameters) const -> VmResult {
    if (function.blocks.empty()) {
        return make_result(VmStatus::ModuleError, "function has no basic blocks");
    }

    std::vector<double> stack;
    std::unordered_map<std::string, double> locals = parameters;
    
    std::vector<ir::Instruction> all_instructions;
    for (const auto& block : function.blocks) {
        all_instructions.insert(all_instructions.end(), block.instructions.begin(), block.instructions.end());
    }
    
    std::unordered_map<std::string, size_t> labels;
    for (size_t i = 0; i < all_instructions.size(); ++i) {
        if (all_instructions[i].kind == ir::InstructionKind::Label && !all_instructions[i].operands.empty()) {
            labels[all_instructions[i].operands.front()] = i;
        }
    }
    
    size_t pc = 0;
    while (pc < all_instructions.size()) {
        const auto& inst = all_instructions[pc];
        
        switch (inst.kind) {
            case ir::InstructionKind::Literal: {
                if (inst.operands.empty()) {
                    return make_result(VmStatus::ModuleError, "literal instruction missing operand");
                }
                const auto parsed = parse_literal(inst.operands.front());
                if (!parsed.has_value()) {
                    return make_result(VmStatus::ModuleError,
                                       "unable to parse literal operand '" + inst.operands.front() + "'");
                }
                stack.push_back(*parsed);
                break;
                }
                case ir::InstructionKind::Reference: {
                    if (inst.operands.empty()) {
                        return make_result(VmStatus::ModuleError, "reference instruction missing operand");
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
                    const auto globalIt = module.globals.find(name);
                    if (globalIt == module.globals.end()) {
                        return make_result(VmStatus::MissingSymbol, "reference to unknown symbol '" + name + "'");
                    }
                    stack.push_back(globalIt->second);
                    break;
                }
                case ir::InstructionKind::Binary: {
                    if (inst.operands.empty()) {
                        return make_result(VmStatus::ModuleError, "binary instruction missing operator");
                    }
                    if (stack.size() < 2) {
                        return make_result(VmStatus::RuntimeError, "binary instruction requires two operands");
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
                            return make_result(VmStatus::RuntimeError, "division by zero during execution");
                        }
                        stack.push_back(left / right);
                    } else if (op == "%") {
                        if (std::abs(right) < kEpsilon) {
                            return make_result(VmStatus::RuntimeError, "modulo by zero during execution");
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
                        return make_result(VmStatus::ModuleError, "unsupported binary operator '" + op + "'");
                    }
                    break;
                }
                case ir::InstructionKind::Unary: {
                    if (inst.operands.empty()) {
                        return make_result(VmStatus::ModuleError, "unary instruction missing operator");
                    }
                    if (stack.empty()) {
                        return make_result(VmStatus::RuntimeError, "unary instruction requires one operand");
                    }
                    const double operand = stack.back();
                    stack.pop_back();
                    const std::string& op = inst.operands.front();
                    if (op == "!") {
                        stack.push_back((operand == 0.0) ? 1.0 : 0.0);
                    } else if (op == "-") {
                        stack.push_back(-operand);
                    } else {
                        return make_result(VmStatus::ModuleError, "unsupported unary operator '" + op + "'");
                    }
                    break;
                }
                case ir::InstructionKind::Return: {
                    if (stack.empty()) {
                        return make_result(VmStatus::RuntimeError, "return instruction requires a value");
                    }
                    const double value = stack.back();
                    VmResult result;
                    result.status = VmStatus::Success;
                    result.has_value = true;
                    result.value = value;
                    return result;
                }
                case ir::InstructionKind::Store: {
                    if (inst.operands.empty()) {
                        return make_result(VmStatus::ModuleError, "store instruction missing operand");
                    }
                    if (stack.empty()) {
                        return make_result(VmStatus::RuntimeError, "store instruction requires a value");
                    }
                    const double value = stack.back();
                    stack.pop_back();
                    locals[inst.operands.front()] = value;
                    break;
                }
                case ir::InstructionKind::Branch: {
                    if (inst.operands.empty()) {
                        return make_result(VmStatus::ModuleError, "branch instruction missing label");
                    }
                    const auto it = labels.find(inst.operands.front());
                    if (it == labels.end()) {
                        return make_result(VmStatus::ModuleError, "branch to undefined label '" + inst.operands.front() + "'");
                    }
                    pc = it->second;
                    continue;
                }
                case ir::InstructionKind::BranchIf: {
                    if (inst.operands.size() < 2) {
                        return make_result(VmStatus::ModuleError, "branch_if instruction requires label and condition");
                    }
                    if (stack.empty()) {
                        return make_result(VmStatus::RuntimeError, "branch_if requires a condition on stack");
                    }
                    const double condition = stack.back();
                    stack.pop_back();
                    const double compare_val = std::stod(inst.operands[1]);
                    if (std::abs(condition - compare_val) < kEpsilon) {
                        const auto it = labels.find(inst.operands.front());
                        if (it == labels.end()) {
                            return make_result(VmStatus::ModuleError, "branch_if to undefined label '" + inst.operands.front() + "'");
                        }
                        pc = it->second;
                        continue;
                    }
                    break;
                }
                case ir::InstructionKind::Call: {
                    if (inst.operands.size() < 2) {
                        return make_result(VmStatus::ModuleError, "call instruction requires function name and arg count");
                    }
                    const std::string& callee_name = inst.operands[0];
                    const size_t arg_count = std::stoull(inst.operands[1]);
                    
                    if (stack.size() < arg_count) {
                        return make_result(VmStatus::RuntimeError, "call requires " + std::to_string(arg_count) + " arguments on stack");
                    }
                    
                    std::unordered_map<std::string, double> call_params;
                    
                    const ir::Function* target_func = nullptr;
                    for (const auto& f : module.module.functions) {
                        if (f.name == callee_name) {
                            target_func = &f;
                            break;
                        }
                    }
                    
                    if (!target_func) {
                        return make_result(VmStatus::ModuleError, "function '" + callee_name + "' not found");
                    }
                    
                    if (target_func->parameters.size() != arg_count) {
                        return make_result(VmStatus::ModuleError, "function '" + callee_name + "' expects " + 
                                          std::to_string(target_func->parameters.size()) + " arguments, got " + std::to_string(arg_count));
                    }
                    
                    std::vector<double> args;
                    for (size_t i = 0; i < arg_count; ++i) {
                        args.push_back(stack[stack.size() - arg_count + i]);
                    }
                    stack.resize(stack.size() - arg_count);
                    
                    for (size_t i = 0; i < args.size(); ++i) {
                        call_params[target_func->parameters[i].name] = args[i];
                    }
                    
                    const auto result = execute_function(module, *target_func, call_params);
                    if (result.status != VmStatus::Success || !result.has_value) {
                        return result;
                    }
                    
                    stack.push_back(result.value);
                    break;
                }
                case ir::InstructionKind::Label:
                case ir::InstructionKind::Comment:
                    break;
            }
        ++pc;
    }

    return make_result(VmStatus::RuntimeError, "function did not encounter a return instruction");
}

[[nodiscard]] auto Vm::normalize_module_name(const ir::Module& module) -> std::string { return join_path(module.path); }

}  // namespace impulse::runtime
