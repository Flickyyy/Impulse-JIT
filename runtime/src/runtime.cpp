#include "impulse/runtime/runtime.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "impulse/ir/interpreter.h"
#include "impulse/ir/optimizer.h"
#include "impulse/ir/ssa.h"

namespace impulse::runtime {

class FrameGuard {
public:
    FrameGuard(const Vm& vm, std::unordered_map<std::string, Value>& locals, std::vector<Value>& stack)
        : vm_(vm) {
        vm_.push_frame(locals, stack);
    }

    FrameGuard(const FrameGuard&) = delete;
    auto operator=(const FrameGuard&) -> FrameGuard& = delete;

    ~FrameGuard() { vm_.pop_frame(); }

private:
    const Vm& vm_;
};

namespace {

constexpr double kEpsilon = 1e-12;

[[nodiscard]] auto encode_value_id(ir::SsaValue value) -> std::uint64_t {
    return (static_cast<std::uint64_t>(value.symbol) << 32U) | static_cast<std::uint64_t>(value.version);
}

[[nodiscard]] auto to_index(double value) -> std::optional<std::size_t> {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    if (value < 0.0) {
        return std::nullopt;
    }
    const double truncated = std::floor(value + kEpsilon);
    if (std::abs(truncated - value) > kEpsilon) {
        return std::nullopt;
    }
    if (truncated > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(truncated);
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

class SsaInterpreter {
public:
    using CallFunction = std::function<VmResult(const ir::Function&, const std::vector<Value>&)>;
    using AllocateArray = std::function<GcObject*(std::size_t)>;
    using MaybeCollect = std::function<void()>;

    SsaInterpreter(const ir::SsaFunction& ssa, const std::unordered_map<std::string, Value>& parameters,
                   std::unordered_map<std::string, Value>& locals, const std::vector<ir::Function>& functions,
                   const std::unordered_map<std::string, Value>& globals, CallFunction call_function,
                   AllocateArray allocate_array, MaybeCollect maybe_collect)
        : ssa_(ssa), parameters_(parameters), locals_(locals), functions_(functions), globals_(globals),
          call_function_(std::move(call_function)), allocate_array_(std::move(allocate_array)),
          maybe_collect_(std::move(maybe_collect)) {
        for (const auto& block : ssa_.blocks) {
            block_lookup_.emplace(block.name, block.id);
        }

        for (const auto& [name, value] : parameters_) {
            if (const auto* symbol = ssa_.find_symbol(name); symbol != nullptr) {
                store_value(ir::SsaValue{symbol->id, 1}, value);
            }
        }
    }

    [[nodiscard]] auto run() -> VmResult {
        if (ssa_.blocks.empty()) {
            return make_result(VmStatus::ModuleError, "function has no basic blocks");
        }

        std::size_t current = 0;
        std::optional<std::size_t> previous;

        while (current < ssa_.blocks.size()) {
            const auto& block = ssa_.blocks[current];

            if (auto error = materialize_phi(block, previous)) {
                return *error;
            }

            bool jumped = false;
            for (const auto& inst : block.instructions) {
                if (auto outcome = execute_instruction(block, inst, current, previous, jumped)) {
                    return *outcome;
                }
                if (jumped) {
                    break;
                }
            }

            if (next_block_.has_value()) {
                previous = current;
                current = *next_block_;
                next_block_.reset();
                continue;
            }

            if (!jumped) {
                if (const auto fallback = pick_fallthrough(block)) {
                    previous = current;
                    current = *fallback;
                    continue;
                }
                return make_result(VmStatus::RuntimeError, "control flow terminated without return");
            }
        }

        return make_result(VmStatus::RuntimeError, "function did not encounter a return instruction");
    }

private:
    [[nodiscard]] auto materialize_phi(const ir::SsaBlock& block, std::optional<std::size_t> previous)
        -> std::optional<VmResult> {
        for (const auto& phi : block.phi_nodes) {
            if (!phi.result.is_valid()) {
                return make_result(VmStatus::ModuleError, "phi node missing result value");
            }

            std::optional<Value> incoming;
            if (previous.has_value()) {
                const auto it = std::find_if(phi.inputs.begin(), phi.inputs.end(), [&](const ir::PhiInput& input) {
                    return input.predecessor == *previous;
                });
                if (it != phi.inputs.end() && it->value.has_value()) {
                    incoming = lookup_value(*it->value);
                }
            }

            if (!incoming.has_value()) {
                for (const auto& input : phi.inputs) {
                    if (!input.value.has_value()) {
                        continue;
                    }
                    incoming = lookup_value(*input.value);
                    if (incoming.has_value()) {
                        break;
                    }
                }
            }

            if (!incoming.has_value()) {
                return make_result(VmStatus::RuntimeError, "phi node missing incoming value");
            }

            store_value(phi.result, *incoming);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto execute_instruction(const ir::SsaBlock& block, const ir::SsaInstruction& inst,
                                           std::size_t current,
                                           std::optional<std::size_t>& previous, bool& jumped)
        -> std::optional<VmResult> {
        if (inst.opcode == "literal") {
            if (!inst.result.has_value() || inst.immediates.empty()) {
                return make_result(VmStatus::ModuleError, "literal instruction missing data");
            }
            const auto parsed = parse_literal(inst.immediates.front());
            if (!parsed.has_value()) {
                return make_result(VmStatus::ModuleError,
                                   "unable to parse literal operand '" + inst.immediates.front() + "'");
            }
            store_value(*inst.result, Value::make_number(*parsed));
            return std::nullopt;
        }

        if (inst.opcode == "unary") {
            if (inst.arguments.empty() || inst.immediates.empty() || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "unary instruction malformed");
            }
            const auto operand = lookup_value(inst.arguments.front());
            if (!operand.has_value() || !operand->is_number()) {
                return make_result(VmStatus::RuntimeError, "unary instruction requires numeric operand");
            }
            const double value = operand->number;
            const std::string& op = inst.immediates.front();
            if (op == "!") {
                store_value(*inst.result, Value::make_number(value == 0.0 ? 1.0 : 0.0));
            } else if (op == "-") {
                store_value(*inst.result, Value::make_number(-value));
            } else {
                return make_result(VmStatus::ModuleError, "unsupported unary operator '" + op + "'");
            }
            return std::nullopt;
        }

        if (inst.opcode == "binary") {
            if (inst.arguments.size() < 2 || inst.immediates.empty() || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "binary instruction malformed");
            }
            const auto lhsValue = lookup_value(inst.arguments[0]);
            const auto rhsValue = lookup_value(inst.arguments[1]);
            if (!lhsValue.has_value() || !rhsValue.has_value() || !lhsValue->is_number() || !rhsValue->is_number()) {
                return make_result(VmStatus::RuntimeError, "binary instruction requires numeric operands");
            }
            const double left = lhsValue->number;
            const double right = rhsValue->number;
            const std::string& op = inst.immediates.front();

            if (op == "+") {
                store_value(*inst.result, Value::make_number(left + right));
            } else if (op == "-") {
                store_value(*inst.result, Value::make_number(left - right));
            } else if (op == "*") {
                store_value(*inst.result, Value::make_number(left * right));
            } else if (op == "/") {
                if (std::abs(right) < kEpsilon) {
                    return make_result(VmStatus::RuntimeError, "division by zero during execution");
                }
                store_value(*inst.result, Value::make_number(left / right));
            } else if (op == "%") {
                if (std::abs(right) < kEpsilon) {
                    return make_result(VmStatus::RuntimeError, "modulo by zero during execution");
                }
                const int leftInt = static_cast<int>(left);
                const int rightInt = static_cast<int>(right);
                store_value(*inst.result, Value::make_number(static_cast<double>(leftInt % rightInt)));
            } else if (op == "==") {
                store_value(*inst.result, Value::make_number(left == right ? 1.0 : 0.0));
            } else if (op == "!=") {
                store_value(*inst.result, Value::make_number(left != right ? 1.0 : 0.0));
            } else if (op == "<") {
                store_value(*inst.result, Value::make_number(left < right ? 1.0 : 0.0));
            } else if (op == "<=") {
                store_value(*inst.result, Value::make_number(left <= right ? 1.0 : 0.0));
            } else if (op == ">") {
                store_value(*inst.result, Value::make_number(left > right ? 1.0 : 0.0));
            } else if (op == ">=") {
                store_value(*inst.result, Value::make_number(left >= right ? 1.0 : 0.0));
            } else if (op == "&&") {
                store_value(*inst.result, Value::make_number((left != 0.0 && right != 0.0) ? 1.0 : 0.0));
            } else if (op == "||") {
                store_value(*inst.result, Value::make_number((left != 0.0 || right != 0.0) ? 1.0 : 0.0));
            } else {
                return make_result(VmStatus::ModuleError, "unsupported binary operator '" + op + "'");
            }
            return std::nullopt;
        }

        if (inst.opcode == "assign") {
            if (inst.arguments.size() != 1 || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "assign instruction malformed");
            }
            const auto value = lookup_value(inst.arguments.front());
            if (!value.has_value()) {
                return make_result(VmStatus::RuntimeError, "assign instruction missing source value");
            }
            store_value(*inst.result, *value);
            return std::nullopt;
        }

        if (inst.opcode == "drop") {
            if (inst.arguments.size() != 1) {
                return make_result(VmStatus::ModuleError, "drop instruction malformed");
            }
            const auto value = lookup_value(inst.arguments.front());
            if (!value.has_value()) {
                return make_result(VmStatus::RuntimeError, "drop instruction missing value");
            }
            return std::nullopt;
        }

        if (inst.opcode == "return") {
            if (inst.arguments.empty()) {
                return make_result(VmStatus::RuntimeError, "return instruction requires a value");
            }
            const auto value = lookup_value(inst.arguments.front());
            if (!value.has_value() || !value->is_number()) {
                return make_result(VmStatus::RuntimeError, "return value must be numeric");
            }
            VmResult result;
            result.status = VmStatus::Success;
            result.has_value = true;
            result.value = value->number;
            return result;
        }

        if (inst.opcode == "branch") {
            if (inst.immediates.empty()) {
                return make_result(VmStatus::ModuleError, "branch instruction missing label");
            }
            const auto target = resolve_block(inst.immediates.front());
            if (!target.has_value()) {
                return make_result(VmStatus::ModuleError,
                                   "branch to undefined label '" + inst.immediates.front() + "'");
            }
            next_block_ = target;
            jumped = true;
            previous = current;
            return std::nullopt;
        }

        if (inst.opcode == "branch_if") {
            if (inst.arguments.empty() || inst.immediates.empty()) {
                return make_result(VmStatus::ModuleError, "branch_if instruction malformed");
            }
            const auto conditionValue = lookup_value(inst.arguments.front());
            if (!conditionValue.has_value() || !conditionValue->is_number()) {
                return make_result(VmStatus::RuntimeError, "branch_if requires numeric condition");
            }
            const auto target = resolve_block(inst.immediates.front());
            if (!target.has_value()) {
                return make_result(VmStatus::ModuleError,
                                   "branch_if to undefined label '" + inst.immediates.front() + "'");
            }
            double compare_val = 0.0;
            if (inst.immediates.size() >= 2) {
                try {
                    compare_val = std::stod(inst.immediates[1]);
                } catch (...) {
                    return make_result(VmStatus::ModuleError,
                                       "branch_if comparison value '" + inst.immediates[1] + "' invalid");
                }
            }

            if (std::abs(conditionValue->number - compare_val) < kEpsilon) {
                next_block_ = target;
            } else {
                const auto fallback = pick_fallthrough(block, target);
                if (!fallback.has_value()) {
                    return make_result(VmStatus::RuntimeError, "branch_if missing fallthrough successor");
                }
                next_block_ = fallback;
            }
            jumped = true;
            previous = current;
            return std::nullopt;
        }

        if (inst.opcode == "call") {
            if (inst.immediates.size() < 2 || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "call instruction malformed");
            }
            const std::string& callee_name = inst.immediates[0];
            std::size_t arg_count = 0;
            try {
                arg_count = static_cast<std::size_t>(std::stoull(inst.immediates[1]));
            } catch (...) {
                return make_result(VmStatus::ModuleError,
                                   "call argument count '" + inst.immediates[1] + "' invalid");
            }

            if (inst.arguments.size() != arg_count) {
                return make_result(VmStatus::ModuleError, "call argument mismatch");
            }

            std::vector<Value> args;
            args.reserve(arg_count);
            for (const auto& arg : inst.arguments) {
                const auto value = lookup_value(arg);
                if (!value.has_value()) {
                    return make_result(VmStatus::RuntimeError, "call argument not initialized");
                }
                args.push_back(*value);
            }

            const ir::Function* target_func = nullptr;
            for (const auto& f : functions_) {
                if (f.name == callee_name) {
                    target_func = &f;
                    break;
                }
            }

            if (target_func == nullptr) {
                return make_result(VmStatus::MissingSymbol, "function '" + callee_name + "' not found");
            }

            if (target_func->parameters.size() != arg_count) {
                return make_result(
                    VmStatus::ModuleError,
                    "function '" + callee_name + "' expects " + std::to_string(target_func->parameters.size()) +
                        " arguments, got " + std::to_string(arg_count));
            }

            auto call_result = call_function_(*target_func, args);
            if (call_result.status != VmStatus::Success || !call_result.has_value) {
                return call_result;
            }

            store_value(*inst.result, Value::make_number(call_result.value));
            return std::nullopt;
        }

        if (inst.opcode == "array_make") {
            if (inst.arguments.size() != 1 || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "array_make instruction malformed");
            }
            const auto lengthValue = lookup_value(inst.arguments.front());
            if (!lengthValue.has_value() || !lengthValue->is_number()) {
                return make_result(VmStatus::RuntimeError, "make_array length must be numeric");
            }
            const auto maybeLength = to_index(lengthValue->number);
            if (!maybeLength.has_value()) {
                return make_result(VmStatus::RuntimeError, "make_array length must be a non-negative integer");
            }
            GcObject* object = allocate_array_(*maybeLength);
            const Value allocated = Value::make_object(object);
            store_value(*inst.result, allocated);
            maybe_collect_();
            return std::nullopt;
        }

        if (inst.opcode == "array_get") {
            if (inst.arguments.size() != 2 || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "array_get instruction malformed");
            }
            const auto arrayValue = lookup_value(inst.arguments[0]);
            const auto indexValue = lookup_value(inst.arguments[1]);
            if (!arrayValue.has_value() || !arrayValue->is_object() || arrayValue->as_object() == nullptr ||
                arrayValue->as_object()->kind != ObjectKind::Array) {
                return make_result(VmStatus::RuntimeError, "array_get requires an array value");
            }
            if (!indexValue.has_value() || !indexValue->is_number()) {
                return make_result(VmStatus::RuntimeError, "array_get index must be numeric");
            }
            const auto maybeIndex = to_index(indexValue->number);
            if (!maybeIndex.has_value()) {
                return make_result(VmStatus::RuntimeError, "array_get index must be a non-negative integer");
            }
            GcObject* object = arrayValue->as_object();
            if (*maybeIndex >= object->fields.size()) {
                return make_result(VmStatus::RuntimeError, "array_get index out of bounds");
            }
            store_value(*inst.result, object->fields[*maybeIndex]);
            return std::nullopt;
        }

        if (inst.opcode == "array_set") {
            if (inst.arguments.size() != 3 || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "array_set instruction malformed");
            }
            const auto arrayValue = lookup_value(inst.arguments[0]);
            const auto indexValue = lookup_value(inst.arguments[1]);
            const auto value = lookup_value(inst.arguments[2]);
            if (!arrayValue.has_value() || !arrayValue->is_object() || arrayValue->as_object() == nullptr ||
                arrayValue->as_object()->kind != ObjectKind::Array) {
                return make_result(VmStatus::RuntimeError, "array_set requires an array value");
            }
            if (!indexValue.has_value() || !indexValue->is_number()) {
                return make_result(VmStatus::RuntimeError, "array_set index must be numeric");
            }
            if (!value.has_value()) {
                return make_result(VmStatus::RuntimeError, "array_set value uninitialised");
            }
            const auto maybeIndex = to_index(indexValue->number);
            if (!maybeIndex.has_value()) {
                return make_result(VmStatus::RuntimeError, "array_set index must be a non-negative integer");
            }
            GcObject* object = arrayValue->as_object();
            if (*maybeIndex >= object->fields.size()) {
                return make_result(VmStatus::RuntimeError, "array_set index out of bounds");
            }
            object->fields[*maybeIndex] = *value;
            store_value(*inst.result, *value);
            return std::nullopt;
        }

        if (inst.opcode == "array_length") {
            if (inst.arguments.size() != 1 || !inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "array_length instruction malformed");
            }
            const auto arrayValue = lookup_value(inst.arguments.front());
            if (!arrayValue.has_value() || !arrayValue->is_object() || arrayValue->as_object() == nullptr ||
                arrayValue->as_object()->kind != ObjectKind::Array) {
                return make_result(VmStatus::RuntimeError, "array_length requires an array value");
            }
            const auto length = static_cast<double>(arrayValue->as_object()->fields.size());
            store_value(*inst.result, Value::make_number(length));
            return std::nullopt;
        }

        return make_result(VmStatus::ModuleError, "unsupported SSA opcode '" + inst.opcode + "'");
    }

    [[nodiscard]] auto lookup_value(const ir::SsaValue& value) -> std::optional<Value> {
        if (!value.is_valid()) {
            return std::nullopt;
        }

        const auto key = encode_value_id(value);
        const auto it = value_cache_.find(key);
        if (it != value_cache_.end()) {
            return it->second;
        }

        if (value.version == 0) {
            if (const auto* symbol = ssa_.find_symbol(value.symbol); symbol != nullptr) {
                if (!symbol->name.empty()) {
                    if (const auto localIt = locals_.find(symbol->name); localIt != locals_.end()) {
                        return localIt->second;
                    }
                    if (const auto paramIt = parameters_.find(symbol->name); paramIt != parameters_.end()) {
                        return paramIt->second;
                    }
                    if (const auto globalIt = globals_.find(symbol->name); globalIt != globals_.end()) {
                        return globalIt->second;
                    }
                }
            }
            return std::nullopt;
        }

        return std::nullopt;
    }

    void store_value(const ir::SsaValue& value, const Value& data) {
        if (!value.is_valid()) {
            return;
        }
        const auto key = encode_value_id(value);
        value_cache_[key] = data;

        const std::string storage_key = "$ssa:" + value.to_string();
        locals_[storage_key] = data;

        if (const auto* symbol = ssa_.find_symbol(value.symbol); symbol != nullptr) {
            if (!symbol->name.empty()) {
                locals_[symbol->name] = data;
            }
        }
    }

    [[nodiscard]] auto resolve_block(const std::string& label) const -> std::optional<std::size_t> {
        const auto it = block_lookup_.find(label);
        if (it == block_lookup_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] auto pick_fallthrough(const ir::SsaBlock& block,
                                        std::optional<std::size_t> exclude = std::nullopt) const
        -> std::optional<std::size_t> {
        for (const auto succ : block.successors) {
            if (exclude.has_value() && succ == *exclude) {
                continue;
            }
            return succ;
        }
        return std::nullopt;
    }

    const ir::SsaFunction& ssa_;
    const std::unordered_map<std::string, Value>& parameters_;
    std::unordered_map<std::string, Value>& locals_;

    const std::vector<ir::Function>& functions_;
    const std::unordered_map<std::string, Value>& globals_;

    std::unordered_map<std::uint64_t, Value> value_cache_;
    std::unordered_map<std::string, std::size_t> block_lookup_;
    std::optional<std::size_t> next_block_;
    CallFunction call_function_;
    AllocateArray allocate_array_;
    MaybeCollect maybe_collect_;
};

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
            loaded.globals[binding.name] = Value::make_number(*eval.value);
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

    std::unordered_map<std::string, Value> parameters;
    for (const auto& parameter : functionIt->parameters) {
        parameters.emplace(parameter.name, Value::make_number(0.0));
    }

    return execute_function(*moduleIt, *functionIt, parameters);
}

[[nodiscard]] auto Vm::execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, Value>& parameters) const -> VmResult {
    if (function.blocks.empty()) {
        return make_result(VmStatus::ModuleError, "function has no basic blocks");
    }

    std::vector<Value> stack;
    std::unordered_map<std::string, Value> locals;
    locals.reserve(parameters.size());
    for (const auto& [name, value] : parameters) {
        locals.emplace(name, value);
    }
    FrameGuard frame_guard(*this, locals, stack);
    auto ssa = ir::build_ssa(function);
    [[maybe_unused]] const bool optimized = ir::optimize_ssa(ssa);

    auto call_function = [this, &module](const ir::Function& target, const std::vector<Value>& args) -> VmResult {
        std::unordered_map<std::string, Value> call_params;
        call_params.reserve(target.parameters.size());
        for (std::size_t i = 0; i < target.parameters.size() && i < args.size(); ++i) {
            call_params[target.parameters[i].name] = args[i];
        }
        return execute_function(module, target, call_params);
    };

    auto allocate_array = [this](std::size_t length) -> GcObject* { return heap_.allocate_array(length); };
    auto collect_fn = [this]() { maybe_collect(); };

    SsaInterpreter interpreter(ssa, parameters, locals, module.module.functions, module.globals,
                               std::move(call_function), std::move(allocate_array), std::move(collect_fn));
    return interpreter.run();
}

[[nodiscard]] auto Vm::normalize_module_name(const ir::Module& module) -> std::string { return join_path(module.path); }

void Vm::collect_garbage() const {
    root_buffer_.clear();
    gather_roots(root_buffer_);
    heap_.collect(root_buffer_);
    root_buffer_.clear();
}

void Vm::push_frame(std::unordered_map<std::string, Value>& locals, std::vector<Value>& stack) const {
    ExecutionFrame frame;
    frame.locals = &locals;
    frame.stack = &stack;
    frames_.push_back(frame);
}

void Vm::pop_frame() const {
    if (!frames_.empty()) {
        frames_.pop_back();
    }
}

void Vm::gather_roots(std::vector<Value*>& out) const {
    for (const auto& module : modules_) {
        for (const auto& entry : module.globals) {
            out.push_back(const_cast<Value*>(&entry.second));
        }
    }

    for (const auto& frame : frames_) {
        if (frame.locals != nullptr) {
            for (auto& pair : *frame.locals) {
                out.push_back(&pair.second);
            }
        }
        if (frame.stack != nullptr) {
            for (auto& value : *frame.stack) {
                out.push_back(&value);
            }
        }
    }
}

void Vm::maybe_collect() const {
    if (heap_.should_collect()) {
        collect_garbage();
    }
}

}  // namespace impulse::runtime
