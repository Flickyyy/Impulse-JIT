#include "impulse/ir/analysis.h"

#include <sstream>

namespace impulse::ir {

auto optimise_with_log(SsaFunction function) -> std::pair<SsaFunction, std::vector<std::string>> {
    std::vector<std::string> log;
    log.push_back("optimizations disabled - using JIT");
    return {std::move(function), std::move(log)};
}

auto analyse_module(const Module& module) -> std::vector<FunctionAnalysis> {
    std::vector<FunctionAnalysis> results;
    results.reserve(module.functions.size());

    for (const auto& function : module.functions) {
        FunctionAnalysis analysis;
        analysis.name = function.name;
        analysis.cfg = build_control_flow_graph(function);
        analysis.ssa_before = build_ssa(function, analysis.cfg);
        auto optimisation_result = optimise_with_log(analysis.ssa_before);
        analysis.ssa_after = std::move(optimisation_result.first);
        analysis.optimisation_log = std::move(optimisation_result.second);
        results.push_back(std::move(analysis));
    }

    return results;
}

}  // namespace impulse::ir
