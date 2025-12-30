#include "impulse/ir/cfg.h"

#include <algorithm>
#include <unordered_set>

namespace impulse::ir {

namespace {

[[nodiscard]] auto is_terminator(InstructionKind kind) -> bool {
    switch (kind) {
        case InstructionKind::Branch:
        case InstructionKind::BranchIf:
        case InstructionKind::Return:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] auto first_non_empty_label(const Function& function) -> std::string {
    if (function.blocks.empty()) {
        return "entry";
    }
    for (const auto& block : function.blocks) {
        if (!block.label.empty()) {
            return block.label;
        }
    }
    return "entry";
}

}  // namespace

auto ControlFlowGraph::find_block(const std::string& label) const -> const Block* {
    const auto it = label_to_block.find(label);
    if (it == label_to_block.end()) {
        return nullptr;
    }
    const std::size_t index = it->second;
    if (index >= blocks.size()) {
        return nullptr;
    }
    return &blocks[index];
}

auto build_control_flow_graph(const Function& function) -> ControlFlowGraph {
    ControlFlowGraph cfg;

    std::vector<const Instruction*> flat_instructions;
    std::vector<std::size_t> instruction_block_index;
    std::vector<std::size_t> block_first_instruction_index(function.blocks.size(), static_cast<std::size_t>(-1));
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& block = function.blocks[block_index];
        const std::size_t first_index = flat_instructions.size();
        for (const auto& inst : block.instructions) {
            flat_instructions.push_back(&inst);
            instruction_block_index.push_back(block_index);
        }
        if (!block.instructions.empty()) {
            block_first_instruction_index[block_index] = first_index;
        }
    }

    cfg.instructions = flat_instructions;

    if (flat_instructions.empty()) {
        ControlFlowGraph::Block empty_block;
        empty_block.name = first_non_empty_label(function);
        empty_block.start_index = 0;
        empty_block.end_index = 0;
        cfg.blocks.push_back(std::move(empty_block));
        cfg.label_to_block[cfg.blocks.front().name] = 0;
        return cfg;
    }

    std::unordered_set<std::size_t> leader_indices;
    leader_indices.insert(0);
    for (std::size_t i = 0; i < flat_instructions.size(); ++i) {
        const auto* inst = flat_instructions[i];
        if (inst == nullptr) {
            continue;
        }
        if (inst->kind == InstructionKind::Label) {
            leader_indices.insert(i);
        } else if (is_terminator(inst->kind) && i + 1 < flat_instructions.size()) {
            leader_indices.insert(i + 1);
        }
    }

    std::vector<std::size_t> leaders(leader_indices.begin(), leader_indices.end());
    std::sort(leaders.begin(), leaders.end());

    if (leaders.back() != flat_instructions.size()) {
        leaders.push_back(flat_instructions.size());
    }

    cfg.blocks.reserve(leaders.size() - 1);
    for (std::size_t idx = 0; idx + 1 < leaders.size(); ++idx) {
        const std::size_t start = leaders[idx];
        const std::size_t end = leaders[idx + 1];

        ControlFlowGraph::Block block;
        block.start_index = start;
        block.end_index = end;

        std::string label;
        if (start < flat_instructions.size()) {
            const auto* first_inst = flat_instructions[start];
            if (first_inst != nullptr && first_inst->kind == InstructionKind::Label && !first_inst->operands.empty()) {
                label = first_inst->operands.front();
            }
        }

        if (label.empty() && start < instruction_block_index.size()) {
            const auto original_block_index = instruction_block_index[start];
            if (original_block_index < function.blocks.size() &&
                block_first_instruction_index[original_block_index] == start) {
                label = function.blocks[original_block_index].label;
            }
        }

        if (label.empty()) {
            label = "block" + std::to_string(idx);
        }

        block.name = label;
        if (!label.empty() && cfg.label_to_block.find(label) == cfg.label_to_block.end()) {
            cfg.label_to_block[label] = cfg.blocks.size();
        }
        cfg.blocks.push_back(std::move(block));
    }

    for (std::size_t idx = 0; idx < cfg.blocks.size(); ++idx) {
        auto& block = cfg.blocks[idx];
        const Instruction* terminator = nullptr;
        for (std::size_t i = block.end_index; i > block.start_index; --i) {
            const auto* candidate = flat_instructions[i - 1];
            if (candidate == nullptr) {
                continue;
            }
            if (candidate->kind == InstructionKind::Label || candidate->kind == InstructionKind::Comment) {
                continue;
            }
            terminator = candidate;
            break;
        }

        if (terminator == nullptr) {
            if (idx + 1 < cfg.blocks.size()) {
                block.successors.push_back(idx + 1);
            }
            continue;
        }

        switch (terminator->kind) {
            case InstructionKind::Branch: {
                if (!terminator->operands.empty()) {
                    const auto it = cfg.label_to_block.find(terminator->operands.front());
                    if (it != cfg.label_to_block.end()) {
                        block.successors.push_back(it->second);
                    }
                }
                break;
            }
            case InstructionKind::BranchIf: {
                if (terminator->operands.size() >= 1) {
                    const auto it = cfg.label_to_block.find(terminator->operands.front());
                    if (it != cfg.label_to_block.end()) {
                        block.successors.push_back(it->second);
                    }
                }
                if (idx + 1 < cfg.blocks.size()) {
                    block.successors.push_back(idx + 1);
                }
                break;
            }
            case InstructionKind::Return:
                break;
            default: {
                if (idx + 1 < cfg.blocks.size()) {
                    block.successors.push_back(idx + 1);
                }
                break;
            }
        }
    }

    for (std::size_t idx = 0; idx < cfg.blocks.size(); ++idx) {
        for (const auto succ : cfg.blocks[idx].successors) {
            if (succ < cfg.blocks.size()) {
                cfg.blocks[succ].predecessors.push_back(idx);
            }
        }
    }

    return cfg;
}

}  // namespace impulse::ir
