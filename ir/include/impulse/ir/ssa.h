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

// Enum-based opcode for fast dispatch (avoids string comparison in hot path)
enum class SsaOpcode : std::uint8_t {
    // Literals and assignments
    Literal,
    LiteralString,
    Assign,
    
    // Arithmetic and logic
    Binary,
    Unary,
    
    // Control flow
    Branch,
    BranchIf,
    Return,
    
    // Function calls
    Call,
    
    // Array operations
    ArrayMake,
    ArrayGet,
    ArraySet,
    ArrayLength,
    ArrayPush,
    ArrayPop,
    
    // Other
    Drop,
    
    // Fallback for unknown opcodes
    Unknown
};

// Convert string opcode to enum (called once during SSA construction)
[[nodiscard]] inline auto opcode_from_string(const std::string& s) -> SsaOpcode {
    // Most common opcodes first for better branch prediction
    if (s == "binary") return SsaOpcode::Binary;
    if (s == "literal") return SsaOpcode::Literal;
    if (s == "assign") return SsaOpcode::Assign;
    if (s == "branch_if") return SsaOpcode::BranchIf;
    if (s == "return") return SsaOpcode::Return;
    if (s == "call") return SsaOpcode::Call;
    if (s == "branch") return SsaOpcode::Branch;
    if (s == "array_get") return SsaOpcode::ArrayGet;
    if (s == "array_set") return SsaOpcode::ArraySet;
    if (s == "array_make") return SsaOpcode::ArrayMake;
    if (s == "array_length") return SsaOpcode::ArrayLength;
    if (s == "array_push") return SsaOpcode::ArrayPush;
    if (s == "array_pop") return SsaOpcode::ArrayPop;
    if (s == "unary") return SsaOpcode::Unary;
    if (s == "literal_string") return SsaOpcode::LiteralString;
    if (s == "drop") return SsaOpcode::Drop;
    return SsaOpcode::Unknown;
}

// Convert enum to string (for debugging/printing)
[[nodiscard]] inline auto opcode_to_string(SsaOpcode op) -> const char* {
    switch (op) {
        case SsaOpcode::Literal: return "literal";
        case SsaOpcode::LiteralString: return "literal_string";
        case SsaOpcode::Assign: return "assign";
        case SsaOpcode::Binary: return "binary";
        case SsaOpcode::Unary: return "unary";
        case SsaOpcode::Branch: return "branch";
        case SsaOpcode::BranchIf: return "branch_if";
        case SsaOpcode::Return: return "return";
        case SsaOpcode::Call: return "call";
        case SsaOpcode::ArrayMake: return "array_make";
        case SsaOpcode::ArrayGet: return "array_get";
        case SsaOpcode::ArraySet: return "array_set";
        case SsaOpcode::ArrayLength: return "array_length";
        case SsaOpcode::ArrayPush: return "array_push";
        case SsaOpcode::ArrayPop: return "array_pop";
        case SsaOpcode::Drop: return "drop";
        case SsaOpcode::Unknown: return "unknown";
    }
    return "unknown";
}

// Enum for binary operators - for fast dispatch in hot path
enum class BinaryOp : std::uint8_t {
    Add,        // +
    Sub,        // -
    Mul,        // *
    Div,        // /
    Mod,        // %
    Lt,         // <
    Le,         // <=
    Gt,         // >
    Ge,         // >=
    Eq,         // ==
    Ne,         // !=
    And,        // &&
    Or,         // ||
    Unknown
};

[[nodiscard]] inline auto binary_op_from_string(const std::string& s) -> BinaryOp {
    if (s == "+") return BinaryOp::Add;
    if (s == "-") return BinaryOp::Sub;
    if (s == "*") return BinaryOp::Mul;
    if (s == "/") return BinaryOp::Div;
    if (s == "%") return BinaryOp::Mod;
    if (s == "<") return BinaryOp::Lt;
    if (s == "<=") return BinaryOp::Le;
    if (s == ">") return BinaryOp::Gt;
    if (s == ">=") return BinaryOp::Ge;
    if (s == "==") return BinaryOp::Eq;
    if (s == "!=") return BinaryOp::Ne;
    if (s == "&&") return BinaryOp::And;
    if (s == "||") return BinaryOp::Or;
    return BinaryOp::Unknown;
}

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
    SsaOpcode op = SsaOpcode::Unknown;  // Fast dispatch opcode
    BinaryOp binary_op = BinaryOp::Unknown;  // For binary instructions
    std::string opcode;                  // Original string (for debugging/unknown opcodes)
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
