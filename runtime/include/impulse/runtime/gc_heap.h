#pragma once

#include <cstddef>
#include <vector>

#include "impulse/runtime/value.h"

namespace impulse::runtime {

class GcHeap {
public:
    GcHeap();
    ~GcHeap();

    GcHeap(const GcHeap&) = delete;
    auto operator=(const GcHeap&) -> GcHeap& = delete;
    GcHeap(GcHeap&&) = delete;
    auto operator=(GcHeap&&) -> GcHeap& = delete;

    [[nodiscard]] auto allocate_array(std::size_t length, const Value& fill = Value::make_nil()) -> GcObject*;

    void collect(const std::vector<Value*>& roots);

    void set_next_gc_threshold(std::size_t bytes);

    [[nodiscard]] auto bytes_allocated() const -> std::size_t;
    [[nodiscard]] auto live_object_count() const -> std::size_t;
    [[nodiscard]] auto next_gc_threshold() const -> std::size_t;
    [[nodiscard]] auto should_collect() const -> bool { return bytes_allocated_ >= next_gc_threshold_; }
    [[nodiscard]] static constexpr auto default_threshold() -> std::size_t {
        return std::size_t{1024} * std::size_t{1024};
    }

private:
    void mark_value(const Value& value);
    void mark_object(GcObject* object);
    void sweep();

    GcObject* objects_ = nullptr;
    std::size_t bytes_allocated_ = 0;
    std::size_t next_gc_threshold_ = default_threshold();
};

}  // namespace impulse::runtime
