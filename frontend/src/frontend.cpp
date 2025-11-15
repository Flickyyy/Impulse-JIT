#include "../include/impulse/frontend/frontend.h"

#include <cstring>
#include <string>

#include "impulse/frontend/lowering.h"
#include "impulse/frontend/parser.h"
#include "impulse/frontend/semantic.h"

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
