#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "impulse/ir/ir.h"

namespace impulse::ir {

enum class EvalStatus : std::uint8_t {
    Success,
    NonConstant,
    Error,
};

struct BindingEvalResult {
    EvalStatus status = EvalStatus::NonConstant;
    std::optional<double> value;
    std::string message;
};

[[nodiscard]] auto interpret_binding(const Binding& binding,
                                     const std::unordered_map<std::string, double>& environment) -> BindingEvalResult;

}  // namespace impulse::ir
