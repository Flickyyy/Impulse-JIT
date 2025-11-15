#include "impulse/frontend/semantic.h"

#include <string>
#include <unordered_map>

namespace impulse::frontend {

namespace {

using NameMap = std::unordered_map<std::string, SourceLocation>;

void reportDuplicate(SemanticResult& result, const Identifier& identifier, const std::string& kind) {
    result.success = false;
    result.diagnostics.push_back(Diagnostic{
        .location = identifier.location,
        .message = "Duplicate " + kind + " '" + identifier.value + "'",
    });
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

}  // namespace

auto analyzeModule(const Module& module) -> SemanticResult {
    SemanticResult result;

    NameMap bindingNames;
    NameMap functionNames;
    NameMap structNames;
    NameMap interfaceNames;

    for (const auto& decl : module.declarations) {
        switch (decl.kind) {
            case Declaration::Kind::Binding:
                checkUnique(result, bindingNames, decl.binding.name, "binding");
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
