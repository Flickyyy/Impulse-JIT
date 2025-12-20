#pragma once

#include <string>
#include <utility>
#include <vector>

#include "impulse/ir/cfg.h"
#include "impulse/ir/ssa.h"
#include "impulse/ir/ir.h"

namespace impulse::ir {

struct FunctionAnalysis {
    std::string name;
    ControlFlowGraph cfg;
    SsaFunction ssa_before;
    SsaFunction ssa_after;
    std::vector<std::string> optimisation_log;
};

[[nodiscard]] auto analyse_module(const Module& module) -> std::vector<FunctionAnalysis>;

[[nodiscard]] auto optimise_with_log(SsaFunction function)
    -> std::pair<SsaFunction, std::vector<std::string>>;

}  // namespace impulse::ir
