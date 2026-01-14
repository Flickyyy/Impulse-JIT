#include "impulse/ir/optimizer.h"

#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace impulse::ir {

namespace {

// Thread-local stats for the last optimization run
thread_local OptimizationStats last_stats;

// Check if a string is a numeric literal (integer or floating point)
[[nodiscard]] auto is_numeric_literal(const std::string& s) -> bool {
    if (s.empty()) return false;
    if (s == "true" || s == "false") return true;
    
    size_t start = 0;
    if (s[0] == '-') start = 1;
    if (start >= s.size()) return false;
    
    bool has_dot = false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (s[i] == '_') {
            continue;  // Numeric separator
        } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

// Parse a numeric literal to double
[[nodiscard]] auto parse_literal(const std::string& s) -> std::optional<double> {
    if (s == "true") return 1.0;
    if (s == "false") return 0.0;
    
    try {
        // Remove underscores
        std::string clean;
        for (char c : s) {
            if (c != '_') clean += c;
        }
        return std::stod(clean);
    } catch (...) {
        return std::nullopt;
    }
}

// Format a double as a literal string (preserving integer representation when possible)
[[nodiscard]] auto format_literal(double value) -> std::string {
    if (std::floor(value) == value && std::abs(value) < 1e15) {
        return std::to_string(static_cast<long long>(value));
    }
    // Use enough precision for round-trip
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.15g", value);
    return buf;
}

// Build a map from SSA value to its defining literal (if it's a constant)
[[nodiscard]] auto build_constant_map(const SsaFunction& function) 
    -> std::unordered_map<std::uint64_t, double> {
    std::unordered_map<std::uint64_t, double> constants;
    
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.instructions) {
            if (inst.op == SsaOpcode::Literal && inst.result.has_value() && !inst.immediates.empty()) {
                auto val = parse_literal(inst.immediates[0]);
                if (val.has_value()) {
                    std::uint64_t key = (static_cast<std::uint64_t>(inst.result->symbol) << 32) | 
                                        inst.result->version;
                    constants[key] = *val;
                }
            }
        }
    }
    
    return constants;
}

// Get constant value for an SSA value, if known
[[nodiscard]] auto get_constant(const SsaValue& val, 
                                const std::unordered_map<std::uint64_t, double>& constants) 
    -> std::optional<double> {
    std::uint64_t key = (static_cast<std::uint64_t>(val.symbol) << 32) | val.version;
    auto it = constants.find(key);
    if (it != constants.end()) {
        return it->second;
    }
    return std::nullopt;
}

}  // namespace

// ============================================================================
// Constant Folding
// ============================================================================
// Evaluates binary and unary operations on constant operands at compile time.
// Example: `%t1 = 3`, `%t2 = 5`, `%t3 = %t1 + %t2` → `%t3 = 8`

auto constant_folding(SsaFunction& function) -> bool {
    bool changed = false;
    auto constants = build_constant_map(function);
    
    // Iterate until no more changes (for chained constants)
    bool pass_changed;
    do {
        pass_changed = false;
        
        for (auto& block : function.blocks) {
            for (auto& inst : block.instructions) {
                // Handle binary operations
                if (inst.op == SsaOpcode::Binary && inst.arguments.size() == 2 && 
                    inst.result.has_value() && !inst.immediates.empty()) {
                    
                    auto left = get_constant(inst.arguments[0], constants);
                    auto right = get_constant(inst.arguments[1], constants);
                    
                    if (left.has_value() && right.has_value()) {
                        std::optional<double> result;
                        const std::string& op = inst.immediates[0];
                        
                        if (op == "+") result = *left + *right;
                        else if (op == "-") result = *left - *right;
                        else if (op == "*") result = *left * *right;
                        else if (op == "/" && *right != 0.0) result = *left / *right;
                        else if (op == "%" && *right != 0.0) result = std::fmod(*left, *right);
                        else if (op == "<") result = (*left < *right) ? 1.0 : 0.0;
                        else if (op == "<=") result = (*left <= *right) ? 1.0 : 0.0;
                        else if (op == ">") result = (*left > *right) ? 1.0 : 0.0;
                        else if (op == ">=") result = (*left >= *right) ? 1.0 : 0.0;
                        else if (op == "==") result = (*left == *right) ? 1.0 : 0.0;
                        else if (op == "!=") result = (*left != *right) ? 1.0 : 0.0;
                        else if (op == "&&") result = (*left != 0.0 && *right != 0.0) ? 1.0 : 0.0;
                        else if (op == "||") result = (*left != 0.0 || *right != 0.0) ? 1.0 : 0.0;
                        
                        if (result.has_value()) {
                            // Convert binary to literal
                            inst.op = SsaOpcode::Literal;
                            inst.opcode = "literal";
                            inst.binary_op = BinaryOp::Unknown;
                            inst.arguments.clear();
                            inst.immediates = {format_literal(*result)};
                            
                            // Add to constants map for further propagation
                            std::uint64_t key = (static_cast<std::uint64_t>(inst.result->symbol) << 32) | 
                                               inst.result->version;
                            constants[key] = *result;
                            
                            pass_changed = true;
                            changed = true;
                            last_stats.constants_folded++;
                        }
                    }
                }
                
                // Handle unary operations
                if (inst.op == SsaOpcode::Unary && inst.arguments.size() == 1 && 
                    inst.result.has_value() && !inst.immediates.empty()) {
                    
                    auto operand = get_constant(inst.arguments[0], constants);
                    
                    if (operand.has_value()) {
                        std::optional<double> result;
                        const std::string& op = inst.immediates[0];
                        
                        if (op == "-") result = -(*operand);
                        else if (op == "!") result = (*operand == 0.0) ? 1.0 : 0.0;
                        
                        if (result.has_value()) {
                            inst.op = SsaOpcode::Literal;
                            inst.opcode = "literal";
                            inst.arguments.clear();
                            inst.immediates = {format_literal(*result)};
                            
                            std::uint64_t key = (static_cast<std::uint64_t>(inst.result->symbol) << 32) | 
                                               inst.result->version;
                            constants[key] = *result;
                            
                            pass_changed = true;
                            changed = true;
                            last_stats.constants_folded++;
                        }
                    }
                }
            }
        }
    } while (pass_changed);
    
    return changed;
}

// ============================================================================
// Strength Reduction
// ============================================================================
// Replaces expensive operations with cheaper equivalents.
// Examples:
//   x * 2   → x + x  (multiplication → addition)
//   x * 1   → x      (identity)
//   x * 0   → 0      (zero)
//   x + 0   → x      (identity)
//   x - 0   → x      (identity)
//   x / 1   → x      (identity)

auto strength_reduction(SsaFunction& function) -> bool {
    bool changed = false;
    auto constants = build_constant_map(function);
    
    for (auto& block : function.blocks) {
        for (auto& inst : block.instructions) {
            if (inst.op != SsaOpcode::Binary || inst.arguments.size() != 2 || 
                !inst.result.has_value() || inst.immediates.empty()) {
                continue;
            }
            
            const std::string& op = inst.immediates[0];
            auto left_const = get_constant(inst.arguments[0], constants);
            auto right_const = get_constant(inst.arguments[1], constants);
            
            // Multiplication optimizations
            if (op == "*") {
                // x * 0 = 0, 0 * x = 0
                if ((right_const.has_value() && *right_const == 0.0) ||
                    (left_const.has_value() && *left_const == 0.0)) {
                    inst.op = SsaOpcode::Literal;
                    inst.opcode = "literal";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments.clear();
                    inst.immediates = {"0"};
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
                
                // x * 1 = x
                if (right_const.has_value() && *right_const == 1.0) {
                    inst.op = SsaOpcode::Assign;
                    inst.opcode = "assign";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments = {inst.arguments[0]};
                    inst.immediates.clear();
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
                
                // 1 * x = x
                if (left_const.has_value() && *left_const == 1.0) {
                    inst.op = SsaOpcode::Assign;
                    inst.opcode = "assign";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments = {inst.arguments[1]};
                    inst.immediates.clear();
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
                
                // x * 2 = x + x (addition is faster than multiplication on most CPUs)
                if (right_const.has_value() && *right_const == 2.0) {
                    inst.immediates = {"+"};
                    inst.binary_op = BinaryOp::Add;
                    inst.arguments = {inst.arguments[0], inst.arguments[0]};
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
                
                // 2 * x = x + x
                if (left_const.has_value() && *left_const == 2.0) {
                    inst.immediates = {"+"};
                    inst.binary_op = BinaryOp::Add;
                    inst.arguments = {inst.arguments[1], inst.arguments[1]};
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
            }
            
            // Addition optimizations
            if (op == "+") {
                // x + 0 = x
                if (right_const.has_value() && *right_const == 0.0) {
                    inst.op = SsaOpcode::Assign;
                    inst.opcode = "assign";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments = {inst.arguments[0]};
                    inst.immediates.clear();
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
                
                // 0 + x = x
                if (left_const.has_value() && *left_const == 0.0) {
                    inst.op = SsaOpcode::Assign;
                    inst.opcode = "assign";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments = {inst.arguments[1]};
                    inst.immediates.clear();
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
            }
            
            // Subtraction optimizations
            if (op == "-") {
                // x - 0 = x
                if (right_const.has_value() && *right_const == 0.0) {
                    inst.op = SsaOpcode::Assign;
                    inst.opcode = "assign";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments = {inst.arguments[0]};
                    inst.immediates.clear();
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
            }
            
            // Division optimizations
            if (op == "/") {
                // x / 1 = x
                if (right_const.has_value() && *right_const == 1.0) {
                    inst.op = SsaOpcode::Assign;
                    inst.opcode = "assign";
                    inst.binary_op = BinaryOp::Unknown;
                    inst.arguments = {inst.arguments[0]};
                    inst.immediates.clear();
                    changed = true;
                    last_stats.strength_reductions++;
                    continue;
                }
            }
        }
    }
    
    return changed;
}

// ============================================================================
// Main Optimization Entry Point
// ============================================================================

auto optimize_ssa(SsaFunction& function) -> bool {
    // Reset stats
    last_stats = OptimizationStats{};
    
    bool changed = false;
    
    // Run constant folding first (may enable more strength reductions)
    changed |= constant_folding(function);
    
    // Run strength reduction
    changed |= strength_reduction(function);
    
    // Run constant folding again (strength reduction may have created new constants)
    changed |= constant_folding(function);
    
    last_stats.total_changes = last_stats.constants_folded + last_stats.strength_reductions;
    
    return changed;
}

auto get_last_optimization_stats() -> const OptimizationStats& {
    return last_stats;
}

}  // namespace impulse::ir
