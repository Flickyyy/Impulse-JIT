#pragma once

#include <vector>

#include "impulse/frontend/ast.h"

namespace impulse::frontend {

struct SemanticResult {
    bool success = true;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] auto analyzeModule(const Module& module) -> SemanticResult;

}  // namespace impulse::frontend
