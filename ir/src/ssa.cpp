#include "impulse/ir/ssa.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "impulse/ir/cfg.h"

namespace impulse::ir {

auto SsaValue::to_string() const -> std::string {
    return name + "." + std::to_string(version);
}

namespace {

constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

enum class ValueKind : std::uint8_t {
    Variable,
    Temporary,
    External,
};

struct ValueRef {
    ValueKind kind = ValueKind::Temporary;
    std::string name;
};

struct PreInstruction {
    SsaOp op = SsaOp::Literal;
    std::optional<ValueRef> result;
    std::optional<SsaValue> fixed_result;
    std::vector<ValueRef> arguments;
    std::vector<std::string> immediates;
};

struct PreBlock {
    std::string name;
    std::vector<PreInstruction> instructions;
};

class SsaBuilder {
   public:
    explicit SsaBuilder(const Function& function) : function_(function) {}

    auto build() -> SsaFunction {
        result_.name = function_.name;
        if (function_.blocks.empty()) {
            return result_;
        }

        cfg_ = build_control_flow_graph(function_);
        if (cfg_.blocks.empty()) {
            return result_;
        }

        entry_block_ = 0;
        pre_blocks_.resize(cfg_.blocks.size());
        phi_variables_.resize(cfg_.blocks.size());
        result_.blocks.resize(cfg_.blocks.size());

        initialize_tracked_variables();
        convert_blocks();
        compute_dominators();
        compute_dominance_frontiers();
        place_phi_nodes();
        initialize_result_blocks();
        rename();
        return std::move(result_);
    }

   private:
    void initialize_tracked_variables() {
        for (const auto& param : function_.parameters) {
            tracked_variables_.insert(param.name);
            version_counters_[param.name] = 0;
        }
    }

    static auto make_variable_ref(const std::string& name) -> ValueRef {
        return ValueRef{ValueKind::Variable, name};
    }

    auto make_temp_ref() -> std::pair<ValueRef, SsaValue> {
        const std::string name = "tmp" + std::to_string(next_temp_index_++);
        SsaValue value{name, 0};
        temp_values_.emplace(name, value);
        return {ValueRef{ValueKind::Temporary, name}, value};
    }

    static auto make_external_ref(const std::string& name) -> ValueRef {
        return ValueRef{ValueKind::External, name};
    }

    void convert_blocks() {
        for (std::size_t block_index = 0; block_index < cfg_.blocks.size(); ++block_index) {
            const auto& cfg_block = cfg_.blocks[block_index];
            auto& pre_block = pre_blocks_[block_index];
            pre_block.name = cfg_block.name;

            std::vector<ValueRef> stack;
            stack.reserve(cfg_block.end_index > cfg_block.start_index ? (cfg_block.end_index - cfg_block.start_index) : 0);

            for (std::size_t i = cfg_block.start_index; i < cfg_block.end_index; ++i) {
                const Instruction* inst = nullptr;
                if (i < cfg_.instructions.size()) {
                    inst = cfg_.instructions[i];
                }
                if (inst == nullptr) {
                    continue;
                }

                switch (inst->kind) {
                    case InstructionKind::Comment:
                    case InstructionKind::Label:
                        break;
                    case InstructionKind::Literal: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        auto [temp_ref, temp_value] = make_temp_ref();
                        PreInstruction pre;
                        pre.op = SsaOp::Literal;
                        pre.result = temp_ref;
                        pre.fixed_result = temp_value;
                        pre.immediates = inst->operands;
                        pre_block.instructions.push_back(std::move(pre));
                        stack.push_back(temp_ref);
                        break;
                    }
                    case InstructionKind::Reference: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        const std::string& name = inst->operands.front();
                        if (tracked_variables_.count(name) > 0) {
                            stack.push_back(make_variable_ref(name));
                        } else {
                            stack.push_back(make_external_ref(name));
                        }
                        break;
                    }
                    case InstructionKind::Binary: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        if (stack.size() < 2) {
                            break;
                        }
                        auto right = stack.back();
                        stack.pop_back();
                        auto left = stack.back();
                        stack.pop_back();
                        auto [temp_ref, temp_value] = make_temp_ref();
                        PreInstruction pre;
                        pre.op = SsaOp::Binary;
                        pre.arguments.push_back(left);
                        pre.arguments.push_back(right);
                        pre.result = temp_ref;
                        pre.fixed_result = temp_value;
                        pre.immediates = inst->operands;
                        pre_block.instructions.push_back(std::move(pre));
                        stack.push_back(temp_ref);
                        break;
                    }
                    case InstructionKind::Unary: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        if (stack.empty()) {
                            break;
                        }
                        auto operand = stack.back();
                        stack.pop_back();
                        auto [temp_ref, temp_value] = make_temp_ref();
                        PreInstruction pre;
                        pre.op = SsaOp::Unary;
                        pre.arguments.push_back(operand);
                        pre.result = temp_ref;
                        pre.fixed_result = temp_value;
                        pre.immediates = inst->operands;
                        pre_block.instructions.push_back(std::move(pre));
                        stack.push_back(temp_ref);
                        break;
                    }
                    case InstructionKind::Call: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        std::size_t arg_count = 0;
                        if (inst->operands.size() >= 2) {
                            try {
                                arg_count = static_cast<std::size_t>(std::stoul(inst->operands[1]));
                            } catch (...) {
                                arg_count = 0;
                            }
                        }
                        if (stack.size() < arg_count) {
                            break;
                        }
                        std::vector<ValueRef> args;
                        args.reserve(arg_count);
                        for (std::size_t a = 0; a < arg_count; ++a) {
                            args.push_back(stack.back());
                            stack.pop_back();
                        }
                        std::reverse(args.begin(), args.end());
                        auto [temp_ref, temp_value] = make_temp_ref();
                        PreInstruction pre;
                        pre.op = SsaOp::Call;
                        pre.arguments = std::move(args);
                        pre.result = temp_ref;
                        pre.fixed_result = temp_value;
                        pre.immediates = inst->operands;
                        pre_block.instructions.push_back(std::move(pre));
                        stack.push_back(temp_ref);
                        break;
                    }
                    case InstructionKind::Store: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        if (stack.empty()) {
                            break;
                        }
                        auto value = stack.back();
                        stack.pop_back();
                        const std::string& name = inst->operands.front();
                        tracked_variables_.insert(name);
                        defsites_[name].insert(block_index);
                        PreInstruction pre;
                        pre.op = SsaOp::Store;
                        pre.arguments.push_back(value);
                        pre.result = make_variable_ref(name);
                        pre_block.instructions.push_back(std::move(pre));
                        break;
                    }
                    case InstructionKind::Drop: {
                        if (stack.empty()) {
                            break;
                        }
                        auto value = stack.back();
                        stack.pop_back();
                        PreInstruction pre;
                        pre.op = SsaOp::Drop;
                        pre.arguments.push_back(value);
                        pre_block.instructions.push_back(std::move(pre));
                        break;
                    }
                    case InstructionKind::Return: {
                        PreInstruction pre;
                        pre.op = SsaOp::Return;
                        if (!stack.empty()) {
                            pre.arguments.push_back(stack.back());
                            stack.pop_back();
                        }
                        pre_block.instructions.push_back(std::move(pre));
                        break;
                    }
                    case InstructionKind::Branch: {
                        PreInstruction pre;
                        pre.op = SsaOp::Branch;
                        pre.immediates = inst->operands;
                        pre_block.instructions.push_back(std::move(pre));
                        break;
                    }
                    case InstructionKind::BranchIf: {
                        if (stack.empty()) {
                            break;
                        }
                        auto condition = stack.back();
                        stack.pop_back();
                        PreInstruction pre;
                        pre.op = SsaOp::BranchIf;
                        pre.arguments.push_back(condition);
                        pre.immediates = inst->operands;
                        pre_block.instructions.push_back(std::move(pre));
                        break;
                    }
                }
            }
        }
    }

    void compute_reverse_postorder() {
        reverse_postorder_.clear();
        if (cfg_.blocks.empty()) {
            return;
        }

        std::vector<bool> visited(cfg_.blocks.size(), false);
        std::function<void(std::size_t)> dfs = [&](std::size_t node) {
            if (visited[node]) {
                return;
            }
            visited[node] = true;
            for (const auto succ : cfg_.blocks[node].successors) {
                if (succ < cfg_.blocks.size()) {
                    dfs(succ);
                }
            }
            reverse_postorder_.push_back(node);
        };

        dfs(entry_block_);
        std::reverse(reverse_postorder_.begin(), reverse_postorder_.end());
    }

    auto intersect(std::pair<std::size_t, std::size_t> nodes, const std::vector<std::size_t>& rpo_position) const
        -> std::size_t {
        std::size_t finger_a = nodes.first;
        std::size_t finger_b = nodes.second;
        while (finger_a != finger_b) {
            while (rpo_position[finger_a] > rpo_position[finger_b]) {
                finger_a = idom_[finger_a];
            }
            while (rpo_position[finger_b] > rpo_position[finger_a]) {
                finger_b = idom_[finger_b];
            }
        }
        return finger_a;
    }

    void compute_dominators() {
        const std::size_t block_count = cfg_.blocks.size();
        idom_.assign(block_count, kInvalidIndex);
        dom_children_.assign(block_count, {});

        compute_reverse_postorder();
        if (reverse_postorder_.empty()) {
            return;
        }

        std::vector<std::size_t> rpo_position(block_count, kInvalidIndex);
        for (std::size_t i = 0; i < reverse_postorder_.size(); ++i) {
            rpo_position[reverse_postorder_[i]] = i;
        }

        const std::size_t start = reverse_postorder_.front();
        idom_[start] = start;

        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 1; i < reverse_postorder_.size(); ++i) {
                const std::size_t block = reverse_postorder_[i];
                std::size_t new_idom = kInvalidIndex;
                for (const auto pred : cfg_.blocks[block].predecessors) {
                    if (pred >= block_count) {
                        continue;
                    }
                    if (idom_[pred] == kInvalidIndex) {
                        continue;
                    }
                    if (new_idom == kInvalidIndex) {
                        new_idom = pred;
                    } else {
                        new_idom = intersect({pred, new_idom}, rpo_position);
                    }
                }
                if (new_idom != kInvalidIndex && idom_[block] != new_idom) {
                    idom_[block] = new_idom;
                    changed = true;
                }
            }
        }

        for (std::size_t block = 0; block < block_count; ++block) {
            const std::size_t idom = idom_[block];
            if (idom == kInvalidIndex || idom == block) {
                continue;
            }
            dom_children_[idom].push_back(block);
        }
    }

    void compute_dominance_frontiers() {
        const std::size_t block_count = cfg_.blocks.size();
        dom_frontiers_.assign(block_count, {});
        if (idom_.empty()) {
            return;
        }
        for (std::size_t block = 0; block < block_count; ++block) {
            const auto& preds = cfg_.blocks[block].predecessors;
            if (preds.size() < 2) {
                continue;
            }
            for (const auto pred : preds) {
                std::size_t runner = pred;
                while (runner != idom_[block] && runner != kInvalidIndex) {
                    auto& frontier = dom_frontiers_[runner];
                    if (std::find(frontier.begin(), frontier.end(), block) == frontier.end()) {
                        frontier.push_back(block);
                    }
                    runner = idom_[runner];
                }
            }
        }
    }

    void place_phi_nodes() {
        for (const auto& [variable, sites] : defsites_) {
            std::vector<std::size_t> worklist(sites.begin(), sites.end());
            std::unordered_set<std::size_t> processed;

            while (!worklist.empty()) {
                const std::size_t block = worklist.back();
                worklist.pop_back();

                if (!processed.insert(block).second) {
                    continue;
                }

                if (block >= dom_frontiers_.size()) {
                    continue;
                }

                for (const auto frontier_block : dom_frontiers_[block]) {
                    if (phi_variables_[frontier_block].insert(variable).second) {
                        if (defsites_.at(variable).count(frontier_block) == 0) {
                            worklist.push_back(frontier_block);
                        }
                    }
                }
            }
        }
    }

    void initialize_result_blocks() {
        for (std::size_t block_index = 0; block_index < cfg_.blocks.size(); ++block_index) {
            const auto& cfg_block = cfg_.blocks[block_index];
            auto& out_block = result_.blocks[block_index];
            out_block.name = cfg_block.name;
            out_block.successors = cfg_block.successors;

            std::vector<std::string> phi_vars(phi_variables_[block_index].begin(), phi_variables_[block_index].end());
            std::sort(phi_vars.begin(), phi_vars.end());

            for (const auto& var : phi_vars) {
                SsaPhiNode node;
                node.variable = var;
                node.result = SsaValue{var, 0};
                for (const auto pred : cfg_block.predecessors) {
                    node.inputs.push_back(SsaPhiInput{pred, std::nullopt});
                }
                out_block.phi_nodes.push_back(std::move(node));
            }
        }
    }

    void initialize_variable_environment() {
        for (const auto& param : function_.parameters) {
            const std::string& name = param.name;
            variable_stack_[name].push_back(SsaValue{name, 0});
            version_counters_[name] = 0;
        }
    }

    auto current_value(const std::string& name) const -> std::optional<SsaValue> {
        const auto it = variable_stack_.find(name);
        if (it == variable_stack_.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.back();
    }

    void push_value(const std::string& name, const SsaValue& value) {
        variable_stack_[name].push_back(value);
    }

    void pop_value(const std::string& name) {
        auto it = variable_stack_.find(name);
        if (it == variable_stack_.end() || it->second.empty()) {
            return;
        }
        it->second.pop_back();
    }

    auto next_version(const std::string& name) -> SsaValue {
        auto& counter = version_counters_[name];
        counter += 1;
        return SsaValue{name, counter};
    }

    auto resolve_temporary(const std::string& name, const std::optional<SsaValue>& fallback) -> SsaValue {
        const auto it = temp_values_.find(name);
        if (it != temp_values_.end()) {
            return it->second;
        }
        if (fallback.has_value()) {
            temp_values_[name] = *fallback;
            return *fallback;
        }
        SsaValue value{name, 0};
        temp_values_[name] = value;
        return value;
    }

    auto resolve_external(const std::string& name) -> SsaValue {
        const auto it = external_values_.find(name);
        if (it != external_values_.end()) {
            return it->second;
        }
        SsaValue value{name, 0};
        external_values_[name] = value;
        return value;
    }

    auto resolve_variable(const std::string& name) -> SsaValue {
        if (const auto current = current_value(name)) {
            return *current;
        }
        const auto it = fallback_values_.find(name);
        if (it != fallback_values_.end()) {
            return it->second;
        }
        SsaValue placeholder{name, 0};
        fallback_values_[name] = placeholder;
        return placeholder;
    }

    auto resolve_value(const ValueRef& ref, const std::optional<SsaValue>& preset = std::nullopt) -> SsaValue {
        switch (ref.kind) {
            case ValueKind::Variable:
                return resolve_variable(ref.name);
            case ValueKind::Temporary:
                return resolve_temporary(ref.name, preset);
            case ValueKind::External:
                return resolve_external(ref.name);
        }
        return SsaValue{ref.name, 0};
    }

    void rename_block(std::size_t block_index) {
        if (block_index >= result_.blocks.size()) {
            return;
        }
        if (visited_[block_index]) {
            return;
        }
        if (block_index != entry_block_ && (block_index >= idom_.size() || idom_[block_index] == kInvalidIndex)) {
            visited_[block_index] = true;
            return;
        }

        visited_[block_index] = true;
        auto& out_block = result_.blocks[block_index];
        const auto& pre_block = pre_blocks_[block_index];

        std::vector<std::string> pushed_variables;
        pushed_variables.reserve(out_block.phi_nodes.size() + pre_block.instructions.size());

        for (auto& phi : out_block.phi_nodes) {
            const auto value = next_version(phi.variable);
            phi.result = value;
            push_value(phi.variable, value);
            pushed_variables.push_back(phi.variable);
        }

        out_block.instructions.reserve(pre_block.instructions.size());
        for (const auto& pre : pre_block.instructions) {
            SsaInstruction inst;
            inst.op = pre.op;
            inst.immediates = pre.immediates;

            inst.arguments.reserve(pre.arguments.size());
            for (const auto& arg : pre.arguments) {
                inst.arguments.push_back(resolve_value(arg));
            }

            if (pre.result.has_value()) {
                const auto& target = *pre.result;
                if (target.kind == ValueKind::Variable) {
                    const auto value = next_version(target.name);
                    inst.result = value;
                    push_value(target.name, value);
                    pushed_variables.push_back(target.name);
                } else if (target.kind == ValueKind::Temporary) {
                    inst.result = resolve_value(target, pre.fixed_result);
                } else {
                    if (pre.fixed_result.has_value()) {
                        inst.result = pre.fixed_result;
                    }
                }
            } else if (pre.fixed_result.has_value()) {
                inst.result = pre.fixed_result;
            }

            out_block.instructions.push_back(std::move(inst));
        }

        for (const auto succ : cfg_.blocks[block_index].successors) {
            if (succ >= result_.blocks.size()) {
                continue;
            }
            auto& succ_block = result_.blocks[succ];
            for (auto& phi : succ_block.phi_nodes) {
                const auto current = current_value(phi.variable);
                if (!current.has_value()) {
                    continue;
                }
                auto it = std::find_if(phi.inputs.begin(), phi.inputs.end(), [block_index](const SsaPhiInput& input) {
                    return input.predecessor == block_index;
                });
                if (it != phi.inputs.end()) {
                    it->value = current;
                }
            }
        }

        for (const auto child : dom_children_[block_index]) {
            rename_block(child);
        }

        for (auto it = pushed_variables.rbegin(); it != pushed_variables.rend(); ++it) {
            pop_value(*it);
        }
    }

    void rename() {
        if (cfg_.blocks.empty()) {
            return;
        }
        visited_.assign(cfg_.blocks.size(), false);
        initialize_variable_environment();
        rename_block(entry_block_);
    }

   private:
    const Function& function_;
    ControlFlowGraph cfg_;
    std::size_t entry_block_ = 0;

    std::vector<PreBlock> pre_blocks_;
    std::vector<std::unordered_set<std::string>> phi_variables_;

    std::unordered_map<std::string, std::unordered_set<std::size_t>> defsites_;
    std::unordered_set<std::string> tracked_variables_;

    std::size_t next_temp_index_ = 0;
    std::unordered_map<std::string, SsaValue> temp_values_;
    std::unordered_map<std::string, SsaValue> external_values_;
    std::unordered_map<std::string, SsaValue> fallback_values_;

    std::vector<std::size_t> idom_;
    std::vector<std::vector<std::size_t>> dom_children_;
    std::vector<std::vector<std::size_t>> dom_frontiers_;
    std::vector<std::size_t> reverse_postorder_;

    std::unordered_map<std::string, std::vector<SsaValue>> variable_stack_;
    std::unordered_map<std::string, std::uint32_t> version_counters_;

    std::vector<bool> visited_;

    SsaFunction result_;
};

}  // namespace

auto build_ssa(const Function& function) -> SsaFunction {
    SsaBuilder builder(function);
    return builder.build();
}

}  // namespace impulse::ir
