#include "impulse/runtime/gc_heap.h"

#include <algorithm>
#include <cassert>

namespace impulse::runtime {

GcHeap::GcHeap() = default;

GcHeap::~GcHeap() {
    GcObject* current = objects_;
    while (current != nullptr) {
        GcObject* next = current->next;
        delete current;
        current = next;
    }
}

auto GcHeap::allocate_array(std::size_t length, const Value& fill) -> GcObject* {
    auto* object = new GcObject();
    object->kind = ObjectKind::Array;
    object->marked = false;
    object->fields.resize(length, fill);
    object->next = objects_;
    objects_ = object;

    bytes_allocated_ += sizeof(GcObject) + (length * sizeof(Value));
    return object;
}

void GcHeap::collect(const std::vector<Value*>& roots) {
    for (Value* root : roots) {
        if (root != nullptr) {
            mark_value(*root);
        }
    }

    // Sweep now calculates bytes_allocated_ internally to avoid second pass
    sweep();

    next_gc_threshold_ = std::max(bytes_allocated_ * std::size_t{2}, default_threshold());
}

void GcHeap::set_next_gc_threshold(std::size_t bytes) {
    next_gc_threshold_ = bytes;
}

void GcHeap::mark_value(const Value& value) {
    if (!value.is_object() || value.object == nullptr) {
        return;
    }
    mark_object(value.object);
}

void GcHeap::mark_object(GcObject* object) {
    if (object == nullptr || object->marked) {
        return;
    }

    std::vector<GcObject*> worklist;
    worklist.push_back(object);

    while (!worklist.empty()) {
        GcObject* current = worklist.back();
        worklist.pop_back();
        if (current == nullptr || current->marked) {
            continue;
        }
        current->marked = true;
        for (auto& field : current->fields) {
            if (field.is_object() && field.object != nullptr && !field.object->marked) {
                worklist.push_back(field.object);
            }
        }
    }
}

void GcHeap::sweep() {
    GcObject** current = &objects_;
    std::size_t live_bytes = 0;
    while (*current != nullptr) {
        if (!(*current)->marked) {
            GcObject* unreached = *current;
            *current = unreached->next;
            delete unreached;
        } else {
            // Track live bytes during sweep to avoid second pass
            live_bytes += sizeof(GcObject) + ((*current)->fields.size() * sizeof(Value));
            (*current)->marked = false;
            current = &((*current)->next);
        }
    }
    bytes_allocated_ = live_bytes;
}

auto GcHeap::bytes_allocated() const -> std::size_t { return bytes_allocated_; }

auto GcHeap::live_object_count() const -> std::size_t {
    std::size_t count = 0;
    for (GcObject* object = objects_; object != nullptr; object = object->next) {
        ++count;
    }
    return count;
}

auto GcHeap::next_gc_threshold() const -> std::size_t { return next_gc_threshold_; }

}  // namespace impulse::runtime
