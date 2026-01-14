/**
 * @file test_optimizer.cpp
 * @brief Tests for SSA optimization passes: Constant Folding and Strength Reduction
 */

#include <gtest/gtest.h>

#include "impulse/ir/optimizer.h"
#include "impulse/ir/ssa.h"

namespace impulse::ir {
namespace {

// Helper to create a literal instruction
auto make_literal(SymbolId sym, std::uint32_t ver, const std::string& value) -> SsaInstruction {
    SsaInstruction inst;
    inst.op = SsaOpcode::Literal;
    inst.opcode = "literal";
    inst.result = SsaValue{sym, ver};
    inst.immediates = {value};
    return inst;
}

// Helper to create a binary instruction
auto make_binary(SymbolId sym, std::uint32_t ver, 
                 const SsaValue& left, const SsaValue& right,
                 const std::string& op) -> SsaInstruction {
    SsaInstruction inst;
    inst.op = SsaOpcode::Binary;
    inst.opcode = "binary";
    inst.result = SsaValue{sym, ver};
    inst.arguments = {left, right};
    inst.immediates = {op};
    return inst;
}

// Helper to create a function with a single block
auto make_function(std::vector<SsaInstruction> instructions) -> SsaFunction {
    SsaFunction func;
    func.name = "test";
    SsaBlock block;
    block.name = "entry";
    block.instructions = std::move(instructions);
    func.blocks.push_back(std::move(block));
    return func;
}

// ============================================================================
// Constant Folding Tests
// ============================================================================

TEST(ConstantFolding, Addition) {
    // %0 = 3
    // %1 = 5
    // %2 = %0 + %1  → should become %2 = 8
    auto func = make_function({
        make_literal(0, 0, "3"),
        make_literal(1, 0, "5"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_TRUE(changed);
    ASSERT_EQ(func.blocks[0].instructions.size(), 3);
    
    auto& folded = func.blocks[0].instructions[2];
    EXPECT_EQ(folded.op, SsaOpcode::Literal);
    ASSERT_FALSE(folded.immediates.empty());
    EXPECT_EQ(folded.immediates[0], "8");
    
    auto stats = get_last_optimization_stats();
    EXPECT_GE(stats.constants_folded, 1);
}

TEST(ConstantFolding, Subtraction) {
    // %0 = 10
    // %1 = 3
    // %2 = %0 - %1  → should become %2 = 7
    auto func = make_function({
        make_literal(0, 0, "10"),
        make_literal(1, 0, "3"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "-")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_TRUE(changed);
    auto& folded = func.blocks[0].instructions[2];
    EXPECT_EQ(folded.op, SsaOpcode::Literal);
    EXPECT_EQ(folded.immediates[0], "7");
}

TEST(ConstantFolding, Multiplication) {
    // %0 = 6
    // %1 = 7
    // %2 = %0 * %1  → should become %2 = 42
    auto func = make_function({
        make_literal(0, 0, "6"),
        make_literal(1, 0, "7"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "*")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_TRUE(changed);
    auto& folded = func.blocks[0].instructions[2];
    EXPECT_EQ(folded.op, SsaOpcode::Literal);
    EXPECT_EQ(folded.immediates[0], "42");
}

TEST(ConstantFolding, Division) {
    // %0 = 20
    // %1 = 4
    // %2 = %0 / %1  → should become %2 = 5
    auto func = make_function({
        make_literal(0, 0, "20"),
        make_literal(1, 0, "4"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "/")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_TRUE(changed);
    auto& folded = func.blocks[0].instructions[2];
    EXPECT_EQ(folded.op, SsaOpcode::Literal);
    EXPECT_EQ(folded.immediates[0], "5");
}

TEST(ConstantFolding, DivisionByZeroNotFolded) {
    // %0 = 10
    // %1 = 0
    // %2 = %0 / %1  → should NOT be folded (division by zero)
    auto func = make_function({
        make_literal(0, 0, "10"),
        make_literal(1, 0, "0"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "/")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_FALSE(changed);
    auto& inst = func.blocks[0].instructions[2];
    EXPECT_EQ(inst.op, SsaOpcode::Binary);
}

TEST(ConstantFolding, Comparison) {
    // %0 = 5
    // %1 = 10
    // %2 = %0 < %1  → should become %2 = 1 (true)
    auto func = make_function({
        make_literal(0, 0, "5"),
        make_literal(1, 0, "10"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "<")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_TRUE(changed);
    auto& folded = func.blocks[0].instructions[2];
    EXPECT_EQ(folded.op, SsaOpcode::Literal);
    EXPECT_EQ(folded.immediates[0], "1");
}

TEST(ConstantFolding, ChainedConstants) {
    // %0 = 2
    // %1 = 3
    // %2 = %0 + %1  → %2 = 5
    // %3 = 4
    // %4 = %2 * %3  → %4 = 20 (chained folding)
    auto func = make_function({
        make_literal(0, 0, "2"),
        make_literal(1, 0, "3"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+"),
        make_literal(3, 0, "4"),
        make_binary(4, 0, SsaValue{2, 0}, SsaValue{3, 0}, "*")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_TRUE(changed);
    
    // Check that both binaries were folded
    auto& inst2 = func.blocks[0].instructions[2];
    auto& inst4 = func.blocks[0].instructions[4];
    
    EXPECT_EQ(inst2.op, SsaOpcode::Literal);
    EXPECT_EQ(inst2.immediates[0], "5");
    
    EXPECT_EQ(inst4.op, SsaOpcode::Literal);
    EXPECT_EQ(inst4.immediates[0], "20");
}

TEST(ConstantFolding, NoConstantsUnchanged) {
    // Non-constant operands should not be folded
    // %0 is assumed to come from elsewhere (parameter)
    // %1 = 5
    // %2 = %0 + %1  → should NOT be folded
    auto func = make_function({
        make_literal(1, 0, "5"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+")
    });
    
    bool changed = constant_folding(func);
    
    EXPECT_FALSE(changed);
    auto& inst = func.blocks[0].instructions[1];
    EXPECT_EQ(inst.op, SsaOpcode::Binary);
}

// ============================================================================
// Strength Reduction Tests
// ============================================================================

TEST(StrengthReduction, MultiplyByZero) {
    // %0 = variable
    // %1 = 0
    // %2 = %0 * %1  → %2 = 0
    auto func = make_function({
        make_literal(1, 0, "0"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "*")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Literal);
    EXPECT_EQ(reduced.immediates[0], "0");
}

TEST(StrengthReduction, MultiplyByOne) {
    // %0 = variable
    // %1 = 1
    // %2 = %0 * %1  → %2 = %0
    auto func = make_function({
        make_literal(1, 0, "1"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "*")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Assign);
    ASSERT_EQ(reduced.arguments.size(), 1);
    EXPECT_EQ(reduced.arguments[0].symbol, 0);
}

TEST(StrengthReduction, OneMultiplyByX) {
    // %0 = 1
    // %1 = variable (symbol 10)
    // %2 = %0 * %1  → %2 = %1
    auto func = make_function({
        make_literal(0, 0, "1"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{10, 0}, "*")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Assign);
    ASSERT_EQ(reduced.arguments.size(), 1);
    EXPECT_EQ(reduced.arguments[0].symbol, 10);
}

TEST(StrengthReduction, MultiplyByTwo) {
    // %0 = variable
    // %1 = 2
    // %2 = %0 * %1  → %2 = %0 + %0
    auto func = make_function({
        make_literal(1, 0, "2"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "*")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Binary);
    EXPECT_EQ(reduced.immediates[0], "+");
    ASSERT_EQ(reduced.arguments.size(), 2);
    EXPECT_EQ(reduced.arguments[0].symbol, 0);
    EXPECT_EQ(reduced.arguments[1].symbol, 0);
}

TEST(StrengthReduction, TwoMultiplyByX) {
    // %0 = 2
    // %1 = variable (symbol 10)
    // %2 = %0 * %1  → %2 = %1 + %1
    auto func = make_function({
        make_literal(0, 0, "2"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{10, 0}, "*")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Binary);
    EXPECT_EQ(reduced.immediates[0], "+");
    ASSERT_EQ(reduced.arguments.size(), 2);
    EXPECT_EQ(reduced.arguments[0].symbol, 10);
    EXPECT_EQ(reduced.arguments[1].symbol, 10);
}

TEST(StrengthReduction, AddZero) {
    // %0 = variable
    // %1 = 0
    // %2 = %0 + %1  → %2 = %0
    auto func = make_function({
        make_literal(1, 0, "0"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Assign);
    ASSERT_EQ(reduced.arguments.size(), 1);
    EXPECT_EQ(reduced.arguments[0].symbol, 0);
}

TEST(StrengthReduction, ZeroAddX) {
    // %0 = 0
    // %1 = variable (symbol 10)
    // %2 = %0 + %1  → %2 = %1
    auto func = make_function({
        make_literal(0, 0, "0"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{10, 0}, "+")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Assign);
    ASSERT_EQ(reduced.arguments.size(), 1);
    EXPECT_EQ(reduced.arguments[0].symbol, 10);
}

TEST(StrengthReduction, SubtractZero) {
    // %0 = variable
    // %1 = 0
    // %2 = %0 - %1  → %2 = %0
    auto func = make_function({
        make_literal(1, 0, "0"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "-")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Assign);
    ASSERT_EQ(reduced.arguments.size(), 1);
    EXPECT_EQ(reduced.arguments[0].symbol, 0);
}

TEST(StrengthReduction, DivideByOne) {
    // %0 = variable
    // %1 = 1
    // %2 = %0 / %1  → %2 = %0
    auto func = make_function({
        make_literal(1, 0, "1"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "/")
    });
    
    bool changed = strength_reduction(func);
    
    EXPECT_TRUE(changed);
    auto& reduced = func.blocks[0].instructions[1];
    EXPECT_EQ(reduced.op, SsaOpcode::Assign);
    ASSERT_EQ(reduced.arguments.size(), 1);
    EXPECT_EQ(reduced.arguments[0].symbol, 0);
}

// ============================================================================
// Combined Optimization Tests
// ============================================================================

TEST(OptimizeSSA, CombinedOptimizations) {
    // %0 = 2
    // %1 = 3
    // %2 = %0 + %1  → constant fold to 5
    // %3 = 2
    // %4 = variable
    // %5 = %4 * %3  → strength reduce to %4 + %4
    auto func = make_function({
        make_literal(0, 0, "2"),
        make_literal(1, 0, "3"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+"),
        make_literal(3, 0, "2"),
        make_binary(5, 0, SsaValue{4, 0}, SsaValue{3, 0}, "*")
    });
    
    bool changed = optimize_ssa(func);
    
    EXPECT_TRUE(changed);
    
    // Check constant folding applied
    auto& inst2 = func.blocks[0].instructions[2];
    EXPECT_EQ(inst2.op, SsaOpcode::Literal);
    EXPECT_EQ(inst2.immediates[0], "5");
    
    // Check strength reduction applied
    auto& inst5 = func.blocks[0].instructions[4];
    EXPECT_EQ(inst5.op, SsaOpcode::Binary);
    EXPECT_EQ(inst5.immediates[0], "+");
    
    // Check stats
    auto stats = get_last_optimization_stats();
    EXPECT_GE(stats.constants_folded, 1);
    EXPECT_GE(stats.strength_reductions, 1);
}

TEST(OptimizeSSA, NoOptimizationsNeeded) {
    // %0 = variable
    // %1 = variable
    // %2 = %0 + %1  → cannot optimize (no constants)
    auto func = make_function({
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+")
    });
    
    bool changed = optimize_ssa(func);
    
    EXPECT_FALSE(changed);
    auto& inst = func.blocks[0].instructions[0];
    EXPECT_EQ(inst.op, SsaOpcode::Binary);
}

TEST(OptimizeSSA, StatsTracking) {
    // Run optimization and verify stats are tracked
    auto func = make_function({
        make_literal(0, 0, "3"),
        make_literal(1, 0, "4"),
        make_binary(2, 0, SsaValue{0, 0}, SsaValue{1, 0}, "+"),  // fold to 7
        make_literal(3, 0, "0"),
        make_binary(4, 0, SsaValue{5, 0}, SsaValue{3, 0}, "+"),  // x + 0 → x
    });
    
    [[maybe_unused]] auto changed = optimize_ssa(func);
    
    auto stats = get_last_optimization_stats();
    EXPECT_GE(stats.constants_folded, 1);
    EXPECT_GE(stats.strength_reductions, 1);
    EXPECT_EQ(stats.total_changes, stats.constants_folded + stats.strength_reductions);
}

}  // namespace
}  // namespace impulse::ir
