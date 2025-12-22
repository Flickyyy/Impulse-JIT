#include "impulse/runtime/runtime.h"

#include <algorithm>
#include <istream>
#include <ostream>
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
// JIT only supports: literal, binary (with +, -, *, /, <, <=, >, >=, ==, !=), assign, return
// It does not support: branch, branch_if, call, array operations, string operations, unary, modulo, logical operators, globals, control flow
[[nodiscard]] static auto can_jit_compile(const ir::SsaFunction& ssa, const std::vector<std::string>& parameter_names) -> bool {
    if (!jit::JitCompiler::is_supported()) {
        return false;
    }
    
    // JIT doesn't handle control flow - only single-block functions (straight-line code)
    if (ssa.blocks.size() != 1) {
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
        "+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!="
    };
    
    // Check that function has a return statement
    bool has_return = false;
    for (const auto& block : ssa.blocks) {
        // Check for phi nodes - these indicate control flow
        if (!block.phi_nodes.empty()) {
            return false;
        }
        
        for (const auto& inst : block.instructions) {
            if (inst.opcode == "return") {
                has_return = true;
            }
            if (inst.opcode == "unary") {
                return false;  // Unary operations not supported
            }
            if (inst.opcode == "binary") {
                // Check if the operator is supported
                if (inst.immediates.empty() || 
                    supported_binary_ops.find(inst.immediates[0]) == supported_binary_ops.end()) {
                    return false;  // Unsupported binary operator (e.g., %, &&, ||)
                }
            } else if (inst.opcode != "literal" && inst.opcode != "assign" && 
                       inst.opcode != "return") {
                return false;  // Unsupported opcode (includes branch, branch_if, call, etc.)
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
            // Module is being reloaded - clear JIT cache for this module
            std::string module_prefix = loaded.name + "::";
            auto it = jit_cache_.begin();
            while (it != jit_cache_.end()) {
                if (it->first.find(module_prefix) == 0) {
                    it = jit_cache_.erase(it);
                } else {
                    ++it;
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

    std::vector<Value> stack;
    std::unordered_map<std::string, Value> locals;
    locals.reserve(parameters.size());
    for (const auto& [name, value] : parameters) {
        locals.emplace(name, value);
    }
    FrameGuard frame_guard(*this, locals, stack);
    auto ssa = ir::build_ssa(function);
    [[maybe_unused]] const bool optimized = ir::optimize_ssa(ssa);

    // Extract parameter names in order
    std::vector<std::string> param_names;
    param_names.reserve(function.parameters.size());
    for (const auto& param : function.parameters) {
        param_names.push_back(param.name);
    }
    
    // Check JIT cache first
    std::string cache_key = module.name + "::" + function.name;
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
            can_jit = can_jit_compile(ssa, param_names);
            if (can_jit) {
                jit::JitCompiler compiler;
                auto [func, buffer] = compiler.compile_with_buffer(ssa, param_names);
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

    SsaInterpreter interpreter(ssa, parameters, locals, module.module.functions, module.globals,
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

}  // namespace impulse::runtime
