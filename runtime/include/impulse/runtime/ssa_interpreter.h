#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/ir.h"
#include "impulse/ir/ssa.h"
#include "impulse/runtime/runtime.h"
#include "impulse/runtime/runtime_utils.h"
#include "impulse/runtime/value.h"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace impulse::runtime {

class SsaInterpreter {
public:
    using CallFunction = std::function<VmResult(const ir::Function&, const std::vector<Value>&)>;
    using AllocateArray = std::function<GcObject*(std::size_t)>;
    using MaybeCollect = std::function<void()>;
    using ReadLine = std::function<std::optional<std::string>()>;
    using BuiltinHandler = std::function<std::optional<VmResult>(const std::string&, const std::vector<Value>&, const std::optional<ir::SsaValue>&)>;
    using InstructionHandler = std::function<std::optional<VmResult>(const ir::SsaBlock&, const ir::SsaInstruction&, std::size_t, std::optional<std::size_t>&, bool&)>;
    using BinaryOpHandler = std::function<std::optional<VmResult>(double, double, const std::optional<ir::SsaValue>&)>;

    SsaInterpreter(const ir::SsaFunction& ssa, const std::unordered_map<std::string, Value>& parameters,
                   std::unordered_map<std::string, Value>& locals, const std::vector<ir::Function>& functions,
                   const std::unordered_map<std::string, Value>& globals, CallFunction call_function,
                   AllocateArray allocate_array, MaybeCollect maybe_collect, std::string* output_buffer,
                   std::ostream* trace, ReadLine read_line);

    [[nodiscard]] auto run() -> VmResult;

private:
    // Hot-path I/O functions - inline for performance
    inline void append_output(const std::string& text, bool newline) const {
        if (output_buffer_ == nullptr) {
            return;
        }
        output_buffer_->append(text);
        if (newline) {
            output_buffer_->push_back('\n');
        }
    }

    inline void trace_builtin(const std::string& name, std::string_view payload) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    builtin " << name;
        if (!payload.empty()) {
            *trace_ << " \"" << escape_string(payload) << "\"";
        }
        *trace_ << '\n';
    }
    [[nodiscard]] auto materialize_phi(const ir::SsaBlock& block, std::optional<std::size_t> previous) -> std::optional<VmResult>;
    
    // Instruction handler functions
    [[nodiscard]] auto handle_literal(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_literal_string(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_unary(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_binary(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_assign(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_drop(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_return(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_branch(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_branch_if(const ir::SsaBlock& block, const ir::SsaInstruction& inst, std::size_t current, std::optional<std::size_t>& previous, bool& jumped) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_call(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_array_make(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_array_get(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_array_set(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    [[nodiscard]] auto handle_array_length(const ir::SsaBlock&, const ir::SsaInstruction& inst, std::size_t, std::optional<std::size_t>&, bool&) -> std::optional<VmResult>;
    
    void init_opcode_handlers();
    void init_binary_op_handlers();
    void init_builtin_table();
    
    [[nodiscard]] auto execute_instruction(const ir::SsaBlock& block, const ir::SsaInstruction& inst,
                                           std::size_t current,
                                           std::optional<std::size_t>& previous, bool& jumped) -> std::optional<VmResult>;
    
    // Hot-path functions - inline for performance
    [[nodiscard]] inline auto lookup_value(const ir::SsaValue& value) -> std::optional<Value> {
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
        }

        return std::nullopt;
    }

    inline void store_value(const ir::SsaValue& value, const Value& data) {
        if (!value.is_valid()) {
            return;
        }
        const auto key = encode_value_id(value);
        value_cache_[key] = data;

        // Optimize: only allocate string if needed for locals storage
        const std::string storage_key = "$ssa:" + value.to_string();
        locals_[storage_key] = data;

        if (const auto* symbol = ssa_.find_symbol(value.symbol); symbol != nullptr) {
            if (!symbol->name.empty()) {
                locals_[symbol->name] = data;
            }
        }

        // Inline trace check to avoid function call overhead when tracing is disabled
        if (trace_ != nullptr) {
            trace_store(value, data);
        }
    }
    // Hot-path control flow functions - inline for performance
    [[nodiscard]] inline auto resolve_block(const std::string& label) const -> std::optional<std::size_t> {
        const auto it = block_lookup_.find(label);
        if (it == block_lookup_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] inline auto pick_fallthrough(const ir::SsaBlock& block, std::optional<std::size_t> exclude = std::nullopt) const -> std::optional<std::size_t> {
        for (const auto succ : block.successors) {
            if (exclude.has_value() && succ == *exclude) {
                continue;
            }
            return succ;
        }
        return std::nullopt;
    }
    
    // Trace functions - inline for performance (nullptr check is fast, compiler optimizes away when disabled)
    inline void trace_block_entry(const ir::SsaBlock& block) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "enter block " << block.id;
        if (!block.name.empty()) {
            *trace_ << " (" << block.name << ')';
        }
        *trace_ << '\n';
    }

    inline void trace_phi_materialization(const ir::SsaBlock& block, const ir::PhiNode& phi, const Value& value) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    phi " << format_ssa_value(phi.result) << " := " << describe_value(value);
        *trace_ << " in block " << block.id << '\n';
    }

    inline void trace_instruction(const ir::SsaBlock& block, const ir::SsaInstruction& inst) const {
        if (trace_ == nullptr) {
            return;
        }
        (void)block;
        *trace_ << "    " << format_ssa_instruction(inst) << '\n';
    }

    inline void trace_store(const ir::SsaValue& destination, const Value& value) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "      -> " << format_ssa_value(destination) << " = " << describe_value(value) << '\n';
    }

    inline void trace_return(const Value& value) const {
        if (trace_ == nullptr) {
            return;
        }
        *trace_ << "    return " << describe_value(value) << '\n';
    }

    inline void trace_branch(std::size_t target, bool taken) const {
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
    std::unordered_map<std::string, BuiltinHandler> builtin_table_;
    std::unordered_map<std::string, InstructionHandler> opcode_handlers_;
    std::unordered_map<std::string, BinaryOpHandler> binary_op_handlers_;
    std::optional<std::size_t> next_block_;
    CallFunction call_function_;
    AllocateArray allocate_array_;
    MaybeCollect maybe_collect_;
    ReadLine read_line_;
    std::string* output_buffer_ = nullptr;
    std::ostream* trace_ = nullptr;
};

}  // namespace impulse::runtime



