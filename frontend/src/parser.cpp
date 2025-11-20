#include "../include/impulse/frontend/parser.h"

#include <sstream>
#include <string>
#include <utility>

namespace impulse::frontend {

Parser::Parser(std::string source) : source_(std::move(source)) {
    Lexer lexer(source_);
    tokens_ = lexer.tokenize();
}

auto Parser::parseModule() -> ParseResult {
    ParseResult result{};

    for (const auto& token : tokens_) {
        if (token.kind == TokenKind::Error) {
            reportError(token, token.lexeme.empty() ? "Lexer error" : token.lexeme);
        }
    }

    if (tokens_.empty()) {
        reportError(Token{TokenKind::EndOfFile, "", 0, 0}, "No tokens produced");
    }

    Module module;
    module.decl = parseModuleDecl();
    module.imports = parseImportList();

    while (!isAtEnd()) {
        if (check(TokenKind::EndOfFile)) {
            break;
        }

        if (auto decl = parseDeclaration()) {
            module.declarations.push_back(std::move(*decl));
        } else {
            break;
        }
    }

    result.module = std::move(module);
    result.diagnostics = diagnostics_;
    result.success = diagnostics_.empty();
    return result;
}

auto Parser::advance() -> const Token& {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

auto Parser::peek() const -> const Token& { return tokens_[current_]; }

auto Parser::previous() const -> const Token& { return tokens_[current_ - 1]; }

auto Parser::isAtEnd() const -> bool { return peek().kind == TokenKind::EndOfFile; }

auto Parser::check(TokenKind kind) const -> bool {
    if (isAtEnd()) {
        return kind == TokenKind::EndOfFile;
    }
    return peek().kind == kind;
}

auto Parser::match(TokenKind kind) -> bool {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

auto Parser::consume(TokenKind kind, const char* message) -> const Token* {
    if (check(kind)) {
        return &advance();
    }
    reportError(peek(), message);
    return nullptr;
}

auto Parser::parseModuleDecl() -> ModuleDecl {
    ModuleDecl decl;
    if (consume(TokenKind::KwModule, "Expected 'module' declaration") == nullptr) {
        return decl;
    }

    decl.path = parsePath("module path");
    const Token* terminator = consume(TokenKind::Semicolon, "Expected ';' after module declaration");
    (void)terminator;
    return decl;
}

auto Parser::parseImportList() -> std::vector<ImportDecl> {
    std::vector<ImportDecl> imports;
    while (match(TokenKind::KwImport)) {
        ImportDecl decl;
        decl.path = parsePath("import path");
        if (match(TokenKind::KwAs)) {
            const Token* alias = consume(TokenKind::Identifier, "Expected identifier after 'as'");
            if (alias != nullptr) {
                decl.alias = makeIdentifier(*alias);
            }
        }
        const Token* terminator = consume(TokenKind::Semicolon, "Expected ';' after import");
        (void)terminator;
        imports.push_back(std::move(decl));
    }
    return imports;
}

auto Parser::parseDeclaration() -> std::optional<Declaration> {
    const bool exported = match(TokenKind::KwExport);

    const auto bindingForKeyword = [this, exported](TokenKind keyword, BindingKind kind) -> std::optional<Declaration> {
        if (!match(keyword)) {
            return std::nullopt;
        }
        Declaration decl;
        decl.kind = Declaration::Kind::Binding;
        decl.exported = exported;
        decl.binding = parseBindingDecl(kind);
        return decl;
    };

    if (auto decl = bindingForKeyword(TokenKind::KwLet, BindingKind::Let)) {
        return decl;
    }
    if (auto decl = bindingForKeyword(TokenKind::KwConst, BindingKind::Const)) {
        return decl;
    }
    if (auto decl = bindingForKeyword(TokenKind::KwVar, BindingKind::Var)) {
        return decl;
    }

    if (match(TokenKind::KwFunc)) {
        Declaration decl;
        decl.kind = Declaration::Kind::Function;
        decl.exported = exported;
        decl.function = parseFunctionDecl();
        return decl;
    }

    if (match(TokenKind::KwStruct)) {
        Declaration decl;
        decl.kind = Declaration::Kind::Struct;
        decl.exported = exported;
        decl.structure = parseStructDecl();
        return decl;
    }

    if (match(TokenKind::KwInterface)) {
        Declaration decl;
        decl.kind = Declaration::Kind::Interface;
        decl.exported = exported;
        decl.interface_decl = parseInterfaceDecl();
        return decl;
    }

    if (!check(TokenKind::EndOfFile)) {
        if (exported) {
            reportError(previous(), "Expected declaration after 'export'");
        }
        reportError(peek(), "Only let/const/var/func/struct/interface declarations are supported in this iteration");
    }
    return std::nullopt;
}

auto Parser::parseBindingDecl(BindingKind kind) -> BindingDecl {
    const auto keywordLiteral = [](BindingKind value) -> const char* {
        switch (value) {
            case BindingKind::Let:
                return "let";
            case BindingKind::Const:
                return "const";
            case BindingKind::Var:
                return "var";
        }
        return "binding";
    };

    const char* keyword = keywordLiteral(kind);
    const std::string nameMessage = std::string{"Expected identifier after '"} + keyword + "'";
    const std::string colonMessage = std::string{"Expected ':' after "} + keyword + " binding name";
    const char* typeMessage = "Expected type name after ':'";
    const char* equalMessage = "Expected '=' before initializer";
    const char* semicolonMessage = "Expected ';' after initializer";

    const Token* name = consume(TokenKind::Identifier, nameMessage.c_str());
    const Token* colon = consume(TokenKind::Colon, colonMessage.c_str());
    (void)colon;
    const Token* typeName = consume(TokenKind::Identifier, typeMessage);
    const Token* equal = consume(TokenKind::Equal, equalMessage);
    (void)equal;

    const size_t exprStart = current_;
    auto expression = parseExpression();
    const size_t exprEnd = current_;
    if (!expression) {
        while (!check(TokenKind::Semicolon) && !check(TokenKind::EndOfFile)) {
            (void)advance();
        }
    }
    const Token* terminator = consume(TokenKind::Semicolon, semicolonMessage);
    (void)terminator;

    BindingDecl decl;
    decl.kind = kind;
    if (name != nullptr) {
        decl.name = makeIdentifier(*name);
    }
    if (typeName != nullptr) {
        decl.type_name = makeIdentifier(*typeName);
    }
    decl.initializer = makeSnippet(TokenRange{exprStart, exprEnd});
    decl.initializer_expr = std::move(expression);
    return decl;
}

auto Parser::parseFunctionDecl() -> FunctionDecl {
    FunctionDecl decl;

    const Token* name = consume(TokenKind::Identifier, "Expected identifier after 'func'");
    if (name != nullptr) {
        decl.name = makeIdentifier(*name);
    }

    const Token* lparen = consume(TokenKind::LParen, "Expected '(' after function name");
    (void)lparen;
    decl.parameters = parseParameterList();
    const Token* rparen = consume(TokenKind::RParen, "Expected ')' to close parameter list");
    (void)rparen;

    if (match(TokenKind::Arrow)) {
        const Token* returnType = consume(TokenKind::Identifier, "Expected return type after '->'");
        if (returnType != nullptr) {
            decl.return_type = makeIdentifier(*returnType);
        }
    }

    const Token* lbrace = consume(TokenKind::LBrace, "Expected '{' to start function body");
    const size_t bodyStart = current_;

    if (lbrace != nullptr) {
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (auto statement = parseStatement()) {
                decl.parsed_body.statements.push_back(std::move(*statement));
            } else {
                // simple synchronization: advance until next semicolon or closing brace
                while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
                    (void)advance();
                }
                if (check(TokenKind::Semicolon)) {
                    advance();
                }
            }
        }
    }

    const Token* rbrace = consume(TokenKind::RBrace, "Expected '}' after function body");
    size_t bodyEnd = (rbrace != nullptr && current_ > 0) ? current_ - 1 : current_;
    if (bodyEnd < bodyStart) {
        bodyEnd = bodyStart;
    }
    decl.body = makeSnippet(TokenRange{bodyStart, bodyEnd});
    return decl;
}

auto Parser::parseStatement() -> std::optional<Statement> {
    if (match(TokenKind::KwReturn)) {
        Statement statement;
        statement.kind = Statement::Kind::Return;
        const Token& keyword = previous();
        statement.location = SourceLocation{keyword.line, keyword.column};

        auto expression = parseExpression();
        if (!expression) {
            reportError(peek(), "Expected expression after 'return'");
            return std::nullopt;
        }
        statement.return_expression = std::move(expression);

        const Token* terminator = consume(TokenKind::Semicolon, "Expected ';' after return statement");
        (void)terminator;
        return statement;
    }

    const auto bindingStatement = [this](TokenKind keyword, BindingKind kind) -> std::optional<Statement> {
        if (!match(keyword)) {
            return std::nullopt;
        }
        const Token keywordToken = previous();
        Statement statement;
        statement.kind = Statement::Kind::Binding;
        statement.location = SourceLocation{keywordToken.line, keywordToken.column};
        statement.binding = parseBindingDecl(kind);
        return statement;
    };

    if (auto stmt = bindingStatement(TokenKind::KwLet, BindingKind::Let)) {
        return stmt;
    }
    if (auto stmt = bindingStatement(TokenKind::KwConst, BindingKind::Const)) {
        return stmt;
    }
    if (auto stmt = bindingStatement(TokenKind::KwVar, BindingKind::Var)) {
        return stmt;
    }

    reportError(peek(), "Only return/let/const/var statements are supported in function bodies");
    return std::nullopt;
}

auto Parser::parseStructDecl() -> StructDecl {
    StructDecl decl;

    const Token* name = consume(TokenKind::Identifier, "Expected identifier after 'struct'");
    if (name != nullptr) {
        decl.name = makeIdentifier(*name);
    }

    const Token* lbrace = consume(TokenKind::LBrace, "Expected '{' to start struct body");
    (void)lbrace;

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        if (auto field = parseFieldDecl()) {
            decl.fields.push_back(std::move(*field));
        }
    }

    const Token* rbrace = consume(TokenKind::RBrace, "Expected '}' after struct body");
    (void)rbrace;
    return decl;
}

auto Parser::parseInterfaceDecl() -> InterfaceDecl {
    InterfaceDecl decl;

    const Token* name = consume(TokenKind::Identifier, "Expected identifier after 'interface'");
    if (name != nullptr) {
        decl.name = makeIdentifier(*name);
    }

    const Token* lbrace = consume(TokenKind::LBrace, "Expected '{' to start interface body");
    (void)lbrace;

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        if (auto method = parseInterfaceMethod()) {
            decl.methods.push_back(std::move(*method));
        }
    }

    const Token* rbrace = consume(TokenKind::RBrace, "Expected '}' after interface body");
    (void)rbrace;
    return decl;
}

auto Parser::parseInterfaceMethod() -> std::optional<InterfaceMethod> {
    InterfaceMethod method;

    (void)match(TokenKind::KwFunc);
    const Token* name = consume(TokenKind::Identifier, "Expected interface method name");
    const Token* lparen = consume(TokenKind::LParen, "Expected '(' after method name");
    (void)lparen;
    method.parameters = parseParameterList();
    const Token* rparen = consume(TokenKind::RParen, "Expected ')' to close method parameters");
    (void)rparen;

    if (match(TokenKind::Arrow)) {
        const Token* returnType = consume(TokenKind::Identifier, "Expected return type after '->'");
        if (returnType != nullptr) {
            method.return_type = makeIdentifier(*returnType);
        }
    }

    const Token* terminator = consume(TokenKind::Semicolon, "Expected ';' after interface method signature");
    if (terminator == nullptr) {
        while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            (void)advance();
        }
        if (check(TokenKind::Semicolon)) {
            advance();
        }
    }

    if (name != nullptr) {
        method.name = makeIdentifier(*name);
    }

    if (name == nullptr) {
        return std::nullopt;
    }
    return method;
}

auto Parser::parseFieldDecl() -> std::optional<FieldDecl> {
    const size_t fieldStart = current_;
    FieldDecl field;

    const Token* name = consume(TokenKind::Identifier, "Expected field name");
    const Token* colon = consume(TokenKind::Colon, "Expected ':' after field name");
    (void)colon;
    const Token* typeName = consume(TokenKind::Identifier, "Expected field type");

    const Token* terminator = consume(TokenKind::Semicolon, "Expected ';' after field declaration");
    if (terminator == nullptr) {
        while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            (void)advance();
        }
        if (check(TokenKind::Semicolon)) {
            advance();
        }
    }

    if (name != nullptr) {
        field.name = makeIdentifier(*name);
    }
    if (typeName != nullptr) {
        field.type_name = makeIdentifier(*typeName);
    }

    if (current_ == fieldStart) {
        advance();
    }

    if (name == nullptr || typeName == nullptr) {
        return std::nullopt;
    }
    return field;
}

auto Parser::parseParameterList() -> std::vector<Parameter> {
    std::vector<Parameter> parameters;
    if (check(TokenKind::RParen)) {
        return parameters;
    }

    while (true) {
        Parameter param;
        const Token* name = consume(TokenKind::Identifier, "Expected parameter name");
        const Token* colon = consume(TokenKind::Colon, "Expected ':' after parameter name");
        (void)colon;
        const Token* typeName = consume(TokenKind::Identifier, "Expected parameter type");

        if (name != nullptr) {
            param.name = makeIdentifier(*name);
        }
        if (typeName != nullptr) {
            param.type_name = makeIdentifier(*typeName);
        }
        parameters.push_back(std::move(param));

        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RParen)) {
            break;
        }
    }

    return parameters;
}

auto Parser::parsePath(const char* context) -> std::vector<Identifier> {
    std::vector<Identifier> path;

    const Token* first = consume(TokenKind::Identifier, "Expected identifier in path");
    if (first != nullptr) {
        path.push_back(makeIdentifier(*first));
    }

    while (match(TokenKind::DoubleColon)) {
        const Token* segment = consume(TokenKind::Identifier, "Expected identifier after '::'");
        if (segment != nullptr) {
            path.push_back(makeIdentifier(*segment));
        }
    }

    if (path.empty()) {
        const Token& token = (current_ > 0) ? previous() : peek();
        reportError(token, context);
    }

    return path;
}

auto Parser::parseExpression() -> std::unique_ptr<Expression> { return parseBinaryExpression(0); }

auto Parser::parseBinaryExpression(int minPrecedence) -> std::unique_ptr<Expression> {
    auto left = parseUnaryExpression();
    if (left == nullptr) {
        return nullptr;
    }

    while (true) {
        const int precedence = binaryPrecedence(peek().kind);
        if (precedence < minPrecedence) {
            break;
        }

        const Token op = advance();
        auto right = parseBinaryExpression(precedence + 1);
        if (right == nullptr) {
            return left;
        }

        auto binary = std::make_unique<Expression>();
        binary->kind = Expression::Kind::Binary;
        binary->location = SourceLocation{op.line, op.column};
        binary->binary_operator = toBinaryOperator(op.kind);
        binary->left = std::move(left);
        binary->right = std::move(right);
        left = std::move(binary);
    }

    return left;
}

auto Parser::parseUnaryExpression() -> std::unique_ptr<Expression> {
    if (match(TokenKind::Bang)) {
        const Token op = previous();
        auto operand = parseUnaryExpression();
        if (operand == nullptr) {
            return nullptr;
        }
        auto unary = std::make_unique<Expression>();
        unary->kind = Expression::Kind::Unary;
        unary->location = SourceLocation{op.line, op.column};
        unary->unary_operator = Expression::UnaryOperator::LogicalNot;
        unary->operand = std::move(operand);
        return unary;
    }

    if (match(TokenKind::Minus)) {
        const Token op = previous();
        auto operand = parseUnaryExpression();
        if (operand == nullptr) {
            return nullptr;
        }
        auto unary = std::make_unique<Expression>();
        unary->kind = Expression::Kind::Unary;
        unary->location = SourceLocation{op.line, op.column};
        unary->unary_operator = Expression::UnaryOperator::Negate;
        unary->operand = std::move(operand);
        return unary;
    }

    return parsePrimaryExpression();
}

auto Parser::parsePrimaryExpression() -> std::unique_ptr<Expression> {
    if (match(TokenKind::IntegerLiteral) || match(TokenKind::FloatLiteral) || match(TokenKind::BooleanLiteral)) {
        const Token literal = previous();
        auto expr = std::make_unique<Expression>();
        expr->kind = Expression::Kind::Literal;
        expr->location = SourceLocation{literal.line, literal.column};
        expr->literal_value = literal.lexeme;
        return expr;
    }

    if (match(TokenKind::Identifier)) {
        const Token ident = previous();
        auto expr = std::make_unique<Expression>();
        expr->kind = Expression::Kind::Identifier;
        expr->location = SourceLocation{ident.line, ident.column};
        expr->identifier = makeIdentifier(ident);
        return expr;
    }

    if (match(TokenKind::LParen)) {
        const Token lparen = previous();
        auto expr = parseExpression();
        const Token* rparen = consume(TokenKind::RParen, "Expected ')' after expression");
        if (expr != nullptr && rparen != nullptr) {
            expr->location = SourceLocation{lparen.line, lparen.column};
        }
        return expr;
    }

    reportError(peek(), "Expected expression");
    if (!isAtEnd()) {
        advance();
    }
    return nullptr;
}

auto Parser::binaryPrecedence(TokenKind kind) -> int {
    switch (kind) {
        case TokenKind::PipePipe:
            return 5;
        case TokenKind::AmpersandAmpersand:
            return 8;
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
            return 10;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return 15;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 20;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return 30;
        default:
            return -1;
    }
}

auto Parser::toBinaryOperator(TokenKind kind) -> Expression::BinaryOperator {
    switch (kind) {
        case TokenKind::Plus:
            return Expression::BinaryOperator::Add;
        case TokenKind::Minus:
            return Expression::BinaryOperator::Subtract;
        case TokenKind::Star:
            return Expression::BinaryOperator::Multiply;
        case TokenKind::Slash:
            return Expression::BinaryOperator::Divide;
        case TokenKind::Percent:
            return Expression::BinaryOperator::Modulo;
        case TokenKind::EqualEqual:
            return Expression::BinaryOperator::Equal;
        case TokenKind::BangEqual:
            return Expression::BinaryOperator::NotEqual;
        case TokenKind::Less:
            return Expression::BinaryOperator::Less;
        case TokenKind::LessEqual:
            return Expression::BinaryOperator::LessEqual;
        case TokenKind::Greater:
            return Expression::BinaryOperator::Greater;
        case TokenKind::GreaterEqual:
            return Expression::BinaryOperator::GreaterEqual;
        case TokenKind::AmpersandAmpersand:
            return Expression::BinaryOperator::LogicalAnd;
        case TokenKind::PipePipe:
            return Expression::BinaryOperator::LogicalOr;
        default:
            return Expression::BinaryOperator::Add;
    }
}

void Parser::reportError(const Token& token, std::string message) {
    diagnostics_.push_back(Diagnostic{
        .location = SourceLocation{token.line, token.column},
        .message = std::move(message),
    });
}

auto Parser::makeIdentifier(const Token& token) const -> Identifier {
    return Identifier{
        .value = token.lexeme,
        .location = SourceLocation{token.line, token.column},
    };
}

auto Parser::makeSnippet(TokenRange range) const -> Snippet {
    Snippet stub;
    stub.location = (range.start < tokens_.size())
                        ? SourceLocation{tokens_[range.start].line, tokens_[range.start].column}
                        : SourceLocation{};

    std::ostringstream builder;
    for (size_t i = range.start; i < range.end && i < tokens_.size(); ++i) {
        if (i != range.start) {
            builder << ' ';
        }
        builder << tokens_[i].lexeme;
    }
    stub.text = builder.str();
    return stub;
}

}  // namespace impulse::frontend
