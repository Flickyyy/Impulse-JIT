#include "../include/impulse/frontend/frontend.h"

#include <cstring>

#include "impulse/frontend/parser.h"

namespace {

[[nodiscard]] auto copy_message(const std::string& message) -> char* {
    auto* buffer = new char[message.size() + 1];
    std::memcpy(buffer, message.c_str(), message.size() + 1);
    return buffer;
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

    ImpulseParseResult cResult{
        .success = result.success,
        .diagnostics = nullptr,
        .diagnostic_count = result.diagnostics.size(),
    };

    if (!result.diagnostics.empty()) {
        cResult.diagnostics = new ImpulseParseDiagnostic[result.diagnostics.size()];
        for (size_t i = 0; i < result.diagnostics.size(); ++i) {
            const auto& diag = result.diagnostics[i];
            cResult.diagnostics[i] = ImpulseParseDiagnostic{
                .message = copy_message(diag.message),
                .line = diag.location.line,
                .column = diag.location.column,
            };
        }
    }

    return cResult;
}

void impulse_free_parse_result(ImpulseParseResult* result) {
    if (result == nullptr || result->diagnostics == nullptr) {
        return;
    }

    for (size_t i = 0; i < result->diagnostic_count; ++i) {
        delete[] result->diagnostics[i].message;
    }
    delete[] result->diagnostics;
    result->diagnostics = nullptr;
    result->diagnostic_count = 0;
}
