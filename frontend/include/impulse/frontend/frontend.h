#pragma once

#ifdef __cplusplus
#include <cstddef>
#else
#include <stdbool.h>
#include <stddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ImpulseParseOptions {
    const char* path;
    const char* source;
};

struct ImpulseParseDiagnostic {
    const char* message;
    size_t line;
    size_t column;
};

struct ImpulseParseResult {
    bool success;
    struct ImpulseParseDiagnostic* diagnostics;
    size_t diagnostic_count;
};

#ifndef __cplusplus
typedef struct ImpulseParseOptions ImpulseParseOptions;
typedef struct ImpulseParseDiagnostic ImpulseParseDiagnostic;
typedef struct ImpulseParseResult ImpulseParseResult;
#endif

#ifdef __cplusplus
[[nodiscard]] auto impulse_parse_module(const ImpulseParseOptions* options) -> ImpulseParseResult;
#else
struct ImpulseParseResult impulse_parse_module(const struct ImpulseParseOptions* options);
#endif
#ifdef __cplusplus
void impulse_free_parse_result(ImpulseParseResult* result);
#else
void impulse_free_parse_result(struct ImpulseParseResult* result);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif
