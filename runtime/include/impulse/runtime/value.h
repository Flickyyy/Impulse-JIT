#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace impulse::runtime {

struct GcObject;

enum class ValueKind : std::uint8_t {
    Nil,
    Number,
    Object,
    String,
};

struct Value {
    ValueKind kind = ValueKind::Nil;
    double number = 0.0;
    GcObject* object = nullptr;
    std::string text;

    [[nodiscard]] static auto make_nil() -> Value { return Value{}; }

    [[nodiscard]] static auto make_number(double v) -> Value {
        Value result;
        result.kind = ValueKind::Number;
        result.number = v;
        result.object = nullptr;
        result.text.clear();
        return result;
    }

    [[nodiscard]] static auto make_object(GcObject* obj) -> Value {
        Value result;
        result.kind = ValueKind::Object;
        result.object = obj;
        result.number = 0.0;
        result.text.clear();
        return result;
    }

    [[nodiscard]] static auto make_string(std::string value) -> Value {
        Value result;
        result.kind = ValueKind::String;
        result.number = 0.0;
        result.object = nullptr;
        result.text = std::move(value);
        return result;
    }

    [[nodiscard]] auto is_number() const -> bool { return kind == ValueKind::Number; }
    [[nodiscard]] auto is_object() const -> bool { return kind == ValueKind::Object; }
    [[nodiscard]] auto is_string() const -> bool { return kind == ValueKind::String; }
    [[nodiscard]] auto is_nil() const -> bool { return kind == ValueKind::Nil; }
    [[nodiscard]] auto as_number() const -> double { return number; }
    [[nodiscard]] auto as_object() const -> GcObject* { return object; }
    [[nodiscard]] auto as_string() const -> std::string_view { return text; }
};

enum class ObjectKind : std::uint8_t {
    Array,
};

struct GcObject {
    ObjectKind kind = ObjectKind::Array;
    bool marked = false;
    std::vector<Value> fields;
    GcObject* next = nullptr;
};

}  // namespace impulse::runtime
