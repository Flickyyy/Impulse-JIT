#include "impulse/frontend/dump.h"

#include <ostream>
#include <string>

namespace impulse::frontend {
namespace {

void indent(std::ostream& out, std::size_t depth) {
    for (std::size_t i = 0; i < depth; ++i) {
        out << "  ";
    }
}

auto token_kind_to_string(TokenKind kind) -> const char* {
    switch (kind) {
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::IntegerLiteral: return "IntegerLiteral";
        case TokenKind::FloatLiteral: return "FloatLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::BooleanLiteral: return "BooleanLiteral";
        case TokenKind::KwModule: return "KwModule";
        case TokenKind::KwImport: return "KwImport";
        case TokenKind::KwAs: return "KwAs";
        case TokenKind::KwExport: return "KwExport";
        case TokenKind::KwFunc: return "KwFunc";
        case TokenKind::KwStruct: return "KwStruct";
        case TokenKind::KwInterface: return "KwInterface";
        case TokenKind::KwLet: return "KwLet";
        case TokenKind::KwVar: return "KwVar";
        case TokenKind::KwConst: return "KwConst";
        case TokenKind::KwIf: return "KwIf";
        case TokenKind::KwElse: return "KwElse";
        case TokenKind::KwWhile: return "KwWhile";
        case TokenKind::KwFor: return "KwFor";
        case TokenKind::KwIn: return "KwIn";
        case TokenKind::KwReturn: return "KwReturn";
        case TokenKind::KwBreak: return "KwBreak";
        case TokenKind::KwContinue: return "KwContinue";
        case TokenKind::KwMatch: return "KwMatch";
        case TokenKind::KwOption: return "KwOption";
        case TokenKind::KwResult: return "KwResult";
        case TokenKind::KwPanic: return "KwPanic";
        case TokenKind::LParen: return "LParen";
        case TokenKind::RParen: return "RParen";
        case TokenKind::LBrace: return "LBrace";
        case TokenKind::RBrace: return "RBrace";
        case TokenKind::LBracket: return "LBracket";
        case TokenKind::RBracket: return "RBracket";
        case TokenKind::Comma: return "Comma";
        case TokenKind::Semicolon: return "Semicolon";
        case TokenKind::Colon: return "Colon";
        case TokenKind::DoubleColon: return "DoubleColon";
        case TokenKind::Dot: return "Dot";
        case TokenKind::Arrow: return "Arrow";
        case TokenKind::Plus: return "Plus";
        case TokenKind::Minus: return "Minus";
        case TokenKind::Star: return "Star";
        case TokenKind::Slash: return "Slash";
        case TokenKind::Percent: return "Percent";
        case TokenKind::Ampersand: return "Ampersand";
        case TokenKind::AmpersandAmpersand: return "AmpersandAmpersand";
        case TokenKind::Pipe: return "Pipe";
        case TokenKind::PipePipe: return "PipePipe";
        case TokenKind::Bang: return "Bang";
        case TokenKind::BangEqual: return "BangEqual";
        case TokenKind::Equal: return "Equal";
        case TokenKind::EqualEqual: return "EqualEqual";
        case TokenKind::Less: return "Less";
        case TokenKind::LessEqual: return "LessEqual";
        case TokenKind::Greater: return "Greater";
        case TokenKind::GreaterEqual: return "GreaterEqual";
        case TokenKind::Error: return "Error";
        case TokenKind::EndOfFile: return "EndOfFile";
    }
    return "Unknown";
}

[[nodiscard]] auto escape_string(const std::string& value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

void dump_identifier(const Identifier& id, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Identifier value=\"" << id.value << "\" loc=" << id.location.line << ':' << id.location.column << '\n';
}

void dump_expression(const Expression* expr, std::ostream& out, std::size_t depth);
void dump_statement(const Statement& stmt, std::ostream& out, std::size_t depth);

auto binding_kind_to_string(BindingKind kind) -> const char* {
    switch (kind) {
        case BindingKind::Let: return "let";
        case BindingKind::Const: return "const";
        case BindingKind::Var: return "var";
    }
    return "unknown";
}

auto expr_kind_to_string(Expression::Kind kind) -> const char* {
    switch (kind) {
        case Expression::Kind::Literal: return "Literal";
        case Expression::Kind::Identifier: return "Identifier";
        case Expression::Kind::Binary: return "Binary";
        case Expression::Kind::Unary: return "Unary";
        case Expression::Kind::Call: return "Call";
    }
    return "Unknown";
}

auto binary_op_to_string(Expression::BinaryOperator op) -> const char* {
    switch (op) {
        case Expression::BinaryOperator::Add: return "+";
        case Expression::BinaryOperator::Subtract: return "-";
        case Expression::BinaryOperator::Multiply: return "*";
        case Expression::BinaryOperator::Divide: return "/";
        case Expression::BinaryOperator::Modulo: return "%";
        case Expression::BinaryOperator::Equal: return "==";
        case Expression::BinaryOperator::NotEqual: return "!=";
        case Expression::BinaryOperator::Less: return "<";
        case Expression::BinaryOperator::LessEqual: return "<=";
        case Expression::BinaryOperator::Greater: return ">";
        case Expression::BinaryOperator::GreaterEqual: return ">=";
        case Expression::BinaryOperator::LogicalAnd: return "&&";
        case Expression::BinaryOperator::LogicalOr: return "||";
    }
    return "?";
}

auto unary_op_to_string(Expression::UnaryOperator op) -> const char* {
    switch (op) {
        case Expression::UnaryOperator::LogicalNot: return "!";
        case Expression::UnaryOperator::Negate: return "-";
    }
    return "?";
}

void dump_binding_decl(const BindingDecl& decl, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Binding kind=" << binding_kind_to_string(decl.kind) << '\n';
    dump_identifier(decl.name, out, depth + 1);
    dump_identifier(decl.type_name, out, depth + 1);
    if (decl.initializer_expr) {
        indent(out, depth + 1);
        out << "InitializerExpr" << '\n';
        dump_expression(decl.initializer_expr.get(), out, depth + 2);
    } else if (!decl.initializer.text.empty()) {
        indent(out, depth + 1);
        out << "InitializerSnippet text=\"" << decl.initializer.text << "\"" << '\n';
    }
}

void dump_call_arguments(const Expression& expr, std::ostream& out, std::size_t depth) {
    if (expr.arguments.empty()) {
        return;
    }
    indent(out, depth);
    out << "Arguments" << '\n';
    for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
        indent(out, depth + 1);
        out << "Arg[" << i << "]" << '\n';
        dump_expression(expr.arguments[i].get(), out, depth + 2);
    }
}

void dump_expression(const Expression* expr, std::ostream& out, std::size_t depth) {
    if (expr == nullptr) {
        indent(out, depth);
        out << "<null-expression>" << '\n';
        return;
    }

    indent(out, depth);
    out << "Expression kind=" << expr_kind_to_string(expr->kind) << " loc=" << expr->location.line << ':'
        << expr->location.column << '\n';

    switch (expr->kind) {
        case Expression::Kind::Literal: {
            indent(out, depth + 1);
            const char* kind_label = "Number";
            switch (expr->literal_kind) {
                case Expression::LiteralKind::Number:
                    kind_label = "Number";
                    break;
                case Expression::LiteralKind::Boolean:
                    kind_label = "Boolean";
                    break;
                case Expression::LiteralKind::String:
                    kind_label = "String";
                    break;
            }
            std::string value = expr->literal_value;
            if (expr->literal_kind == Expression::LiteralKind::String) {
                value = escape_string(value);
            }
            out << "Literal kind=" << kind_label << " value=\"" << value << "\"" << '\n';
            break;
        }
        case Expression::Kind::Identifier: {
            dump_identifier(expr->identifier, out, depth + 1);
            break;
        }
        case Expression::Kind::Binary: {
            indent(out, depth + 1);
            out << "Operator " << binary_op_to_string(expr->binary_operator) << '\n';
            indent(out, depth + 1);
            out << "Left" << '\n';
            dump_expression(expr->left.get(), out, depth + 2);
            indent(out, depth + 1);
            out << "Right" << '\n';
            dump_expression(expr->right.get(), out, depth + 2);
            break;
        }
        case Expression::Kind::Unary: {
            indent(out, depth + 1);
            out << "Operator " << unary_op_to_string(expr->unary_operator) << '\n';
            indent(out, depth + 1);
            out << "Operand" << '\n';
            dump_expression(expr->operand.get(), out, depth + 2);
            break;
        }
        case Expression::Kind::Call: {
            indent(out, depth + 1);
            out << "Callee " << expr->callee << '\n';
            dump_call_arguments(*expr, out, depth + 1);
            break;
        }
    }
}

void dump_statements(const std::vector<Statement>& statements, std::ostream& out, std::size_t depth) {
    for (std::size_t i = 0; i < statements.size(); ++i) {
        indent(out, depth);
        out << "Statement[" << i << "]" << '\n';
        dump_statement(statements[i], out, depth + 1);
    }
}

auto statement_kind_to_string(Statement::Kind kind) -> const char* {
    switch (kind) {
        case Statement::Kind::Return: return "Return";
        case Statement::Kind::Binding: return "Binding";
        case Statement::Kind::If: return "If";
        case Statement::Kind::While: return "While";
        case Statement::Kind::For: return "For";
        case Statement::Kind::Break: return "Break";
        case Statement::Kind::Continue: return "Continue";
        case Statement::Kind::ExprStmt: return "ExprStmt";
        case Statement::Kind::Assign: return "Assign";
    }
    return "Unknown";
}

void dump_statement(const Statement& stmt, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Kind " << statement_kind_to_string(stmt.kind) << " loc=" << stmt.location.line << ':'
        << stmt.location.column << '\n';

    switch (stmt.kind) {
        case Statement::Kind::Return: {
            indent(out, depth + 1);
            out << "ReturnValue" << '\n';
            dump_expression(stmt.return_expression.get(), out, depth + 2);
            break;
        }
        case Statement::Kind::Binding: {
            dump_binding_decl(stmt.binding, out, depth + 1);
            break;
        }
        case Statement::Kind::If: {
            indent(out, depth + 1);
            out << "Condition" << '\n';
            dump_expression(stmt.condition.get(), out, depth + 2);
            indent(out, depth + 1);
            out << "Then" << '\n';
            dump_statements(stmt.then_body, out, depth + 2);
            indent(out, depth + 1);
            out << "Else" << '\n';
            dump_statements(stmt.else_body, out, depth + 2);
            break;
        }
        case Statement::Kind::While: {
            indent(out, depth + 1);
            out << "Condition" << '\n';
            dump_expression(stmt.condition.get(), out, depth + 2);
            indent(out, depth + 1);
            out << "Body" << '\n';
            dump_statements(stmt.then_body, out, depth + 2);
            break;
        }
        case Statement::Kind::For: {
            indent(out, depth + 1);
            out << "Initializer" << '\n';
            if (stmt.for_initializer) {
                dump_statement(*stmt.for_initializer, out, depth + 2);
            } else {
                indent(out, depth + 2);
                out << "<none>" << '\n';
            }
            indent(out, depth + 1);
            out << "Condition" << '\n';
            dump_expression(stmt.condition.get(), out, depth + 2);
            indent(out, depth + 1);
            out << "Increment" << '\n';
            if (stmt.for_increment) {
                dump_statement(*stmt.for_increment, out, depth + 2);
            } else {
                indent(out, depth + 2);
                out << "<none>" << '\n';
            }
            indent(out, depth + 1);
            out << "Body" << '\n';
            dump_statements(stmt.then_body, out, depth + 2);
            break;
        }
        case Statement::Kind::Break:
        case Statement::Kind::Continue: {
            break;
        }
        case Statement::Kind::ExprStmt: {
            indent(out, depth + 1);
            out << "Expression" << '\n';
            dump_expression(stmt.expr.get(), out, depth + 2);
            break;
        }
        case Statement::Kind::Assign: {
            indent(out, depth + 1);
            out << "Target" << '\n';
            dump_identifier(stmt.assign_target, out, depth + 2);
            indent(out, depth + 1);
            out << "Value" << '\n';
            dump_expression(stmt.assign_value.get(), out, depth + 2);
            break;
        }
    }
}

void dump_parameters(const std::vector<Parameter>& params, std::ostream& out, std::size_t depth) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        indent(out, depth);
        out << "Param[" << i << "]" << '\n';
        dump_identifier(params[i].name, out, depth + 1);
        dump_identifier(params[i].type_name, out, depth + 1);
    }
}

void dump_function(const FunctionDecl& func, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Function" << '\n';
    dump_identifier(func.name, out, depth + 1);
    if (func.return_type.has_value()) {
        dump_identifier(*func.return_type, out, depth + 1);
    } else {
        indent(out, depth + 1);
        out << "ReturnType <none>" << '\n';
    }
    indent(out, depth + 1);
    out << "Parameters" << '\n';
    dump_parameters(func.parameters, out, depth + 2);
    indent(out, depth + 1);
    out << "Body" << '\n';
    dump_statements(func.parsed_body.statements, out, depth + 2);
}

void dump_struct(const StructDecl& decl, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Struct" << '\n';
    dump_identifier(decl.name, out, depth + 1);
    for (std::size_t i = 0; i < decl.fields.size(); ++i) {
        indent(out, depth + 1);
        out << "Field[" << i << "]" << '\n';
        dump_identifier(decl.fields[i].name, out, depth + 2);
        dump_identifier(decl.fields[i].type_name, out, depth + 2);
    }
}

void dump_interface(const InterfaceDecl& decl, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Interface" << '\n';
    dump_identifier(decl.name, out, depth + 1);
    for (std::size_t i = 0; i < decl.methods.size(); ++i) {
        indent(out, depth + 1);
        out << "Method[" << i << "]" << '\n';
        dump_identifier(decl.methods[i].name, out, depth + 2);
        indent(out, depth + 2);
        out << "Parameters" << '\n';
        dump_parameters(decl.methods[i].parameters, out, depth + 3);
        if (decl.methods[i].return_type.has_value()) {
            dump_identifier(*decl.methods[i].return_type, out, depth + 2);
        } else {
            indent(out, depth + 2);
            out << "ReturnType <none>" << '\n';
        }
    }
}

void dump_declaration(const Declaration& decl, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Declaration exported=" << (decl.exported ? "true" : "false") << '\n';
    switch (decl.kind) {
        case Declaration::Kind::Binding:
            dump_binding_decl(decl.binding, out, depth + 1);
            break;
        case Declaration::Kind::Function:
            dump_function(decl.function, out, depth + 1);
            break;
        case Declaration::Kind::Struct:
            dump_struct(decl.structure, out, depth + 1);
            break;
        case Declaration::Kind::Interface:
            dump_interface(decl.interface_decl, out, depth + 1);
            break;
    }
}

void dump_module_decl(const ModuleDecl& decl, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "ModuleDecl" << '\n';
    if (decl.path.empty()) {
        indent(out, depth + 1);
        out << "<anonymous>" << '\n';
        return;
    }
    for (const auto& segment : decl.path) {
        dump_identifier(segment, out, depth + 1);
    }
}

void dump_import(const ImportDecl& decl, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << "Import" << '\n';
    for (const auto& segment : decl.path) {
        dump_identifier(segment, out, depth + 1);
    }
    if (decl.alias.has_value()) {
        indent(out, depth + 1);
        out << "Alias" << '\n';
        dump_identifier(*decl.alias, out, depth + 2);
    }
}

}  // namespace

void dump_tokens(const std::vector<Token>& tokens, std::ostream& out) {
    for (const auto& token : tokens) {
        out << '[' << token.line << ':' << token.column << "] " << token_kind_to_string(token.kind);
        if (!token.lexeme.empty()) {
            out << " \"" << token.lexeme << '\"';
        }
        out << '\n';
    }
}

void dump_ast(const Module& module, std::ostream& out) {
    out << "Module" << '\n';
    dump_module_decl(module.decl, out, 1);
    out << "Imports" << '\n';
    if (module.imports.empty()) {
        indent(out, 1);
        out << "<none>" << '\n';
    } else {
        for (std::size_t i = 0; i < module.imports.size(); ++i) {
            indent(out, 1);
            out << "Import[" << i << "]" << '\n';
            dump_import(module.imports[i], out, 2);
        }
    }
    out << "Declarations" << '\n';
    if (module.declarations.empty()) {
        indent(out, 1);
        out << "<none>" << '\n';
        return;
    }
    for (std::size_t i = 0; i < module.declarations.size(); ++i) {
        indent(out, 1);
        out << "Decl[" << i << "]" << '\n';
        dump_declaration(module.declarations[i], out, 2);
    }
}

}  // namespace impulse::frontend
