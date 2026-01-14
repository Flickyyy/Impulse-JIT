#pragma once

#include "impulse/ir/ssa.h"

namespace impulse::ir {

// Main optimization entry point - runs all enabled optimizations
[[nodiscard]] auto optimize_ssa(SsaFunction& function) -> bool;

// Individual optimization passes (for testing and fine-grained control)

// Constant Folding: Evaluate constant expressions at compile time
// Example: `3 + 5` → `8`, `2 * 4` → `8`
[[nodiscard]] auto constant_folding(SsaFunction& function) -> bool;

// Strength Reduction: Replace expensive operations with cheaper ones
// Example: `x * 2` → `x + x`, `x * 1` → `x`, `x + 0` → `x`
[[nodiscard]] auto strength_reduction(SsaFunction& function) -> bool;

// Statistics for optimization passes
struct OptimizationStats {
    int constants_folded = 0;
    int strength_reductions = 0;
    int total_changes = 0;
};

// Get stats from last optimization run (for testing/debugging)
[[nodiscard]] auto get_last_optimization_stats() -> const OptimizationStats&;

}  // namespace impulse::ir
