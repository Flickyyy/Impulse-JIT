#include "impulse/runtime/runtime.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "impulse/ir/interpreter.h"
#include "impulse/ir/optimizer.h"
#include "impulse/jit/jit.h"
#include "impulse/runtime/runtime_utils.h"
#include "impulse/runtime/ssa_interpreter.h"

namespace impulse::runtime {

class FrameGuard {
public:
    FrameGuard(const Vm& vm, std::unordered_map<std::string, Value>& locals, std::vector<Value>& stack)
        : vm_(vm) {
        vm_.push_frame(locals, stack);
    }

    FrameGuard(const FrameGuard&) = delete;
    auto operator=(const FrameGuard&) -> FrameGuard& = delete;

    ~FrameGuard() { vm_.pop_frame(); }

private:
    const Vm& vm_;
};

// Check if an SSA function can be JIT compiled
// JIT supports: literal, binary (with +, -, *, /, %, <, <=, >, >=, ==, !=, &&, ||), unary (-, !), assign, return
// JIT also supports: branch, branch_if (control flow), phi nodes (via SSA deconstruction)
// It does not support: call, array operations, string operations, globals
[[nodiscard]] static auto can_jit_compile(const ir::SsaFunction& ssa, const std::vector<std::string>& parameter_names) -> bool {
    if (!jit::JitCompiler::is_supported()) {
        return false;
    }
    
    // Build set of parameter symbol names
    std::unordered_set<std::string> param_names_set(parameter_names.begin(), parameter_names.end());
    
    // Build set of parameter symbol IDs
    std::unordered_set<ir::SymbolId> param_symbol_ids;
    for (const auto& symbol : ssa.symbols) {
        if (param_names_set.find(symbol.name) != param_names_set.end()) {
            param_symbol_ids.insert(symbol.id);
        }
    }
    
    // Supported binary operators
    const std::unordered_set<std::string> supported_binary_ops = {
        "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "||"
    };
    
    // Check that function has a return statement
    // Phi nodes are now supported via SSA deconstruction
    bool has_return = false;
    for (const auto& block : ssa.blocks) {
        for (const auto& inst : block.instructions) {
            if (inst.opcode == "return") {
                has_return = true;
            }
            if (inst.opcode == "unary") {
                // Check if unary operator is supported (-, !)
                if (inst.immediates.empty()) {
                    return false;
                }
                const std::string& op = inst.immediates[0];
                if (op != "-" && op != "!") {
                    return false;  // Unsupported unary operator
                }
            }
            if (inst.opcode == "binary") {
                // Check if the operator is supported
                if (inst.immediates.empty() || 
                    supported_binary_ops.find(inst.immediates[0]) == supported_binary_ops.end()) {
                    return false;  // Unsupported binary operator (e.g., %, &&, ||)
                }
            } else if (inst.opcode == "branch" || inst.opcode == "branch_if") {
                // Control flow is now supported by JIT
                continue;
            } else if (inst.opcode != "literal" && inst.opcode != "assign" && 
                       inst.opcode != "return") {
                return false;  // Unsupported opcode (includes call, array ops, etc.)
            }
            
            // Check if any arguments reference non-parameter symbols (i.e., globals)
            for (const auto& arg : inst.arguments) {
                if (arg.version == 0) {
                    // Version 0 means it's a reference to a symbol, not a computed value
                    // If it's not a parameter, it's likely a global
                    if (param_symbol_ids.find(arg.symbol) == param_symbol_ids.end()) {
                        return false;  // Uses globals, not supported
                    }
                }
            }
        }
    }
    
    // Must have a return statement
    if (!has_return) {
        return false;
    }
    
    return true;
}

auto Vm::load(ir::Module module) -> VmLoadResult {
    VmLoadResult result;
    LoadedModule loaded;
    loaded.name = normalize_module_name(module);
    loaded.module = std::move(module);

    std::unordered_map<std::string, double> environment;
    for (const auto& binding : loaded.module.bindings) {
        const auto eval = ir::interpret_binding(binding, environment);
        if (eval.status == ir::EvalStatus::Success && eval.value.has_value()) {
            environment[binding.name] = *eval.value;
            loaded.globals[binding.name] = Value::make_number(*eval.value);
            continue;
        }

        result.success = false;
        if (!eval.message.empty()) {
            result.diagnostics.push_back("binding '" + binding.name + "': " + eval.message);
        } else {
            result.diagnostics.push_back("binding '" + binding.name + "' failed to evaluate");
        }
    }

    if (result.success) {
        const auto existing = std::find_if(modules_.begin(), modules_.end(), [&](const LoadedModule& candidate) {
            return candidate.name == loaded.name;
        });
        if (existing != modules_.end()) {
            // Module is being reloaded - clear caches for this module
            std::string module_prefix = loaded.name + "::";
            auto it = jit_cache_.begin();
            while (it != jit_cache_.end()) {
                if (it->first.find(module_prefix) == 0) {
                    it = jit_cache_.erase(it);
                } else {
                    ++it;
                }
            }
            // Clear SSA cache for this module
            auto ssa_it = ssa_cache_.begin();
            while (ssa_it != ssa_cache_.end()) {
                if (ssa_it->first.find(module_prefix) == 0) {
                    ssa_it = ssa_cache_.erase(ssa_it);
                } else {
                    ++ssa_it;
                }
            }
            *existing = std::move(loaded);
        } else {
            modules_.push_back(std::move(loaded));
        }
    }

    return result;
}

[[nodiscard]] auto Vm::run(const std::string& module_name, const std::string& entry) const -> VmResult {
    const auto moduleIt = std::find_if(modules_.begin(), modules_.end(), [&](const LoadedModule& module) {
        return module.name == module_name;
    });

    if (moduleIt == modules_.end()) {
        return make_result(VmStatus::MissingSymbol, "module '" + module_name + "' not loaded");
    }

    const auto functionIt = std::find_if(moduleIt->module.functions.begin(), moduleIt->module.functions.end(),
                                         [&](const ir::Function& function) { return function.name == entry; });
    if (functionIt == moduleIt->module.functions.end()) {
        return make_result(VmStatus::MissingSymbol, "function '" + entry + "' not found in module '" + module_name + "'");
    }

    std::unordered_map<std::string, Value> parameters;
    for (const auto& parameter : functionIt->parameters) {
        parameters.emplace(parameter.name, Value::make_number(0.0));
    }

    output_buffer_.clear();
    auto result = execute_function(*moduleIt, *functionIt, parameters, &output_buffer_);
    if (!output_buffer_.empty() && (result.status == VmStatus::Success || result.message.empty())) {
        result.message = output_buffer_;
    }
    return result;
}

[[nodiscard]] auto Vm::execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, Value>& parameters,
                                        std::string* output_buffer) const -> VmResult {
    if (function.blocks.empty()) {
        return make_result(VmStatus::ModuleError, "function has no basic blocks");
    }

    // Start profiling timer
    const auto start_time = profiling_enabled_ 
        ? std::chrono::high_resolution_clock::now() 
        : std::chrono::high_resolution_clock::time_point{};
    bool was_jit_compiled = false;

    std::vector<Value> stack;
    std::unordered_map<std::string, Value> locals;
    locals.reserve(parameters.size());
    for (const auto& [name, value] : parameters) {
        locals.emplace(name, value);
    }
    FrameGuard frame_guard(*this, locals, stack);
    
    // Check SSA cache first (major performance optimization - SSA building is expensive)
    std::string cache_key = module.name + "::" + function.name;
    const ir::SsaFunction* ssa_ptr = nullptr;
    auto ssa_cache_it = ssa_cache_.find(cache_key);
    if (ssa_cache_it != ssa_cache_.end()) {
        // Use cached SSA (use pointer to avoid copy)
        ssa_ptr = &ssa_cache_it->second;
    } else {
        // Build SSA and cache it
        ir::SsaFunction ssa = ir::build_ssa(function);
        [[maybe_unused]] const bool optimized = ir::optimize_ssa(ssa);
        ssa_cache_[cache_key] = std::move(ssa);  // Cache the SSA representation
        ssa_ptr = &ssa_cache_[cache_key];
    }

    // Extract parameter names in order
    std::vector<std::string> param_names;
    param_names.reserve(function.parameters.size());
    for (const auto& param : function.parameters) {
        param_names.push_back(param.name);
    }
    
    // Check JIT cache (cache_key already computed above)
    auto cache_it = jit_cache_.find(cache_key);
    
    // Try JIT compilation if the function is suitable
    // JIT can compile any suitable function, not just entry points
    if (jit_enabled_) {
        jit::JitFunction jit_func = nullptr;
        bool can_jit = false;
        
        if (cache_it != jit_cache_.end()) {
            // Use cached result
            jit_func = cache_it->second.function;
            can_jit = cache_it->second.can_jit;
        } else {
            // Check if function can be JIT compiled and compile if possible
            can_jit = can_jit_compile(*ssa_ptr, param_names);
            if (can_jit) {
                jit::JitCompiler compiler;
                auto [func, buffer] = compiler.compile_with_buffer(*ssa_ptr, param_names);
                jit_func = func;
                
                // Cache the result with the code buffer to keep executable memory alive
                JitCacheEntry entry;
                entry.function = jit_func;
                entry.code_buffer = std::move(buffer);
                entry.can_jit = can_jit;
                jit_cache_[cache_key] = std::move(entry);
            } else {
                // Cache that it can't be JIT compiled
                JitCacheEntry entry;
                entry.function = nullptr;
                entry.can_jit = false;
                jit_cache_[cache_key] = std::move(entry);
            }
        }
        
        if (can_jit && jit_func != nullptr) {
            was_jit_compiled = true;
            
            // Write trace output for JIT-compiled functions (to match interpreter behavior)
            if (trace_stream_ != nullptr) {
                *trace_stream_ << "enter function " << function.name << '\n';
            }
            
            // Prepare arguments array for JIT function
            std::vector<double> args_array;
            args_array.reserve(function.parameters.size());
            for (const auto& param : function.parameters) {
                auto it = parameters.find(param.name);
                if (it != parameters.end() && it->second.is_number()) {
                    args_array.push_back(it->second.as_number());
                } else {
                    args_array.push_back(0.0);
                }
            }
            
            // Call JIT compiled function
            // Use a valid pointer even for empty arrays to avoid nullptr issues
            double dummy = 0.0;
            double* args_ptr = args_array.empty() ? &dummy : args_array.data();
            double result_value = jit_func(args_ptr);
            
            // Write exit trace for JIT-compiled functions
            if (trace_stream_ != nullptr) {
                *trace_stream_ << "exit function " << function.name;
                *trace_stream_ << " = " << result_value;
                *trace_stream_ << '\n';
            }
            
            // Update profiling data
            if (profiling_enabled_) {
                const auto end_time = std::chrono::high_resolution_clock::now();
                const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
                std::string full_name = module.name + "::" + function.name;
                auto& profile = profiling_data_[full_name];
                profile.full_name = full_name;
                profile.call_count++;
                profile.total_time += duration;
                if (duration < profile.min_time) {
                    profile.min_time = duration;
                }
                if (duration > profile.max_time) {
                    profile.max_time = duration;
                }
                profile.was_jit_compiled = was_jit_compiled;
            }
            
            VmResult result;
            result.status = VmStatus::Success;
            result.has_value = true;
            result.value = result_value;
            // Mark that JIT was used (store in message for now, could add a field later)
            result.message = "__JIT_USED__";
            
            return result;
        }
        // JIT compilation failed or not suitable, fall through to interpreter
    }

    auto call_function = [this, &module, output_buffer](const ir::Function& target,
                                                        const std::vector<Value>& args) -> VmResult {
        std::unordered_map<std::string, Value> call_params;
        call_params.reserve(target.parameters.size());
        for (std::size_t i = 0; i < target.parameters.size() && i < args.size(); ++i) {
            call_params[target.parameters[i].name] = args[i];
        }
        return execute_function(module, target, call_params, output_buffer);
    };

    auto allocate_array = [this](std::size_t length) -> GcObject* { return heap_.allocate_array(length); };
    auto collect_fn = [this]() { maybe_collect(); };
    auto read_line = [this]() -> std::optional<std::string> {
        if (read_line_provider_) {
            if (auto provided = read_line_provider_()) {
                return provided;
            }
            return std::string{};
        }
        if (input_stream_ != nullptr) {
            std::string line;
            if (std::getline(*input_stream_, line)) {
                return line;
            }
            if (input_stream_->eof()) {
                input_stream_->clear();
            }
            return std::string{};
        }
        return std::string{};
    };

    if (trace_stream_ != nullptr) {
        *trace_stream_ << "enter function " << function.name << '\n';
    }

    SsaInterpreter interpreter(*ssa_ptr, parameters, locals, module.module.functions, module.globals,
                               std::move(call_function), std::move(allocate_array), std::move(collect_fn),
                               output_buffer, trace_stream_, std::move(read_line));
    auto result = interpreter.run();

    if (trace_stream_ != nullptr) {
        *trace_stream_ << "exit function " << function.name;
        if (result.status == VmStatus::Success && result.has_value) {
            *trace_stream_ << " = " << result.value;
        } else {
            *trace_stream_ << " status=" << static_cast<int>(result.status);
            if (!result.message.empty()) {
                *trace_stream_ << " message='" << result.message << "'";
            }
        }
        *trace_stream_ << '\n';
    }

    // Update profiling data for interpreter path
    if (profiling_enabled_) {
        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        std::string full_name = module.name + "::" + function.name;
        auto& profile = profiling_data_[full_name];
        profile.full_name = full_name;
        profile.call_count++;
        profile.total_time += duration;
        if (duration < profile.min_time) {
            profile.min_time = duration;
        }
        if (duration > profile.max_time) {
            profile.max_time = duration;
        }
        profile.was_jit_compiled = was_jit_compiled;
    }

    return result;
}

[[nodiscard]] auto Vm::normalize_module_name(const ir::Module& module) -> std::string { 
    return join_path(module.path); 
}

void Vm::set_trace_stream(std::ostream* stream) const { 
    trace_stream_ = stream; 
}

void Vm::set_input_stream(std::istream* stream) const { 
    input_stream_ = stream; 
}

void Vm::set_read_line_provider(std::function<std::optional<std::string>()> provider) const {
    read_line_provider_ = std::move(provider);
}

void Vm::set_jit_enabled(bool enabled) const {
    jit_enabled_ = enabled;
}

void Vm::collect_garbage() const {
    root_buffer_.clear();
    gather_roots(root_buffer_);
    heap_.collect(root_buffer_);
    root_buffer_.clear();
}

void Vm::push_frame(std::unordered_map<std::string, Value>& locals, std::vector<Value>& stack) const {
    ExecutionFrame frame;
    frame.locals = &locals;
    frame.stack = &stack;
    frames_.push_back(frame);
}

void Vm::pop_frame() const {
    if (!frames_.empty()) {
        frames_.pop_back();
    }
}

void Vm::gather_roots(std::vector<Value*>& out) const {
    for (const auto& module : modules_) {
        for (const auto& entry : module.globals) {
            out.push_back(const_cast<Value*>(&entry.second));
        }
    }

    for (const auto& frame : frames_) {
        if (frame.locals != nullptr) {
            for (auto& pair : *frame.locals) {
                out.push_back(&pair.second);
            }
        }
        if (frame.stack != nullptr) {
            for (auto& value : *frame.stack) {
                out.push_back(&value);
            }
        }
    }
}

void Vm::maybe_collect() const {
    if (heap_.should_collect()) {
        collect_garbage();
    }
}

auto Vm::is_function_cached(const std::string& module_name, const std::string& function_name) const -> bool {
    std::string cache_key = module_name + "::" + function_name;
    return jit_cache_.find(cache_key) != jit_cache_.end();
}

auto Vm::is_function_jit_compiled(const std::string& module_name, const std::string& function_name) const -> bool {
    std::string cache_key = module_name + "::" + function_name;
    auto it = jit_cache_.find(cache_key);
    if (it != jit_cache_.end()) {
        return it->second.can_jit && it->second.function != nullptr;
    }
    return false;
}

auto Vm::get_jit_cache_size() const -> size_t {
    return jit_cache_.size();
}

void Vm::set_profiling_enabled(bool enabled) const {
    profiling_enabled_ = enabled;
}

void Vm::reset_profiling() const {
    profiling_data_.clear();
}

void Vm::dump_profiling_results(std::ostream& out) const {
    if (profiling_data_.empty()) {
        out << "No profiling data collected.\n";
        return;
    }

    // Convert to vector and sort by total time (descending)
    std::vector<const FunctionProfile*> profiles;
    profiles.reserve(profiling_data_.size());
    for (const auto& [name, profile] : profiling_data_) {
        profiles.push_back(&profile);
    }
    std::sort(profiles.begin(), profiles.end(), 
        [](const FunctionProfile* a, const FunctionProfile* b) {
            return a->total_time > b->total_time;
        });

    // Calculate total time across all functions
    std::chrono::nanoseconds total_all_time{0};
    uint64_t total_calls = 0;
    for (const auto* profile : profiles) {
        total_all_time += profile->total_time;
        total_calls += profile->call_count;
    }

    out << "=== Function Profiling Results ===\n\n";
    out << "Total functions profiled: " << profiles.size() << "\n";
    out << "Total calls: " << total_calls << "\n";
    out << "Total time: " << std::chrono::duration_cast<std::chrono::milliseconds>(total_all_time).count() << " ms\n\n";

    // Header
    out << std::left << std::setw(40) << "Function Name"
        << std::right << std::setw(12) << "Calls"
        << std::setw(15) << "Total (ms)"
        << std::setw(15) << "Avg (ms)"
        << std::setw(15) << "Min (ns)"
        << std::setw(15) << "Max (ns)"
        << std::setw(12) << "JIT"
        << std::setw(12) << "% Total"
        << "\n";
    out << std::string(140, '-') << "\n";

    // Data rows
    for (const auto* profile : profiles) {
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(profile->total_time).count();
        auto avg_ns = profile->call_count > 0 
            ? profile->total_time.count() / profile->call_count 
            : 0;
        auto avg_ms = avg_ns / 1'000'000.0;
        auto min_ns = profile->min_time.count();
        auto max_ns = profile->max_time.count();
        double percent = total_all_time.count() > 0 
            ? (100.0 * profile->total_time.count()) / total_all_time.count() 
            : 0.0;

        out << std::left << std::setw(40) << profile->full_name
            << std::right << std::setw(12) << profile->call_count
            << std::setw(15) << total_ms
            << std::fixed << std::setprecision(3) << std::setw(15) << avg_ms
            << std::setprecision(0) << std::setw(15) << min_ns
            << std::setw(15) << max_ns
            << std::setw(12) << (profile->was_jit_compiled ? "Yes" : "No")
            << std::setprecision(2) << std::setw(12) << percent << "%"
            << "\n";
    }
    out << "\n";
}

auto Vm::get_profiling_results() const -> std::string {
    std::ostringstream oss;
    dump_profiling_results(oss);
    return oss.str();
}

}  // namespace impulse::runtime
