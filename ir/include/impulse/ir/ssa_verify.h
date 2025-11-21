#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "impulse/ir/ssa.h"

namespace impulse::ir {

struct SsaValidationIssue {
    std::string message;
    std::size_t block = static_cast<std::size_t>(-1);
    std::optional<std::size_t> instruction;
};

struct SsaValidationResult {
    bool success = false;
    std::vector<SsaValidationIssue> issues;
};

[[nodiscard]] auto validate_ssa(const SsaFunction& function) -> SsaValidationResult;

}  // namespace impulse::ir
