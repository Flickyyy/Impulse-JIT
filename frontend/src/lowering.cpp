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

int label_counter = 0;

[[nodiscard]] auto generate_label() -> std::string {
    return "L" + std::to_string(label_counter++);
}

struct LoopContext {
    std::string continue_label;
    std::string break_label;
};

struct ScopeLocal {
    std::string name;
    bool requires_drop = false;
};

void lower_statement_to_instructions(const Statement& statement, std::vector<ir::Instruction>& instructions,
                                     std::vector<LoopContext>& loop_stack,
                                     std::vector<std::vector<ScopeLocal>>& scope_stack);

void emit_scope_drops(const std::vector<ScopeLocal>& locals, std::vector<ir::Instruction>& instructions) {
    for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
        if (!it->requires_drop || it->name.empty()) {
            continue;
        }
        ir::Instruction reference;
        reference.kind = ir::InstructionKind::Reference;
        reference.operands = std::vector<std::string>{it->name};
        instructions.push_back(std::move(reference));

        ir::Instruction drop;
        drop.kind = ir::InstructionKind::Drop;
        instructions.push_back(std::move(drop));
    }
}

void lower_statements_to_instructions(const std::vector<Statement>& statements,
                                      std::vector<ir::Instruction>& instructions,
                                      std::vector<LoopContext>& loop_stack,
                                      std::vector<std::vector<ScopeLocal>>& scope_stack) {
    scope_stack.emplace_back();
    for (const auto& stmt : statements) {
        lower_statement_to_instructions(stmt, instructions, loop_stack, scope_stack);
    }
    emit_scope_drops(scope_stack.back(), instructions);
    scope_stack.pop_back();
}

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
        case Expression::BinaryOperator::Modulo:
            return "%";
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
        case Expression::BinaryOperator::LogicalAnd:
            return "&&";
        case Expression::BinaryOperator::LogicalOr:
            return "||";
    }
    return "?";
}

void lower_expression_to_stack(const Expression& expr, std::vector<ir::Instruction>& instructions) {
    switch (expr.kind) {
        case Expression::Kind::Literal:
            if (expr.literal_kind == Expression::LiteralKind::String) {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::StringLiteral,
                    .operands = std::vector<std::string>{expr.literal_value},
                });
            } else {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Literal,
                    .operands = std::vector<std::string>{expr.literal_value},
                });
            }
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
        case Expression::Kind::Unary:
            if (expr.operand) {
                lower_expression_to_stack(*expr.operand, instructions);
            }
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Unary,
                .operands = std::vector<std::string>{
                    expr.unary_operator == Expression::UnaryOperator::LogicalNot ? "!" : "-"
                },
            });
            break;
        case Expression::Kind::Call: {
            if (expr.callee == "array") {
                for (const auto& arg : expr.arguments) {
                    if (arg) {
                        lower_expression_to_stack(*arg, instructions);
                    }
                }
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::MakeArray,
                });
                break;
            }
            if (expr.callee == "array_get") {
                for (const auto& arg : expr.arguments) {
                    if (arg) {
                        lower_expression_to_stack(*arg, instructions);
                    }
                }
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::ArrayGet,
                });
                break;
            }
            if (expr.callee == "array_set") {
                for (const auto& arg : expr.arguments) {
                    if (arg) {
                        lower_expression_to_stack(*arg, instructions);
                    }
                }
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::ArraySet,
                });
                break;
            }
            if (expr.callee == "array_length") {
                for (const auto& arg : expr.arguments) {
                    if (arg) {
                        lower_expression_to_stack(*arg, instructions);
                    }
                }
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::ArrayLength,
                });
                break;
            }
            for (const auto& arg : expr.arguments) {
                lower_expression_to_stack(*arg, instructions);
            }
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Call,
                .operands = std::vector<std::string>{expr.callee, std::to_string(expr.arguments.size())},
            });
            break;
        }
    }
}

void lower_statement_to_instructions(const Statement& statement, std::vector<ir::Instruction>& instructions,
                                     std::vector<LoopContext>& loop_stack,
                                     std::vector<std::vector<ScopeLocal>>& scope_stack) {
    switch (statement.kind) {
        case Statement::Kind::Return:
            if (statement.return_expression) {
                lower_expression_to_stack(*statement.return_expression, instructions);
            }
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Return,
                .operands = {},
            });
            break;
        case Statement::Kind::Binding:
            if (statement.binding.initializer_expr) {
                lower_expression_to_stack(*statement.binding.initializer_expr, instructions);
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Store,
                    .operands = std::vector<std::string>{statement.binding.name.value},
                });
                if (!scope_stack.empty()) {
                    scope_stack.back().push_back(ScopeLocal{statement.binding.name.value, false});
                }
            }
            break;
        case Statement::Kind::If: {
            if (!statement.condition) {
                break;
            }

            const std::string else_label = generate_label();
            const std::string end_label = generate_label();

            lower_expression_to_stack(*statement.condition, instructions);
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::BranchIf,
                .operands = std::vector<std::string>{else_label, "0"},
            });

            lower_statements_to_instructions(statement.then_body, instructions, loop_stack, scope_stack);

            if (!statement.else_body.empty()) {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Branch,
                    .operands = std::vector<std::string>{end_label},
                });
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Label,
                    .operands = std::vector<std::string>{else_label},
                });
                lower_statements_to_instructions(statement.else_body, instructions, loop_stack, scope_stack);
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Label,
                    .operands = std::vector<std::string>{end_label},
                });
            } else {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Label,
                    .operands = std::vector<std::string>{else_label},
                });
            }
            break;
        }
        case Statement::Kind::While: {
            if (!statement.condition) {
                break;
            }

            const std::string loop_label = generate_label();
            const std::string end_label = generate_label();

            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Label,
                .operands = std::vector<std::string>{loop_label},
            });

            lower_expression_to_stack(*statement.condition, instructions);
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::BranchIf,
                .operands = std::vector<std::string>{end_label, "0"},
            });

            loop_stack.push_back(LoopContext{.continue_label = loop_label, .break_label = end_label});
            lower_statements_to_instructions(statement.then_body, instructions, loop_stack, scope_stack);
            loop_stack.pop_back();

            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Branch,
                .operands = std::vector<std::string>{loop_label},
            });
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Label,
                .operands = std::vector<std::string>{end_label},
            });
            break;
        }
        case Statement::Kind::For: {
            if (statement.for_initializer) {
                lower_statement_to_instructions(*statement.for_initializer, instructions, loop_stack, scope_stack);
            }

            const std::string loop_label = generate_label();
            const std::string end_label = generate_label();
            const bool has_increment = static_cast<bool>(statement.for_increment);
            const std::string continue_label = has_increment ? generate_label() : loop_label;

            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Label,
                .operands = std::vector<std::string>{loop_label},
            });

            if (statement.condition) {
                lower_expression_to_stack(*statement.condition, instructions);
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::BranchIf,
                    .operands = std::vector<std::string>{end_label, "0"},
                });
            }

            loop_stack.push_back(LoopContext{.continue_label = continue_label, .break_label = end_label});
            lower_statements_to_instructions(statement.then_body, instructions, loop_stack, scope_stack);
            loop_stack.pop_back();

            if (has_increment) {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Label,
                    .operands = std::vector<std::string>{continue_label},
                });
                lower_statement_to_instructions(*statement.for_increment, instructions, loop_stack, scope_stack);
            }

            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Branch,
                .operands = std::vector<std::string>{loop_label},
            });
            instructions.push_back(ir::Instruction{
                .kind = ir::InstructionKind::Label,
                .operands = std::vector<std::string>{end_label},
            });
            break;
        }
        case Statement::Kind::Break:
            if (!loop_stack.empty()) {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Branch,
                    .operands = std::vector<std::string>{loop_stack.back().break_label},
                });
            }
            break;
        case Statement::Kind::Continue:
            if (!loop_stack.empty()) {
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Branch,
                    .operands = std::vector<std::string>{loop_stack.back().continue_label},
                });
            }
            break;
        case Statement::Kind::ExprStmt:
            if (statement.expr) {
                lower_expression_to_stack(*statement.expr, instructions);
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Drop,
                    .operands = {},
                });
            }
            break;
        case Statement::Kind::Assign:
            if (statement.assign_value) {
                lower_expression_to_stack(*statement.assign_value, instructions);
                instructions.push_back(ir::Instruction{
                    .kind = ir::InstructionKind::Store,
                    .operands = std::vector<std::string>{statement.assign_target.value},
                });
            }
            break;
    }
}

}  // namespace

auto lower_to_ir(const Module& module) -> ir::Module {
    label_counter = 0;
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
                    std::vector<LoopContext> loop_stack;
                    std::vector<std::vector<ScopeLocal>> scope_stack;
                    lower_statements_to_instructions(decl.function.parsed_body.statements, entry.instructions,
                                                     loop_stack, scope_stack);
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
