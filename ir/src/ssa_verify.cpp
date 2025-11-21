#include "impulse/ir/ssa_verify.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace impulse::ir {
namespace {

constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

struct ValueDef {
    std::size_t block = kInvalidIndex;
    std::optional<std::size_t> instruction;
    bool is_phi = false;
};

struct DominatorInfo {
    std::vector<std::size_t> idom;
    std::vector<std::vector<std::size_t>> children;
    std::vector<std::size_t> preorder;
    std::vector<std::size_t> postorder;
};

void push_issue(SsaValidationResult& result, std::string message, std::size_t block,
                std::optional<std::size_t> instruction = std::nullopt) {
    result.issues.push_back(SsaValidationIssue{
        .message = std::move(message),
        .block = block,
        .instruction = instruction,
    });
}

[[nodiscard]] auto compute_predecessors(const SsaFunction& fn, SsaValidationResult& result)
    -> std::vector<std::vector<std::size_t>> {
    const std::size_t block_count = fn.blocks.size();
    std::vector<std::vector<std::size_t>> predecessors(block_count);

    for (std::size_t index = 0; index < block_count; ++index) {
        const auto& block = fn.blocks[index];
        for (const auto succ : block.successors) {
            if (succ >= block_count) {
                push_issue(result, "Successor index out of range", index);
                continue;
            }
            predecessors[succ].push_back(index);
        }
    }

    return predecessors;
}

[[nodiscard]] auto compute_reverse_postorder(const SsaFunction& fn, const std::vector<bool>& reachable)
    -> std::vector<std::size_t> {
    const std::size_t block_count = fn.blocks.size();
    std::vector<std::size_t> rpo;
    if (block_count == 0) {
        return rpo;
    }

    std::vector<bool> visited(block_count, false);
    std::vector<std::size_t> index_stack;
    std::vector<std::size_t> successor_index(block_count, 0);

    if (reachable[0]) {
        index_stack.push_back(0);
    }

    while (!index_stack.empty()) {
        const std::size_t node = index_stack.back();
        if (!visited[node]) {
            visited[node] = true;
        }

        auto& succ_pos = successor_index[node];
        const auto& successors = fn.blocks[node].successors;
        if (succ_pos < successors.size()) {
            const std::size_t succ = successors[succ_pos++];
            if (succ < block_count && reachable[succ] && !visited[succ]) {
                index_stack.push_back(succ);
            }
            continue;
        }

        index_stack.pop_back();
        rpo.push_back(node);
    }

    std::reverse(rpo.begin(), rpo.end());
    return rpo;
}

[[nodiscard]] auto compute_dominators(const SsaFunction& fn, const std::vector<std::vector<std::size_t>>& predecessors,
                                      const std::vector<bool>& reachable)
    -> DominatorInfo {
    const std::size_t block_count = fn.blocks.size();
    DominatorInfo info;
    info.idom.assign(block_count, kInvalidIndex);
    info.children.assign(block_count, {});
    info.preorder.assign(block_count, kInvalidIndex);
    info.postorder.assign(block_count, kInvalidIndex);

    if (block_count == 0 || !reachable[0]) {
        return info;
    }

    const auto rpo = compute_reverse_postorder(fn, reachable);
    if (rpo.empty()) {
        return info;
    }

    std::vector<std::size_t> rpo_position(block_count, kInvalidIndex);
    for (std::size_t i = 0; i < rpo.size(); ++i) {
        rpo_position[rpo[i]] = i;
    }

    info.idom[rpo.front()] = rpo.front();
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 1; i < rpo.size(); ++i) {
            const std::size_t block = rpo[i];
            if (!reachable[block]) {
                continue;
            }
            std::size_t new_idom = kInvalidIndex;
            for (const auto pred : predecessors[block]) {
                if (!reachable[pred]) {
                    continue;
                }
                if (info.idom[pred] == kInvalidIndex) {
                    continue;
                }
                if (new_idom == kInvalidIndex) {
                    new_idom = pred;
                } else {
                    std::size_t finger_a = pred;
                    std::size_t finger_b = new_idom;
                    while (finger_a != finger_b) {
                        while (rpo_position[finger_a] > rpo_position[finger_b]) {
                            finger_a = info.idom[finger_a];
                        }
                        while (rpo_position[finger_b] > rpo_position[finger_a]) {
                            finger_b = info.idom[finger_b];
                        }
                    }
                    new_idom = finger_a;
                }
            }
            if (new_idom != kInvalidIndex && info.idom[block] != new_idom) {
                info.idom[block] = new_idom;
                changed = true;
            }
        }
    }

    for (std::size_t block = 0; block < block_count; ++block) {
        if (!reachable[block]) {
            continue;
        }
        const std::size_t parent = info.idom[block];
        if (parent == kInvalidIndex || parent == block) {
            continue;
        }
        info.children[parent].push_back(block);
    }

    std::size_t preorder_counter = 0;
    std::size_t postorder_counter = 0;
    std::stack<std::pair<std::size_t, std::size_t>> stack;
    stack.emplace(rpo.front(), 0);
    while (!stack.empty()) {
        auto [node, child_index] = stack.top();
        if (child_index == 0 && info.preorder[node] == kInvalidIndex) {
            info.preorder[node] = preorder_counter++;
        }

        if (child_index < info.children[node].size()) {
            const std::size_t child = info.children[node][child_index];
            stack.top().second += 1;
            if (info.preorder[child] == kInvalidIndex) {
                stack.emplace(child, 0);
            }
            continue;
        }

        info.postorder[node] = postorder_counter++;
        stack.pop();
    }

    return info;
}

[[nodiscard]] auto dominates(const DominatorInfo& info, std::size_t a, std::size_t b) -> bool {
    if (a == b) {
        return true;
    }
    if (a >= info.preorder.size() || b >= info.preorder.size()) {
        return false;
    }
    if (info.preorder[a] == kInvalidIndex || info.postorder[a] == kInvalidIndex) {
        return false;
    }
    if (info.preorder[b] == kInvalidIndex || info.postorder[b] == kInvalidIndex) {
        return false;
    }
    return info.preorder[a] <= info.preorder[b] && info.postorder[a] >= info.postorder[b];
}

}  // namespace

auto validate_ssa(const SsaFunction& fn) -> SsaValidationResult {
    SsaValidationResult result;
    const std::size_t block_count = fn.blocks.size();
    if (block_count == 0) {
        result.success = true;
        return result;
    }

    const auto predecessors = compute_predecessors(fn, result);

    std::vector<bool> reachable(block_count, false);
    std::stack<std::size_t> stack;
    reachable[0] = true;
    stack.push(0);
    while (!stack.empty()) {
        const std::size_t node = stack.top();
        stack.pop();
        for (const auto succ : fn.blocks[node].successors) {
            if (succ >= block_count) {
                continue;
            }
            if (!reachable[succ]) {
                reachable[succ] = true;
                stack.push(succ);
            }
        }
    }

    for (std::size_t block = 0; block < block_count; ++block) {
        if (!reachable[block]) {
            push_issue(result, "Unreachable block", block);
        }
    }

    const auto dom_info = compute_dominators(fn, predecessors, reachable);

    std::unordered_map<std::string, ValueDef> definitions;
    definitions.reserve(block_count * 4);

    const auto record_definition = [&](const SsaValue& value, std::size_t block, std::optional<std::size_t> instruction,
                                      bool is_phi) {
        const std::string key = value.to_string();
        const auto [it, inserted] = definitions.emplace(key, ValueDef{block, instruction, is_phi});
        if (!inserted) {
            push_issue(result, "Duplicate definition for value " + key, block, instruction);
        }
    };

    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto& block = fn.blocks[block_index];
        const auto& preds = predecessors[block_index];
        const std::size_t pred_count = preds.size();

        for (std::size_t phi_index = 0; phi_index < block.phi_nodes.size(); ++phi_index) {
            const auto& phi = block.phi_nodes[phi_index];
            record_definition(phi.result, block_index, std::nullopt, true);

            if (phi.inputs.size() != pred_count) {
                push_issue(result, "Phi node input count does not match predecessor count", block_index, phi_index);
            }

            std::unordered_set<std::size_t> phi_predecessors;
            for (const auto& input : phi.inputs) {
                if (input.predecessor >= block_count) {
                    push_issue(result, "Phi input references invalid predecessor", block_index, phi_index);
                }
                phi_predecessors.insert(input.predecessor);
            }

            if (phi.inputs.size() == pred_count && phi_predecessors.size() != pred_count) {
                push_issue(result, "Phi node has duplicate predecessors", block_index, phi_index);
            }
        }

        for (std::size_t inst_index = 0; inst_index < block.instructions.size(); ++inst_index) {
            const auto& inst = block.instructions[inst_index];
            if (inst.result.has_value()) {
                record_definition(*inst.result, block_index, inst_index, false);
            }
        }
    }

    for (std::size_t block_index = 0; block_index < block_count; ++block_index) {
        const auto& block = fn.blocks[block_index];
        const auto& preds = predecessors[block_index];

        for (std::size_t phi_index = 0; phi_index < block.phi_nodes.size(); ++phi_index) {
            const auto& phi = block.phi_nodes[phi_index];
            std::unordered_set<std::size_t> seen;
            for (const auto& input : phi.inputs) {
                if (!input.value.has_value()) {
                    push_issue(result, "Phi input missing value", block_index, phi_index);
                    continue;
                }
                const auto key = input.value->to_string();
                const auto def_it = definitions.find(key);
                if (def_it == definitions.end()) {
                    if (!(input.value->version == 0 && input.value->name.rfind("tmp", 0) != 0U)) {
                        push_issue(result, "Phi input uses undefined value " + key, block_index, phi_index);
                    }
                    continue;
                }
                const auto& def = def_it->second;
                if (input.predecessor >= block_count) {
                    continue;
                }
                if (std::find(preds.begin(), preds.end(), input.predecessor) == preds.end()) {
                    push_issue(result, "Phi input predecessor not in CFG", block_index, phi_index);
                }
                if (!dominates(dom_info, def.block, input.predecessor)) {
                    push_issue(result, "Definition of value " + key + " does not dominate predecessor", block_index,
                               phi_index);
                }
                seen.insert(input.predecessor);
            }
            if (phi.inputs.size() == preds.size() && seen.size() != preds.size()) {
                push_issue(result, "Phi node missing inputs for some predecessors", block_index, phi_index);
            }
        }

        for (std::size_t inst_index = 0; inst_index < block.instructions.size(); ++inst_index) {
            const auto& inst = block.instructions[inst_index];
            for (const auto& arg : inst.arguments) {
                const auto key = arg.to_string();
                const auto def_it = definitions.find(key);
                if (def_it == definitions.end()) {
                    if (!(arg.version == 0 && arg.name.rfind("tmp", 0) != 0U)) {
                        push_issue(result, "Use of undefined value " + key, block_index, inst_index);
                    }
                    continue;
                }
                const auto& def = def_it->second;
                if (def.block == block_index) {
                    if (!def.is_phi) {
                        if (!def.instruction.has_value() || def.instruction.value() >= inst_index) {
                            push_issue(result, "Value " + key + " used before definition", block_index, inst_index);
                        }
                    }
                } else if (!dominates(dom_info, def.block, block_index)) {
                    push_issue(result, "Definition of value " + key + " does not dominate use", block_index,
                               inst_index);
                }
            }
        }
    }

    result.success = result.issues.empty();
    return result;
}

}  // namespace impulse::ir
