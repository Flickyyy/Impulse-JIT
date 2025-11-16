#include "impulse/frontend/semantic.h"

#include <string>
#include <unordered_map>

#include "impulse/frontend/expression_eval.h"

namespace impulse::frontend {

namespace {

using NameMap = std::unordered_map<std::string, SourceLocation>;

void reportDuplicate(SemanticResult& result, const SourceLocation& location, const std::string& value,
                     const std::string& kind) {
    result.success = false;
    result.diagnostics.push_back(Diagnostic{
        .location = location,
        .message = "Duplicate " + kind + " '" + value + "'",
    });
}

void reportDuplicate(SemanticResult& result, const Identifier& identifier, const std::string& kind) {
    reportDuplicate(result, identifier.location, identifier.value, kind);
}

void checkUnique(SemanticResult& result, NameMap& map, const Identifier& identifier, const std::string& kind) {
    if (identifier.value.empty()) {
        return;
    }

    const auto [iter, inserted] = map.emplace(identifier.value, identifier.location);
    if (!inserted) {
        reportDuplicate(result, identifier, kind);
    }
}

void checkUniqueKey(SemanticResult& result, NameMap& map, const std::string& key, const SourceLocation& location,
                    const std::string& kind) {
    if (key.empty()) {
        return;
    }
    const auto [iter, inserted] = map.emplace(key, location);
    if (!inserted) {
        reportDuplicate(result, location, key, kind);
    }
}

[[nodiscard]] auto joinPath(const std::vector<Identifier>& path) -> std::string {
    std::string combined;
    combined.reserve(path.size() * 6);
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            combined += "::";
        }
        combined += path[i].value;
    }
    return combined;
}

void reportConstDiagnostic(SemanticResult& result, const BindingDecl& binding, const std::string& message) {
    const SourceLocation location = binding.initializer_expr ? binding.initializer_expr->location : binding.name.location;
    result.success = false;
    result.diagnostics.push_back(Diagnostic{
        .location = location,
        .message = std::string{"const binding '"} + binding.name.value + "' " + message,
    });
}

void checkConstInitializer(SemanticResult& result, const BindingDecl& binding) {
    if (!binding.initializer_expr) {
        reportConstDiagnostic(result, binding, "requires an initializer");
        return;
    }

    const auto evaluation = evaluateNumericExpression(*binding.initializer_expr);
    switch (evaluation.status) {
        case ExpressionEvalStatus::Constant:
            return;
        case ExpressionEvalStatus::NonConstant:
            reportConstDiagnostic(result, binding, "requires a compile-time constant initializer");
            return;
        case ExpressionEvalStatus::Error: {
            const std::string detail = evaluation.message.has_value() ? *evaluation.message : "invalid initializer";
            reportConstDiagnostic(result, binding, std::string{"initializer error: "} + detail);
            return;
        }
    }
}

}  // namespace

auto analyzeModule(const Module& module) -> SemanticResult {
    SemanticResult result;

    NameMap importPaths;
    NameMap importAliases;
    for (const auto& importDecl : module.imports) {
        const auto key = joinPath(importDecl.path);
        const SourceLocation location = importDecl.path.empty() ? SourceLocation{} : importDecl.path.front().location;
        checkUniqueKey(result, importPaths, key, location, "import path");
        if (importDecl.alias.has_value()) {
            checkUnique(result, importAliases, *importDecl.alias, "import alias");
        }
    }

    NameMap bindingNames;
    NameMap functionNames;
    NameMap structNames;
    NameMap interfaceNames;

    for (const auto& decl : module.declarations) {
        switch (decl.kind) {
            case Declaration::Kind::Binding:
                checkUnique(result, bindingNames, decl.binding.name, "binding");
                if (decl.binding.kind == BindingKind::Const) {
                    checkConstInitializer(result, decl.binding);
                }
                break;
            case Declaration::Kind::Function:
                checkUnique(result, functionNames, decl.function.name, "function");
                break;
            case Declaration::Kind::Struct: {
                checkUnique(result, structNames, decl.structure.name, "struct");
                NameMap fieldNames;
                const auto prefix = std::string{"field in struct "} + decl.structure.name.value;
                for (const auto& field : decl.structure.fields) {
                    checkUnique(result, fieldNames, field.name, prefix);
                }
                break;
            }
            case Declaration::Kind::Interface: {
                checkUnique(result, interfaceNames, decl.interface_decl.name, "interface");
                NameMap methodNames;
                const auto prefix = std::string{"method in interface "} + decl.interface_decl.name.value;
                for (const auto& method : decl.interface_decl.methods) {
                    checkUnique(result, methodNames, method.name, prefix);
                }
                break;
            }
        }
    }

    return result;
}

}  // namespace impulse::frontend
