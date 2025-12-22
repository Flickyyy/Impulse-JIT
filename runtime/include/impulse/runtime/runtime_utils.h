#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "impulse/ir/ir.h"
#include "impulse/ir/ssa.h"
#include "impulse/runtime/runtime.h"
#include "impulse/runtime/value.h"

namespace impulse::runtime {

// Utility functions for runtime
inline constexpr double kEpsilon = 1e-12;

// Hot-path functions - inline for performance
[[nodiscard]] inline auto encode_value_id(ir::SsaValue value) -> std::uint64_t {
    return (static_cast<std::uint64_t>(value.symbol) << 32U) | static_cast<std::uint64_t>(value.version);
}

[[nodiscard]] inline auto make_result(VmStatus status, std::string message) -> VmResult {
    VmResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] inline auto to_index(double value) -> std::optional<std::size_t> {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    if (value < 0.0) {
        return std::nullopt;
    }
    const double truncated = std::floor(value + kEpsilon);
    if (std::abs(truncated - value) > kEpsilon) {
        return std::nullopt;
    }
    if (truncated > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(truncated);
}

// Non-hot-path functions - keep in .cpp
[[nodiscard]] auto parse_literal(const std::string& operand) -> std::optional<double>;
[[nodiscard]] auto format_ssa_value(const ir::SsaValue& value) -> std::string;
[[nodiscard]] auto join_ssa_values(const std::vector<ir::SsaValue>& values) -> std::string;
[[nodiscard]] auto join_strings(const std::vector<std::string>& values) -> std::string;
[[nodiscard]] auto escape_string(std::string_view value) -> std::string;
[[nodiscard]] auto format_number(double value) -> std::string;
[[nodiscard]] auto format_value_for_output(const Value& value) -> std::string;
[[nodiscard]] auto format_ssa_instruction(const ir::SsaInstruction& inst) -> std::string;
[[nodiscard]] auto describe_value(const Value& value) -> std::string;
[[nodiscard]] auto join_path(const std::vector<std::string>& segments) -> std::string;

}  // namespace impulse::runtime

