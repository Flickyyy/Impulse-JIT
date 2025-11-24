#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../ir/include/impulse/ir/cfg.h"
#include "../ir/include/impulse/ir/interpreter.h"
#include "../ir/include/impulse/ir/ssa.h"
#include "../ir/include/impulse/ir/constant_propagation.h"
#include "../ir/include/impulse/ir/dead_code_elimination.h"
#include "../ir/include/impulse/ir/copy_propagation.h"
#include "../ir/include/impulse/ir/optimizer.h"

namespace {

void testEmitIrText() {
    const std::string source = R"(module demo;
func main() -> int {
    return 0;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    assert(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("^entry") != std::string::npos);
    assert(irText.find("return") != std::string::npos || irText.find('#') != std::string::npos);
}

void testInterfaceIrEmission() {
    const std::string source = R"(module demo;
interface Display {
    func show(self: Display) -> string;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    assert(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("interface Display") != std::string::npos);
    assert(irText.find("func show") != std::string::npos);
}

void testBindingExpressionLowering() {
    const std::string source = R"(module demo;
let value: int = 1 + 2 * 3;
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    assert(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("let value: int = (1 + (2 * 3));  # = 7") != std::string::npos);
    assert(irText.find("literal 1") != std::string::npos);
    assert(irText.find("literal 2") != std::string::npos);
    assert(irText.find("literal 3") != std::string::npos);
    assert(irText.find("binary +") != std::string::npos);
    assert(irText.find("binary *") != std::string::npos);
    assert(irText.find("store value") != std::string::npos);
}

void testExportedDeclarations() {
    const std::string source = R"(module demo;
export let value: int = 1;
export func main() -> int {
    return value;
}
export struct Point {
    x: int;
}
export interface Display {
    func show(self: Display);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    assert(result.success);
    assert(result.module.declarations.size() == 4);
    for (const auto& decl : result.module.declarations) {
        assert(decl.exported);
    }

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    assert(irText.find("export let value") != std::string::npos);
    assert(irText.find("export func main") != std::string::npos);
    assert(irText.find("export struct Point") != std::string::npos);
    assert(irText.find("export interface Display") != std::string::npos);
}

void testIrBindingInterpreter() {
    const std::string source = R"(module demo;
let a: int = 2;
let b: int = a * 5 + 3;
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    assert(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    bool foundB = false;

    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
        if (binding.name == "b") {
            assert(eval.status == impulse::ir::EvalStatus::Success);
            assert(eval.value.has_value());
            assert(std::abs(*eval.value - 13.0) < 1e-9);
            foundB = true;
        }
    }

    assert(foundB && "Interpreter did not evaluate binding 'b'");
}

void testCfgLinearFunction() {
    impulse::ir::Function function;
    function.name = "linear";
    impulse::ir::BasicBlock block;
    block.label = "entry";
    block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"0"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(std::move(block));

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    assert(cfg.blocks.size() == 1);
    const auto& entry = cfg.blocks.front();
    assert(entry.name == "entry");
    assert(entry.successors.empty());
    assert(entry.predecessors.empty());
    assert(entry.end_index - entry.start_index == 2);
}

void testCfgBranchIfFunction() {
    impulse::ir::Function function;
    function.name = "branch_if";
    impulse::ir::BasicBlock block;
    block.label = "entry";
    block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"cond"}});
    block.instructions.push_back({impulse::ir::InstructionKind::BranchIf, {"target", "0"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    block.instructions.push_back({impulse::ir::InstructionKind::Label, {"target"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"2"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(std::move(block));

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    assert(cfg.blocks.size() == 3);
    const auto& entry = cfg.blocks[0];
    assert(entry.successors.size() == 2);

    bool hasFallthrough = false;
    bool hasTarget = false;
    for (const auto succ : entry.successors) {
        if (succ == 1) {
            hasFallthrough = true;
        }
        if (succ == 2) {
            hasTarget = true;
        }
    }
    assert(hasFallthrough && hasTarget);
    const auto& target = cfg.blocks[2];
    assert(target.name == "target");
    assert(target.predecessors.size() == 1 && target.predecessors.front() == 0);
}

void testCfgBranchFunction() {
    impulse::ir::Function function;
    function.name = "branch";
    impulse::ir::BasicBlock block;
    block.label = "entry";
    block.instructions.push_back({impulse::ir::InstructionKind::Branch, {"exit"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Label, {"exit"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(std::move(block));

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    assert(cfg.blocks.size() == 2);
    const auto& entry = cfg.blocks[0];
    assert(entry.successors.size() == 1);
    const auto exitIndex = entry.successors.front();
    assert(exitIndex == 1);
    const auto& exitBlock = cfg.blocks[exitIndex];
    assert(exitBlock.name == "exit");
    assert(exitBlock.predecessors.size() == 1 && exitBlock.predecessors.front() == 0);
}

void testSsaPreservesCfgLayout() {
    impulse::ir::Function function;
    function.name = "ssa_layout";

    impulse::ir::FunctionParameter param;
    param.name = "value";
    param.type = "int";
    function.parameters.push_back(param);

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Branch, {"exit"}});
    function.blocks.push_back(entry);

    impulse::ir::BasicBlock exit;
    exit.label = "exit";
    exit.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(exit);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    const auto ssa = impulse::ir::build_ssa(function, cfg);

    assert(ssa.name == function.name);
    for (const auto& parameter : function.parameters) {
        const auto* symbol = ssa.find_symbol(parameter.name);
        assert(symbol != nullptr);
    }
    assert(ssa.blocks.size() == cfg.blocks.size());

    for (std::size_t index = 0; index < cfg.blocks.size(); ++index) {
        const auto& cfgBlock = cfg.blocks[index];
        const auto& ssaBlock = ssa.blocks[index];
        assert(ssaBlock.id == index);
        assert(ssaBlock.name == cfgBlock.name);
        assert(ssaBlock.successors == cfgBlock.successors);
        assert(ssaBlock.predecessors == cfgBlock.predecessors);
        if (index == 0) {
            assert(ssaBlock.immediate_dominator == 0);
        } else {
            assert(ssaBlock.immediate_dominator < ssa.blocks.size());
        }
        if (!ssaBlock.dominator_children.empty()) {
            for (const auto child : ssaBlock.dominator_children) {
                assert(child < ssa.blocks.size());
                assert(ssa.blocks[child].immediate_dominator == index);
            }
        }
    }

    const auto* exitBlock = ssa.find_block("exit");
    assert(exitBlock != nullptr);
    assert(exitBlock->immediate_dominator != std::numeric_limits<std::size_t>::max());
    assert(exitBlock->dominance_frontier.empty());

    const auto* entryBlock = ssa.find_block("entry");
    assert(entryBlock != nullptr);
    assert(entryBlock->dominance_frontier.empty());
}

void testSsaInsertsPhiNodes() {
    impulse::ir::Function function;
    function.name = "ssa_phi";

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::BranchIf, {"then", "1"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Branch, {"else"}});
    function.blocks.push_back(entry);

    impulse::ir::BasicBlock thenBlock;
    thenBlock.label = "then";
    thenBlock.instructions.push_back({impulse::ir::InstructionKind::Literal, {"10"}});
    thenBlock.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
    thenBlock.instructions.push_back({impulse::ir::InstructionKind::Branch, {"merge"}});
    function.blocks.push_back(thenBlock);

    impulse::ir::BasicBlock elseBlock;
    elseBlock.label = "else";
    elseBlock.instructions.push_back({impulse::ir::InstructionKind::Literal, {"20"}});
    elseBlock.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
    elseBlock.instructions.push_back({impulse::ir::InstructionKind::Branch, {"merge"}});
    function.blocks.push_back(elseBlock);

    impulse::ir::BasicBlock merge;
    merge.label = "merge";
    merge.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(merge);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    const auto ssa = impulse::ir::build_ssa(function, cfg);

    const auto* mergeBlock = ssa.find_block("merge");
    assert(mergeBlock != nullptr);
    assert(!mergeBlock->phi_nodes.empty());
    const auto& phi = mergeBlock->phi_nodes.front();
    const auto* symbol = ssa.find_symbol(phi.symbol);
    assert(symbol != nullptr);
    assert(symbol->name == "x");
}

void testSsaPopulatesPhiInputs() {
    impulse::ir::Function function;
    function.name = "ssa_phi_inputs";

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::BranchIf, {"then", "0"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Branch, {"else"}});
    function.blocks.push_back(entry);

    impulse::ir::BasicBlock thenBlock;
    thenBlock.label = "then";
    thenBlock.instructions.push_back({impulse::ir::InstructionKind::Literal, {"10"}});
    thenBlock.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
    thenBlock.instructions.push_back({impulse::ir::InstructionKind::Branch, {"merge"}});
    function.blocks.push_back(thenBlock);

    impulse::ir::BasicBlock elseBlock;
    elseBlock.label = "else";
    elseBlock.instructions.push_back({impulse::ir::InstructionKind::Literal, {"20"}});
    elseBlock.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
    elseBlock.instructions.push_back({impulse::ir::InstructionKind::Branch, {"merge"}});
    function.blocks.push_back(elseBlock);

    impulse::ir::BasicBlock merge;
    merge.label = "merge";
    merge.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(merge);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    const auto ssa = impulse::ir::build_ssa(function, cfg);

    const auto* mergeBlock = ssa.find_block("merge");
    assert(mergeBlock != nullptr);
    assert(mergeBlock->phi_nodes.size() == 1);
    const auto& phi = mergeBlock->phi_nodes.front();
    assert(phi.result.is_valid());
    assert(phi.inputs.size() == mergeBlock->predecessors.size());

    std::unordered_set<std::size_t> predecessors;
    for (const auto& input : phi.inputs) {
        assert(predecessors.insert(input.predecessor).second);
        assert(input.value.has_value());
        assert(input.value->symbol == phi.result.symbol);
        assert(input.value->version > 0);
    }
}

void testSsaMaterializesInstructions() {
    impulse::ir::Function function;
    function.name = "ssa_materialize";

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"42"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"value"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"value"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(entry);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    const auto ssa = impulse::ir::build_ssa(function, cfg);

    assert(ssa.blocks.size() == 1);
    const auto& block = ssa.blocks.front();
    assert(block.instructions.size() == 3);
    const auto& literal = block.instructions[0];
    assert(literal.opcode == "literal");
    assert(literal.result.has_value());

    const auto& assign = block.instructions[1];
    assert(assign.opcode == "assign");
    assert(assign.result.has_value());
    const auto* symbol = ssa.find_symbol(assign.result->symbol);
    assert(symbol != nullptr);
    assert(symbol->name == "value");
    assert(assign.arguments.size() == 1);

    const auto& ret = block.instructions[2];
    assert(ret.opcode == "return");
    assert(ret.arguments.size() == 1);
    assert(ret.arguments.front().symbol == assign.result->symbol);
}

void testSsaConstantPropagation() {
    impulse::ir::Function function;
    function.name = "ssa_constprop";

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"2"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"3"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"y"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"x"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"y"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Binary, {"+"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(entry);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    auto ssa = impulse::ir::build_ssa(function, cfg);

    const bool mutated = impulse::ir::propagate_constants(ssa);
    assert(mutated);

    const auto& block = ssa.blocks.front();
    bool saw_folded = false;
    for (const auto& inst : block.instructions) {
        if (inst.opcode == "literal" && inst.result.has_value() && !inst.immediates.empty() && inst.immediates.front() == "5") {
            saw_folded = true;
        }
    }
    assert(saw_folded);
}

void testSsaDeadAssignmentElimination() {
    {
        impulse::ir::Function function;
        function.name = "ssa_dead_assign";

        impulse::ir::BasicBlock entry;
        entry.label = "entry";
        entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
        entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"unused"}});
        entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"2"}});
        entry.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
        function.blocks.push_back(entry);

        const auto cfg = impulse::ir::build_control_flow_graph(function);
        auto ssa = impulse::ir::build_ssa(function, cfg);

        const bool eliminated = impulse::ir::eliminate_dead_assignments(ssa);
        assert(eliminated);

        const auto& block = ssa.blocks.front();
        assert(block.instructions.size() == 2);
        assert(block.instructions.front().opcode == "literal");
        assert(block.instructions.back().opcode == "return");
    }

    {
        impulse::ir::Function function;
        function.name = "ssa_dead_phi";

        impulse::ir::BasicBlock entry;
        entry.label = "entry";
        entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
        entry.instructions.push_back({impulse::ir::InstructionKind::BranchIf, {"then", "0"}});
        entry.instructions.push_back({impulse::ir::InstructionKind::Branch, {"else"}});
        function.blocks.push_back(entry);

        impulse::ir::BasicBlock then_block;
        then_block.label = "then";
        then_block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"10"}});
        then_block.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
        then_block.instructions.push_back({impulse::ir::InstructionKind::Branch, {"merge"}});
        function.blocks.push_back(then_block);

        impulse::ir::BasicBlock else_block;
        else_block.label = "else";
        else_block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"20"}});
        else_block.instructions.push_back({impulse::ir::InstructionKind::Store, {"x"}});
        else_block.instructions.push_back({impulse::ir::InstructionKind::Branch, {"merge"}});
        function.blocks.push_back(else_block);

        impulse::ir::BasicBlock merge;
        merge.label = "merge";
        merge.instructions.push_back({impulse::ir::InstructionKind::Literal, {"0"}});
        merge.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
        function.blocks.push_back(merge);

        const auto cfg = impulse::ir::build_control_flow_graph(function);
        auto ssa = impulse::ir::build_ssa(function, cfg);

        const bool eliminated = impulse::ir::eliminate_dead_assignments(ssa);
        assert(eliminated);

        const auto* merge_block = ssa.find_block("merge");
        assert(merge_block != nullptr);
        assert(merge_block->phi_nodes.empty());

        const auto* then_block_ssa = ssa.find_block("then");
        assert(then_block_ssa != nullptr);
        assert(then_block_ssa->phi_nodes.empty());
        assert(then_block_ssa->instructions.size() == 1);
        assert(then_block_ssa->instructions.front().opcode == "branch");

        const auto* else_block_ssa = ssa.find_block("else");
        assert(else_block_ssa != nullptr);
        assert(else_block_ssa->instructions.size() == 1);
        assert(else_block_ssa->instructions.front().opcode == "branch");
    }
}

void testSsaCopyPropagation() {
    impulse::ir::Function function;
    function.name = "ssa_copy_propagation";

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"5"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"a"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"a"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"b"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"b"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(entry);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    auto ssa = impulse::ir::build_ssa(function, cfg);

    const bool propagated = impulse::ir::propagate_copies(ssa);
    assert(propagated);

    const bool eliminated = impulse::ir::eliminate_dead_assignments(ssa);
    assert(eliminated);

    const auto& block = ssa.blocks.front();
    assert(block.instructions.size() == 2);
    assert(block.instructions.front().opcode == "literal");
    assert(block.instructions.back().opcode == "return");
    assert(block.instructions.back().arguments.size() == 1);
    assert(block.instructions.front().result.has_value());
    assert(block.instructions.back().arguments.front().symbol == block.instructions.front().result->symbol);
}

void testSsaOptimizerPipeline() {
    impulse::ir::Function function;
    function.name = "ssa_optimizer_pipeline";

    impulse::ir::BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back({impulse::ir::InstructionKind::Literal, {"1"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"a"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"a"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"a"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Binary, {"+"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Store, {"b"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Reference, {"b"}});
    entry.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(entry);

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    auto ssa = impulse::ir::build_ssa(function, cfg);

    const bool optimized = impulse::ir::optimize_ssa(ssa);
    assert(optimized);

    const auto& block = ssa.blocks.front();
    assert(block.instructions.size() == 2);
    const auto& literal = block.instructions.front();
    const auto& ret = block.instructions.back();
    assert(literal.opcode == "literal");
    assert(ret.opcode == "return");
    assert(literal.immediates.size() == 1);
    assert(literal.immediates.front() == "2");
    assert(literal.result.has_value());
    assert(ret.arguments.size() == 1);
    assert(ret.arguments.front().symbol == literal.result->symbol);
}

}  // namespace

auto runIRTests() -> int {
    testEmitIrText();
    testInterfaceIrEmission();
    testBindingExpressionLowering();
    testExportedDeclarations();
    testIrBindingInterpreter();
    testCfgLinearFunction();
    testCfgBranchIfFunction();
    testCfgBranchFunction();
    testSsaPreservesCfgLayout();
    testSsaInsertsPhiNodes();
    testSsaPopulatesPhiInputs();
    testSsaMaterializesInstructions();
    testSsaConstantPropagation();
    testSsaDeadAssignmentElimination();
    testSsaCopyPropagation();
    testSsaOptimizerPipeline();
    return 16;
}
