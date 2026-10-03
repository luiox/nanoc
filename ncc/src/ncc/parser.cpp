#include "ncc/parser.hpp"
#include <sstream>
#include <stdexcept>

Parser::Parser(const std::vector<Token>& tokens) : m_tokens(tokens), m_pos(0) {}

Token Parser::currentToken() const {
    if (m_pos >= m_tokens.size()) {
        return Token(NTokenKind::TOKEN_EOF, "", 0, 0);
    }
    return m_tokens[m_pos];
}

Token Parser::peekToken() const {
    if (m_pos + 1 >= m_tokens.size()) {
        return Token(NTokenKind::TOKEN_EOF, "", 0, 0);
    }
    return m_tokens[m_pos + 1];
}

void Parser::advance() {
    if (m_pos < m_tokens.size()) {
        m_pos++;
    }
}

bool Parser::match(NTokenKind kind) {
    if (currentToken().kind == kind) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(NTokenKind kind) {
    if (currentToken().kind == kind) {
        advance();
        return true;
    }
    error("Expected token kind " + std::to_string(static_cast<int>(kind)) + " but got "
          + std::to_string(static_cast<int>(currentToken().kind)));
    return false;
}

void Parser::error(const std::string& message) {
    Token token = currentToken();
    std::ostringstream oss;
    oss << "Parse error at line " << token.line << ", column " << token.column << ": "
        << message;
    throw std::runtime_error(oss.str());
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>(currentToken().line, currentToken().column);

    while (currentToken().kind != NTokenKind::TOKEN_EOF) {
        auto decl = parseDeclaration();
        if (decl) {
            program->declarations.push_back(std::move(decl));
        }
    }

    return program;
}

std::unique_ptr<Decl> Parser::parseDeclaration() {
    // 检查是否是类型关键字
    if (currentToken().kind == NTokenKind::KEYWORD_INT
        || currentToken().kind == NTokenKind::KEYWORD_CHAR
        || currentToken().kind == NTokenKind::KEYWORD_VOID) {

        // 保存当前位置
        size_t startPos = m_pos;

        // 获取类型
        std::string type = currentToken().value;
        advance();

        // 指针层级：int* p / int* f()（多级由语义分析显式报不支持）
        while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
            advance();
        }

        // 获取名称
        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected identifier after type");
        }
        std::string name = currentToken().value;
        advance();

        // 检查是函数声明还是变量声明
        if (currentToken().kind == NTokenKind::DELIMITER_LPAREN) {
            // 函数声明
            m_pos = startPos; // 回退
            return parseFuncDeclaration();
        } else {
            // 变量声明
            m_pos = startPos; // 回退
            return parseVarDeclaration();
        }
    }

    error("Expected declaration");
    return nullptr;
}

std::unique_ptr<VarDeclaration> Parser::parseVarDeclaration() {
    // 获取类型
    if (currentToken().kind != NTokenKind::KEYWORD_INT
        && currentToken().kind != NTokenKind::KEYWORD_CHAR
        && currentToken().kind != NTokenKind::KEYWORD_VOID) {
        error("Expected type keyword");
    }

    std::string type = currentToken().value;
    int line = currentToken().line;
    int column = currentToken().column;
    advance();

    // 指针层级
    int pointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++pointerDepth;
        advance();
    }

    // 获取名称
    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected identifier");
    }

    std::string name = currentToken().value;
    advance();

    auto varDecl = std::make_unique<VarDeclaration>(type, name, line, column);
    varDecl->pointerDepth = pointerDepth;

    // 数组后缀：int a[10]；多维在此一并解析（arrayDims≥2 由语义分析报不支持）
    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        varDecl->isArray = true;
        varDecl->arrayDims = 1;
        advance();
        if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
            error("Expected integer constant array size");
        }
        varDecl->arraySize = std::stoi(currentToken().value);
        advance();
        expect(NTokenKind::DELIMITER_RBRACKET);
        while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            ++varDecl->arrayDims;
            advance();
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                error("Expected integer constant array size");
            }
            advance();
            expect(NTokenKind::DELIMITER_RBRACKET);
        }
    }

    // 检查是否有初始化
    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseExpression();
    }

    // 期望分号
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return varDecl;
}

std::unique_ptr<Stmt> Parser::parseVarDeclarationStmt() {
    // 获取类型
    if (currentToken().kind != NTokenKind::KEYWORD_INT
        && currentToken().kind != NTokenKind::KEYWORD_CHAR
        && currentToken().kind != NTokenKind::KEYWORD_VOID) {
        error("Expected type keyword");
    }

    std::string type = currentToken().value;
    int line = currentToken().line;
    int column = currentToken().column;
    advance();

    // 指针层级
    int pointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++pointerDepth;
        advance();
    }

    // 获取名称
    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected identifier");
    }

    std::string name = currentToken().value;
    advance();

    auto varDecl = std::make_unique<StmtVarDeclaration>(type, name, line, column);
    varDecl->pointerDepth = pointerDepth;

    // 数组后缀：与 parseVarDeclaration 相同的多维解析策略
    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        varDecl->isArray = true;
        varDecl->arrayDims = 1;
        advance();
        if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
            error("Expected integer constant array size");
        }
        varDecl->arraySize = std::stoi(currentToken().value);
        advance();
        expect(NTokenKind::DELIMITER_RBRACKET);
        while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            ++varDecl->arrayDims;
            advance();
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                error("Expected integer constant array size");
            }
            advance();
            expect(NTokenKind::DELIMITER_RBRACKET);
        }
    }

    // 检查是否有初始化
    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseExpression();
    }

    // 期望分号
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return varDecl;
}

std::unique_ptr<FuncDeclaration> Parser::parseFuncDeclaration() {
    // 获取返回类型
    if (currentToken().kind != NTokenKind::KEYWORD_INT
        && currentToken().kind != NTokenKind::KEYWORD_CHAR
        && currentToken().kind != NTokenKind::KEYWORD_VOID) {
        error("Expected return type");
    }

    std::string returnType = currentToken().value;
    int line = currentToken().line;
    int column = currentToken().column;
    advance();

    // 返回类型指针层级：int* f()
    int returnPointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++returnPointerDepth;
        advance();
    }

    // 获取函数名
    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected function name");
    }

    std::string name = currentToken().value;
    advance();

    auto funcDecl = std::make_unique<FuncDeclaration>(returnType, name, line, column);
    funcDecl->returnPointerDepth = returnPointerDepth;

    // 解析参数列表
    expect(NTokenKind::DELIMITER_LPAREN);

    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        do {
            // 解析参数
            if (currentToken().kind != NTokenKind::KEYWORD_INT
                && currentToken().kind != NTokenKind::KEYWORD_CHAR
                && currentToken().kind != NTokenKind::KEYWORD_VOID) {
                error("Expected parameter type");
            }

            std::string paramType = currentToken().value;
            advance();

            // 参数指针层级：int f(int* a, char* s)
            int paramPointerDepth = 0;
            while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
                ++paramPointerDepth;
                advance();
            }

            if (currentToken().kind != NTokenKind::IDENTIFIER) {
                error("Expected parameter name");
            }

            std::string paramName = currentToken().value;
            advance();

            // 数组形参不支持：数组实参传给指针形参即完成退化（PRD R1.2）
            if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                error("array parameters are not supported; declare the parameter as a "
                      "pointer");
            }

            auto param =
              std::make_unique<VarDeclaration>(paramType, paramName, line, column);
            param->pointerDepth = paramPointerDepth;
            funcDecl->parameters.push_back(std::move(param));

        } while (match(NTokenKind::DELIMITER_COMMA));
    }

    expect(NTokenKind::DELIMITER_RPAREN);

    // 解析函数体
    funcDecl->body = parseCompoundStatement();

    return funcDecl;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    switch (currentToken().kind) {
    case NTokenKind::DELIMITER_LBRACE:
        return parseCompoundStatement();
    case NTokenKind::KEYWORD_IF:
        return parseIfStatement();
    case NTokenKind::KEYWORD_WHILE:
        return parseWhileStatement();
    case NTokenKind::KEYWORD_FOR:
        return parseForStatement();
    case NTokenKind::KEYWORD_RETURN:
        return parseReturnStatement();
    case NTokenKind::KEYWORD_BREAK:
        return parseBreakStatement();
    case NTokenKind::KEYWORD_CONTINUE:
        return parseContinueStatement();
    case NTokenKind::KEYWORD_INT:
    case NTokenKind::KEYWORD_CHAR:
    case NTokenKind::KEYWORD_VOID:
        // 变量声明作为语句处理
        return parseVarDeclarationStmt();
    default:
        return parseExprStatement();
    }
}

std::unique_ptr<CompoundStmt> Parser::parseCompoundStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::DELIMITER_LBRACE);

    auto compound = std::make_unique<CompoundStmt>(line, column);

    while (currentToken().kind != NTokenKind::DELIMITER_RBRACE
           && currentToken().kind != NTokenKind::TOKEN_EOF) {
        auto stmt = parseStatement();
        if (stmt) {
            compound->statements.push_back(std::move(stmt));
        }
    }

    expect(NTokenKind::DELIMITER_RBRACE);

    return compound;
}

std::unique_ptr<IfStmt> Parser::parseIfStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_IF);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto ifStmt = std::make_unique<IfStmt>(line, column);
    ifStmt->condition = parseExpression();

    expect(NTokenKind::DELIMITER_RPAREN);

    ifStmt->thenBranch = parseStatement();

    if (currentToken().kind == NTokenKind::KEYWORD_ELSE) {
        advance();
        ifStmt->elseBranch = parseStatement();
    }

    return ifStmt;
}

std::unique_ptr<WhileStmt> Parser::parseWhileStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_WHILE);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto whileStmt = std::make_unique<WhileStmt>(line, column);
    whileStmt->condition = parseExpression();

    expect(NTokenKind::DELIMITER_RPAREN);

    whileStmt->body = parseStatement();

    return whileStmt;
}

std::unique_ptr<ForStmt> Parser::parseForStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_FOR);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto forStmt = std::make_unique<ForStmt>(line, column);

    // 初始化部分
    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        forStmt->init = parseStatement();
    } else {
        advance();
    }

    // 条件部分
    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        forStmt->condition = parseExpression();
    }
    expect(NTokenKind::DELIMITER_SEMICOLON);

    // 增量部分
    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        forStmt->increment = parseExpression();
    }
    expect(NTokenKind::DELIMITER_RPAREN);

    forStmt->body = parseStatement();

    return forStmt;
}

std::unique_ptr<ReturnStmt> Parser::parseReturnStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_RETURN);

    auto returnStmt = std::make_unique<ReturnStmt>(line, column);

    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        returnStmt->value = parseExpression();
    }

    expect(NTokenKind::DELIMITER_SEMICOLON);

    return returnStmt;
}

std::unique_ptr<BreakStmt> Parser::parseBreakStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_BREAK);
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return std::make_unique<BreakStmt>(line, column);
}

std::unique_ptr<ContinueStmt> Parser::parseContinueStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_CONTINUE);
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return std::make_unique<ContinueStmt>(line, column);
}

std::unique_ptr<ExprStmt> Parser::parseExprStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    auto exprStmt = std::make_unique<ExprStmt>(line, column);
    exprStmt->expression = parseExpression();

    expect(NTokenKind::DELIMITER_SEMICOLON);

    return exprStmt;
}

std::unique_ptr<Expr> Parser::parseExpression() { return parseAssignment(); }

std::unique_ptr<Expr> Parser::parseAssignment() {
    auto expr = parseLogicalOr();

    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        // 左值形式：标识符（x = v）、下标（a[i] = v）、解引用（*p = v）
        const bool validTarget = expr->type == ASTNodeType::IDENTIFIER_EXPR
                                 || expr->type == ASTNodeType::INDEX_EXPR
                                 || (expr->type == ASTNodeType::UNARY_EXPR
                                     && static_cast<const UnaryExpr&>(*expr).op == "*");
        if (!validTarget) {
            error("Invalid assignment target");
        }

        int line = expr->line;
        int column = expr->column;
        auto assignExpr = std::make_unique<AssignExpr>(std::move(expr), line, column);

        advance(); // 跳过 '='
        assignExpr->value = parseAssignment();

        return assignExpr;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (currentToken().kind == NTokenKind::OPERATOR_LOGICAL_OR) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseLogicalAnd();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto expr = parseEquality();

    while (currentToken().kind == NTokenKind::OPERATOR_LOGICAL_AND) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseEquality();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto expr = parseRelational();

    while (currentToken().kind == NTokenKind::OPERATOR_EQUAL
           || currentToken().kind == NTokenKind::OPERATOR_NOT_EQUAL) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseRelational();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseRelational() {
    auto expr = parseAdditive();

    while (currentToken().kind == NTokenKind::OPERATOR_LESS
           || currentToken().kind == NTokenKind::OPERATOR_LESS_EQUAL
           || currentToken().kind == NTokenKind::OPERATOR_GREATER
           || currentToken().kind == NTokenKind::OPERATOR_GREATER_EQUAL) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseAdditive();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    auto expr = parseMultiplicative();

    while (currentToken().kind == NTokenKind::OPERATOR_PLUS
           || currentToken().kind == NTokenKind::OPERATOR_MINUS) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseMultiplicative();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    auto expr = parseUnary();

    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY
           || currentToken().kind == NTokenKind::OPERATOR_DIVIDE
           || currentToken().kind == NTokenKind::OPERATOR_MODULO) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseUnary();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (currentToken().kind == NTokenKind::OPERATOR_MINUS
        || currentToken().kind == NTokenKind::OPERATOR_LOGICAL_NOT
        || currentToken().kind == NTokenKind::OPERATOR_AMPERSAND
        || currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        // 前缀一元：- ! & *（& 只能作用于左值、* 只能作用于指针，由语义检查）
        // * 与 [] 的优先级与 C 一致：*p[i] 解析为 *(p[i])
        auto unaryExpr = std::make_unique<UnaryExpr>(op, line, column);
        unaryExpr->operand = parseUnary();

        return unaryExpr;
    }

    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();

    // 后缀下标：a[i]、p[i]、a[i][j]（多维下标由语义分析报不支持）
    while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto indexExpr = std::make_unique<IndexExpr>(std::move(expr), line, column);
        indexExpr->index = parseExpression();
        expect(NTokenKind::DELIMITER_RBRACKET);

        expr = std::move(indexExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token token = currentToken();

    switch (token.kind) {
    case NTokenKind::INTEGER_CONSTANT: {
        int value = std::stoi(token.value);
        advance();
        return std::make_unique<IntegerLiteral>(value, token.line, token.column);
    }

    case NTokenKind::CHAR_CONSTANT: {
        char value = token.value[0];
        advance();
        return std::make_unique<CharLiteral>(value, token.line, token.column);
    }

    case NTokenKind::STRING_CONSTANT: {
        std::string value = token.value;
        advance();
        return std::make_unique<StringLiteral>(value, token.line, token.column);
    }

    case NTokenKind::KEYWORD_NULL: {
        int line = token.line;
        int column = token.column;
        advance();
        return std::make_unique<NullLiteral>(line, column);
    }

    case NTokenKind::IDENTIFIER: {
        std::string name = token.value;
        int line = token.line;
        int column = token.column;
        advance();

        // 检查是否是函数调用
        if (currentToken().kind == NTokenKind::DELIMITER_LPAREN) {
            return parseCall(name);
        }

        return std::make_unique<IdentifierExpr>(name, line, column);
    }

    case NTokenKind::DELIMITER_LPAREN: {
        advance();
        auto expr = parseExpression();
        expect(NTokenKind::DELIMITER_RPAREN);
        return expr;
    }

    default:
        error("Unexpected token: " + token.value);
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parseCall(const std::string& callee) {
    Token token = currentToken();
    auto callExpr = std::make_unique<CallExpr>(callee, token.line, token.column);

    expect(NTokenKind::DELIMITER_LPAREN);

    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        do {
            callExpr->arguments.push_back(parseExpression());
        } while (match(NTokenKind::DELIMITER_COMMA));
    }

    expect(NTokenKind::DELIMITER_RPAREN);

    return callExpr;
}