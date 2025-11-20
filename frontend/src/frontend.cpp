#include "../include/impulse/frontend/frontend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/frontend/lowering.h"
#include "impulse/frontend/parser.h"
#include "impulse/frontend/semantic.h"
#include "impulse/ir/interpreter.h"
#include "impulse/runtime/runtime.h"

namespace ir = ::impulse::ir;
namespace runtime = ::impulse::runtime;

namespace {

[[nodiscard]] auto copy_message(const std::string& message) -> char* {
    auto* buffer = new char[message.size() + 1];
    std::memcpy(buffer, message.c_str(), message.size() + 1);
    return buffer;
}

struct CopiedDiagnostics {
    ImpulseParseDiagnostic* data = nullptr;
    size_t count = 0;
};

struct BindingEvaluation {
    std::string name;
    bool evaluated = false;
    double value = 0.0;
    std::string message;
};

struct CopiedBindings {
    ImpulseBindingValue* data = nullptr;
    size_t count = 0;
};

struct EvaluationSummary {
    bool success = false;
    std::vector<impulse::frontend::Diagnostic> diagnostics;
    std::vector<BindingEvaluation> bindings;
    std::unordered_map<std::string, double> environment;
    std::optional<ir::Module> lowered_module;
};

[[nodiscard]] auto module_name_from_lowered(const ir::Module& module) -> std::string {
    if (module.path.empty()) {
        return "<anonymous>";
    }
    std::ostringstream builder;
    for (size_t i = 0; i < module.path.size(); ++i) {
        if (i != 0) {
            builder << "::";
        }
        builder << module.path[i];
    }
    return builder.str();
}

[[nodiscard]] auto copy_diagnostics(const std::vector<impulse::frontend::Diagnostic>& diagnostics)
    -> CopiedDiagnostics {
    if (diagnostics.empty()) {
        return {};
    }

    auto* buffer = new ImpulseParseDiagnostic[diagnostics.size()];
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& diag = diagnostics[i];
        buffer[i] = ImpulseParseDiagnostic{
            .message = copy_message(diag.message),
            .line = diag.location.line,
            .column = diag.location.column,
        };
    }
    return CopiedDiagnostics{buffer, diagnostics.size()};
}

void free_diagnostics(ImpulseParseDiagnostic* diagnostics, size_t count) {
    if (diagnostics == nullptr) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        delete[] diagnostics[i].message;
    }
    delete[] diagnostics;
}

[[nodiscard]] auto copy_binding_values(const std::vector<BindingEvaluation>& values) -> CopiedBindings {
    if (values.empty()) {
        return {};
    }
    auto* buffer = new ImpulseBindingValue[values.size()];
    for (size_t i = 0; i < values.size(); ++i) {
        const auto& value = values[i];
        buffer[i] = ImpulseBindingValue{
            .name = copy_message(value.name),
            .evaluated = value.evaluated,
            .value = value.value,
            .message = value.message.empty() ? nullptr : copy_message(value.message),
        };
    }
    return CopiedBindings{buffer, values.size()};
}

void free_binding_values(ImpulseBindingValue* values, size_t count) {
    if (values == nullptr) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        delete[] values[i].name;
        delete[] values[i].message;
    }
    delete[] values;
}

[[nodiscard]] auto evaluate_module_internal(const ImpulseParseOptions* options) -> EvaluationSummary {
    EvaluationSummary summary;
    if (options == nullptr || options->source == nullptr) {
        return summary;
    }

    impulse::frontend::Parser parser(options->source);
    auto parseResult = parser.parseModule();

    summary.diagnostics = parseResult.diagnostics;
    summary.success = parseResult.success;

    if (!parseResult.success) {
        return summary;
    }

    auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    summary.success = summary.success && semantic.success;
    summary.diagnostics.insert(summary.diagnostics.end(), semantic.diagnostics.begin(), semantic.diagnostics.end());

    if (!semantic.success) {
        return summary;
    }

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);
    summary.lowered_module = lowered;
    std::unordered_map<std::string, double> environment;
    summary.bindings.reserve(lowered.bindings.size());

    for (const auto& binding : lowered.bindings) {
        BindingEvaluation evaluation;
        evaluation.name = binding.name;
        const auto eval = ir::interpret_binding(binding, environment);
        switch (eval.status) {
            case ir::EvalStatus::Success:
                if (eval.value.has_value()) {
                    evaluation.evaluated = true;
                    evaluation.value = *eval.value;
                    environment[binding.name] = *eval.value;
                } else {
                    evaluation.message = "No value produced";
                }
                break;
            case ir::EvalStatus::NonConstant:
                evaluation.message = eval.message.empty() ? std::string{"Expression depends on unevaluated bindings"}
                                                          : eval.message;
                break;
            case ir::EvalStatus::Error:
                summary.success = false;
                evaluation.message = eval.message.empty() ? std::string{"Evaluation error"} : eval.message;
                break;
        }
        summary.bindings.push_back(std::move(evaluation));
    }

    summary.environment = environment;

    return summary;
}

}  // namespace

auto impulse_parse_module(const ImpulseParseOptions* options) -> ImpulseParseResult {
    if (options == nullptr || options->source == nullptr) {
        return ImpulseParseResult{
            .success = false,
            .diagnostics = nullptr,
            .diagnostic_count = 0,
        };
    }

    impulse::frontend::Parser parser(options->source);
    auto result = parser.parseModule();

    const auto copied = copy_diagnostics(result.diagnostics);
    return ImpulseParseResult{
        .success = result.success,
        .diagnostics = copied.data,
        .diagnostic_count = copied.count,
    };
}

void impulse_free_parse_result(ImpulseParseResult* result) {
    if (result == nullptr) {
        return;
    }
    free_diagnostics(result->diagnostics, result->diagnostic_count);
    result->diagnostics = nullptr;
    result->diagnostic_count = 0;
}

auto impulse_emit_ir(const ImpulseParseOptions* options) -> ImpulseIrResult {
    if (options == nullptr || options->source == nullptr) {
        return ImpulseIrResult{
            .success = false,
            .diagnostics = nullptr,
            .diagnostic_count = 0,
            .ir_text = nullptr,
        };
    }

    impulse::frontend::Parser parser(options->source);
    auto result = parser.parseModule();

    const auto copied = copy_diagnostics(result.diagnostics);

    ImpulseIrResult irResult{
        .success = result.success,
        .diagnostics = copied.data,
        .diagnostic_count = copied.count,
        .ir_text = nullptr,
    };

    if (result.success) {
        const auto irText = impulse::frontend::emit_ir_text(result.module);
        irResult.ir_text = copy_message(irText);
    }

    return irResult;
}

void impulse_free_ir_result(ImpulseIrResult* result) {
    if (result == nullptr) {
        return;
    }
    free_diagnostics(result->diagnostics, result->diagnostic_count);
    result->diagnostics = nullptr;
    result->diagnostic_count = 0;
    if (result->ir_text != nullptr) {
        delete[] result->ir_text;
        result->ir_text = nullptr;
    }
}

auto impulse_check_module(const ImpulseParseOptions* options) -> ImpulseSemanticResult {
    if (options == nullptr || options->source == nullptr) {
        return ImpulseSemanticResult{
            .success = false,
            .diagnostics = nullptr,
            .diagnostic_count = 0,
        };
    }

    impulse::frontend::Parser parser(options->source);
    auto parseResult = parser.parseModule();

    std::vector<impulse::frontend::Diagnostic> combined = parseResult.diagnostics;
    bool overallSuccess = parseResult.success;

    if (parseResult.success) {
        auto semantic = impulse::frontend::analyzeModule(parseResult.module);
        overallSuccess = overallSuccess && semantic.success;
        combined.insert(combined.end(), semantic.diagnostics.begin(), semantic.diagnostics.end());
    }

    const auto copied = copy_diagnostics(combined);
    return ImpulseSemanticResult{
        .success = overallSuccess,
        .diagnostics = copied.data,
        .diagnostic_count = copied.count,
    };
}

void impulse_free_semantic_result(ImpulseSemanticResult* result) {
    if (result == nullptr) {
        return;
    }
    free_diagnostics(result->diagnostics, result->diagnostic_count);
    result->diagnostics = nullptr;
    result->diagnostic_count = 0;
}

auto impulse_evaluate_bindings(const ImpulseParseOptions* options) -> ImpulseEvalResult {
    if (options == nullptr || options->source == nullptr) {
        return ImpulseEvalResult{
            .success = false,
            .diagnostics = nullptr,
            .diagnostic_count = 0,
            .bindings = nullptr,
            .binding_count = 0,
        };
    }

    const auto summary = evaluate_module_internal(options);
    const auto copiedDiagnostics = copy_diagnostics(summary.diagnostics);
    const auto copiedValues = copy_binding_values(summary.bindings);
    return ImpulseEvalResult{
        .success = summary.success,
        .diagnostics = copiedDiagnostics.data,
        .diagnostic_count = copiedDiagnostics.count,
        .bindings = copiedValues.data,
        .binding_count = copiedValues.count,
    };
}

void impulse_free_eval_result(ImpulseEvalResult* result) {
    if (result == nullptr) {
        return;
    }
    free_diagnostics(result->diagnostics, result->diagnostic_count);
    result->diagnostics = nullptr;
    result->diagnostic_count = 0;
    free_binding_values(result->bindings, result->binding_count);
    result->bindings = nullptr;
    result->binding_count = 0;
}

auto impulse_run_module(const ImpulseParseOptions* options, const char* entry_binding) -> ImpulseRunResult {
    if (options == nullptr || options->source == nullptr) {
        return ImpulseRunResult{
            .success = false,
            .diagnostics = nullptr,
            .diagnostic_count = 0,
            .has_exit_code = false,
            .exit_code = 0,
            .message = copy_message("Invalid options supplied"),
        };
    }

    const auto summary = evaluate_module_internal(options);
    const auto copiedDiagnostics = copy_diagnostics(summary.diagnostics);

    ImpulseRunResult result{
        .success = false,
        .diagnostics = copiedDiagnostics.data,
        .diagnostic_count = copiedDiagnostics.count,
        .has_exit_code = false,
        .exit_code = 0,
        .message = nullptr,
    };

    if (!summary.success) {
        result.message = copy_message("Module contains errors; unable to run");
        return result;
    }

    const std::string entry = (entry_binding != nullptr && entry_binding[0] != '\0') ? std::string(entry_binding)
                                                                                      : std::string("main");

    if (summary.lowered_module.has_value()) {
        runtime::Vm vm;
        const auto loadResult = vm.load(*summary.lowered_module);
        if (!loadResult.success) {
            const std::string reason = loadResult.diagnostics.empty() ? std::string{"Runtime load failed"}
                                                                     : loadResult.diagnostics.front();
            result.message = copy_message(reason);
            return result;
        }

        const auto moduleName = module_name_from_lowered(*summary.lowered_module);
        const auto vmResult = vm.run(moduleName, entry);
        if (vmResult.status == runtime::VmStatus::Success && vmResult.has_value) {
            result.success = true;
            result.has_exit_code = true;
            result.exit_code = static_cast<int>(std::llround(vmResult.value));
            return result;
        }

        if (vmResult.status != runtime::VmStatus::MissingSymbol) {
            const std::string reason = vmResult.message.empty() ? std::string{"Runtime execution failed"}
                                                                : vmResult.message;
            result.message = copy_message(reason);
            return result;
        }
    }

    const auto bindingIt = std::find_if(summary.bindings.begin(), summary.bindings.end(),
                                        [&](const BindingEvaluation& value) { return value.name == entry; });

    if (bindingIt == summary.bindings.end()) {
        result.message = copy_message("Symbol '" + entry + "' not found");
        return result;
    }

    if (!bindingIt->evaluated) {
        const std::string reason = bindingIt->message.empty() ? std::string{"Binding is not constant"} : bindingIt->message;
        result.message = copy_message(reason);
        return result;
    }

    result.success = true;
    result.has_exit_code = true;
    result.exit_code = static_cast<int>(std::llround(bindingIt->value));
    return result;
}

void impulse_free_run_result(ImpulseRunResult* result) {
    if (result == nullptr) {
        return;
    }
    free_diagnostics(result->diagnostics, result->diagnostic_count);
    result->diagnostics = nullptr;
    result->diagnostic_count = 0;
    if (result->message != nullptr) {
        delete[] result->message;
        result->message = nullptr;
    }
}
