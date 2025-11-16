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

struct ImpulseIrResult {
    bool success;
    struct ImpulseParseDiagnostic* diagnostics;
    size_t diagnostic_count;
    char* ir_text;
};

struct ImpulseSemanticResult {
    bool success;
    struct ImpulseParseDiagnostic* diagnostics;
    size_t diagnostic_count;
};

struct ImpulseBindingValue {
    char* name;
    bool evaluated;
    double value;
    char* message;
};

struct ImpulseEvalResult {
    bool success;
    struct ImpulseParseDiagnostic* diagnostics;
    size_t diagnostic_count;
    struct ImpulseBindingValue* bindings;
    size_t binding_count;
};

#ifndef __cplusplus
typedef struct ImpulseParseOptions ImpulseParseOptions;
typedef struct ImpulseParseDiagnostic ImpulseParseDiagnostic;
typedef struct ImpulseParseResult ImpulseParseResult;
typedef struct ImpulseIrResult ImpulseIrResult;
typedef struct ImpulseSemanticResult ImpulseSemanticResult;
typedef struct ImpulseBindingValue ImpulseBindingValue;
typedef struct ImpulseEvalResult ImpulseEvalResult;
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
[[nodiscard]] auto impulse_emit_ir(const ImpulseParseOptions* options) -> ImpulseIrResult;
#else
struct ImpulseIrResult impulse_emit_ir(const struct ImpulseParseOptions* options);
#endif
#ifdef __cplusplus
void impulse_free_ir_result(ImpulseIrResult* result);
#else
void impulse_free_ir_result(struct ImpulseIrResult* result);
#endif

#ifdef __cplusplus
[[nodiscard]] auto impulse_check_module(const ImpulseParseOptions* options) -> ImpulseSemanticResult;
#else
struct ImpulseSemanticResult impulse_check_module(const struct ImpulseParseOptions* options);
#endif
#ifdef __cplusplus
void impulse_free_semantic_result(ImpulseSemanticResult* result);
#else
void impulse_free_semantic_result(struct ImpulseSemanticResult* result);
#endif

#ifdef __cplusplus
[[nodiscard]] auto impulse_evaluate_bindings(const ImpulseParseOptions* options) -> ImpulseEvalResult;
#else
struct ImpulseEvalResult impulse_evaluate_bindings(const struct ImpulseParseOptions* options);
#endif
#ifdef __cplusplus
void impulse_free_eval_result(ImpulseEvalResult* result);
#else
void impulse_free_eval_result(struct ImpulseEvalResult* result);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif
