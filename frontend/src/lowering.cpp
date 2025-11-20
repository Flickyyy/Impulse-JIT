#include "impulse/frontend/lowering.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "impulse/frontend/expression_eval.h"
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

[[nodiscard]] auto format_constant(double value) -> std::string {
    if (!std::isfinite(value)) {
        return {};
    }
    const double rounded = std::round(value);
    if (std::abs(value - rounded) < 1e-9) {
        return std::to_string(static_cast<long long>(rounded));
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

[[nodiscard]] auto binary_operator_token(Expression::BinaryOperator op) -> std::string_view {
    switch (op) {
        case Expression::BinaryOperator::Add:
            return "+";
        case Expression::BinaryOperator::Subtract:
            return "-";
        case Expression::BinaryOperator::Multiply:
            return "*";
        case Expression::BinaryOperator::Divide:
            return "/";
        case Expression::BinaryOperator::Equal:
            return "==";
        case Expression::BinaryOperator::NotEqual:
            return "!=";
        case Expression::BinaryOperator::Less:
            return "<";
        case Expression::BinaryOperator::LessEqual:
            return "<=";
        case Expression::BinaryOperator::Greater:
            return ">";
        case Expression::BinaryOperator::GreaterEqual:
            return ">=";
    }
    return "?";
}

void lower_expression_to_stack(const Expression& expr, std::vector<ir::Instruction>& instructions) {
    switch (expr.kind) {
        case Expression::Kind::Literal:
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Literal,
                .operands = std::vector<std::string>{expr.literal_value},
            });
            break;
        case Expression::Kind::Identifier:
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Reference,
                .operands = std::vector<std::string>{expr.identifier.value},
            });
            break;
        case Expression::Kind::Binary:
            if (expr.left) {
                lower_expression_to_stack(*expr.left, instructions);
            }
            if (expr.right) {
                lower_expression_to_stack(*expr.right, instructions);
            }
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Binary,
                .operands = std::vector<std::string>{std::string{binary_operator_token(expr.binary_operator)}},
            });
            break;
    }
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
                if (decl.binding.initializer_expr) {
                    binding.initializer = printExpression(*decl.binding.initializer_expr);
                    const auto evaluation = evaluateNumericExpression(*decl.binding.initializer_expr);
                    if (evaluation.status == ExpressionEvalStatus::Constant && evaluation.value.has_value()) {
                        const auto formatted = format_constant(*evaluation.value);
                        if (!formatted.empty()) {
                            binding.constant_value = formatted;
                        }
                    }
                    lower_expression_to_stack(*decl.binding.initializer_expr, binding.initializer_instructions);
                    binding.initializer_instructions.push_back(ir::Instruction{
                        .kind = ir::InstructionKind::Store,
                        .operands = std::vector<std::string>{decl.binding.name.value},
                    });
                } else {
                    binding.initializer = decl.binding.initializer.text;
                }
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

                if (!decl.function.parsed_body.statements.empty()) {
                    ir::FunctionBuilder builder(function);
                    auto& entry = builder.entry();
                    for (const auto& statement : decl.function.parsed_body.statements) {
                        switch (statement.kind) {
                            case Statement::Kind::Return:
                                if (statement.return_expression) {
                                    lower_expression_to_stack(*statement.return_expression, entry.instructions);
                                }
                                entry.instructions.push_back(ir::Instruction{
                                    .kind = ir::InstructionKind::Return,
                                    .operands = {},
                                });
                                break;
                            case Statement::Kind::Binding:
                                if (statement.binding.initializer_expr) {
                                    lower_expression_to_stack(*statement.binding.initializer_expr, entry.instructions);
                                    entry.instructions.push_back(ir::Instruction{
                                        .kind = ir::InstructionKind::Store,
                                        .operands = std::vector<std::string>{statement.binding.name.value},
                                    });
                                } else {
                                    entry.instructions.push_back(ir::Instruction{
                                        .kind = ir::InstructionKind::Comment,
                                        .operands = std::vector<std::string>{std::string{"unlowered binding "} +
                                                                             statement.binding.name.value},
                                    });
                                }
                                break;
                        }
                    }
                } else if (!function.body_snippet.empty()) {
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
