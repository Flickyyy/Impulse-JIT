#include "impulse/ir/copy_propagation.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace impulse::ir {
namespace {

[[nodiscard]] auto make_key(const SsaValue& value) -> std::string {
    return value.to_string();
}

[[nodiscard]] auto resolve(const SsaValue& value, std::unordered_map<std::string, SsaValue>& mapping)
    -> SsaValue {
    auto current = value;
    auto key = make_key(current);
    auto it = mapping.find(key);
    if (it == mapping.end()) {
        return current;
    }
    // Path compression
    const auto resolved = resolve(it->second, mapping);
    if (resolved.symbol != it->second.symbol || resolved.version != it->second.version) {
        it->second = resolved;
    }
    return resolved;
}

[[nodiscard]] auto values_equal(const SsaValue& lhs, const SsaValue& rhs) -> bool {
    return lhs.symbol == rhs.symbol && lhs.version == rhs.version;
}

}  // namespace

auto propagate_copies(SsaFunction& function) -> bool {
    bool mutated = false;
    std::unordered_map<std::string, SsaValue> replacements;

    const auto record_replacement = [&](const SsaValue& destination, const SsaValue& source) {
        if (!destination.is_valid() || !source.is_valid()) {
            return;
        }
        if (values_equal(destination, source)) {
            return;
        }
        const auto canonical = resolve(source, replacements);
        if (values_equal(destination, canonical)) {
            return;
        }
        replacements[make_key(destination)] = canonical;
        mutated = true;
    };

    for (const auto& block : function.blocks) {
        for (const auto& phi : block.phi_nodes) {
            if (phi.inputs.empty()) {
                continue;
            }
            std::optional<SsaValue> canonical;
            bool divergent = false;
            for (const auto& input : phi.inputs) {
                if (!input.value.has_value()) {
                    divergent = true;
                    break;
                }
                const auto resolved = resolve(*input.value, replacements);
                if (!canonical.has_value()) {
                    canonical = resolved;
                    continue;
                }
                if (!values_equal(*canonical, resolved)) {
                    divergent = true;
                    break;
                }
            }
            if (!divergent && canonical.has_value()) {
                record_replacement(phi.result, *canonical);
            }
        }

        for (const auto& inst : block.instructions) {
            if (inst.opcode != "assign") {
                continue;
            }
            if (!inst.result.has_value() || inst.arguments.size() != 1) {
                continue;
            }
            record_replacement(*inst.result, inst.arguments.front());
        }
    }

    if (replacements.empty()) {
        return mutated;
    }

    for (auto& block : function.blocks) {
        for (auto& phi : block.phi_nodes) {
            for (auto& input : phi.inputs) {
                if (!input.value.has_value()) {
                    continue;
                }
                const auto resolved = resolve(*input.value, replacements);
                if (!values_equal(resolved, *input.value)) {
                    input.value = resolved;
                    mutated = true;
                }
            }
        }

        for (auto& inst : block.instructions) {
            for (auto& argument : inst.arguments) {
                const auto resolved = resolve(argument, replacements);
                if (!values_equal(argument, resolved)) {
                    argument = resolved;
                    mutated = true;
                }
            }
        }
    }

    return mutated;
}

}  // namespace impulse::ir
