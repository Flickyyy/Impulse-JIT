#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/ir.h"

namespace impulse::ir {

struct ControlFlowGraph {
    struct Block {
        std::string name;
        std::size_t start_index = 0;  // Inclusive index into flattened instruction list
        std::size_t end_index = 0;    // Exclusive index into flattened instruction list
        std::vector<std::size_t> successors;
        std::vector<std::size_t> predecessors;
    };

    std::vector<Block> blocks;
    std::vector<const Instruction*> instructions;
    std::unordered_map<std::string, std::size_t> label_to_block;

    [[nodiscard]] auto find_block(const std::string& label) const -> const Block*;
};

[[nodiscard]] auto build_control_flow_graph(const Function& function) -> ControlFlowGraph;

}  // namespace impulse::ir
