#include "impulse/ir/ssa.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {

using impulse::ir::ControlFlowGraph;
using impulse::ir::Function;
using impulse::ir::FunctionParameter;
using impulse::ir::InstructionKind;
using impulse::ir::Instruction;
using impulse::ir::PhiNode;
using impulse::ir::PhiInput;
using impulse::ir::SsaBlock;
using impulse::ir::SsaFunction;
using impulse::ir::SsaInstruction;
using impulse::ir::SsaSymbol;
using impulse::ir::SsaValue;
using impulse::ir::SymbolId;

class SymbolTable {
public:
    void add_parameter(const FunctionParameter& parameter) {
        const SymbolId id = next_id_++;
        symbols_.push_back({id, parameter.name, parameter.type});
        name_to_id_.emplace(parameter.name, id);
    }

    auto get_or_create(const std::string& name) -> SymbolId {
        const auto it = name_to_id_.find(name);
        if (it != name_to_id_.end()) {
            return it->second;
        }
        const SymbolId id = next_id_++;
        symbols_.push_back({id, name, {}});
        name_to_id_.emplace(name, id);
        return id;
    }

    [[nodiscard]] auto create_temporary() -> SymbolId {
        const SymbolId id = next_id_++;
        const std::string name = "%t" + std::to_string(temp_counter_++);
        symbols_.push_back({id, name, {}});
        name_to_id_.emplace(name, id);
        return id;
    }

    [[nodiscard]] auto find(const std::string& name) const -> std::optional<SymbolId> {
        const auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] auto symbols() const -> const std::vector<SsaSymbol>& { return symbols_; }

private:
    SymbolId next_id_ = 1;
    std::uint64_t temp_counter_ = 0;
    std::vector<SsaSymbol> symbols_;
    std::unordered_map<std::string, SymbolId> name_to_id_;
};

[[nodiscard]] auto compute_reverse_postorder(const ControlFlowGraph& cfg) -> std::vector<std::size_t> {
    std::vector<std::size_t> order;
    order.reserve(cfg.blocks.size());
    std::vector<bool> visited(cfg.blocks.size(), false);

    const auto dfs = [&](auto&& self, std::size_t block_index) -> void {
        if (block_index >= cfg.blocks.size() || visited[block_index]) {
            return;
        }
        visited[block_index] = true;
        const auto& block = cfg.blocks[block_index];
        for (const auto succ : block.successors) {
            self(self, succ);
        }
        order.push_back(block_index);
    };

    if (!cfg.blocks.empty()) {
        dfs(dfs, 0);
    }

    std::reverse(order.begin(), order.end());
    return order;
}

[[nodiscard]] auto compute_immediate_dominators(const ControlFlowGraph& cfg) -> std::vector<std::size_t> {
    if (cfg.blocks.empty()) {
        return {};
    }

    constexpr std::size_t kUndefined = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> idom(cfg.blocks.size(), kUndefined);
    idom[0] = 0;

    const auto rpo = compute_reverse_postorder(cfg);
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t idx = 1; idx < rpo.size(); ++idx) {
            const std::size_t block_index = rpo[idx];
            const auto& block = cfg.blocks[block_index];

            std::size_t new_idom = kUndefined;
            for (const auto pred : block.predecessors) {
                if (pred >= cfg.blocks.size()) {
                    continue;
                }
                if (idom[pred] == kUndefined) {
                    continue;
                }
                if (new_idom == kUndefined) {
                    new_idom = pred;
                } else {
                    std::size_t finger1 = pred;
                    std::size_t finger2 = new_idom;
                    while (finger1 != finger2) {
                        while (finger1 > finger2) {
                            finger1 = idom[finger1];
                        }
                        while (finger2 > finger1) {
                            finger2 = idom[finger2];
                        }
                    }
                    new_idom = finger1;
                }
            }

            if (new_idom != kUndefined && idom[block_index] != new_idom) {
                idom[block_index] = new_idom;
                changed = true;
            }
        }
    }

    return idom;
}

[[nodiscard]] auto compute_dominance_frontiers(const ControlFlowGraph& cfg,
                                               const std::vector<std::size_t>& idom)
    -> std::vector<std::vector<std::size_t>> {
    std::vector<std::vector<std::size_t>> frontiers(cfg.blocks.size());
    for (std::size_t b = 0; b < cfg.blocks.size(); ++b) {
        const auto& block = cfg.blocks[b];
        if (block.predecessors.size() < 2) {
            continue;
        }
        for (const auto pred : block.predecessors) {
            std::size_t runner = pred;
            while (runner != idom[b] && runner < cfg.blocks.size()) {
                frontiers[runner].push_back(b);
                runner = idom[runner];
            }
        }
    }
    return frontiers;
}

[[nodiscard]] auto build_dominator_tree(const std::vector<std::size_t>& idom)
    -> std::vector<std::vector<std::size_t>> {
    std::vector<std::vector<std::size_t>> children(idom.size());
    for (std::size_t block = 1; block < idom.size(); ++block) {
        const std::size_t parent = idom[block];
        if (parent < idom.size()) {
            children[parent].push_back(block);
        }
    }
    return children;
}

class RenameContext {
public:
    RenameContext(const Function& function, const ControlFlowGraph& cfg, SsaFunction& ssa, SymbolTable& symbols)
        : function_(function), cfg_(cfg), ssa_(ssa), symbols_(symbols) {
        block_instructions_.resize(cfg_.blocks.size());
        for (std::size_t block_index = 0; block_index < cfg_.blocks.size(); ++block_index) {
            const auto& block = cfg_.blocks[block_index];
            if (block.start_index >= block.end_index) {
                continue;
            }
            for (std::size_t i = block.start_index; i < block.end_index && i < cfg_.instructions.size(); ++i) {
                block_instructions_[block_index].push_back(cfg_.instructions[i]);
            }
        }
    }

    void run() {
        initialize_parameters();
        if (!ssa_.blocks.empty()) {
            rename_block(0);
        }
    }

private:
    void initialize_parameters() {
        for (const auto& parameter : function_.parameters) {
            const auto symbol_id = symbols_.find(parameter.name);
            if (!symbol_id.has_value()) {
                continue;
            }
            push_existing(SsaValue{*symbol_id, 1});
        }
    }

    void rename_block(std::size_t block_index) {
        if (block_index >= ssa_.blocks.size()) {
            return;
        }

        auto& block = ssa_.blocks[block_index];
        std::vector<SymbolId> defined_symbols;
        defined_symbols.reserve(block.phi_nodes.size());
        std::vector<SsaInstruction> materialized;
        std::vector<SsaValue> eval_stack;

        for (auto& phi : block.phi_nodes) {
            const auto value = next_version(phi.symbol);
            phi.result = value;
            defined_symbols.push_back(phi.symbol);
        }

        if (block_index < block_instructions_.size()) {
            for (const auto* inst : block_instructions_[block_index]) {
                if (inst == nullptr) {
                    continue;
                }
                switch (inst->kind) {
                    case InstructionKind::Literal: {
                        SsaInstruction out;
                        out.opcode = "literal";
                        if (!inst->operands.empty()) {
                            out.immediates.push_back(inst->operands.front());
                        }
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::StringLiteral: {
                        SsaInstruction out;
                        out.opcode = "literal_string";
                        if (!inst->operands.empty()) {
                            out.immediates.push_back(inst->operands.front());
                        }
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::Reference: {
                        if (inst->operands.empty()) {
                            break;
                        }
                        const auto symbol_id = symbols_.get_or_create(inst->operands.front());
                        const auto current_value = current(symbol_id);
                        eval_stack.push_back(current_value.value_or(SsaValue{symbol_id, 0}));
                        break;
                    }
                    case InstructionKind::Unary: {
                        if (eval_stack.empty()) {
                            break;
                        }
                        const auto operand = pop(eval_stack);
                        SsaInstruction out;
                        out.opcode = "unary";
                        out.arguments.push_back(operand);
                        if (!inst->operands.empty()) {
                            out.immediates.push_back(inst->operands.front());
                        }
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::Binary: {
                        if (eval_stack.size() < 2) {
                            break;
                        }
                        const auto rhs = pop(eval_stack);
                        const auto lhs = pop(eval_stack);
                        SsaInstruction out;
                        out.opcode = "binary";
                        out.arguments.push_back(lhs);
                        out.arguments.push_back(rhs);
                        if (!inst->operands.empty()) {
                            out.immediates.push_back(inst->operands.front());
                        }
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::Store: {
                        if (inst->operands.empty() || eval_stack.empty()) {
                            break;
                        }
                        const auto value = pop(eval_stack);
                        const SymbolId symbol = symbols_.get_or_create(inst->operands.front());
                        const auto versioned = next_version(symbol);
                        defined_symbols.push_back(symbol);

                        SsaInstruction out;
                        out.opcode = "assign";
                        out.arguments.push_back(value);
                        out.result = versioned;
                        materialized.push_back(out);
                        break;
                    }
                    case InstructionKind::Drop: {
                        if (eval_stack.empty()) {
                            break;
                        }
                        SsaInstruction out;
                        out.opcode = "drop";
                        out.arguments.push_back(pop(eval_stack));
                        materialized.push_back(out);
                        break;
                    }
                    case InstructionKind::Branch: {
                        SsaInstruction out;
                        out.opcode = "branch";
                        if (!inst->operands.empty()) {
                            out.immediates.push_back(inst->operands.front());
                        }
                        materialized.push_back(out);
                        break;
                    }
                    case InstructionKind::BranchIf: {
                        if (eval_stack.empty()) {
                            break;
                        }
                        SsaInstruction out;
                        out.opcode = "branch_if";
                        out.arguments.push_back(pop(eval_stack));
                        out.immediates = inst->operands;
                        materialized.push_back(out);
                        break;
                    }
                    case InstructionKind::Return: {
                        SsaInstruction out;
                        out.opcode = "return";
                        if (!eval_stack.empty()) {
                            out.arguments.push_back(pop(eval_stack));
                        }
                        materialized.push_back(out);
                        break;
                    }
                    case InstructionKind::Call: {
                        if (inst->operands.size() < 2) {
                            break;
                        }
                        const auto arg_count = static_cast<std::size_t>(std::stoul(inst->operands[1]));
                        if (eval_stack.size() < arg_count) {
                            break;
                        }
                        std::vector<SsaValue> args;
                        args.reserve(arg_count);
                        for (std::size_t i = 0; i < arg_count; ++i) {
                            args.push_back(pop(eval_stack));
                        }
                        std::reverse(args.begin(), args.end());

                        SsaInstruction out;
                        out.opcode = "call";
                        out.arguments = std::move(args);
                        out.immediates = inst->operands;
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::MakeArray: {
                        if (eval_stack.empty()) {
                            break;
                        }
                        SsaInstruction out;
                        out.opcode = "array_make";
                        out.arguments.push_back(pop(eval_stack));
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::ArrayGet: {
                        if (eval_stack.size() < 2) {
                            break;
                        }
                        const auto index = pop(eval_stack);
                        const auto array = pop(eval_stack);
                        SsaInstruction out;
                        out.opcode = "array_get";
                        out.arguments = {array, index};
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::ArraySet: {
                        if (eval_stack.size() < 3) {
                            break;
                        }
                        const auto value = pop(eval_stack);
                        const auto index = pop(eval_stack);
                        const auto array = pop(eval_stack);
                        SsaInstruction out;
                        out.opcode = "array_set";
                        out.arguments = {array, index, value};
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::ArrayLength: {
                        if (eval_stack.empty()) {
                            break;
                        }
                        SsaInstruction out;
                        out.opcode = "array_length";
                        out.arguments.push_back(pop(eval_stack));
                        const auto result = make_temporary();
                        out.result = result;
                        materialized.push_back(out);
                        eval_stack.push_back(result);
                        break;
                    }
                    case InstructionKind::Label:
                    case InstructionKind::Comment:
                        break;
                }
            }
        }

        // Set enum opcodes for fast dispatch (avoids string comparison in hot path)
        for (auto& inst : materialized) {
            inst.op = impulse::ir::opcode_from_string(inst.opcode);
            // For binary instructions, also set the binary operator enum
            if (inst.op == impulse::ir::SsaOpcode::Binary && !inst.immediates.empty()) {
                inst.binary_op = impulse::ir::binary_op_from_string(inst.immediates[0]);
            }
        }

        block.instructions = std::move(materialized);

        for (const auto successor : block.successors) {
            if (successor >= ssa_.blocks.size()) {
                continue;
            }
            auto& successor_block = ssa_.blocks[successor];
            for (auto& phi : successor_block.phi_nodes) {
                const auto current_value = current(phi.symbol);
                auto it = std::find_if(phi.inputs.begin(), phi.inputs.end(), [&](const PhiInput& input) {
                    return input.predecessor == block_index;
                });
                if (it != phi.inputs.end()) {
                    it->value = current_value;
                } else {
                    PhiInput input;
                    input.predecessor = block_index;
                    input.value = current_value;
                    phi.inputs.push_back(input);
                }
            }
        }

        for (const auto child : block.dominator_children) {
            rename_block(child);
        }

        for (auto it = defined_symbols.rbegin(); it != defined_symbols.rend(); ++it) {
            pop(*it);
        }
    }

    [[nodiscard]] auto next_version(SymbolId symbol) -> SsaValue {
        auto& counter = counters_[symbol];
        counter += 1;
        stacks_[symbol].push_back(counter);
        return SsaValue{symbol, counter};
    }

    void push_existing(const SsaValue& value) {
        auto& stack = stacks_[value.symbol];
        stack.push_back(value.version);
        auto& counter = counters_[value.symbol];
        if (counter < value.version) {
            counter = value.version;
        }
    }

    [[nodiscard]] auto current(SymbolId symbol) const -> std::optional<SsaValue> {
        const auto it = stacks_.find(symbol);
        if (it == stacks_.end() || it->second.empty()) {
            return std::nullopt;
        }
        return SsaValue{symbol, it->second.back()};
    }

    [[nodiscard]] auto make_temporary() -> SsaValue {
        const SymbolId id = symbols_.create_temporary();
        return SsaValue{id, 1};
    }

    static auto pop(std::vector<SsaValue>& stack) -> SsaValue {
        const auto value = stack.back();
        stack.pop_back();
        return value;
    }

    void pop(SymbolId symbol) {
        auto it = stacks_.find(symbol);
        if (it == stacks_.end() || it->second.empty()) {
            return;
        }
        it->second.pop_back();
    }

    const Function& function_;
    const ControlFlowGraph& cfg_;
    SsaFunction& ssa_;
    SymbolTable& symbols_;
    std::unordered_map<SymbolId, std::vector<std::uint32_t>> stacks_;
    std::unordered_map<SymbolId, std::uint32_t> counters_;
    std::vector<std::vector<const Instruction*>> block_instructions_;
};

}  // namespace

auto impulse::ir::SsaFunction::find_block(const std::string& block_name) const -> const SsaBlock* {
    for (const auto& block : blocks) {
        if (block.name == block_name) {
            return &block;
        }
    }
    return nullptr;
}

auto SsaFunction::find_symbol(SymbolId id) const -> const SsaSymbol* {
    // Use index map for O(1) lookup instead of linear search
    const auto it = symbol_id_index_.find(id);
    if (it != symbol_id_index_.end()) {
        return it->second;
    }
    return nullptr;
}

auto SsaFunction::find_symbol(const std::string& name) const -> const SsaSymbol* {
    // Use index map for O(1) lookup instead of linear search
    const auto it = symbol_name_index_.find(name);
    if (it != symbol_name_index_.end()) {
        return it->second;
    }
    return nullptr;
}

auto impulse::ir::build_ssa(const Function& function, const ControlFlowGraph& cfg) -> SsaFunction {
    SymbolTable symbol_table;
    for (const auto& parameter : function.parameters) {
        symbol_table.add_parameter(parameter);
    }

    SsaFunction ssa;
    ssa.name = function.name;

    const auto idom = compute_immediate_dominators(cfg);
    const auto dom_tree = build_dominator_tree(idom);
    const auto dom_frontiers = compute_dominance_frontiers(cfg, idom);

    ssa.blocks.reserve(cfg.blocks.size());
    for (std::size_t index = 0; index < cfg.blocks.size(); ++index) {
        const auto& cfg_block = cfg.blocks[index];

        SsaBlock block;
        block.id = index;
        block.name = cfg_block.name;
        block.successors = cfg_block.successors;
        block.predecessors = cfg_block.predecessors;
        if (index < idom.size()) {
            block.immediate_dominator = idom[index];
        }
        if (index < dom_tree.size()) {
            block.dominator_children = dom_tree[index];
        }
        if (index < dom_frontiers.size()) {
            block.dominance_frontier = dom_frontiers[index];
        }

        ssa.blocks.push_back(std::move(block));
    }

    std::vector<std::unordered_set<SymbolId>> existing_phi(ssa.blocks.size());
    std::unordered_map<SymbolId, std::vector<std::size_t>> definition_sites;

    std::unordered_map<const Instruction*, std::size_t> instruction_to_block;
    for (std::size_t block_index = 0; block_index < cfg.blocks.size(); ++block_index) {
        const auto& cfg_block = cfg.blocks[block_index];
        for (std::size_t i = cfg_block.start_index; i < cfg_block.end_index && i < cfg.instructions.size(); ++i) {
            const auto* inst = cfg.instructions[i];
            if (inst == nullptr) {
                continue;
            }
            instruction_to_block[inst] = block_index;
        }
    }

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& source_block = function.blocks[block_index];
        for (const auto& instruction : source_block.instructions) {
            if (instruction.kind != InstructionKind::Store || instruction.operands.empty()) {
                continue;
            }
            const std::string& name = instruction.operands.front();
            const SymbolId id = symbol_table.get_or_create(name);
            std::size_t ssa_index = block_index;
            const auto it = instruction_to_block.find(&instruction);
            if (it != instruction_to_block.end()) {
                ssa_index = it->second;
            } else {
                const auto* cfg_block = cfg.find_block(source_block.label.empty() ? ssa.blocks[block_index].name
                                                                                   : source_block.label);
                if (cfg_block != nullptr) {
                    ssa_index = static_cast<std::size_t>(cfg_block - cfg.blocks.data());
                }
            }
            definition_sites[id].push_back(ssa_index);
        }
    }

    if (!ssa.blocks.empty()) {
        for (const auto& parameter : function.parameters) {
            if (const auto symbol_id = symbol_table.find(parameter.name)) {
                definition_sites[*symbol_id].push_back(0);
            }
        }
    }

    for (auto& [symbol, sites] : definition_sites) {
        std::vector<std::size_t> worklist;
        worklist.reserve(sites.size());
        std::unordered_set<std::size_t> visited;
        for (const auto site : sites) {
            worklist.push_back(site);
            visited.insert(site);
        }

        while (!worklist.empty()) {
            const std::size_t block_index = worklist.back();
            worklist.pop_back();

            if (block_index >= dom_frontiers.size()) {
                continue;
            }

            for (const auto frontier_block : dom_frontiers[block_index]) {
                if (frontier_block >= ssa.blocks.size()) {
                    continue;
                }
                if (!existing_phi[frontier_block].emplace(symbol).second) {
                    continue;
                }

                PhiNode node;
                node.symbol = symbol;
                node.result = SsaValue{symbol, 0};
                ssa.blocks[frontier_block].phi_nodes.push_back(std::move(node));

                if (visited.insert(frontier_block).second) {
                    worklist.push_back(frontier_block);
                }
            }
        }
    }

    for (auto& block : ssa.blocks) {
        for (auto& phi : block.phi_nodes) {
            phi.inputs.clear();
            phi.inputs.reserve(block.predecessors.size());
            for (const auto predecessor : block.predecessors) {
                PhiInput input;
                input.predecessor = predecessor;
                input.value = std::nullopt;
                phi.inputs.push_back(input);
            }
        }
    }

    if (!ssa.blocks.empty()) {
        RenameContext rename(function, cfg, ssa, symbol_table);
        rename.run();
    }

    ssa.symbols = symbol_table.symbols();
    
    // Build symbol index maps for O(1) lookup performance
    for (const auto& symbol : ssa.symbols) {
        ssa.symbol_id_index_[symbol.id] = &symbol;
        if (!symbol.name.empty()) {
            ssa.symbol_name_index_[symbol.name] = &symbol;
        }
    }
    
    return ssa;
}

auto impulse::ir::build_ssa(const Function& function) -> SsaFunction {
    const auto cfg = build_control_flow_graph(function);
    return build_ssa(function, cfg);
}
