#include "impulse/ir/printer.h"

#include <sstream>
#include <string>

namespace impulse::ir {

namespace {

[[nodiscard]] auto join_path(const std::vector<std::string>& path) -> std::string {
    if (path.empty()) {
        return "<anonymous>";
    }
    std::ostringstream os;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            os << "::";
        }
        os << path[i];
    }
    return os.str();
}

[[nodiscard]] auto to_string(StorageClass storage) -> std::string {
    switch (storage) {
        case StorageClass::Let:
            return "let";
        case StorageClass::Const:
            return "const";
        case StorageClass::Var:
            return "var";
    }
    return "let";
}

[[nodiscard]] auto format_instruction(const Instruction& inst) -> std::string {
    std::ostringstream out;
    switch (inst.kind) {
        case InstructionKind::Comment:
            if (!inst.operands.empty()) {
                out << "# " << inst.operands.front();
            } else {
                out << "# <comment>";
            }
            break;
        case InstructionKind::Return:
            out << "return";
            if (!inst.operands.empty() && !inst.operands.front().empty()) {
                out << ' ' << inst.operands.front();
            }
            break;
        case InstructionKind::Literal:
            out << "literal";
            if (!inst.operands.empty()) {
                out << ' ' << inst.operands.front();
            }
            break;
        case InstructionKind::Reference:
            out << "reference";
            if (!inst.operands.empty()) {
                out << ' ' << inst.operands.front();
            }
            break;
        case InstructionKind::Binary:
            out << "binary";
            if (!inst.operands.empty()) {
                out << ' ' << inst.operands.front();
            }
            break;
        case InstructionKind::Unary:
            out << "unary";
            if (!inst.operands.empty()) {
                out << ' ' << inst.operands.front();
            }
            break;
        case InstructionKind::Store:
            out << "store";
            if (!inst.operands.empty()) {
                out << ' ' << inst.operands.front();
            }
            break;
    }
    return out.str();
}

}  // namespace

auto print_module(const Module& module) -> std::string {
    std::ostringstream out;
    out << "module " << join_path(module.path) << "\n\n";

    if (!module.structs.empty()) {
        for (const auto& structure : module.structs) {
            if (structure.exported) {
                out << "export ";
            }
            out << "struct " << structure.name << " {\n";
            for (const auto& field : structure.fields) {
                out << "  " << field.name << ": " << field.type << ";\n";
            }
            out << "}\n\n";
        }
    }

    if (!module.interfaces.empty()) {
        for (const auto& interfaceDecl : module.interfaces) {
            if (interfaceDecl.exported) {
                out << "export ";
            }
            out << "interface " << interfaceDecl.name << " {\n";
            for (const auto& method : interfaceDecl.methods) {
                out << "  func " << method.name << '(';
                for (size_t i = 0; i < method.parameters.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    const auto& param = method.parameters[i];
                    out << param.name << ": " << param.type;
                }
                out << ')';
                if (method.return_type.has_value()) {
                    out << " -> " << *method.return_type;
                }
                out << ";\n";
            }
            out << "}\n\n";
        }
    }

    if (!module.bindings.empty()) {
        for (const auto& binding : module.bindings) {
            if (binding.exported) {
                out << "export ";
            }
            out << to_string(binding.storage) << ' ' << binding.name << ": " << binding.type;
            if (!binding.initializer.empty()) {
                out << " = " << binding.initializer;
            }
            out << ";";
            if (binding.constant_value.has_value()) {
                out << "  # = " << *binding.constant_value;
            }
            out << "\n";
            if (!binding.initializer_instructions.empty()) {
                out << "  {\n";
                for (const auto& inst : binding.initializer_instructions) {
                    out << "    " << format_instruction(inst) << "\n";
                }
                out << "  }\n";
            }
        }
        out << "\n";
    }

    for (const auto& function : module.functions) {
        if (function.exported) {
            out << "export ";
        }
        out << "func " << function.name << '(';
        for (size_t i = 0; i < function.parameters.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            const auto& param = function.parameters[i];
            out << param.name << ": " << param.type;
        }
        out << ')';
        if (function.return_type.has_value()) {
            out << " -> " << *function.return_type;
        }
        out << " {\n";

        if (!function.blocks.empty()) {
            for (const auto& block : function.blocks) {
                const auto& label = block.label.empty() ? std::string{"block"} : block.label;
                out << "  ^" << label << ":\n";
                if (block.instructions.empty()) {
                    out << "    # empty block\n";
                } else {
                    for (const auto& inst : block.instructions) {
                        out << "    " << format_instruction(inst) << "\n";
                    }
                }
            }
        } else if (!function.body_snippet.empty()) {
            out << "  # snippet\n";
            out << "  " << function.body_snippet << "\n";
        } else {
            out << "  # empty body\n";
        }

        out << "}\n\n";
    }

    return out.str();
}

}  // namespace impulse::ir
