#include "impulse/frontend/semantic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "impulse/frontend/expression_eval.h"

namespace impulse::frontend {

namespace {

using NameMap = std::unordered_map<std::string, SourceLocation>;

enum class TypeKind : std::uint8_t {
    Int,
    Float,
    Bool,
    String,
    Array,
    Void,
    Struct,
    Interface,
    Unknown,
    Error,
};

struct TypeInfo {
    TypeKind kind = TypeKind::Unknown;
    std::string name;
};

struct StructInfo {
    TypeInfo type;
    std::uint32_t type_id = 0;
    std::vector<std::string> field_order;
    std::unordered_map<std::string, TypeInfo> fields;
    std::unordered_map<std::string, std::size_t> field_offsets;
    std::size_t payload_size = 0;
};

struct FunctionSignature {
    SourceLocation location;
    std::vector<TypeInfo> parameters;
    TypeInfo return_type;
};

struct TypeEnvironment {
    explicit TypeEnvironment(TypeEnvironment* parent_env = nullptr) : parent(parent_env) {}

    void define(const std::string& name, const TypeInfo& type) {
        bindings[name] = type;
    }

    [[nodiscard]] auto lookup(const std::string& name) const -> std::optional<TypeInfo> {
        const auto it = bindings.find(name);
        if (it != bindings.end()) {
            return it->second;
        }
        if (parent != nullptr) {
            return parent->lookup(name);
        }
        return std::nullopt;
    }

    TypeEnvironment* parent = nullptr;
    std::unordered_map<std::string, TypeInfo> bindings;
};

void addDiagnostic(SemanticResult& result, const SourceLocation& location, std::string message) {
    result.success = false;
    result.diagnostics.push_back(Diagnostic{.location = location, .message = std::move(message)});
}

[[nodiscard]] auto makePrimitive(TypeKind kind) -> TypeInfo {
    switch (kind) {
        case TypeKind::Int:
            return TypeInfo{TypeKind::Int, "int"};
        case TypeKind::Float:
            return TypeInfo{TypeKind::Float, "float"};
        case TypeKind::Bool:
            return TypeInfo{TypeKind::Bool, "bool"};
        case TypeKind::String:
            return TypeInfo{TypeKind::String, "string"};
        case TypeKind::Void:
            return TypeInfo{TypeKind::Void, "void"};
        default:
            break;
    }
    return TypeInfo{};
}

[[nodiscard]] auto makeStructType(const std::string& name) -> TypeInfo {
    return TypeInfo{TypeKind::Struct, name};
}

[[nodiscard]] auto makeInterfaceType(const std::string& name) -> TypeInfo {
    return TypeInfo{TypeKind::Interface, name};
}

[[nodiscard]] auto makeArrayType() -> TypeInfo {
    return TypeInfo{TypeKind::Array, "array"};
}

[[nodiscard]] auto makeErrorType() -> TypeInfo {
    return TypeInfo{TypeKind::Error, "<error>"};
}

[[nodiscard]] auto typeToString(const TypeInfo& type) -> std::string {
    if (!type.name.empty()) {
        return type.name;
    }
    switch (type.kind) {
        case TypeKind::Int:
            return "int";
        case TypeKind::Float:
            return "float";
        case TypeKind::Bool:
            return "bool";
        case TypeKind::String:
            return "string";
        case TypeKind::Array:
            return "array";
        case TypeKind::Void:
            return "void";
        case TypeKind::Struct:
        case TypeKind::Interface:
        case TypeKind::Unknown:
        case TypeKind::Error:
            return type.name.empty() ? std::string{"<unknown>"} : type.name;
    }
    return std::string{"<unknown>"};
}

[[nodiscard]] auto describeTypeCategory(const TypeInfo& type) -> std::string {
    switch (type.kind) {
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Bool:
        case TypeKind::String:
        case TypeKind::Array:
        case TypeKind::Void:
            return std::string{"primitive type '"} + typeToString(type) + "'";
        case TypeKind::Struct:
            return std::string{"struct type '"} + typeToString(type) + "'";
        case TypeKind::Interface:
            return std::string{"interface type '"} + typeToString(type) + "'";
        case TypeKind::Unknown:
            return "unknown type";
        case TypeKind::Error:
            return "invalid type";
    }
    return "type";
}

[[nodiscard]] auto isNumeric(const TypeInfo& type) -> bool {
    if (type.kind == TypeKind::Unknown) {
        return true;
    }
    return type.kind == TypeKind::Int || type.kind == TypeKind::Float || type.kind == TypeKind::Bool;
}

[[nodiscard]] auto isTruthy(const TypeInfo& type) -> bool {
    return type.kind == TypeKind::Bool || type.kind == TypeKind::Int;
}

[[nodiscard]] auto isError(const TypeInfo& type) -> bool {
    return type.kind == TypeKind::Error;
}

[[nodiscard]] auto isVoid(const TypeInfo& type) -> bool {
    return type.kind == TypeKind::Void;
}

[[nodiscard]] auto numericResultType(const TypeInfo& lhs, const TypeInfo& rhs) -> TypeInfo {
    if (lhs.kind == TypeKind::Float || rhs.kind == TypeKind::Float) {
        return makePrimitive(TypeKind::Float);
    }
    return makePrimitive(TypeKind::Int);
}

[[nodiscard]] auto isAssignable(const TypeInfo& target, const TypeInfo& value) -> bool {
    if (isError(target) || isError(value)) {
        return true;
    }
    if (target.kind == TypeKind::Unknown || value.kind == TypeKind::Unknown) {
        return true;
    }
    if (target.kind == value.kind) {
        if ((target.kind == TypeKind::Struct || target.kind == TypeKind::Interface) && target.name != value.name) {
            return false;
        }
        return true;
    }
    if (target.kind == TypeKind::Float && value.kind == TypeKind::Int) {
        return true;
    }
    if ((target.kind == TypeKind::Int && value.kind == TypeKind::Bool) ||
        (target.kind == TypeKind::Bool && value.kind == TypeKind::Int)) {
        return true;
    }
    return false;
}

[[nodiscard]] auto areEqualityComparable(const TypeInfo& lhs, const TypeInfo& rhs) -> bool {
    if (isError(lhs) || isError(rhs)) {
        return true;
    }
    if (lhs.kind == rhs.kind) {
        if ((lhs.kind == TypeKind::Struct || lhs.kind == TypeKind::Interface) && lhs.name != rhs.name) {
            return false;
        }
        return true;
    }
    if (isNumeric(lhs) && isNumeric(rhs)) {
        return true;
    }
    return false;
}

[[nodiscard]] auto areOrderedComparable(const TypeInfo& lhs, const TypeInfo& rhs) -> bool {
    return isNumeric(lhs) && isNumeric(rhs);
}

auto checkUnique(SemanticResult& result, NameMap& map, const Identifier& identifier, const std::string& kind) -> bool {
    if (identifier.value.empty()) {
        return false;
    }

    const auto [iter, inserted] = map.emplace(identifier.value, identifier.location);
    if (!inserted) {
        addDiagnostic(result, identifier.location, "Duplicate " + kind + " '" + identifier.value + "'");
    }
    return inserted;
}

auto checkUniqueKey(SemanticResult& result, NameMap& map, const std::string& key, const SourceLocation& location,
                    const std::string& kind) -> bool {
    if (key.empty()) {
        return false;
    }
    const auto [iter, inserted] = map.emplace(key, location);
    if (!inserted) {
        addDiagnostic(result, location, "Duplicate " + kind + " '" + key + "'");
    }
    return inserted;
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
    addDiagnostic(result, location, std::string{"const binding '"} + binding.name.value + "' " + message);
}

void checkConstInitializer(SemanticResult& result, const BindingDecl& binding) {
    if (!binding.initializer_expr) {
        reportConstDiagnostic(result, binding, "requires an initializer");
        return;
    }

    if (binding.initializer_expr->kind == Expression::Kind::Literal &&
        binding.initializer_expr->literal_kind == Expression::LiteralKind::String) {
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

class TypeRegistry {
public:
    TypeRegistry() {
        (void)registerType(makePrimitive(TypeKind::Int));
        (void)registerType(makePrimitive(TypeKind::Float));
        (void)registerType(makePrimitive(TypeKind::Bool));
        (void)registerType(makePrimitive(TypeKind::String));
        (void)registerType(makeArrayType());
        (void)registerType(makePrimitive(TypeKind::Void));
    }

    [[nodiscard]] auto registerStruct(const StructDecl& decl) -> const TypeInfo* {
        if (const TypeInfo* conflict = registerType(makeStructType(decl.name.value))) {
            return conflict;
        }
        StructInfo info;
        info.type = makeStructType(decl.name.value);
        info.type_id = nextStructTypeId++;
        info.field_order.reserve(decl.fields.size());
        structs.emplace(decl.name.value, std::move(info));
        return nullptr;
    }

    [[nodiscard]] auto registerInterface(const InterfaceDecl& decl) -> const TypeInfo* {
        return registerType(makeInterfaceType(decl.name.value));
    }

    void defineStructFields(const std::string& name, std::vector<std::pair<std::string, TypeInfo>> fieldsInfo) {
        auto it = structs.find(name);
        if (it == structs.end()) {
            return;
        }
        it->second.field_order.clear();
        it->second.fields.clear();
        it->second.field_offsets.clear();
        it->second.field_order.reserve(fieldsInfo.size());
        std::size_t offset = 0;
        for (auto& [fieldName, fieldType] : fieldsInfo) {
            it->second.field_order.push_back(fieldName);
            it->second.fields.emplace(fieldName, std::move(fieldType));
            it->second.field_offsets.emplace(fieldName, offset);
            offset += sizeof(double);
        }
        it->second.payload_size = offset;
    }

    [[nodiscard]] auto lookupStruct(const std::string& name) const -> const StructInfo* {
        const auto it = structs.find(name);
        if (it == structs.end()) {
            return nullptr;
        }
        return &it->second;
    }

    [[nodiscard]] auto resolve(const Identifier& typeName, SemanticResult& result, const std::string& context) const
        -> std::optional<TypeInfo> {
        if (typeName.value.empty()) {
            addDiagnostic(result, typeName.location, "Missing type name for " + context);
            return std::nullopt;
        }

        const auto it = types.find(typeName.value);
        if (it == types.end()) {
            addDiagnostic(result, typeName.location,
                          "Unknown type '" + typeName.value + "' referenced in " + context);
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] auto resolve(const Identifier& typeName) const -> std::optional<TypeInfo> {
        if (typeName.value.empty()) {
            return std::nullopt;
        }
        const auto it = types.find(typeName.value);
        if (it == types.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    [[nodiscard]] auto registerType(const TypeInfo& type) -> const TypeInfo* {
        if (type.name.empty()) {
            return nullptr;
        }
        const auto [iter, inserted] = types.emplace(type.name, type);
        if (!inserted) {
            return &iter->second;
        }
        return nullptr;
    }

    std::unordered_map<std::string, TypeInfo> types;
    std::unordered_map<std::string, StructInfo> structs;
    std::uint32_t nextStructTypeId = 1;
};

class TypeChecker {
public:
    TypeChecker(SemanticResult& semanticResult, const TypeRegistry& registry,
                const std::unordered_map<std::string, FunctionSignature>& signatures,
                std::unordered_map<std::string, TypeInfo>& globals)
        : result(semanticResult), types(registry), functions(signatures), globalBindings(globals) {}

    void checkGlobalBinding(const BindingDecl& binding) {
        const auto declaredOpt = types.resolve(binding.type_name, result, "binding declaration");
        TypeInfo declared = declaredOpt.value_or(makeErrorType());

        if (declared.kind == TypeKind::Void) {
            addDiagnostic(result, binding.type_name.location,
                          "Binding '" + binding.name.value + "' cannot have type 'void'");
            declared = makeErrorType();
        }

        if (!binding.initializer_expr) {
            addDiagnostic(result, binding.name.location,
                          "Binding '" + binding.name.value + "' requires an initializer");
            return;
        }

        TypeEnvironment env(nullptr);
        TypeEnvironment* previousEnv = currentEnv;
        currentEnv = &env;
        const TypeInfo initializerType = checkExpression(*binding.initializer_expr, env);
        currentEnv = previousEnv;
        if (!isAssignable(declared, initializerType)) {
            addDiagnostic(result, binding.initializer_expr->location,
                          "Cannot assign expression of type '" + typeToString(initializerType) +
                              "' to binding '" + binding.name.value + "' of type '" + typeToString(declared) + "'");
        }

        globalBindings[binding.name.value] = declared;
    }

    void checkFunction(const FunctionDecl& decl, const FunctionSignature& signature) {
        TypeEnvironment env;
        for (size_t i = 0; i < decl.parameters.size(); ++i) {
            const auto& param = decl.parameters[i];
            TypeInfo paramType = makeErrorType();
            if (i < signature.parameters.size()) {
                paramType = signature.parameters[i];
            }
            if (paramType.kind == TypeKind::Void) {
                addDiagnostic(result, param.type_name.location,
                              "Parameter '" + param.name.value + "' cannot have type 'void'");
            }
            env.define(param.name.value, paramType);
        }

        const bool guaranteesReturn =
            checkBlock(decl.parsed_body.statements, env, signature.return_type, /*insideLoop=*/false);
        if (!isVoid(signature.return_type) && !guaranteesReturn) {
            addDiagnostic(result, decl.name.location,
                          "Function '" + decl.name.value + "' may not return a value");
        }
    }

    [[nodiscard]] auto checkExpression(const Expression& expr, TypeEnvironment& env) -> TypeInfo {
        switch (expr.kind) {
            case Expression::Kind::Literal:
                return typeForLiteral(expr);
            case Expression::Kind::Identifier:
                return resolveIdentifier(expr.identifier);
            case Expression::Kind::Binary:
                return checkBinary(expr, env);
            case Expression::Kind::Unary:
                return checkUnary(expr, env);
            case Expression::Kind::Call:
                return checkCall(expr, env);
        }
        return makeErrorType();
    }

    [[nodiscard]] auto checkBlock(const std::vector<Statement>& statements, TypeEnvironment& env,
                                  const TypeInfo& returnType, bool insideLoop) -> bool {
        TypeEnvironment* previousEnv = currentEnv;
        currentEnv = &env;
        bool guaranteedReturn = false;
        for (const auto& stmt : statements) {
            if (guaranteedReturn) {
                break;
            }
            guaranteedReturn = checkStatement(stmt, env, returnType, insideLoop) || guaranteedReturn;
        }
        currentEnv = previousEnv;
        return guaranteedReturn;
    }

    [[nodiscard]] auto checkStatement(const Statement& statement, TypeEnvironment& env, const TypeInfo& returnType,
                                      bool insideLoop) -> bool {
        TypeEnvironment* previousEnv = currentEnv;
        currentEnv = &env;
        bool statementReturns = false;
        switch (statement.kind) {
            case Statement::Kind::Return: {
                if (isVoid(returnType)) {
                    if (statement.return_expression) {
                        TypeInfo valueType = checkExpression(*statement.return_expression, env);
                        if (!isError(valueType) && !isVoid(valueType)) {
                            addDiagnostic(result, statement.return_expression->location,
                                          "Return statement cannot produce a value in a function returning void");
                        }
                    }
                    break;
                }

                if (!statement.return_expression) {
                    addDiagnostic(result, statement.location,
                                  "Return statement requires a value of type '" + typeToString(returnType) + "'");
                    break;
                }
                const TypeInfo valueType = checkExpression(*statement.return_expression, env);
                if (!isAssignable(returnType, valueType)) {
                    addDiagnostic(result, statement.return_expression->location,
                                  "Cannot return expression of type '" + typeToString(valueType) +
                                      "' from function returning '" + typeToString(returnType) + "'");
                }
                statementReturns = true;
                break;
            }
            case Statement::Kind::Binding:
                checkLocalBinding(statement.binding, env);
                break;
            case Statement::Kind::If: {
                if (statement.condition) {
                    const TypeInfo conditionType = checkExpression(*statement.condition, env);
                    if (!isError(conditionType) && !isTruthy(conditionType)) {
                        addDiagnostic(result, statement.condition->location,
                                      "If condition must evaluate to a boolean-compatible expression");
                    }
                }
                TypeEnvironment thenEnv(&env);
                const bool thenReturns = checkBlock(statement.then_body, thenEnv, returnType, insideLoop);
                TypeEnvironment elseEnv(&env);
                const bool elseReturns = checkBlock(statement.else_body, elseEnv, returnType, insideLoop);
                if (!statement.else_body.empty() && thenReturns && elseReturns) {
                    statementReturns = true;
                }
                break;
            }
            case Statement::Kind::While: {
                if (statement.condition) {
                    const TypeInfo conditionType = checkExpression(*statement.condition, env);
                    if (!isError(conditionType) && !isTruthy(conditionType)) {
                        addDiagnostic(result, statement.condition->location,
                                      "While condition must evaluate to a boolean-compatible expression");
                    }
                }
                TypeEnvironment loopEnv(&env);
                (void)checkBlock(statement.then_body, loopEnv, returnType, /*insideLoop=*/true);
                break;
            }
            case Statement::Kind::For: {
                TypeEnvironment loopEnv(&env);
                if (statement.for_initializer) {
                    (void)checkStatement(*statement.for_initializer, loopEnv, returnType, /*insideLoop=*/false);
                }
                if (statement.condition) {
                    const TypeInfo conditionType = checkExpression(*statement.condition, loopEnv);
                    if (!isError(conditionType) && !isTruthy(conditionType)) {
                        addDiagnostic(result, statement.condition->location,
                                      "For-loop condition must evaluate to a boolean-compatible expression");
                    }
                }
                (void)checkBlock(statement.then_body, loopEnv, returnType, /*insideLoop=*/true);
                if (statement.for_increment) {
                    (void)checkStatement(*statement.for_increment, loopEnv, returnType, /*insideLoop=*/true);
                }
                break;
            }
            case Statement::Kind::Break:
                if (!insideLoop) {
                    addDiagnostic(result, statement.location, "'break' statement is only valid inside a loop");
                }
                break;
            case Statement::Kind::Continue:
                if (!insideLoop) {
                    addDiagnostic(result, statement.location, "'continue' statement is only valid inside a loop");
                }
                break;
            case Statement::Kind::ExprStmt:
                if (statement.expr) {
                    [[maybe_unused]] const TypeInfo exprType = checkExpression(*statement.expr, env);
                }
                break;
            case Statement::Kind::Assign: {
                // Check that target variable exists
                const TypeInfo targetType = resolveIdentifier(statement.assign_target);
                if (isError(targetType)) {
                    // Error already reported by resolveIdentifier
                    break;
                }
                // Check value expression
                if (statement.assign_value) {
                    [[maybe_unused]] const TypeInfo valueType = checkExpression(*statement.assign_value, env);
                    // Type compatibility for dynamic typing is relaxed
                }
                break;
            }
        }
        currentEnv = previousEnv;
        return statementReturns;
    }

private:
    [[nodiscard]] auto typeForLiteral(const Expression& expr) -> TypeInfo {
        switch (expr.literal_kind) {
            case Expression::LiteralKind::Boolean:
                return makePrimitive(TypeKind::Bool);
            case Expression::LiteralKind::String:
                return makePrimitive(TypeKind::String);
            case Expression::LiteralKind::Number: {
                const bool isFloatLiteral = expr.literal_value.find('.') != std::string::npos ||
                                            expr.literal_value.find('e') != std::string::npos ||
                                            expr.literal_value.find('E') != std::string::npos;
                if (isFloatLiteral) {
                    return makePrimitive(TypeKind::Float);
                }
                return makePrimitive(TypeKind::Int);
            }
        }
        return makeErrorType();
    }

    [[nodiscard]] auto resolveIdentifier(const Identifier& identifier) -> TypeInfo {
        if (currentEnv != nullptr) {
            if (const auto local = currentEnv->lookup(identifier.value)) {
                return *local;
            }
        }

        const auto globalIt = globalBindings.find(identifier.value);
        if (globalIt != globalBindings.end()) {
            return globalIt->second;
        }

        addDiagnostic(result, identifier.location, "Unknown identifier '" + identifier.value + "'");
        return makeErrorType();
    }

    [[nodiscard]] auto checkBinary(const Expression& expr, TypeEnvironment& env) -> TypeInfo {
        if (!expr.left || !expr.right) {
            return makeErrorType();
        }

        TypeEnvironment* previousEnv = currentEnv;
        currentEnv = &env;
        const TypeInfo leftType = checkExpression(*expr.left, env);
        const TypeInfo rightType = checkExpression(*expr.right, env);
        currentEnv = previousEnv;

        switch (expr.binary_operator) {
            case Expression::BinaryOperator::Add:
                if (leftType.kind == TypeKind::String && rightType.kind == TypeKind::String) {
                    return makePrimitive(TypeKind::String);
                }
                [[fallthrough]];
            case Expression::BinaryOperator::Subtract:
            case Expression::BinaryOperator::Multiply:
            case Expression::BinaryOperator::Divide:
            case Expression::BinaryOperator::Modulo: {
                if (!isNumeric(leftType) || !isNumeric(rightType)) {
                    addDiagnostic(result, expr.location,
                                  "Arithmetic operator requires numeric operands but got '" +
                                      typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return makeErrorType();
                }
                return numericResultType(leftType, rightType);
            }
            case Expression::BinaryOperator::Equal:
            case Expression::BinaryOperator::NotEqual: {
                if (!areEqualityComparable(leftType, rightType)) {
                    addDiagnostic(result, expr.location,
                                  "Cannot compare types '" + typeToString(leftType) + "' and '" +
                                      typeToString(rightType) + "'");
                }
                return makePrimitive(TypeKind::Int);
            }
            case Expression::BinaryOperator::Less:
            case Expression::BinaryOperator::LessEqual:
            case Expression::BinaryOperator::Greater:
            case Expression::BinaryOperator::GreaterEqual: {
                if (!areOrderedComparable(leftType, rightType)) {
                    addDiagnostic(result, expr.location,
                                  "Comparison requires numeric operands but got '" + typeToString(leftType) +
                                      "' and '" + typeToString(rightType) + "'");
                }
                return makePrimitive(TypeKind::Int);
            }
            case Expression::BinaryOperator::LogicalAnd:
            case Expression::BinaryOperator::LogicalOr: {
                if (!isTruthy(leftType) || !isTruthy(rightType)) {
                    addDiagnostic(result, expr.location,
                                  "Logical operator requires boolean-compatible operands but got '" +
                                      typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                }
                return makePrimitive(TypeKind::Int);
            }
        }
        return makeErrorType();
    }

    [[nodiscard]] auto checkUnary(const Expression& expr, TypeEnvironment& env) -> TypeInfo {
        if (!expr.operand) {
            return makeErrorType();
        }

        TypeEnvironment* previousEnv = currentEnv;
        currentEnv = &env;
        const TypeInfo operandType = checkExpression(*expr.operand, env);
        currentEnv = previousEnv;

        switch (expr.unary_operator) {
            case Expression::UnaryOperator::LogicalNot:
                if (!isTruthy(operandType)) {
                    addDiagnostic(result, expr.location,
                                  "Logical not expects a boolean-compatible operand but got '" +
                                      typeToString(operandType) + "'");
                }
                return makePrimitive(TypeKind::Int);
            case Expression::UnaryOperator::Negate:
                if (!isNumeric(operandType)) {
                    addDiagnostic(result, expr.location,
                                  "Unary minus expects a numeric operand but got '" + typeToString(operandType) + "'");
                    return makeErrorType();
                }
                return operandType.kind == TypeKind::Float ? makePrimitive(TypeKind::Float)
                                                            : makePrimitive(TypeKind::Int);
        }
        return makeErrorType();
    }

    [[nodiscard]] auto checkCall(const Expression& expr, TypeEnvironment& env) -> TypeInfo {
        if (const auto builtin = checkBuiltinCall(expr, env)) {
            return *builtin;
        }

        const auto it = functions.find(expr.callee);
        if (it == functions.end()) {
            addDiagnostic(result, expr.location, "Unknown function '" + expr.callee + "'");
            return makeErrorType();
        }

        const FunctionSignature& signature = it->second;
        if (expr.arguments.size() != signature.parameters.size()) {
            addDiagnostic(result, expr.location,
                          "Function '" + expr.callee + "' expects " +
                              std::to_string(signature.parameters.size()) +
                              " argument(s) but received " + std::to_string(expr.arguments.size()));
        }

        const size_t count = std::min(expr.arguments.size(), signature.parameters.size());
        for (size_t i = 0; i < count; ++i) {
            if (!expr.arguments[i]) {
                continue;
            }
            TypeEnvironment* previousEnv = currentEnv;
            currentEnv = &env;
            const TypeInfo argType = checkExpression(*expr.arguments[i], env);
            currentEnv = previousEnv;

            const TypeInfo& paramType = signature.parameters[i];
            if (!isAssignable(paramType, argType)) {
                addDiagnostic(result, expr.arguments[i]->location,
                              "Cannot convert argument of type '" + typeToString(argType) +
                                  "' to parameter of type '" + typeToString(paramType) + "'");
            }
        }

        if (isVoid(signature.return_type)) {
            return makePrimitive(TypeKind::Void);
        }
        return signature.return_type;
    }

    void checkLocalBinding(const BindingDecl& binding, TypeEnvironment& env) {
        const auto declaredOpt = types.resolve(binding.type_name, result, "binding declaration");
        TypeInfo declared = declaredOpt.value_or(makeErrorType());

        if (declared.kind == TypeKind::Void) {
            addDiagnostic(result, binding.type_name.location,
                          "Binding '" + binding.name.value + "' cannot have type 'void'");
            declared = makeErrorType();
        }

        if (!binding.initializer_expr) {
            addDiagnostic(result, binding.name.location,
                          "Binding '" + binding.name.value + "' requires an initializer");
            env.define(binding.name.value, declared);
            return;
        }

        TypeEnvironment* previousEnv = currentEnv;
        currentEnv = &env;
        const TypeInfo initializerType = checkExpression(*binding.initializer_expr, env);
        currentEnv = previousEnv;

        if (!isAssignable(declared, initializerType)) {
            addDiagnostic(result, binding.initializer_expr->location,
                          "Cannot assign expression of type '" + typeToString(initializerType) +
                              "' to binding '" + binding.name.value + "' of type '" + typeToString(declared) + "'");
        }

        env.define(binding.name.value, declared);
    }

    SemanticResult& result;
    const TypeRegistry& types;
    const std::unordered_map<std::string, FunctionSignature>& functions;
    std::unordered_map<std::string, TypeInfo>& globalBindings;
    TypeEnvironment* currentEnv = nullptr;

    [[nodiscard]] auto checkBuiltinCall(const Expression& expr, TypeEnvironment& env) -> std::optional<TypeInfo> {
        auto evaluateArgument = [&](std::size_t index) -> TypeInfo {
            if (index >= expr.arguments.size() || !expr.arguments[index]) {
                return makeErrorType();
            }
            TypeEnvironment* previousEnv = currentEnv;
            currentEnv = &env;
            const TypeInfo type = checkExpression(*expr.arguments[index], env);
            currentEnv = previousEnv;
            return type;
        };

        if (expr.callee == "print" || expr.callee == "println") {
            for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::Int);
        }

        if (expr.callee == "string_length") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin 'string_length' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo valueType = evaluateArgument(0);
                if (!isError(valueType) && valueType.kind != TypeKind::String && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "string_length expects a string but got '" + typeToString(valueType) + "'");
                }
            }
            for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::Int);
        }

        if (expr.callee == "string_equals") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'string_equals' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
                if (!expr.arguments[i]) {
                    continue;
                }
                const TypeInfo valueType = evaluateArgument(i);
                if (!isError(valueType) && valueType.kind != TypeKind::String && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[i]->location,
                                  "string_equals expects strings but got '" + typeToString(valueType) + "'");
                }
            }
            return makePrimitive(TypeKind::Int);
        }

        if (expr.callee == "string_concat") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'string_concat' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
                if (!expr.arguments[i]) {
                    continue;
                }
                const TypeInfo valueType = evaluateArgument(i);
                if (!isError(valueType) && valueType.kind != TypeKind::String && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[i]->location,
                                  "string_concat expects strings but got '" + typeToString(valueType) + "'");
                }
            }
            return makePrimitive(TypeKind::String);
        }

        if (expr.callee == "string_repeat") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'string_repeat' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo valueType = evaluateArgument(0);
                if (!isError(valueType) && valueType.kind != TypeKind::String && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "string_repeat expects a string but got '" + typeToString(valueType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                const TypeInfo countType = evaluateArgument(1);
                if (!isError(countType) && countType.kind != TypeKind::Int && countType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[1]->location,
                                  "string_repeat count must be an int but got '" + typeToString(countType) + "'");
                }
            }
            for (std::size_t i = 2; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::String);
        }

        if (expr.callee == "string_slice") {
            if (expr.arguments.size() != 3) {
                addDiagnostic(result, expr.location,
                              "Builtin 'string_slice' expects 3 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo valueType = evaluateArgument(0);
                if (!isError(valueType) && valueType.kind != TypeKind::String && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "string_slice expects a string but got '" + typeToString(valueType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                const TypeInfo startType = evaluateArgument(1);
                if (!isError(startType) && startType.kind != TypeKind::Int && startType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[1]->location,
                                  "string_slice start must be an int but got '" + typeToString(startType) + "'");
                }
            }
            if (expr.arguments.size() >= 3 && expr.arguments[2]) {
                const TypeInfo countType = evaluateArgument(2);
                if (!isError(countType) && countType.kind != TypeKind::Int && countType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[2]->location,
                                  "string_slice count must be an int but got '" + typeToString(countType) + "'");
                }
            }
            for (std::size_t i = 3; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::String);
        }

        if (expr.callee == "string_lower" || expr.callee == "string_upper" || expr.callee == "string_trim") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin '" + expr.callee + "' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo valueType = evaluateArgument(0);
                if (!isError(valueType) && valueType.kind != TypeKind::String && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  expr.callee + " expects a string but got '" + typeToString(valueType) + "'");
                }
            }
            for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::String);
        }

        if (expr.callee == "array") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo lengthType = evaluateArgument(0);
                if (!isError(lengthType) && !isNumeric(lengthType)) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array length must be numeric but got '" + typeToString(lengthType) + "'");
                }
            }
            for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makeArrayType();
        }

        if (expr.callee == "array_get") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_get' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array && arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_get expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                const TypeInfo indexType = evaluateArgument(1);
                if (!isError(indexType) && !isNumeric(indexType)) {
                    addDiagnostic(result, expr.arguments[1]->location,
                                  "array_get index must be numeric but got '" + typeToString(indexType) + "'");
                }
            }
            for (std::size_t i = 2; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return TypeInfo{TypeKind::Unknown, "array_element"};
        }

        if (expr.callee == "array_fill") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_fill' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array &&
                    arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_fill expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                (void)evaluateArgument(1);
            }
            for (std::size_t i = 2; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makeArrayType();
        }

        if (expr.callee == "array_push") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_push' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array && arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_push expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                (void)evaluateArgument(1);
            }
            for (std::size_t i = 2; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makeArrayType();
        }

        if (expr.callee == "array_pop") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_pop' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array && arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_pop expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return TypeInfo{TypeKind::Unknown, "array_element"};
        }

        if (expr.callee == "array_join") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_join' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array && arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_join expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                const TypeInfo sepType = evaluateArgument(1);
                if (!isError(sepType) && sepType.kind != TypeKind::String && sepType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[1]->location,
                                  "array_join separator must be a string but got '" + typeToString(sepType) + "'");
                }
            }
            for (std::size_t i = 2; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::String);
        }

        if (expr.callee == "array_sum") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_sum' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array &&
                    arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_sum expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::Int);
        }

        if (expr.callee == "array_set") {
            if (expr.arguments.size() != 3) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_set' expects 3 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            TypeInfo valueType = TypeInfo{TypeKind::Unknown, "array_element"};
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array && arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_set expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            if (expr.arguments.size() >= 2 && expr.arguments[1]) {
                const TypeInfo indexType = evaluateArgument(1);
                if (!isError(indexType) && !isNumeric(indexType)) {
                    addDiagnostic(result, expr.arguments[1]->location,
                                  "array_set index must be numeric but got '" + typeToString(indexType) + "'");
                }
            }
            if (expr.arguments.size() >= 3 && expr.arguments[2]) {
                valueType = evaluateArgument(2);
            }
            for (std::size_t i = 3; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return valueType;
        }

        if (expr.callee == "array_length") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin 'array_length' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo arrayType = evaluateArgument(0);
                if (!isError(arrayType) && arrayType.kind != TypeKind::Array && arrayType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "array_length expects an array but got '" + typeToString(arrayType) + "'");
                }
            }
            for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::Int);
        }

        if (expr.callee == "read_line") {
            if (!expr.arguments.empty()) {
                addDiagnostic(result, expr.location,
                              "Builtin 'read_line' expects 0 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
                if (expr.arguments[i]) {
                    (void)evaluateArgument(i);
                }
            }
            return makePrimitive(TypeKind::String);
        }

        // Math functions from std::math module
        if (expr.callee == "sqrt" || expr.callee == "std::math::sqrt") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin 'sqrt' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo valueType = evaluateArgument(0);
                if (!isError(valueType) && !isNumeric(valueType) && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  "sqrt expects a numeric argument but got '" + typeToString(valueType) + "'");
                }
            }
            return makePrimitive(TypeKind::Float);
        }

        if (expr.callee == "sin" || expr.callee == "cos" || expr.callee == "tan" ||
            expr.callee == "abs" || expr.callee == "floor" || expr.callee == "ceil" ||
            expr.callee == "round" || expr.callee == "exp" || expr.callee == "log" ||
            expr.callee == "log10" || expr.callee == "std::math::sin" || expr.callee == "std::math::cos" ||
            expr.callee == "std::math::tan" || expr.callee == "std::math::abs" ||
            expr.callee == "std::math::floor" || expr.callee == "std::math::ceil" ||
            expr.callee == "std::math::round" || expr.callee == "std::math::exp" ||
            expr.callee == "std::math::log" || expr.callee == "std::math::log10") {
            if (expr.arguments.size() != 1) {
                addDiagnostic(result, expr.location,
                              "Builtin '" + expr.callee + "' expects 1 argument but received " +
                                  std::to_string(expr.arguments.size()));
            }
            if (!expr.arguments.empty() && expr.arguments[0]) {
                const TypeInfo valueType = evaluateArgument(0);
                if (!isError(valueType) && !isNumeric(valueType) && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[0]->location,
                                  expr.callee + " expects a numeric argument but got '" + typeToString(valueType) + "'");
                }
            }
            return makePrimitive(TypeKind::Float);
        }

        if (expr.callee == "pow" || expr.callee == "std::math::pow") {
            if (expr.arguments.size() != 2) {
                addDiagnostic(result, expr.location,
                              "Builtin 'pow' expects 2 arguments but received " +
                                  std::to_string(expr.arguments.size()));
            }
            for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
                if (!expr.arguments[i]) {
                    continue;
                }
                const TypeInfo valueType = evaluateArgument(i);
                if (!isError(valueType) && !isNumeric(valueType) && valueType.kind != TypeKind::Unknown) {
                    addDiagnostic(result, expr.arguments[i]->location,
                                  "pow expects numeric arguments but got '" + typeToString(valueType) + "'");
                }
            }
            return makePrimitive(TypeKind::Float);
        }

        return std::nullopt;
    }
};

}  // namespace

auto analyzeModule(const Module& module) -> SemanticResult {
    SemanticResult result;

    NameMap importPaths;
    NameMap importAliases;
    for (const auto& importDecl : module.imports) {
        const std::string key = joinPath(importDecl.path);
        const SourceLocation location = importDecl.path.empty() ? SourceLocation{}
                                                                : importDecl.path.front().location;
        checkUniqueKey(result, importPaths, key, location, "import path");
        if (importDecl.alias.has_value()) {
            checkUnique(result, importAliases, *importDecl.alias, "import alias");
        }
    }

    TypeRegistry typeRegistry;

    NameMap bindingNames;
    NameMap functionNames;
    NameMap structNames;
    NameMap interfaceNames;

    std::vector<const BindingDecl*> globalBindings;
    std::vector<const FunctionDecl*> functionDecls;
    std::vector<const StructDecl*> structDecls;
    std::vector<const InterfaceDecl*> interfaceDecls;

    for (const auto& decl : module.declarations) {
        switch (decl.kind) {
            case Declaration::Kind::Binding:
                (void)checkUnique(result, bindingNames, decl.binding.name, "binding");
                if (decl.binding.kind == BindingKind::Const) {
                    checkConstInitializer(result, decl.binding);
                }
                globalBindings.push_back(&decl.binding);
                break;
            case Declaration::Kind::Function:
                (void)checkUnique(result, functionNames, decl.function.name, "function");
                functionDecls.push_back(&decl.function);
                break;
            case Declaration::Kind::Struct: {
                const bool inserted = checkUnique(result, structNames, decl.structure.name, "struct");
                if (inserted) {
                    if (const TypeInfo* conflict = typeRegistry.registerStruct(decl.structure)) {
                        addDiagnostic(result, decl.structure.name.location,
                                      "Type name '" + decl.structure.name.value + "' conflicts with existing " +
                                          describeTypeCategory(*conflict));
                    }
                }
                NameMap fieldNames;
                const std::string prefix = "field in struct " + decl.structure.name.value;
                for (const auto& field : decl.structure.fields) {
                    (void)checkUnique(result, fieldNames, field.name, prefix);
                }
                structDecls.push_back(&decl.structure);
                break;
            }
            case Declaration::Kind::Interface: {
                const bool inserted = checkUnique(result, interfaceNames, decl.interface_decl.name, "interface");
                if (inserted) {
                    if (const TypeInfo* conflict = typeRegistry.registerInterface(decl.interface_decl)) {
                        addDiagnostic(result, decl.interface_decl.name.location,
                                      "Type name '" + decl.interface_decl.name.value +
                                          "' conflicts with existing " + describeTypeCategory(*conflict));
                    }
                }
                NameMap methodNames;
                const std::string prefix = "method in interface " + decl.interface_decl.name.value;
                for (const auto& method : decl.interface_decl.methods) {
                    (void)checkUnique(result, methodNames, method.name, prefix);
                }
                interfaceDecls.push_back(&decl.interface_decl);
                break;
            }
        }
    }

    for (const auto* structure : structDecls) {
        std::vector<std::pair<std::string, TypeInfo>> resolvedFields;
        resolvedFields.reserve(structure->fields.size());
        for (const auto& field : structure->fields) {
            const auto fieldTypeOpt = typeRegistry.resolve(field.type_name, result, "struct field");
            TypeInfo fieldType = fieldTypeOpt.value_or(makeErrorType());
            if (fieldTypeOpt.has_value() && fieldType.kind == TypeKind::Void) {
                addDiagnostic(result, field.type_name.location,
                              "Field '" + field.name.value + "' cannot have type 'void'");
            }
            resolvedFields.emplace_back(field.name.value, std::move(fieldType));
        }
        typeRegistry.defineStructFields(structure->name.value, std::move(resolvedFields));
    }

    for (const auto* interfaceDecl : interfaceDecls) {
        for (const auto& method : interfaceDecl->methods) {
            for (const auto& param : method.parameters) {
                const auto paramType = typeRegistry.resolve(param.type_name, result, "interface method parameter");
                if (paramType.has_value() && paramType->kind == TypeKind::Void) {
                    addDiagnostic(result, param.type_name.location,
                                  "Interface parameter '" + param.name.value + "' cannot have type 'void'");
                }
            }
            if (method.return_type.has_value()) {
                const auto returnType = typeRegistry.resolve(*method.return_type, result, "interface method return type");
                if (returnType.has_value() && returnType->kind == TypeKind::Void) {
                    addDiagnostic(result, method.return_type->location,
                                  "Interface method cannot explicitly return 'void'");
                }
            }
        }
    }

    std::unordered_map<std::string, FunctionSignature> functionSignatures;
    functionSignatures.reserve(functionDecls.size());
    for (const auto* func : functionDecls) {
        FunctionSignature signature;
        signature.location = func->name.location;

        for (const auto& param : func->parameters) {
            const auto type = typeRegistry.resolve(param.type_name, result, "parameter type");
            signature.parameters.push_back(type.value_or(makeErrorType()));
            if (type.has_value() && type->kind == TypeKind::Void) {
                addDiagnostic(result, param.type_name.location,
                              "Parameter '" + param.name.value + "' cannot have type 'void'");
            }
        }

        if (func->return_type.has_value()) {
            const auto returnType = typeRegistry.resolve(*func->return_type, result, "function return type");
            signature.return_type = returnType.value_or(makeErrorType());
            if (returnType.has_value() && returnType->kind == TypeKind::Void) {
                signature.return_type = makePrimitive(TypeKind::Void);
            }
        } else {
            signature.return_type = makePrimitive(TypeKind::Void);
        }

        functionSignatures[func->name.value] = std::move(signature);
    }

    std::unordered_map<std::string, TypeInfo> globalBindingTypes;
    globalBindingTypes.reserve(globalBindings.size());

    TypeChecker checker(result, typeRegistry, functionSignatures, globalBindingTypes);

    for (const auto& decl : module.declarations) {
        switch (decl.kind) {
            case Declaration::Kind::Binding:
                checker.checkGlobalBinding(decl.binding);
                break;
            case Declaration::Kind::Function: {
                const auto it = functionSignatures.find(decl.function.name.value);
                if (it != functionSignatures.end()) {
                    checker.checkFunction(decl.function, it->second);
                }
                break;
            }
            case Declaration::Kind::Struct:
            case Declaration::Kind::Interface:
                break;
        }
    }

    return result;
}

}  // namespace impulse::frontend
