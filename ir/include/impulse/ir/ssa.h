#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/cfg.h"

namespace impulse::ir {

using SymbolId = std::uint32_t;

struct SsaValue {
    SymbolId symbol = 0;
    std::uint32_t version = 0;

    [[nodiscard]] auto is_valid() const -> bool { return version != 0 || symbol != 0; }
    [[nodiscard]] auto to_string() const -> std::string {
        return "v" + std::to_string(symbol) + "." + std::to_string(version);
    }
};

struct PhiInput {
    std::size_t predecessor = 0;
    std::optional<SsaValue> value;
};

struct PhiNode {
    SsaValue result;
    SymbolId symbol = 0;
    std::vector<PhiInput> inputs;
};

struct SsaInstruction {
    std::string opcode;
    std::vector<SsaValue> arguments;
    std::vector<std::string> immediates;
    std::optional<SsaValue> result;
};

struct SsaBlock {
    std::size_t id = 0;
    std::string name;
    std::vector<PhiNode> phi_nodes;
    std::vector<SsaInstruction> instructions;
    std::vector<std::size_t> successors;
    std::vector<std::size_t> predecessors;
    std::size_t immediate_dominator = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> dominator_children;
    std::vector<std::size_t> dominance_frontier;
};

struct SsaSymbol {
    SymbolId id = 0;
    std::string name;
    std::string type;
};

struct SsaFunction {
    std::string name;
    std::vector<SsaSymbol> symbols;
    std::vector<SsaBlock> blocks;
    // Performance optimization: O(1) symbol lookup by ID
    std::unordered_map<SymbolId, const SsaSymbol*> symbol_id_index_;
    // Performance optimization: O(1) symbol lookup by name
    std::unordered_map<std::string, const SsaSymbol*> symbol_name_index_;

    [[nodiscard]] auto find_block(const std::string& block_name) const -> const SsaBlock*;
    [[nodiscard]] auto find_symbol(SymbolId id) const -> const SsaSymbol*;
    [[nodiscard]] auto find_symbol(const std::string& name) const -> const SsaSymbol*;
};

[[nodiscard]] auto build_ssa(const Function& function, const ControlFlowGraph& cfg) -> SsaFunction;
[[nodiscard]] auto build_ssa(const Function& function) -> SsaFunction;

}  // namespace impulse::ir
