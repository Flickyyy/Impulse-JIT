#include "impulse/runtime/runtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <istream>
#include <ostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

[[nodiscard]] auto format_ssa_value(const ir::SsaValue& value) -> std::string {
    if (!value.is_valid()) {
        return "<invalid>";
    }
    std::ostringstream out;
    out << value.to_string();
    return out.str();
}

[[nodiscard]] auto join_ssa_values(const std::vector<ir::SsaValue>& values) -> std::string {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << format_ssa_value(values[i]);
    }
    return out.str();
}

[[nodiscard]] auto join_strings(const std::vector<std::string>& values) -> std::string {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << values[i];
    }
    return out.str();
}

[[nodiscard]] auto escape_string(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

[[nodiscard]] auto format_number(double value) -> std::string {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value > 0 ? "inf" : "-inf";
    }

    const double rounded = std::round(value);
    if (std::abs(value - rounded) < kEpsilon) {
        std::ostringstream out;
        out << static_cast<long long>(rounded);
        return out.str();
    }

    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

[[nodiscard]] auto format_value_for_output(const Value& value) -> std::string {
    switch (value.kind) {
        case ValueKind::Nil:
            return "nil";
        case ValueKind::Number:
            return format_number(value.number);
        case ValueKind::String:
            return std::string{value.as_string()};
        case ValueKind::Object:
            if (value.object == nullptr) {
                return "object@null";
            }
            if (value.object->kind == ObjectKind::Array) {
                return "[array length=" + std::to_string(value.object->fields.size()) + "]";
            }
            std::ostringstream out;
            out << "object@" << static_cast<const void*>(value.object);
            return out.str();
    }
    return "<value>";
}

[[nodiscard]] auto format_ssa_instruction(const ir::SsaInstruction& inst) -> std::string {
    std::ostringstream out;
    if (inst.result.has_value()) {
        out << format_ssa_value(*inst.result) << " = ";
    }
    out << inst.opcode;
    if (!inst.arguments.empty()) {
        out << " args(" << join_ssa_values(inst.arguments) << ')';
    }
    if (!inst.immediates.empty()) {
        out << " imm(" << join_strings(inst.immediates) << ')';
    }
    return out.str();
}

[[nodiscard]] auto describe_value(const Value& value) -> std::string {
    std::ostringstream out;
    switch (value.kind) {
        case ValueKind::Nil:
            out << "nil";
            break;
        case ValueKind::Number:
            out << value.number;
            break;
        case ValueKind::Object:
            if (value.object == nullptr) {
                out << "object@null";
                break;
            }
            if (value.object->kind == ObjectKind::Array) {
                out << "[array length=" << value.object->fields.size() << "]";
                break;
            }
            out << "object@" << static_cast<const void*>(value.object);
            break;
        case ValueKind::String:
            out << '"' << escape_string(value.as_string()) << '"';
            break;
    }
    return out.str();
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
        using ReadLine = std::function<std::optional<std::string>()>;

        SsaInterpreter(const ir::SsaFunction& ssa, const std::unordered_map<std::string, Value>& parameters,
                                     std::unordered_map<std::string, Value>& locals, const std::vector<ir::Function>& functions,
                                     const std::unordered_map<std::string, Value>& globals, CallFunction call_function,
                                     AllocateArray allocate_array, MaybeCollect maybe_collect, std::string* output_buffer,
                                     std::ostream* trace, ReadLine read_line)
        : ssa_(ssa),
          parameters_(parameters),
          locals_(locals),
          functions_(functions),
          globals_(globals),
          call_function_(std::move(call_function)),
          allocate_array_(std::move(allocate_array)),
          maybe_collect_(std::move(maybe_collect)),
                    output_buffer_(output_buffer),
                    trace_(trace),
                    read_line_(std::move(read_line)) {
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

            trace_block_entry(block);

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
    void append_output(const std::string& text, bool newline) const {
        if (output_buffer_ == nullptr) {
            return;
        }
        output_buffer_->append(text);
        if (newline) {
            output_buffer_->push_back('\n');
        }
    }

    void trace_builtin(const std::string& name, std::string_view payload) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    builtin " << name;
        if (!payload.empty()) {
            *trace_ << " \"" << escape_string(payload) << "\"";
        }
        *trace_ << '\n';
    }

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
                // Initialize undefined phi to default value (0)
                // This handles variables defined only in conditional branches
                incoming = Value::make_number(0);
            }

            store_value(phi.result, *incoming);
            trace_phi_materialization(block, phi, *incoming);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto execute_instruction(const ir::SsaBlock& block, const ir::SsaInstruction& inst,
                                           std::size_t current,
                                           std::optional<std::size_t>& previous, bool& jumped)
        -> std::optional<VmResult> {
        trace_instruction(block, inst);

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

        if (inst.opcode == "literal_string") {
            if (!inst.result.has_value()) {
                return make_result(VmStatus::ModuleError, "literal_string instruction missing result");
            }
            const std::string value = inst.immediates.empty() ? std::string{} : inst.immediates.front();
            store_value(*inst.result, Value::make_string(value));
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
            const std::string& op = inst.immediates.front();

            if (!lhsValue.has_value() || !rhsValue.has_value()) {
                return make_result(VmStatus::RuntimeError, "binary instruction missing operands");
            }

            if (op == "+" && lhsValue->is_string() && rhsValue->is_string()) {
                std::string combined{lhsValue->as_string()};
                combined.append(rhsValue->as_string());
                store_value(*inst.result, Value::make_string(std::move(combined)));
                return std::nullopt;
            }

            if ((op == "==" || op == "!=") && lhsValue->is_string() && rhsValue->is_string()) {
                const bool equal = lhsValue->as_string() == rhsValue->as_string();
                const bool result_value = (op == "==") ? equal : !equal;
                store_value(*inst.result, Value::make_number(result_value ? 1.0 : 0.0));
                return std::nullopt;
            }

            if (!lhsValue->is_number() || !rhsValue->is_number()) {
                return make_result(VmStatus::RuntimeError, "binary instruction requires numeric operands");
            }

            const double left = lhsValue->number;
            const double right = rhsValue->number;

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
            trace_return(*value);
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
            trace_branch(*target, true);
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
                trace_branch(*target, true);
            } else {
                const auto fallback = pick_fallthrough(block, target);
                if (!fallback.has_value()) {
                    return make_result(VmStatus::RuntimeError, "branch_if missing fallthrough successor");
                }
                next_block_ = fallback;
                trace_branch(*fallback, false);
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

            if (callee_name == "print" || callee_name == "println") {
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "print requires destination for result");
                }
                std::ostringstream text;
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i != 0) {
                        text << ' ';
                    }
                    text << format_value_for_output(args[i]);
                }
                const bool newline = (callee_name == "println");
                const std::string payload = text.str();
                append_output(payload, newline);
                trace_builtin(callee_name, payload);
                store_value(*inst.result, Value::make_number(0.0));
                return std::nullopt;
            }

            if (callee_name == "string_length") {
                if (args.size() != 1) {
                    return make_result(VmStatus::RuntimeError, "string_length expects exactly one argument");
                }
                if (!args[0].is_string()) {
                    return make_result(VmStatus::RuntimeError, "string_length expects a string argument");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "string_length requires destination for result");
                }
                const auto length = static_cast<double>(args[0].as_string().size());
                store_value(*inst.result, Value::make_number(length));
                return std::nullopt;
            }

            if (callee_name == "string_equals") {
                if (args.size() != 2) {
                    return make_result(VmStatus::RuntimeError, "string_equals expects exactly two arguments");
                }
                if (!args[0].is_string() || !args[1].is_string()) {
                    return make_result(VmStatus::RuntimeError, "string_equals expects string arguments");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "string_equals requires destination for result");
                }
                const bool equal = args[0].as_string() == args[1].as_string();
                store_value(*inst.result, Value::make_number(equal ? 1.0 : 0.0));
                trace_builtin(callee_name, equal ? "true" : "false");
                return std::nullopt;
            }

            if (callee_name == "string_concat") {
                if (args.size() != 2) {
                    return make_result(VmStatus::RuntimeError, "string_concat expects exactly two arguments");
                }
                if (!args[0].is_string() || !args[1].is_string()) {
                    return make_result(VmStatus::RuntimeError, "string_concat expects string arguments");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "string_concat requires destination for result");
                }
                std::string combined{args[0].as_string()};
                combined.append(args[1].as_string());
                trace_builtin(callee_name, combined);
                store_value(*inst.result, Value::make_string(std::move(combined)));
                return std::nullopt;
            }

            if (callee_name == "string_repeat") {
                if (args.size() != 2) {
                    return make_result(VmStatus::RuntimeError, "string_repeat expects exactly two arguments");
                }
                if (!args[0].is_string()) {
                    return make_result(VmStatus::RuntimeError, "string_repeat expects a string value");
                }
                if (!args[1].is_number()) {
                    return make_result(VmStatus::RuntimeError, "string_repeat expects a numeric repeat count");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "string_repeat requires destination for result");
                }
                const auto maybeCount = to_index(args[1].number);
                if (!maybeCount.has_value()) {
                    return make_result(VmStatus::RuntimeError,
                                       "string_repeat count must be a non-negative integer");
                }
                const std::string_view pattern = args[0].as_string();
                std::string repeated;
                repeated.reserve(pattern.size() * *maybeCount);
                for (std::size_t i = 0; i < *maybeCount; ++i) {
                    repeated.append(pattern);
                }
                trace_builtin(callee_name, repeated);
                store_value(*inst.result, Value::make_string(std::move(repeated)));
                return std::nullopt;
            }

            if (callee_name == "string_slice") {
                if (args.size() != 3) {
                    return make_result(VmStatus::RuntimeError, "string_slice expects exactly three arguments");
                }
                if (!args[0].is_string()) {
                    return make_result(VmStatus::RuntimeError, "string_slice expects a string value");
                }
                if (!args[1].is_number() || !args[2].is_number()) {
                    return make_result(VmStatus::RuntimeError,
                                       "string_slice expects numeric start/count arguments");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "string_slice requires destination for result");
                }
                const auto maybeStart = to_index(args[1].number);
                const auto maybeCount = to_index(args[2].number);
                if (!maybeStart.has_value() || !maybeCount.has_value()) {
                    return make_result(VmStatus::RuntimeError,
                                       "string_slice start/count must be non-negative integers");
                }
                const std::string_view text = args[0].as_string();
                if (*maybeStart > text.size()) {
                    return make_result(VmStatus::RuntimeError, "string_slice start exceeds string length");
                }
                if (*maybeStart + *maybeCount > text.size()) {
                    return make_result(VmStatus::RuntimeError, "string_slice exceeds string bounds");
                }
                std::string sliced{text.substr(*maybeStart, *maybeCount)};
                trace_builtin(callee_name, sliced);
                store_value(*inst.result, Value::make_string(std::move(sliced)));
                return std::nullopt;
            }

            if (callee_name == "string_lower" || callee_name == "string_upper") {
                if (args.size() != 1) {
                    return make_result(VmStatus::RuntimeError, callee_name + " expects exactly one argument");
                }
                if (!args[0].is_string()) {
                    return make_result(VmStatus::RuntimeError, callee_name + " expects a string argument");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, callee_name + " requires destination for result");
                }
                std::string transformed{args[0].as_string()};
                for (char& ch : transformed) {
                    const auto u = static_cast<unsigned char>(ch);
                    if (callee_name == "string_lower") {
                        ch = static_cast<char>(std::tolower(u));
                    } else {
                        ch = static_cast<char>(std::toupper(u));
                    }
                }
                trace_builtin(callee_name, transformed);
                store_value(*inst.result, Value::make_string(std::move(transformed)));
                return std::nullopt;
            }

            if (callee_name == "string_trim") {
                if (args.size() != 1) {
                    return make_result(VmStatus::RuntimeError, "string_trim expects exactly one argument");
                }
                if (!args[0].is_string()) {
                    return make_result(VmStatus::RuntimeError, "string_trim expects a string argument");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "string_trim requires destination for result");
                }
                const std::string_view text = args[0].as_string();
                std::size_t begin = 0;
                while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
                    ++begin;
                }
                std::size_t end = text.size();
                while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
                    --end;
                }
                std::string trimmed{text.substr(begin, end - begin)};
                trace_builtin(callee_name, trimmed);
                store_value(*inst.result, Value::make_string(std::move(trimmed)));
                return std::nullopt;
            }

            if (callee_name == "array_push") {
                if (args.size() != 2) {
                    return make_result(VmStatus::RuntimeError, "array_push expects exactly two arguments");
                }
                if (!args[0].is_object() || args[0].as_object() == nullptr ||
                    args[0].as_object()->kind != ObjectKind::Array) {
                    return make_result(VmStatus::RuntimeError, "array_push requires an array value");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "array_push requires destination for result");
                }
                GcObject* object = args[0].as_object();
                object->fields.push_back(args[1]);
                trace_builtin(callee_name, "len=" + std::to_string(object->fields.size()));
                store_value(*inst.result, args[0]);
                maybe_collect_();
                return std::nullopt;
            }

            if (callee_name == "array_pop") {
                if (args.size() != 1) {
                    return make_result(VmStatus::RuntimeError, "array_pop expects exactly one argument");
                }
                if (!args[0].is_object() || args[0].as_object() == nullptr ||
                    args[0].as_object()->kind != ObjectKind::Array) {
                    return make_result(VmStatus::RuntimeError, "array_pop requires an array value");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "array_pop requires destination for result");
                }
                GcObject* object = args[0].as_object();
                if (object->fields.empty()) {
                    return make_result(VmStatus::RuntimeError, "array_pop cannot operate on an empty array");
                }
                Value popped = object->fields.back();
                object->fields.pop_back();
                trace_builtin(callee_name, describe_value(popped));
                store_value(*inst.result, popped);
                return std::nullopt;
            }

            if (callee_name == "array_join") {
                if (args.size() != 2) {
                    return make_result(VmStatus::RuntimeError, "array_join expects exactly two arguments");
                }
                if (!args[0].is_object() || args[0].as_object() == nullptr ||
                    args[0].as_object()->kind != ObjectKind::Array) {
                    return make_result(VmStatus::RuntimeError, "array_join requires an array value");
                }
                if (!args[1].is_string()) {
                    return make_result(VmStatus::RuntimeError, "array_join expects a string separator");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "array_join requires destination for result");
                }
                const std::string_view separator = args[1].as_string();
                std::ostringstream builder;
                const auto& elements = args[0].as_object()->fields;
                for (std::size_t i = 0; i < elements.size(); ++i) {
                    const Value& value = elements[i];
                    if (i != 0) {
                        builder << separator;
                    }
                    if (value.is_string()) {
                        builder << value.as_string();
                        continue;
                    }
                    if (value.is_number()) {
                        builder << format_number(value.number);
                        continue;
                    }
                    if (value.is_nil()) {
                        continue;
                    }
                    return make_result(VmStatus::RuntimeError, "array_join encountered unsupported element type");
                }
                const std::string merged = builder.str();
                trace_builtin(callee_name, merged);
                store_value(*inst.result, Value::make_string(merged));
                return std::nullopt;
            }

            if (callee_name == "array_fill") {
                if (args.size() != 2) {
                    return make_result(VmStatus::RuntimeError, "array_fill expects exactly two arguments");
                }
                if (!args[0].is_object() || args[0].as_object() == nullptr ||
                    args[0].as_object()->kind != ObjectKind::Array) {
                    return make_result(VmStatus::RuntimeError, "array_fill requires an array value");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "array_fill requires destination for result");
                }
                GcObject* object = args[0].as_object();
                for (auto& field : object->fields) {
                    field = args[1];
                }
                trace_builtin(callee_name, "len=" + std::to_string(object->fields.size()));
                store_value(*inst.result, args[0]);
                return std::nullopt;
            }

            if (callee_name == "array_sum") {
                if (args.size() != 1) {
                    return make_result(VmStatus::RuntimeError, "array_sum expects exactly one argument");
                }
                if (!args[0].is_object() || args[0].as_object() == nullptr ||
                    args[0].as_object()->kind != ObjectKind::Array) {
                    return make_result(VmStatus::RuntimeError, "array_sum requires an array value");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "array_sum requires destination for result");
                }
                double total = 0.0;
                for (const auto& field : args[0].as_object()->fields) {
                    if (field.is_number()) {
                        total += field.number;
                    } else if (field.is_nil()) {
                        continue;
                    } else {
                        return make_result(VmStatus::RuntimeError,
                                           "array_sum encountered non-numeric element");
                    }
                }
                trace_builtin(callee_name, std::to_string(total));
                store_value(*inst.result, Value::make_number(total));
                return std::nullopt;
            }

            if (callee_name == "read_line") {
                if (!args.empty()) {
                    return make_result(VmStatus::RuntimeError, "read_line expects no arguments");
                }
                if (!inst.result.has_value()) {
                    return make_result(VmStatus::ModuleError, "read_line requires destination for result");
                }
                std::string line;
                if (read_line_) {
                    if (auto fetched = read_line_()) {
                        line = std::move(*fetched);
                    }
                }
                trace_builtin(callee_name, line);
                store_value(*inst.result, Value::make_string(std::move(line)));
                return std::nullopt;
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

        trace_store(value, data);
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

    void trace_block_entry(const ir::SsaBlock& block) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "enter block " << block.id;
        if (!block.name.empty()) {
            *trace_ << " (" << block.name << ')';
        }
        *trace_ << '\n';
    }

    void trace_phi_materialization(const ir::SsaBlock& block, const ir::PhiNode& phi, const Value& value) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    phi " << format_ssa_value(phi.result) << " := " << describe_value(value);
        *trace_ << " in block " << block.id << '\n';
    }

    void trace_instruction(const ir::SsaBlock& block, const ir::SsaInstruction& inst) const {
        if (trace_ == nullptr) {
            return;
        }
        (void)block;
        *trace_ << "    " << format_ssa_instruction(inst) << '\n';
    }

    void trace_store(const ir::SsaValue& destination, const Value& value) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "      -> " << format_ssa_value(destination) << " = " << describe_value(value) << '\n';
    }

    void trace_return(const Value& value) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    return " << describe_value(value) << '\n';
    }

    void trace_branch(std::size_t target, bool taken) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    -> branch ";
        if (target < ssa_.blocks.size()) {
            const auto& block = ssa_.blocks[target];
            *trace_ << block.id;
            if (!block.name.empty()) {
                *trace_ << " (" << block.name << ')';
            }
        } else {
            *trace_ << "<invalid>";
        }
        *trace_ << (taken ? " [taken]" : " [skipped]") << '\n';
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
    ReadLine read_line_;
    std::string* output_buffer_ = nullptr;
    std::ostream* trace_ = nullptr;
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

    output_buffer_.clear();
    auto result = execute_function(*moduleIt, *functionIt, parameters, &output_buffer_);
    if (!output_buffer_.empty() && (result.status == VmStatus::Success || result.message.empty())) {
        result.message = output_buffer_;
    }
    return result;
}

[[nodiscard]] auto Vm::execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, Value>& parameters,
                                        std::string* output_buffer) const -> VmResult {
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

    auto call_function = [this, &module, output_buffer](const ir::Function& target,
                                                        const std::vector<Value>& args) -> VmResult {
        std::unordered_map<std::string, Value> call_params;
        call_params.reserve(target.parameters.size());
        for (std::size_t i = 0; i < target.parameters.size() && i < args.size(); ++i) {
            call_params[target.parameters[i].name] = args[i];
        }
        return execute_function(module, target, call_params, output_buffer);
    };

    auto allocate_array = [this](std::size_t length) -> GcObject* { return heap_.allocate_array(length); };
    auto collect_fn = [this]() { maybe_collect(); };
    auto read_line = [this]() -> std::optional<std::string> {
        if (read_line_provider_) {
            if (auto provided = read_line_provider_()) {
                return provided;
            }
            return std::string{};
        }
        if (input_stream_ != nullptr) {
            std::string line;
            if (std::getline(*input_stream_, line)) {
                return line;
            }
            if (input_stream_->eof()) {
                input_stream_->clear();
            }
            return std::string{};
        }
        return std::string{};
    };

    if (trace_stream_ != nullptr) {
        *trace_stream_ << "enter function " << function.name << '\n';
    }

    SsaInterpreter interpreter(ssa, parameters, locals, module.module.functions, module.globals,
                               std::move(call_function), std::move(allocate_array), std::move(collect_fn),
                               output_buffer, trace_stream_, std::move(read_line));
    auto result = interpreter.run();

    if (trace_stream_ != nullptr) {
        *trace_stream_ << "exit function " << function.name;
        if (result.status == VmStatus::Success && result.has_value) {
            *trace_stream_ << " = " << result.value;
        } else {
            *trace_stream_ << " status=" << static_cast<int>(result.status);
            if (!result.message.empty()) {
                *trace_stream_ << " message='" << result.message << "'";
            }
        }
        *trace_stream_ << '\n';
    }

    return result;
}

[[nodiscard]] auto Vm::normalize_module_name(const ir::Module& module) -> std::string { return join_path(module.path); }

void Vm::set_trace_stream(std::ostream* stream) const { trace_stream_ = stream; }

void Vm::set_input_stream(std::istream* stream) const { input_stream_ = stream; }

void Vm::set_read_line_provider(std::function<std::optional<std::string>()> provider) const {
    read_line_provider_ = std::move(provider);
}

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
