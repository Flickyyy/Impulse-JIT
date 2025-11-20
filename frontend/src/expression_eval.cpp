#include "impulse/frontend/expression_eval.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace impulse::frontend {

namespace {

constexpr double kEpsilon = 1e-12;

[[nodiscard]] auto binary_operator_to_string(Expression::BinaryOperator op) -> std::string_view {
    switch (op) {
        case Expression::BinaryOperator::Add:
            return "+";
        case Expression::BinaryOperator::Subtract:
            return "-";
        case Expression::BinaryOperator::Multiply:
            return "*";
        case Expression::BinaryOperator::Divide:
            return "/";
        case Expression::BinaryOperator::Equal:
            return "==";
        case Expression::BinaryOperator::NotEqual:
            return "!=";
        case Expression::BinaryOperator::Less:
            return "<";
        case Expression::BinaryOperator::LessEqual:
            return "<=";
        case Expression::BinaryOperator::Greater:
            return ">";
        case Expression::BinaryOperator::GreaterEqual:
            return ">=";
    }
    return "+";
}

[[nodiscard]] auto strip_numeric_separators(std::string_view literal) -> std::string {
    std::string sanitized;
    sanitized.reserve(literal.size());
    for (char ch : literal) {
        if (ch != '_') {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

[[nodiscard]] auto parse_literal_value(const std::string& literal) -> ExpressionEvalResult {
    if (literal.empty()) {
        return ExpressionEvalResult{
            .status = ExpressionEvalStatus::Error,
            .value = std::nullopt,
            .message = std::string{"Empty numeric literal"},
        };
    }

    if (literal == "true") {
        return ExpressionEvalResult{ExpressionEvalStatus::Constant, 1.0, std::nullopt};
    }
    if (literal == "false") {
        return ExpressionEvalResult{ExpressionEvalStatus::Constant, 0.0, std::nullopt};
    }

    try {
        const std::string sanitized = strip_numeric_separators(literal);
        if (sanitized.empty()) {
            return ExpressionEvalResult{
                .status = ExpressionEvalStatus::Error,
                .value = std::nullopt,
                .message = std::string{"Invalid numeric literal"},
            };
        }
        size_t processed = 0;
        const double value = std::stod(sanitized, &processed);
        if (processed != sanitized.size() || !std::isfinite(value)) {
            return ExpressionEvalResult{
                .status = ExpressionEvalStatus::Error,
                .value = std::nullopt,
                .message = std::string{"Invalid numeric literal"},
            };
        }
        return ExpressionEvalResult{
            .status = ExpressionEvalStatus::Constant,
            .value = value,
            .message = std::nullopt,
        };
    } catch (...) {
        return ExpressionEvalResult{
            .status = ExpressionEvalStatus::Error,
            .value = std::nullopt,
            .message = std::string{"Invalid numeric literal"},
        };
    }
}

[[nodiscard]] auto combine_results(const ExpressionEvalResult& lhs, const ExpressionEvalResult& rhs,
                                   Expression::BinaryOperator op) -> ExpressionEvalResult {
    if (lhs.status == ExpressionEvalStatus::Error) {
        return lhs;
    }
    if (rhs.status == ExpressionEvalStatus::Error) {
        return rhs;
    }
    if (lhs.status != ExpressionEvalStatus::Constant || rhs.status != ExpressionEvalStatus::Constant ||
        !lhs.value.has_value() || !rhs.value.has_value()) {
        return ExpressionEvalResult{
            .status = ExpressionEvalStatus::NonConstant,
            .value = std::nullopt,
            .message = std::nullopt,
        };
    }

    const double left = *lhs.value;
    const double right = *rhs.value;
    switch (op) {
        case Expression::BinaryOperator::Add:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, left + right, std::nullopt};
        case Expression::BinaryOperator::Subtract:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, left - right, std::nullopt};
        case Expression::BinaryOperator::Multiply:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, left * right, std::nullopt};
        case Expression::BinaryOperator::Divide:
            if (std::abs(right) < kEpsilon) {
                return ExpressionEvalResult{
                    .status = ExpressionEvalStatus::Error,
                    .value = std::nullopt,
                    .message = std::string{"Division by zero in constant expression"},
                };
            }
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, left / right, std::nullopt};
        case Expression::BinaryOperator::Equal:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, (left == right) ? 1.0 : 0.0, std::nullopt};
        case Expression::BinaryOperator::NotEqual:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, (left != right) ? 1.0 : 0.0, std::nullopt};
        case Expression::BinaryOperator::Less:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, (left < right) ? 1.0 : 0.0, std::nullopt};
        case Expression::BinaryOperator::LessEqual:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, (left <= right) ? 1.0 : 0.0, std::nullopt};
        case Expression::BinaryOperator::Greater:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, (left > right) ? 1.0 : 0.0, std::nullopt};
        case Expression::BinaryOperator::GreaterEqual:
            return ExpressionEvalResult{ExpressionEvalStatus::Constant, (left >= right) ? 1.0 : 0.0, std::nullopt};
    }
    return ExpressionEvalResult{
        .status = ExpressionEvalStatus::Error,
        .value = std::nullopt,
        .message = std::string{"Unsupported binary operator"},
    };
}

}  // namespace

auto printExpression(const Expression& expr) -> std::string {
    switch (expr.kind) {
        case Expression::Kind::Literal:
            return expr.literal_value;
        case Expression::Kind::Identifier:
            return expr.identifier.value;
        case Expression::Kind::Binary: {
            std::string result = "(";
            if (expr.left) {
                result += printExpression(*expr.left);
            }
            result += ' ';
            result += binary_operator_to_string(expr.binary_operator);
            result += ' ';
            if (expr.right) {
                result += printExpression(*expr.right);
            }
            result += ')';
            return result;
        }
    }
    return {};
}

auto evaluateNumericExpression(const Expression& expr) -> ExpressionEvalResult {
    switch (expr.kind) {
        case Expression::Kind::Literal:
            return parse_literal_value(expr.literal_value);
        case Expression::Kind::Identifier:
            return ExpressionEvalResult{
                .status = ExpressionEvalStatus::NonConstant,
                .value = std::nullopt,
                .message = std::nullopt,
            };
        case Expression::Kind::Binary:
            if (expr.left == nullptr || expr.right == nullptr) {
                return ExpressionEvalResult{
                    .status = ExpressionEvalStatus::Error,
                    .value = std::nullopt,
                    .message = std::string{"Incomplete binary expression"},
                };
            }
            return combine_results(evaluateNumericExpression(*expr.left), evaluateNumericExpression(*expr.right),
                                   expr.binary_operator);
    }
    return ExpressionEvalResult{
        .status = ExpressionEvalStatus::Error,
        .value = std::nullopt,
        .message = std::string{"Unsupported expression kind"},
    };
}

}  // namespace impulse::frontend
