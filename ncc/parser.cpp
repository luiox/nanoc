#include "parser.h"
#include <stdexcept>
#include <sstream>

Parser::Parser(const std::vector<Token>& tokens) 
    : m_tokens(tokens), m_pos(0) {
}

Token Parser::currentToken() const {
    if (m_pos >= m_tokens.size()) {
        return Token(TokenType::TOKEN_EOF, "", 0, 0);
    }
    return m_tokens[m_pos];
}

Token Parser::peekToken() const {
    if (m_pos + 1 >= m_tokens.size()) {
        return Token(TokenType::TOKEN_EOF, "", 0, 0);
    }
    return m_tokens[m_pos + 1];
}

void Parser::advance() {
    if (m_pos < m_tokens.size()) {
        m_pos++;
    }
}

bool Parser::match(TokenType type) {
    if (currentToken().type == type) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenType type) {
    if (currentToken().type == type) {
        advance();
        return true;
    }
    error("Expected token type " + std::to_string(static_cast<int>(type)) + 
          " but got " + std::to_string(static_cast<int>(currentToken().type)));
    return false;
}

void Parser::error(const std::string& message) {
    Token token = currentToken();
    std::ostringstream oss;
    oss << "Parse error at line " << token.line << ", column " << token.column 
        << ": " << message;
    throw std::runtime_error(oss.str());
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>(currentToken().line, currentToken().column);
    
    while (currentToken().type != TokenType::TOKEN_EOF) {
        auto decl = parseDeclaration();
        if (decl) {
            program->declarations.push_back(std::move(decl));
        }
    }
    
    return program;
}

std::unique_ptr<Decl> Parser::parseDeclaration() {
    // 检查是否是类型关键字
    if (currentToken().type == TokenType::KEYWORD_INT || 
        currentToken().type == TokenType::KEYWORD_CHAR ||
        currentToken().type == TokenType::KEYWORD_VOID) {
        
        // 保存当前位置
        size_t startPos = m_pos;
        
        // 获取类型
        std::string type = currentToken().value;
        advance();
        
        // 获取名称
        if (currentToken().type != TokenType::IDENTIFIER) {
            error("Expected identifier after type");
        }
        std::string name = currentToken().value;
        advance();
        
        // 检查是函数声明还是变量声明
        if (currentToken().type == TokenType::DELIMITER_LPAREN) {
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
    if (currentToken().type != TokenType::KEYWORD_INT && 
        currentToken().type != TokenType::KEYWORD_CHAR &&
        currentToken().type != TokenType::KEYWORD_VOID) {
        error("Expected type keyword");
    }
    
    std::string type = currentToken().value;
    int line = currentToken().line;
    int column = currentToken().column;
    advance();
    
    // 获取名称
    if (currentToken().type != TokenType::IDENTIFIER) {
        error("Expected identifier");
    }
    
    std::string name = currentToken().value;
    advance();
    
    auto varDecl = std::make_unique<VarDeclaration>(type, name, line, column);
    
    // 检查是否有初始化
    if (currentToken().type == TokenType::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseExpression();
    }
    
    // 期望分号
    expect(TokenType::DELIMITER_SEMICOLON);
    
    return varDecl;
}

std::unique_ptr<FuncDeclaration> Parser::parseFuncDeclaration() {
    // 获取返回类型
    if (currentToken().type != TokenType::KEYWORD_INT && 
        currentToken().type != TokenType::KEYWORD_CHAR &&
        currentToken().type != TokenType::KEYWORD_VOID) {
        error("Expected return type");
    }
    
    std::string returnType = currentToken().value;
    int line = currentToken().line;
    int column = currentToken().column;
    advance();
    
    // 获取函数名
    if (currentToken().type != TokenType::IDENTIFIER) {
        error("Expected function name");
    }
    
    std::string name = currentToken().value;
    advance();
    
    auto funcDecl = std::make_unique<FuncDeclaration>(returnType, name, line, column);
    
    // 解析参数列表
    expect(TokenType::DELIMITER_LPAREN);
    
    if (currentToken().type != TokenType::DELIMITER_RPAREN) {
        do {
            // 解析参数
            if (currentToken().type != TokenType::KEYWORD_INT && 
                currentToken().type != TokenType::KEYWORD_CHAR &&
                currentToken().type != TokenType::KEYWORD_VOID) {
                error("Expected parameter type");
            }
            
            std::string paramType = currentToken().value;
            advance();
            
            if (currentToken().type != TokenType::IDENTIFIER) {
                error("Expected parameter name");
            }
            
            std::string paramName = currentToken().value;
            advance();
            
            funcDecl->parameters.push_back(
                std::make_unique<VarDeclaration>(paramType, paramName, line, column));
            
        } while (match(TokenType::DELIMITER_COMMA));
    }
    
    expect(TokenType::DELIMITER_RPAREN);
    
    // 解析函数体
    funcDecl->body = parseCompoundStatement();
    
    return funcDecl;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    switch (currentToken().type) {
        case TokenType::DELIMITER_LBRACE:
            return parseCompoundStatement();
        case TokenType::KEYWORD_IF:
            return parseIfStatement();
        case TokenType::KEYWORD_WHILE:
            return parseWhileStatement();
        case TokenType::KEYWORD_FOR:
            return parseForStatement();
        case TokenType::KEYWORD_RETURN:
            return parseReturnStatement();
        case TokenType::KEYWORD_BREAK:
            return parseBreakStatement();
        case TokenType::KEYWORD_CONTINUE:
            return parseContinueStatement();
        default:
            return parseExprStatement();
    }
}

std::unique_ptr<CompoundStmt> Parser::parseCompoundStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::DELIMITER_LBRACE);
    
    auto compound = std::make_unique<CompoundStmt>(line, column);
    
    while (currentToken().type != TokenType::DELIMITER_RBRACE && 
           currentToken().type != TokenType::TOKEN_EOF) {
        auto stmt = parseStatement();
        if (stmt) {
            compound->statements.push_back(std::move(stmt));
        }
    }
    
    expect(TokenType::DELIMITER_RBRACE);
    
    return compound;
}

std::unique_ptr<IfStmt> Parser::parseIfStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::KEYWORD_IF);
    expect(TokenType::DELIMITER_LPAREN);
    
    auto ifStmt = std::make_unique<IfStmt>(line, column);
    ifStmt->condition = parseExpression();
    
    expect(TokenType::DELIMITER_RPAREN);
    
    ifStmt->thenBranch = parseStatement();
    
    if (currentToken().type == TokenType::KEYWORD_ELSE) {
        advance();
        ifStmt->elseBranch = parseStatement();
    }
    
    return ifStmt;
}

std::unique_ptr<WhileStmt> Parser::parseWhileStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::KEYWORD_WHILE);
    expect(TokenType::DELIMITER_LPAREN);
    
    auto whileStmt = std::make_unique<WhileStmt>(line, column);
    whileStmt->condition = parseExpression();
    
    expect(TokenType::DELIMITER_RPAREN);
    
    whileStmt->body = parseStatement();
    
    return whileStmt;
}

std::unique_ptr<ForStmt> Parser::parseForStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::KEYWORD_FOR);
    expect(TokenType::DELIMITER_LPAREN);
    
    auto forStmt = std::make_unique<ForStmt>(line, column);
    
    // 初始化部分
    if (currentToken().type != TokenType::DELIMITER_SEMICOLON) {
        forStmt->init = parseStatement();
    } else {
        advance();
    }
    
    // 条件部分
    if (currentToken().type != TokenType::DELIMITER_SEMICOLON) {
        forStmt->condition = parseExpression();
    }
    expect(TokenType::DELIMITER_SEMICOLON);
    
    // 增量部分
    if (currentToken().type != TokenType::DELIMITER_RPAREN) {
        forStmt->increment = parseExpression();
    }
    expect(TokenType::DELIMITER_RPAREN);
    
    forStmt->body = parseStatement();
    
    return forStmt;
}

std::unique_ptr<ReturnStmt> Parser::parseReturnStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::KEYWORD_RETURN);
    
    auto returnStmt = std::make_unique<ReturnStmt>(line, column);
    
    if (currentToken().type != TokenType::DELIMITER_SEMICOLON) {
        returnStmt->value = parseExpression();
    }
    
    expect(TokenType::DELIMITER_SEMICOLON);
    
    return returnStmt;
}

std::unique_ptr<BreakStmt> Parser::parseBreakStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::KEYWORD_BREAK);
    expect(TokenType::DELIMITER_SEMICOLON);
    
    return std::make_unique<BreakStmt>(line, column);
}

std::unique_ptr<ContinueStmt> Parser::parseContinueStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    expect(TokenType::KEYWORD_CONTINUE);
    expect(TokenType::DELIMITER_SEMICOLON);
    
    return std::make_unique<ContinueStmt>(line, column);
}

std::unique_ptr<ExprStmt> Parser::parseExprStatement() {
    int line = currentToken().line;
    int column = currentToken().column;
    
    auto exprStmt = std::make_unique<ExprStmt>(line, column);
    exprStmt->expression = parseExpression();
    
    expect(TokenType::DELIMITER_SEMICOLON);
    
    return exprStmt;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> Parser::parseAssignment() {
    auto expr = parseLogicalOr();
    
    if (currentToken().type == TokenType::OPERATOR_ASSIGN) {
        // 检查左边是否是标识符
        if (expr->type != ASTNodeType::IDENTIFIER_EXPR) {
            error("Left side of assignment must be an identifier");
        }
        
        auto identifier = static_cast<IdentifierExpr*>(expr.get());
        auto assignExpr = std::make_unique<AssignExpr>(identifier->name, 
                                                       expr->line, expr->column);
        
        advance(); // 跳过 '='
        assignExpr->value = parseAssignment();
        
        return assignExpr;
    }
    
    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    
    while (currentToken().type == TokenType::OPERATOR_LOGICAL_OR) {
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
    
    while (currentToken().type == TokenType::OPERATOR_LOGICAL_AND) {
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
    
    while (currentToken().type == TokenType::OPERATOR_EQUAL || 
           currentToken().type == TokenType::OPERATOR_NOT_EQUAL) {
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
    
    while (currentToken().type == TokenType::OPERATOR_LESS || 
           currentToken().type == TokenType::OPERATOR_LESS_EQUAL ||
           currentToken().type == TokenType::OPERATOR_GREATER || 
           currentToken().type == TokenType::OPERATOR_GREATER_EQUAL) {
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
    
    while (currentToken().type == TokenType::OPERATOR_PLUS || 
           currentToken().type == TokenType::OPERATOR_MINUS) {
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
    
    while (currentToken().type == TokenType::OPERATOR_MULTIPLY || 
           currentToken().type == TokenType::OPERATOR_DIVIDE ||
           currentToken().type == TokenType::OPERATOR_MODULO) {
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
    if (currentToken().type == TokenType::OPERATOR_MINUS || 
        currentToken().type == TokenType::OPERATOR_LOGICAL_NOT) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();
        
        auto unaryExpr = std::make_unique<UnaryExpr>(op, line, column);
        unaryExpr->operand = parseUnary();
        
        return unaryExpr;
    }
    
    return parsePrimary();
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token token = currentToken();
    
    switch (token.type) {
        case TokenType::INTEGER_CONSTANT: {
            int value = std::stoi(token.value);
            advance();
            return std::make_unique<IntegerLiteral>(value, token.line, token.column);
        }
        
        case TokenType::CHAR_CONSTANT: {
            char value = token.value[0];
            advance();
            return std::make_unique<CharLiteral>(value, token.line, token.column);
        }
        
        case TokenType::IDENTIFIER: {
            std::string name = token.value;
            int line = token.line;
            int column = token.column;
            advance();
            
            // 检查是否是函数调用
            if (currentToken().type == TokenType::DELIMITER_LPAREN) {
                return parseCall(name);
            }
            
            return std::make_unique<IdentifierExpr>(name, line, column);
        }
        
        case TokenType::DELIMITER_LPAREN: {
            advance();
            auto expr = parseExpression();
            expect(TokenType::DELIMITER_RPAREN);
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
    
    expect(TokenType::DELIMITER_LPAREN);
    
    if (currentToken().type != TokenType::DELIMITER_RPAREN) {
        do {
            callExpr->arguments.push_back(parseExpression());
        } while (match(TokenType::DELIMITER_COMMA));
    }
    
    expect(TokenType::DELIMITER_RPAREN);
    
    return callExpr;
}