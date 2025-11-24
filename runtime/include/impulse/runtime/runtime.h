#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/ir.h"
#include "impulse/runtime/gc_heap.h"
#include "impulse/runtime/value.h"

namespace impulse::runtime {

enum class VmStatus : std::uint8_t {
    Success,
    ModuleError,
    MissingSymbol,
    RuntimeError,
};

struct VmResult {
    VmStatus status = VmStatus::RuntimeError;
    bool has_value = false;
    double value = 0.0;
    std::string message;
};

struct VmLoadResult {
    bool success = true;
    std::vector<std::string> diagnostics;
};

class FrameGuard;

class Vm {
public:
    auto load(ir::Module module) -> VmLoadResult;

    [[nodiscard]] auto run(const std::string& module_name, const std::string& entry) const -> VmResult;

    void collect_garbage() const;

private:
    friend class FrameGuard;

    struct LoadedModule {
        std::string name;
        ir::Module module;
        std::unordered_map<std::string, Value> globals;
    };

    struct ExecutionFrame {
        std::unordered_map<std::string, Value>* locals = nullptr;
        std::vector<Value>* stack = nullptr;
    };

    [[nodiscard]] auto execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, Value>& parameters) const -> VmResult;

    [[nodiscard]] static auto normalize_module_name(const ir::Module& module) -> std::string;

    void push_frame(std::unordered_map<std::string, Value>& locals, std::vector<Value>& stack) const;
    void pop_frame() const;
    void gather_roots(std::vector<Value*>& out) const;
    void maybe_collect() const;

    std::vector<LoadedModule> modules_;
    mutable GcHeap heap_;
    mutable std::vector<ExecutionFrame> frames_;
    mutable std::vector<Value*> root_buffer_;
};

}  // namespace impulse::runtime
