#include "impulse/runtime/runtime.h"

#include <algorithm>
#include <istream>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "impulse/ir/interpreter.h"
#include "impulse/ir/optimizer.h"
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

}  // namespace impulse::runtime
