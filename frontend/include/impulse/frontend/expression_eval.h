#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "impulse/frontend/ast.h"

namespace impulse::frontend {

enum class ExpressionEvalStatus : std::uint8_t {
    Constant,
    NonConstant,
    Error,
};

struct ExpressionEvalResult {
    ExpressionEvalStatus status = ExpressionEvalStatus::NonConstant;
    std::optional<double> value;
    std::optional<std::string> message;
};

[[nodiscard]] auto printExpression(const Expression& expr) -> std::string;
[[nodiscard]] auto evaluateNumericExpression(const Expression& expr) -> ExpressionEvalResult;

}  // namespace impulse::frontend
