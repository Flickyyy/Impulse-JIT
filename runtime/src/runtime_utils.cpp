#include "impulse/runtime/runtime_utils.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "impulse/ir/ssa.h"

namespace impulse::runtime {

// encode_value_id, to_index, and make_result are now inline in the header for performance

[[nodiscard]] auto strip_numeric_separators(const std::string& literal) -> std::string {
    std::string sanitized;
    sanitized.reserve(literal.size());
    for (char ch : literal) {
        if (ch != '_') {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

[[nodiscard]] auto parse_literal(const std::string& operand) -> std::optional<double> {
    if (operand.empty()) {
        return std::nullopt;
    }
    if (operand == "true") {
        return 1.0;
    }
    if (operand == "false") {
        return 0.0;
    }
    try {
        const std::string sanitized = strip_numeric_separators(operand);
        if (sanitized.empty()) {
            return std::nullopt;
        }
        size_t processed = 0;
        const double value = std::stod(sanitized, &processed);
        if (processed != sanitized.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] auto format_ssa_value(const ir::SsaValue& value) -> std::string {
    if (!value.is_valid()) {
        return "<invalid>";
    }
    std::ostringstream out;
    out << value.to_string();
    return out.str();
}

[[nodiscard]] auto join_ssa_values(const std::vector<ir::SsaValue>& values) -> std::string {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << format_ssa_value(values[i]);
    }
    return out.str();
}

[[nodiscard]] auto join_strings(const std::vector<std::string>& values) -> std::string {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << values[i];
    }
    return out.str();
}

[[nodiscard]] auto escape_string(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

[[nodiscard]] auto format_number(double value) -> std::string {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value > 0 ? "inf" : "-inf";
    }

    const double rounded = std::round(value);
    if (std::abs(value - rounded) < kEpsilon) {
        std::ostringstream out;
        out << static_cast<long long>(rounded);
        return out.str();
    }

    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

[[nodiscard]] auto format_value_for_output(const Value& value) -> std::string {
    switch (value.kind) {
        case ValueKind::Nil:
            return "nil";
        case ValueKind::Number:
            return format_number(value.number);
        case ValueKind::String:
            return std::string{value.as_string()};
        case ValueKind::Object:
            if (value.object == nullptr) {
                return "object@null";
            }
            if (value.object->kind == ObjectKind::Array) {
                return "[array length=" + std::to_string(value.object->fields.size()) + "]";
            }
            std::ostringstream out;
            out << "object@" << static_cast<const void*>(value.object);
            return out.str();
    }
    return "<value>";
}

[[nodiscard]] auto format_ssa_instruction(const ir::SsaInstruction& inst) -> std::string {
    std::ostringstream out;
    if (inst.result.has_value()) {
        out << format_ssa_value(*inst.result) << " = ";
    }
    out << inst.opcode;
    if (!inst.arguments.empty()) {
        out << " args(" << join_ssa_values(inst.arguments) << ')';
    }
    if (!inst.immediates.empty()) {
        out << " imm(" << join_strings(inst.immediates) << ')';
    }
    return out.str();
}

[[nodiscard]] auto describe_value(const Value& value) -> std::string {
    std::ostringstream out;
    switch (value.kind) {
        case ValueKind::Nil:
            out << "nil";
            break;
        case ValueKind::Number:
            out << value.number;
            break;
        case ValueKind::Object:
            if (value.object == nullptr) {
                out << "object@null";
                break;
            }
            if (value.object->kind == ObjectKind::Array) {
                out << "[array length=" << value.object->fields.size() << "]";
                break;
            }
            out << "object@" << static_cast<const void*>(value.object);
            break;
        case ValueKind::String:
            out << '"' << escape_string(value.as_string()) << '"';
            break;
    }
    return out.str();
}

// make_result is now inline in the header for performance

[[nodiscard]] auto join_path(const std::vector<std::string>& segments) -> std::string {
    if (segments.empty()) {
        return "<anonymous>";
    }
    std::ostringstream builder;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) {
            builder << "::";
        }
        builder << segments[i];
    }
    return builder.str();
}

}  // namespace impulse::runtime

