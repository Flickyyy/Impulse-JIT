#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/ir.h"
#include "impulse/jit/jit.h"
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

    void set_trace_stream(std::ostream* stream) const;
    void set_input_stream(std::istream* stream) const;
    void set_read_line_provider(std::function<std::optional<std::string>()> provider) const;
    void set_jit_enabled(bool enabled) const;

    void collect_garbage() const;

    // JIT cache inspection API (for testing)
    // Check if a function is cached
    [[nodiscard]] auto is_function_cached(const std::string& module_name, const std::string& function_name) const -> bool;
    // Check if a function was JIT compiled (returns true only if cached AND has compiled function)
    [[nodiscard]] auto is_function_jit_compiled(const std::string& module_name, const std::string& function_name) const -> bool;
    // Get cache entry count (for testing)
    [[nodiscard]] auto get_jit_cache_size() const -> size_t;

    // Profiling API
    void set_profiling_enabled(bool enabled) const;
    void reset_profiling() const;
    void dump_profiling_results(std::ostream& out) const;
    [[nodiscard]] auto get_profiling_results() const -> std::string;

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

    struct JitCacheEntry {
        jit::JitFunction function = nullptr;
        jit::CodeBuffer code_buffer;  // Keep executable memory alive
        bool can_jit = false;
        
        JitCacheEntry() = default;
        JitCacheEntry(JitCacheEntry&&) = default;
        auto operator=(JitCacheEntry&&) -> JitCacheEntry& = default;
        JitCacheEntry(const JitCacheEntry&) = delete;
        auto operator=(const JitCacheEntry&) -> JitCacheEntry& = delete;
    };

    [[nodiscard]] auto execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, Value>& parameters,
                                        std::string* output_buffer) const -> VmResult;

    [[nodiscard]] static auto normalize_module_name(const ir::Module& module) -> std::string;

    void push_frame(std::unordered_map<std::string, Value>& locals, std::vector<Value>& stack) const;
    void pop_frame() const;
    void gather_roots(std::vector<Value*>& out) const;
    void maybe_collect() const;

    std::vector<LoadedModule> modules_;
    mutable GcHeap heap_;
    mutable std::vector<ExecutionFrame> frames_;
    mutable std::vector<Value*> root_buffer_;
    mutable std::ostream* trace_stream_ = nullptr;
    mutable std::istream* input_stream_ = nullptr;
    mutable std::function<std::optional<std::string>()> read_line_provider_;
    mutable bool jit_enabled_ = true;
    mutable std::string output_buffer_;
    // JIT cache: maps (module_name, function_name) -> JitCacheEntry
    mutable std::unordered_map<std::string, JitCacheEntry> jit_cache_;
    // SSA cache: maps (module_name, function_name) -> SsaFunction
    // This avoids rebuilding SSA on every function call (major performance bottleneck)
    mutable std::unordered_map<std::string, ir::SsaFunction> ssa_cache_;
    
    // Profiling data
    struct FunctionProfile {
        std::string full_name;  // "module::function"
        uint64_t call_count = 0;
        std::chrono::nanoseconds total_time{0};
        std::chrono::nanoseconds min_time{std::chrono::nanoseconds::max()};
        std::chrono::nanoseconds max_time{0};
        bool was_jit_compiled = false;
    };
    mutable bool profiling_enabled_ = false;
    mutable std::unordered_map<std::string, FunctionProfile> profiling_data_;
};

}  // namespace impulse::runtime
