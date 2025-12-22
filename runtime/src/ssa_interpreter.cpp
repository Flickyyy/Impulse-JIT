#include "impulse/runtime/ssa_interpreter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "impulse/runtime/runtime_utils.h"

using impulse::runtime::kEpsilon;

namespace impulse::runtime {

SsaInterpreter::SsaInterpreter(const ir::SsaFunction& ssa, const std::unordered_map<std::string, Value>& parameters,
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
    
    // Build function lookup map for O(1) function lookup (performance optimization)
    for (const auto& func : functions_) {
        function_lookup_[func.name] = &func;
    }

    for (const auto& [name, value] : parameters_) {
        if (const auto* symbol = ssa_.find_symbol(name); symbol != nullptr) {
            store_value(ir::SsaValue{symbol->id, 1}, value);
        }
    }
    
    // Initialize built-in function table
    init_builtin_table();
}

auto SsaInterpreter::run() -> VmResult {
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

// append_output and trace_builtin are now inline in the header for performance

auto SsaInterpreter::materialize_phi(const ir::SsaBlock& block, std::optional<std::size_t> previous)
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

auto SsaInterpreter::execute_instruction(const ir::SsaBlock& block, const ir::SsaInstruction& inst,
                                         std::size_t current,
                                         std::optional<std::size_t>& previous, bool& jumped)
    -> std::optional<VmResult> {
    trace_instruction(block, inst);

    // Fast opcode dispatch using string comparison chain (compiler can optimize this)
    // This avoids std::function overhead and hash map lookup
    const std::string& opcode = inst.opcode;
    
    // Most common opcodes first for better branch prediction
    if (opcode == "array_get") {
        return handle_array_get(block, inst, current, previous, jumped);
    }
    if (opcode == "array_set") {
        return handle_array_set(block, inst, current, previous, jumped);
    }
    if (opcode == "binary") {
        return handle_binary(block, inst, current, previous, jumped);
    }
    if (opcode == "branch_if") {
        return handle_branch_if(block, inst, current, previous, jumped);
    }
    if (opcode == "call") {
        return handle_call(block, inst, current, previous, jumped);
    }
    if (opcode == "literal") {
        return handle_literal(block, inst, current, previous, jumped);
    }
    if (opcode == "assign") {
        return handle_assign(block, inst, current, previous, jumped);
    }
    if (opcode == "return") {
        return handle_return(block, inst, current, previous, jumped);
    }
    if (opcode == "branch") {
        return handle_branch(block, inst, current, previous, jumped);
    }
    if (opcode == "array_length") {
        return handle_array_length(block, inst, current, previous, jumped);
    }
    if (opcode == "array_make") {
        return handle_array_make(block, inst, current, previous, jumped);
    }
    if (opcode == "unary") {
        return handle_unary(block, inst, current, previous, jumped);
    }
    if (opcode == "literal_string") {
        return handle_literal_string(block, inst, current, previous, jumped);
    }
    if (opcode == "drop") {
        return handle_drop(block, inst, current, previous, jumped);
    }

    return make_result(VmStatus::ModuleError, "unsupported SSA opcode '" + inst.opcode + "'");
}

auto SsaInterpreter::handle_literal(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_literal_string(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
    if (!inst.result.has_value()) {
        return make_result(VmStatus::ModuleError, "literal_string instruction missing result");
    }
    const std::string value = inst.immediates.empty() ? std::string{} : inst.immediates.front();
    store_value(*inst.result, Value::make_string(value));
    return std::nullopt;
}

auto SsaInterpreter::handle_unary(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_binary(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
    if (inst.arguments.size() < 2 || inst.immediates.empty() || !inst.result.has_value()) {
        return make_result(VmStatus::ModuleError, "binary instruction malformed");
    }
    const auto lhsValue = lookup_value(inst.arguments[0]);
    const auto rhsValue = lookup_value(inst.arguments[1]);
    const std::string& op = inst.immediates.front();

    if (!lhsValue.has_value() || !rhsValue.has_value()) {
        return make_result(VmStatus::RuntimeError, "binary instruction missing operands");
    }

    // String operations
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

    // Fast binary operator dispatch using string comparison (compiler can optimize this)
    // Most common operators first for better branch prediction
    if (op == "+") {
        store_value(*inst.result, Value::make_number(left + right));
        return std::nullopt;
    }
    if (op == "-") {
        store_value(*inst.result, Value::make_number(left - right));
        return std::nullopt;
    }
    if (op == "*") {
        store_value(*inst.result, Value::make_number(left * right));
        return std::nullopt;
    }
    if (op == "<") {
        store_value(*inst.result, Value::make_number(left < right));
        return std::nullopt;
    }
    if (op == "<=") {
        store_value(*inst.result, Value::make_number(left <= right));
        return std::nullopt;
    }
    if (op == ">") {
        store_value(*inst.result, Value::make_number(left > right));
        return std::nullopt;
    }
    if (op == ">=") {
        store_value(*inst.result, Value::make_number(left >= right));
        return std::nullopt;
    }
    if (op == "==") {
        store_value(*inst.result, Value::make_number(left == right));
        return std::nullopt;
    }
    if (op == "!=") {
        store_value(*inst.result, Value::make_number(left != right));
        return std::nullopt;
    }
    if (op == "||") {
        store_value(*inst.result, Value::make_number(left || right));
        return std::nullopt;
    }
    if (op == "&&") {
        store_value(*inst.result, Value::make_number(left && right));
        return std::nullopt;
    }
    if (op == "/") {
        if (std::abs(right) < kEpsilon) {
            return make_result(VmStatus::RuntimeError, "division by zero during execution");
        }
        store_value(*inst.result, Value::make_number(left / right));
        return std::nullopt;
    }
    if (op == "%") {
        const auto leftIndex = to_index(left);
        const auto rightIndex = to_index(right);
        if (!leftIndex.has_value() || !rightIndex.has_value() || *rightIndex == 0) {
            return make_result(VmStatus::RuntimeError, "modulo requires non-negative integer operands and non-zero divisor");
        }
        store_value(*inst.result, Value::make_number(static_cast<double>(*leftIndex % *rightIndex)));
        return std::nullopt;
    }

    return make_result(VmStatus::ModuleError, "unsupported binary operator '" + op + "'");
}

auto SsaInterpreter::handle_assign(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_drop(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
    if (inst.arguments.size() != 1) {
        return make_result(VmStatus::ModuleError, "drop instruction malformed");
    }
    const auto value = lookup_value(inst.arguments.front());
    if (!value.has_value()) {
        return make_result(VmStatus::RuntimeError, "drop instruction missing value");
    }
    return std::nullopt;
}

auto SsaInterpreter::handle_return(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_branch(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t current, std::optional<std::size_t>& previous, bool& jumped) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_branch_if(const ir::SsaBlock& block, const ir::SsaInstruction& inst, std::size_t current, std::optional<std::size_t>& previous, bool& jumped) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_call(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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

    // Check built-in function table first
    const auto builtin_it = builtin_table_.find(callee_name);
    if (builtin_it != builtin_table_.end()) {
        return builtin_it->second(callee_name, args, inst.result);
    }

    // Not a built-in, look up in module functions using O(1) map lookup
    const ir::Function* target_func = nullptr;
    const auto func_it = function_lookup_.find(callee_name);
    if (func_it != function_lookup_.end()) {
        target_func = func_it->second;
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

auto SsaInterpreter::handle_array_make(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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

auto SsaInterpreter::handle_array_get(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
    if (inst.arguments.size() != 2 || !inst.result.has_value()) {
        return make_result(VmStatus::ModuleError, "array_get instruction malformed");
    }
    const auto arrayValue = lookup_value(inst.arguments[0]);
    const auto indexValue = lookup_value(inst.arguments[1]);
    if (!arrayValue.has_value() || !arrayValue->is_object()) {
        return make_result(VmStatus::RuntimeError, "array_get requires an array value");
    }
    GcObject* object = arrayValue->as_object();
    if (object == nullptr || object->kind != ObjectKind::Array) {
        return make_result(VmStatus::RuntimeError, "array_get requires an array value");
    }
    if (!indexValue.has_value() || !indexValue->is_number()) {
        return make_result(VmStatus::RuntimeError, "array_get index must be numeric");
    }
    // Fast path: convert to index and check bounds in one go
    const double indexNum = indexValue->number;
    if (indexNum < 0.0 || indexNum != std::floor(indexNum)) {
        return make_result(VmStatus::RuntimeError, "array_get index must be a non-negative integer");
    }
    const std::size_t index = static_cast<std::size_t>(indexNum);
    if (index >= object->fields.size()) {
        return make_result(VmStatus::RuntimeError, "array_get index out of bounds");
    }
    store_value(*inst.result, object->fields[index]);
    return std::nullopt;
}

auto SsaInterpreter::handle_array_set(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
    if (inst.arguments.size() != 3 || !inst.result.has_value()) {
        return make_result(VmStatus::ModuleError, "array_set instruction malformed");
    }
    const auto arrayValue = lookup_value(inst.arguments[0]);
    const auto indexValue = lookup_value(inst.arguments[1]);
    const auto value = lookup_value(inst.arguments[2]);
    if (!arrayValue.has_value() || !arrayValue->is_object()) {
        return make_result(VmStatus::RuntimeError, "array_set requires an array value");
    }
    GcObject* object = arrayValue->as_object();
    if (object == nullptr || object->kind != ObjectKind::Array) {
        return make_result(VmStatus::RuntimeError, "array_set requires an array value");
    }
    if (!indexValue.has_value() || !indexValue->is_number()) {
        return make_result(VmStatus::RuntimeError, "array_set index must be numeric");
    }
    if (!value.has_value()) {
        return make_result(VmStatus::RuntimeError, "array_set value uninitialised");
    }
    // Fast path: convert to index and check bounds in one go
    const double indexNum = indexValue->number;
    if (indexNum < 0.0 || indexNum != std::floor(indexNum)) {
        return make_result(VmStatus::RuntimeError, "array_set index must be a non-negative integer");
    }
    const std::size_t index = static_cast<std::size_t>(indexNum);
    if (index >= object->fields.size()) {
        return make_result(VmStatus::RuntimeError, "array_set index out of bounds");
    }
    object->fields[index] = *value;
    store_value(*inst.result, *value);
    return std::nullopt;
}

auto SsaInterpreter::handle_array_length(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult> {
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


// lookup_value and store_value are now inline in the header for performance

// resolve_block and pick_fallthrough are now inline in the header for performance

// Trace functions are now inline in the header for performance

// Builtin function implementations - this is a large function, so it's kept here
void SsaInterpreter::init_builtin_table() {
    // I/O functions
    builtin_table_["print"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "print requires destination for result");
        }
        std::ostringstream text;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) text << ' ';
            text << format_value_for_output(args[i]);
        }
        append_output(text.str(), false);
        trace_builtin(name, text.str());
        store_value(*result, Value::make_number(0.0));
        return std::nullopt;
    };
    builtin_table_["println"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "println requires destination for result");
        }
        std::ostringstream text;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) text << ' ';
            text << format_value_for_output(args[i]);
        }
        append_output(text.str(), true);
        trace_builtin(name, text.str());
        store_value(*result, Value::make_number(0.0));
        return std::nullopt;
    };
    
    // String functions
    builtin_table_["string_length"] = [this](const std::string&, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 1) {
            return make_result(VmStatus::RuntimeError, "string_length expects exactly one argument");
        }
        if (!args[0].is_string()) {
            return make_result(VmStatus::RuntimeError, "string_length expects a string argument");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "string_length requires destination for result");
        }
        const auto length = static_cast<double>(args[0].as_string().size());
        store_value(*result, Value::make_number(length));
        return std::nullopt;
    };

    builtin_table_["string_equals"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "string_equals expects exactly two arguments");
        }
        if (!args[0].is_string() || !args[1].is_string()) {
            return make_result(VmStatus::RuntimeError, "string_equals expects string arguments");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "string_equals requires destination for result");
        }
        const bool equal = args[0].as_string() == args[1].as_string();
        store_value(*result, Value::make_number(equal ? 1.0 : 0.0));
        trace_builtin(name, equal ? "true" : "false");
        return std::nullopt;
    };

    builtin_table_["string_concat"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "string_concat expects exactly two arguments");
        }
        if (!args[0].is_string() || !args[1].is_string()) {
            return make_result(VmStatus::RuntimeError, "string_concat expects string arguments");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "string_concat requires destination for result");
        }
        std::string combined{args[0].as_string()};
        combined.append(args[1].as_string());
        trace_builtin(name, combined);
        store_value(*result, Value::make_string(std::move(combined)));
        return std::nullopt;
    };

    builtin_table_["string_repeat"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "string_repeat expects exactly two arguments");
        }
        if (!args[0].is_string()) {
            return make_result(VmStatus::RuntimeError, "string_repeat expects a string value");
        }
        if (!args[1].is_number()) {
            return make_result(VmStatus::RuntimeError, "string_repeat expects a numeric repeat count");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "string_repeat requires destination for result");
        }
        const auto maybeCount = to_index(args[1].number);
        if (!maybeCount.has_value()) {
            return make_result(VmStatus::RuntimeError, "string_repeat count must be a non-negative integer");
        }
        const std::string_view pattern = args[0].as_string();
        std::string repeated;
        repeated.reserve(pattern.size() * *maybeCount);
        for (std::size_t i = 0; i < *maybeCount; ++i) {
            repeated.append(pattern);
        }
        trace_builtin(name, repeated);
        store_value(*result, Value::make_string(std::move(repeated)));
        return std::nullopt;
    };

    builtin_table_["string_slice"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 3) {
            return make_result(VmStatus::RuntimeError, "string_slice expects exactly three arguments");
        }
        if (!args[0].is_string()) {
            return make_result(VmStatus::RuntimeError, "string_slice expects a string value");
        }
        if (!args[1].is_number() || !args[2].is_number()) {
            return make_result(VmStatus::RuntimeError, "string_slice expects numeric start/count arguments");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "string_slice requires destination for result");
        }
        const auto maybeStart = to_index(args[1].number);
        const auto maybeCount = to_index(args[2].number);
        if (!maybeStart.has_value() || !maybeCount.has_value()) {
            return make_result(VmStatus::RuntimeError, "string_slice start/count must be non-negative integers");
        }
        const std::string_view text = args[0].as_string();
        if (*maybeStart > text.size()) {
            return make_result(VmStatus::RuntimeError, "string_slice start exceeds string length");
        }
        if (*maybeStart + *maybeCount > text.size()) {
            return make_result(VmStatus::RuntimeError, "string_slice exceeds string bounds");
        }
        std::string sliced{text.substr(*maybeStart, *maybeCount)};
        trace_builtin(name, sliced);
        store_value(*result, Value::make_string(std::move(sliced)));
        return std::nullopt;
    };

    builtin_table_["string_lower"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 1) {
            return make_result(VmStatus::RuntimeError, name + " expects exactly one argument");
        }
        if (!args[0].is_string()) {
            return make_result(VmStatus::RuntimeError, name + " expects a string argument");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, name + " requires destination for result");
        }
        std::string transformed{args[0].as_string()};
        for (char& ch : transformed) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        trace_builtin(name, transformed);
        store_value(*result, Value::make_string(std::move(transformed)));
        return std::nullopt;
    };
    
    builtin_table_["string_upper"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 1) {
            return make_result(VmStatus::RuntimeError, name + " expects exactly one argument");
        }
        if (!args[0].is_string()) {
            return make_result(VmStatus::RuntimeError, name + " expects a string argument");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, name + " requires destination for result");
        }
        std::string transformed{args[0].as_string()};
        for (char& ch : transformed) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        trace_builtin(name, transformed);
        store_value(*result, Value::make_string(std::move(transformed)));
        return std::nullopt;
    };
    
    builtin_table_["string_trim"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 1) {
            return make_result(VmStatus::RuntimeError, "string_trim expects exactly one argument");
        }
        if (!args[0].is_string()) {
            return make_result(VmStatus::RuntimeError, "string_trim expects a string argument");
        }
        if (!result.has_value()) {
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
        trace_builtin(name, trimmed);
        store_value(*result, Value::make_string(std::move(trimmed)));
        return std::nullopt;
    };

    // Array functions
    builtin_table_["array_push"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "array_push expects exactly two arguments");
        }
        if (!args[0].is_object() || args[0].as_object() == nullptr || args[0].as_object()->kind != ObjectKind::Array) {
            return make_result(VmStatus::RuntimeError, "array_push requires an array value");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "array_push requires destination for result");
        }
        GcObject* object = args[0].as_object();
        object->fields.push_back(args[1]);
        trace_builtin(name, "len=" + std::to_string(object->fields.size()));
        store_value(*result, args[0]);
        maybe_collect_();
        return std::nullopt;
    };

    builtin_table_["array_pop"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 1) {
            return make_result(VmStatus::RuntimeError, "array_pop expects exactly one argument");
        }
        if (!args[0].is_object() || args[0].as_object() == nullptr || args[0].as_object()->kind != ObjectKind::Array) {
            return make_result(VmStatus::RuntimeError, "array_pop requires an array value");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "array_pop requires destination for result");
        }
        GcObject* object = args[0].as_object();
        if (object->fields.empty()) {
            return make_result(VmStatus::RuntimeError, "array_pop cannot operate on an empty array");
        }
        Value popped = object->fields.back();
        object->fields.pop_back();
        trace_builtin(name, describe_value(popped));
        store_value(*result, popped);
        return std::nullopt;
    };

    builtin_table_["array_join"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "array_join expects exactly two arguments");
        }
        if (!args[0].is_object() || args[0].as_object() == nullptr || args[0].as_object()->kind != ObjectKind::Array) {
            return make_result(VmStatus::RuntimeError, "array_join requires an array value");
        }
        if (!args[1].is_string()) {
            return make_result(VmStatus::RuntimeError, "array_join expects a string separator");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "array_join requires destination for result");
        }
        const std::string_view separator = args[1].as_string();
        std::ostringstream builder;
        const auto& elements = args[0].as_object()->fields;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const Value& value = elements[i];
            if (i != 0) builder << separator;
            if (value.is_string()) {
                builder << value.as_string();
            } else if (value.is_number()) {
                builder << format_number(value.number);
            } else if (value.is_nil()) {
                continue;
            } else {
                return make_result(VmStatus::RuntimeError, "array_join encountered unsupported element type");
            }
        }
        const std::string merged = builder.str();
        trace_builtin(name, merged);
        store_value(*result, Value::make_string(merged));
        return std::nullopt;
    };

    builtin_table_["array_fill"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "array_fill expects exactly two arguments");
        }
        if (!args[0].is_object() || args[0].as_object() == nullptr || args[0].as_object()->kind != ObjectKind::Array) {
            return make_result(VmStatus::RuntimeError, "array_fill requires an array value");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "array_fill requires destination for result");
        }
        GcObject* object = args[0].as_object();
        for (auto& field : object->fields) {
            field = args[1];
        }
        trace_builtin(name, "len=" + std::to_string(object->fields.size()));
        store_value(*result, args[0]);
        return std::nullopt;
    };

    builtin_table_["array_sum"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 1) {
            return make_result(VmStatus::RuntimeError, "array_sum expects exactly one argument");
        }
        if (!args[0].is_object() || args[0].as_object() == nullptr || args[0].as_object()->kind != ObjectKind::Array) {
            return make_result(VmStatus::RuntimeError, "array_sum requires an array value");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "array_sum requires destination for result");
        }
        double total = 0.0;
        for (const auto& field : args[0].as_object()->fields) {
            if (field.is_number()) {
                total += field.number;
            } else if (!field.is_nil()) {
                return make_result(VmStatus::RuntimeError, "array_sum encountered non-numeric element");
            }
        }
        trace_builtin(name, std::to_string(total));
        store_value(*result, Value::make_number(total));
        return std::nullopt;
    };

    builtin_table_["read_line"] = [this](const std::string& name, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (!args.empty()) {
            return make_result(VmStatus::RuntimeError, "read_line expects no arguments");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "read_line requires destination for result");
        }
        std::string line;
        if (read_line_) {
            if (auto fetched = read_line_()) {
                line = std::move(*fetched);
            }
        }
        trace_builtin(name, line);
        store_value(*result, Value::make_string(std::move(line)));
        return std::nullopt;
    };
    
    // Math functions - simple unary
    auto make_unary_math = [this](const std::string& name, double (*func)(double)) {
        builtin_table_[name] = [this, func, name](const std::string&, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
            if (args.size() != 1) {
                return make_result(VmStatus::RuntimeError, name + " expects exactly one argument");
            }
            if (!args[0].is_number()) {
                return make_result(VmStatus::RuntimeError, name + " expects a numeric argument");
            }
            if (!result.has_value()) {
                return make_result(VmStatus::ModuleError, name + " requires destination for result");
            }
            const double res = func(args[0].number);
            store_value(*result, Value::make_number(res));
            return std::nullopt;
        };
    };
    
    make_unary_math("sqrt", std::sqrt);
    make_unary_math("std::math::sqrt", std::sqrt);
    make_unary_math("sin", std::sin);
    make_unary_math("std::math::sin", std::sin);
    make_unary_math("cos", std::cos);
    make_unary_math("std::math::cos", std::cos);
    make_unary_math("tan", std::tan);
    make_unary_math("std::math::tan", std::tan);
    make_unary_math("abs", std::abs);
    make_unary_math("std::math::abs", std::abs);
    make_unary_math("floor", std::floor);
    make_unary_math("std::math::floor", std::floor);
    make_unary_math("ceil", std::ceil);
    make_unary_math("std::math::ceil", std::ceil);
    make_unary_math("round", std::round);
    make_unary_math("std::math::round", std::round);
    make_unary_math("exp", std::exp);
    make_unary_math("std::math::exp", std::exp);
    make_unary_math("log", std::log);
    make_unary_math("std::math::log", std::log);
    make_unary_math("log10", std::log10);
    make_unary_math("std::math::log10", std::log10);
    
    // Binary math function
    builtin_table_["pow"] = [this](const std::string&, const std::vector<Value>& args, const std::optional<ir::SsaValue>& result) -> std::optional<VmResult> {
        if (args.size() != 2) {
            return make_result(VmStatus::RuntimeError, "pow expects exactly two arguments");
        }
        if (!args[0].is_number() || !args[1].is_number()) {
            return make_result(VmStatus::RuntimeError, "pow expects numeric arguments");
        }
        if (!result.has_value()) {
            return make_result(VmStatus::ModuleError, "pow requires destination for result");
        }
        const double res = std::pow(args[0].number, args[1].number);
        store_value(*result, Value::make_number(res));
        return std::nullopt;
    };
    builtin_table_["std::math::pow"] = builtin_table_["pow"];
}

}  // namespace impulse::runtime




