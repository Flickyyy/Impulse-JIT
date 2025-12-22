#include <gtest/gtest.h>
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

TEST(IRTest, EmitIrText) {
    const std::string source = R"(module demo;
func main() -> int {
    return 0;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    EXPECT_NE(irText.find("^entry"), std::string::npos);
    EXPECT_TRUE(irText.find("return") != std::string::npos || irText.find('#') != std::string::npos);
}

TEST(IRTest, InterfaceIrEmission) {
    const std::string source = R"(module demo;
interface Display {
    func show(self: Display) -> string;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    EXPECT_NE(irText.find("interface Display"), std::string::npos);
    EXPECT_NE(irText.find("func show"), std::string::npos);
}

TEST(IRTest, BindingExpressionLowering) {
    const std::string source = R"(module demo;
let value: int = 1 + 2 * 3;
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult result = parser.parseModule();
    ASSERT_TRUE(result.success);

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    EXPECT_NE(irText.find("let value: int = (1 + (2 * 3));  # = 7"), std::string::npos);
    EXPECT_NE(irText.find("literal 1"), std::string::npos);
    EXPECT_NE(irText.find("literal 2"), std::string::npos);
    EXPECT_NE(irText.find("literal 3"), std::string::npos);
    EXPECT_NE(irText.find("binary +"), std::string::npos);
    EXPECT_NE(irText.find("binary *"), std::string::npos);
    EXPECT_NE(irText.find("store value"), std::string::npos);
}

TEST(IRTest, ExportedDeclarations) {
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
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.module.declarations.size(), 4);
    for (const auto& decl : result.module.declarations) {
        EXPECT_TRUE(decl.exported);
    }

    const auto irText = impulse::frontend::emit_ir_text(result.module);
    EXPECT_NE(irText.find("export let value"), std::string::npos);
    EXPECT_NE(irText.find("export func main"), std::string::npos);
    EXPECT_NE(irText.find("export struct Point"), std::string::npos);
    EXPECT_NE(irText.find("export interface Display"), std::string::npos);
}

TEST(IRTest, IrBindingInterpreter) {
    const std::string source = R"(module demo;
let a: int = 2;
let b: int = a * 5 + 3;
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    std::unordered_map<std::string, double> environment;
    bool foundB = false;

    for (const auto& binding : lowered.bindings) {
        const auto eval = impulse::ir::interpret_binding(binding, environment);
        if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
            environment.emplace(binding.name, *eval.value);
        }
        if (binding.name == "b") {
            EXPECT_EQ(eval.status, impulse::ir::EvalStatus::Success);
            ASSERT_TRUE(eval.value.has_value());
            EXPECT_LT(std::abs(*eval.value - 13.0), 1e-9);
            foundB = true;
        }
    }

    EXPECT_TRUE(foundB) << "Interpreter did not evaluate binding 'b'";
}

TEST(IRTest, CfgLinearFunction) {
    impulse::ir::Function function;
    function.name = "linear";
    impulse::ir::BasicBlock block;
    block.label = "entry";
    block.instructions.push_back({impulse::ir::InstructionKind::Literal, {"0"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(std::move(block));

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    ASSERT_EQ(cfg.blocks.size(), 1);
    const auto& entry = cfg.blocks.front();
    EXPECT_EQ(entry.name, "entry");
    EXPECT_TRUE(entry.successors.empty());
    EXPECT_TRUE(entry.predecessors.empty());
    EXPECT_EQ(entry.end_index - entry.start_index, 2);
}

TEST(IRTest, CfgBranchIfFunction) {
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
    ASSERT_EQ(cfg.blocks.size(), 3);
    const auto& entry = cfg.blocks[0];
    EXPECT_EQ(entry.successors.size(), 2);

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
    EXPECT_TRUE(hasFallthrough);
    EXPECT_TRUE(hasTarget);
    const auto& target = cfg.blocks[2];
    EXPECT_EQ(target.name, "target");
    EXPECT_EQ(target.predecessors.size(), 1);
    EXPECT_EQ(target.predecessors.front(), 0);
}

TEST(IRTest, CfgBranchFunction) {
    impulse::ir::Function function;
    function.name = "branch";
    impulse::ir::BasicBlock block;
    block.label = "entry";
    block.instructions.push_back({impulse::ir::InstructionKind::Branch, {"exit"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Label, {"exit"}});
    block.instructions.push_back({impulse::ir::InstructionKind::Return, {}});
    function.blocks.push_back(std::move(block));

    const auto cfg = impulse::ir::build_control_flow_graph(function);
    ASSERT_EQ(cfg.blocks.size(), 2);
    const auto& entry = cfg.blocks[0];
    EXPECT_EQ(entry.successors.size(), 1);
    const auto exitIndex = entry.successors.front();
    EXPECT_EQ(exitIndex, 1);
    const auto& exitBlock = cfg.blocks[exitIndex];
    EXPECT_EQ(exitBlock.name, "exit");
    EXPECT_EQ(exitBlock.predecessors.size(), 1);
    EXPECT_EQ(exitBlock.predecessors.front(), 0);
}

TEST(IRTest, SsaPreservesCfgLayout) {
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

    EXPECT_EQ(ssa.name, function.name);
    for (const auto& parameter : function.parameters) {
        const auto* symbol = ssa.find_symbol(parameter.name);
        EXPECT_NE(symbol, nullptr);
    }
    EXPECT_EQ(ssa.blocks.size(), cfg.blocks.size());

    for (std::size_t index = 0; index < cfg.blocks.size(); ++index) {
        const auto& cfgBlock = cfg.blocks[index];
        const auto& ssaBlock = ssa.blocks[index];
        EXPECT_EQ(ssaBlock.id, index);
        EXPECT_EQ(ssaBlock.name, cfgBlock.name);
        EXPECT_EQ(ssaBlock.successors, cfgBlock.successors);
        EXPECT_EQ(ssaBlock.predecessors, cfgBlock.predecessors);
        if (index == 0) {
            EXPECT_EQ(ssaBlock.immediate_dominator, 0);
        } else {
            EXPECT_LT(ssaBlock.immediate_dominator, ssa.blocks.size());
        }
        if (!ssaBlock.dominator_children.empty()) {
            for (const auto child : ssaBlock.dominator_children) {
                EXPECT_LT(child, ssa.blocks.size());
                EXPECT_EQ(ssa.blocks[child].immediate_dominator, index);
            }
        }
    }

    const auto* exitBlock = ssa.find_block("exit");
    ASSERT_NE(exitBlock, nullptr);
    EXPECT_NE(exitBlock->immediate_dominator, std::numeric_limits<std::size_t>::max());
    EXPECT_TRUE(exitBlock->dominance_frontier.empty());

    const auto* entryBlock = ssa.find_block("entry");
    ASSERT_NE(entryBlock, nullptr);
    EXPECT_TRUE(entryBlock->dominance_frontier.empty());
}

TEST(IRTest, SsaInsertsPhiNodes) {
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
    ASSERT_NE(mergeBlock, nullptr);
    EXPECT_FALSE(mergeBlock->phi_nodes.empty());
    const auto& phi = mergeBlock->phi_nodes.front();
    const auto* symbol = ssa.find_symbol(phi.symbol);
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->name, "x");
}

TEST(IRTest, SsaPopulatesPhiInputs) {
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
    ASSERT_NE(mergeBlock, nullptr);
    EXPECT_EQ(mergeBlock->phi_nodes.size(), 1);
    const auto& phi = mergeBlock->phi_nodes.front();
    EXPECT_TRUE(phi.result.is_valid());
    EXPECT_EQ(phi.inputs.size(), mergeBlock->predecessors.size());

    std::unordered_set<std::size_t> predecessors;
    for (const auto& input : phi.inputs) {
        EXPECT_TRUE(predecessors.insert(input.predecessor).second);
        EXPECT_TRUE(input.value.has_value());
        EXPECT_EQ(input.value->symbol, phi.result.symbol);
        EXPECT_GT(input.value->version, 0);
    }
}

TEST(IRTest, SsaMaterializesInstructions) {
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

    EXPECT_EQ(ssa.blocks.size(), 1);
    const auto& block = ssa.blocks.front();
    EXPECT_EQ(block.instructions.size(), 3);
    const auto& literal = block.instructions[0];
    EXPECT_EQ(literal.opcode, "literal");
    EXPECT_TRUE(literal.result.has_value());

    const auto& assign = block.instructions[1];
    EXPECT_EQ(assign.opcode, "assign");
    EXPECT_TRUE(assign.result.has_value());
    const auto* symbol = ssa.find_symbol(assign.result->symbol);
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->name, "value");
    EXPECT_EQ(assign.arguments.size(), 1);

    const auto& ret = block.instructions[2];
    EXPECT_EQ(ret.opcode, "return");
    EXPECT_EQ(ret.arguments.size(), 1);
    EXPECT_EQ(ret.arguments.front().symbol, assign.result->symbol);
}
