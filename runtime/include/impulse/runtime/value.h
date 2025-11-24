#pragma once

#include <cstdint>
#include <vector>

namespace impulse::runtime {

struct GcObject;

enum class ValueKind : std::uint8_t {
    Nil,
    Number,
    Object,
};

struct Value {
    ValueKind kind = ValueKind::Nil;
    double number = 0.0;
    GcObject* object = nullptr;

    [[nodiscard]] static auto make_nil() -> Value { return Value{}; }

    [[nodiscard]] static auto make_number(double v) -> Value {
        Value result;
        result.kind = ValueKind::Number;
        result.number = v;
        return result;
    }

    [[nodiscard]] static auto make_object(GcObject* obj) -> Value {
        Value result;
        result.kind = ValueKind::Object;
        result.object = obj;
        return result;
    }

    [[nodiscard]] auto is_number() const -> bool { return kind == ValueKind::Number; }
    [[nodiscard]] auto is_object() const -> bool { return kind == ValueKind::Object; }
    [[nodiscard]] auto is_nil() const -> bool { return kind == ValueKind::Nil; }
    [[nodiscard]] auto as_number() const -> double { return number; }
    [[nodiscard]] auto as_object() const -> GcObject* { return object; }
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
