#include "impulse/ir/dead_code_elimination.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace impulse::ir {
namespace {

[[nodiscard]] auto value_key(const SsaValue& value) -> std::string {
    return value.to_string();
}

[[nodiscard]] auto is_removable_opcode(const std::string& opcode) -> bool {
    return opcode == "literal" || opcode == "unary" || opcode == "binary" || opcode == "assign";
}

}  // namespace

auto eliminate_dead_assignments(SsaFunction& function) -> bool {
    bool mutated = false;
    bool changed = true;

    while (changed) {
        changed = false;
        std::unordered_map<std::string, std::size_t> use_count;

        const auto note_use = [&](const SsaValue& value) {
            if (!value.is_valid()) {
                return;
            }
            ++use_count[value_key(value)];
        };

        for (const auto& block : function.blocks) {
            for (const auto& phi : block.phi_nodes) {
                for (const auto& input : phi.inputs) {
                    if (input.value.has_value()) {
                        note_use(*input.value);
                    }
                }
            }
            for (const auto& inst : block.instructions) {
                for (const auto& argument : inst.arguments) {
                    note_use(argument);
                }
            }
        }

        for (auto& block : function.blocks) {
            auto& phi_nodes = block.phi_nodes;
            for (auto it = phi_nodes.begin(); it != phi_nodes.end();) {
                const auto key = value_key(it->result);
                const auto count_it = use_count.find(key);
                const std::size_t count = count_it != use_count.end() ? count_it->second : 0;
                if (count == 0) {
                    it = phi_nodes.erase(it);
                    mutated = true;
                    changed = true;
                } else {
                    ++it;
                }
            }

            auto& instructions = block.instructions;
            for (auto it = instructions.begin(); it != instructions.end();) {
                if (!it->result.has_value() || !is_removable_opcode(it->opcode)) {
                    ++it;
                    continue;
                }
                const auto key = value_key(*it->result);
                const auto count_it = use_count.find(key);
                const std::size_t count = count_it != use_count.end() ? count_it->second : 0;
                if (count == 0) {
                    it = instructions.erase(it);
                    mutated = true;
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
    }

    return mutated;
}

}  // namespace impulse::ir
