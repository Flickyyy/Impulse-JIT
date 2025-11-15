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
    const auto bindingForKeyword = [this](TokenKind keyword, BindingKind kind) -> std::optional<Declaration> {
        if (!match(keyword)) {
            return std::nullopt;
        }
        Declaration decl;
        decl.kind = Declaration::Kind::Binding;
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
        decl.function = parseFunctionDecl();
        return decl;
    }

    if (!check(TokenKind::EndOfFile)) {
        reportError(peek(), "Only let/const/var/func declarations are supported in this iteration");
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
    while (!check(TokenKind::Semicolon) && !check(TokenKind::EndOfFile)) {
        (void)advance();
    }
    const size_t exprEnd = current_;
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
    size_t bodyStart = current_;
    size_t bodyEnd = current_;

    if (lbrace != nullptr) {
        size_t depth = 1;
        while (!isAtEnd() && depth > 0) {
            const Token& token = peek();
            (void)advance();
            if (token.kind == TokenKind::LBrace) {
                ++depth;
            } else if (token.kind == TokenKind::RBrace) {
                --depth;
            }
        }

        if (depth == 0) {
            bodyEnd = (current_ > 0) ? current_ - 1 : current_;
        } else {
            reportError(previous(), "Unterminated function body");
            bodyEnd = current_;
        }
    }

    if (bodyEnd < bodyStart) {
        bodyEnd = bodyStart;
    }
    decl.body = makeSnippet(TokenRange{bodyStart, bodyEnd});
    return decl;
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
