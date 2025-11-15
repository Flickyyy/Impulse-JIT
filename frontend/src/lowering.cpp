#include "impulse/frontend/lowering.h"

#include <utility>

#include "impulse/ir/builder.h"
#include "impulse/ir/printer.h"

namespace impulse::frontend {

namespace ir = ::impulse::ir;

namespace {

[[nodiscard]] auto to_storage(BindingKind kind) -> ir::StorageClass {
    switch (kind) {
        case BindingKind::Let:
            return ir::StorageClass::Let;
        case BindingKind::Const:
            return ir::StorageClass::Const;
        case BindingKind::Var:
            return ir::StorageClass::Var;
    }
    return ir::StorageClass::Let;
}

}  // namespace

auto lower_to_ir(const Module& module) -> ir::Module {
    ir::Module lowered;
    lowered.path.reserve(module.decl.path.size());
    for (const auto& segment : module.decl.path) {
        lowered.path.push_back(segment.value);
    }

    for (const auto& decl : module.declarations) {
        switch (decl.kind) {
            case Declaration::Kind::Binding: {
                ir::Binding binding;
                binding.storage = to_storage(decl.binding.kind);
                binding.name = decl.binding.name.value;
                binding.type = decl.binding.type_name.value;
                binding.initializer = decl.binding.initializer.text;
                binding.exported = decl.exported;
                lowered.bindings.push_back(std::move(binding));
                break;
            }
            case Declaration::Kind::Function: {
                ir::Function function;
                function.name = decl.function.name.value;
                function.exported = decl.exported;
                for (const auto& param : decl.function.parameters) {
                    ir::FunctionParameter loweredParam;
                    loweredParam.name = param.name.value;
                    loweredParam.type = param.type_name.value;
                    function.parameters.push_back(std::move(loweredParam));
                }
                if (decl.function.return_type.has_value()) {
                    function.return_type = decl.function.return_type->value;
                }
                function.body_snippet = decl.function.body.text;

                if (!function.body_snippet.empty()) {
                    ir::FunctionBuilder builder(function);
                    builder.appendComment(function.body_snippet);
                }

                lowered.functions.push_back(std::move(function));
                break;
            }
            case Declaration::Kind::Struct: {
                ir::Struct structure;
                structure.name = decl.structure.name.value;
                structure.exported = decl.exported;
                for (const auto& field : decl.structure.fields) {
                    ir::StructField loweredField;
                    loweredField.name = field.name.value;
                    loweredField.type = field.type_name.value;
                    structure.fields.push_back(std::move(loweredField));
                }
                lowered.structs.push_back(std::move(structure));
                break;
            }
            case Declaration::Kind::Interface: {
                ir::Interface interfaceDecl;
                interfaceDecl.name = decl.interface_decl.name.value;
                interfaceDecl.exported = decl.exported;
                for (const auto& method : decl.interface_decl.methods) {
                    ir::InterfaceMethod loweredMethod;
                    loweredMethod.name = method.name.value;
                    for (const auto& param : method.parameters) {
                        ir::FunctionParameter loweredParam;
                        loweredParam.name = param.name.value;
                        loweredParam.type = param.type_name.value;
                        loweredMethod.parameters.push_back(std::move(loweredParam));
                    }
                    if (method.return_type.has_value()) {
                        loweredMethod.return_type = method.return_type->value;
                    }
                    interfaceDecl.methods.push_back(std::move(loweredMethod));
                }
                lowered.interfaces.push_back(std::move(interfaceDecl));
                break;
            }
        }
    }

    return lowered;
}

auto emit_ir_text(const Module& module) -> std::string {
    auto lowered = lower_to_ir(module);
    return ir::print_module(lowered);
}

}  // namespace impulse::frontend
