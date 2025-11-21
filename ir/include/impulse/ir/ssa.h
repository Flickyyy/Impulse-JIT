#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "impulse/ir/ir.h"

namespace impulse::ir {

struct SsaValue {
    std::string name;
    std::uint32_t version = 0;

    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] auto operator==(const SsaValue& other) const -> bool {
        return name == other.name && version == other.version;
    }
};

struct SsaPhiInput {
    std::size_t predecessor = 0;
    std::optional<SsaValue> value;
};

struct SsaPhiNode {
    std::string variable;
    SsaValue result;
    std::vector<SsaPhiInput> inputs;
};

enum class SsaOp : std::uint8_t {
    Literal,
    Binary,
    Unary,
    Call,
    Store,
    Drop,
    Return,
    Branch,
    BranchIf,
};

struct SsaInstruction {
    SsaOp op = SsaOp::Literal;
    std::optional<SsaValue> result;
    std::vector<SsaValue> arguments;
    std::vector<std::string> immediates;
};

struct SsaBlock {
    std::string name;
    std::vector<SsaPhiNode> phi_nodes;
    std::vector<SsaInstruction> instructions;
    std::vector<std::size_t> successors;
};

struct SsaFunction {
    std::string name;
    std::vector<SsaBlock> blocks;
};

[[nodiscard]] auto build_ssa(const Function& function) -> SsaFunction;

}  // namespace impulse::ir
