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
    // typedef 只出现在文件作用域
    if (currentToken().kind == NTokenKind::KEYWORD_TYPEDEF) {
        return parseTypedefDeclaration();
    }

    // 类型开头：builtin 关键字或 struct
    if (currentToken().kind == NTokenKind::KEYWORD_INT
        || currentToken().kind == NTokenKind::KEYWORD_CHAR
        || currentToken().kind == NTokenKind::KEYWORD_VOID
        || currentToken().kind == NTokenKind::KEYWORD_STRUCT) {

        // 保存当前位置
        size_t startPos = m_pos;

        bool isStructTag = false;
        parseTypePrefix(isStructTag, m_tokens[startPos].line, m_tokens[startPos].column);

        // struct Tag 后跟 { → 定义；跟 ; → 前向声明；否则回退按变量/函数声明解析
        if (isStructTag) {
            if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
                const Token& kw = m_tokens[startPos];
                std::string tag = m_tokens[startPos + 1].value;
                auto structDecl = parseStructBody(tag, kw.line, kw.column);
                expect(NTokenKind::DELIMITER_SEMICOLON);
                return structDecl;
            }
            if (currentToken().kind == NTokenKind::DELIMITER_SEMICOLON) {
                const Token& kw = m_tokens[startPos];
                auto structDecl =
                  std::make_unique<StructDeclaration>(m_tokens[startPos + 1].value,
                                                      kw.line,
                                                      kw.column);
                structDecl->isForward = true;
                advance(); // 消费 ;
                return structDecl;
            }
        }

        // 名称探测（当前位置已在类型前缀之后）：判断函数声明还是变量声明
        while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
            advance();
        }
        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected identifier after type");
        }
        advance();

        if (currentToken().kind == NTokenKind::DELIMITER_LPAREN) {
            // 函数声明
            m_pos = startPos; // 回退
            return parseFuncDeclaration();
        }
        // 变量声明
        m_pos = startPos; // 回退
        return parseVarDeclaration();
    }

    error("Expected declaration");
    return nullptr;
}

// 类型前缀：builtin 关键字或 `struct Tag`；line/column 返回首个 token 位置
std::string Parser::parseTypePrefix(bool& isStructTag, int& line, int& column) {
    isStructTag = false;
    line = currentToken().line;
    column = currentToken().column;

    if (currentToken().kind == NTokenKind::KEYWORD_STRUCT) {
        advance();
        isStructTag = true;
        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected struct tag after 'struct'");
        }
        std::string tag = currentToken().value;
        advance();
        return tag;
    }

    if (currentToken().kind == NTokenKind::KEYWORD_INT
        || currentToken().kind == NTokenKind::KEYWORD_CHAR
        || currentToken().kind == NTokenKind::KEYWORD_VOID) {
        std::string type = currentToken().value;
        advance();
        return type;
    }

    error("Expected type keyword");
    return "";
}

// struct 主体：`{ field; field; ... }`（不含结尾分号；tag 调用方已消费）
std::unique_ptr<StructDeclaration> Parser::parseStructBody(const std::string& tag,
                                                           int line,
                                                           int column) {
    auto structDecl = std::make_unique<StructDeclaration>(tag, line, column);
    expect(NTokenKind::DELIMITER_LBRACE);

    while (currentToken().kind != NTokenKind::DELIMITER_RBRACE
           && currentToken().kind != NTokenKind::TOKEN_EOF) {
        if (currentToken().kind == NTokenKind::KEYWORD_TYPEDEF) {
            error("typedef is not allowed inside a struct");
        }
        bool fieldIsStruct = false;
        int fieldLine = 0;
        int fieldColumn = 0;
        std::string fieldType = parseTypePrefix(fieldIsStruct, fieldLine, fieldColumn);

        int pointerDepth = 0;
        while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
            ++pointerDepth;
            advance();
        }

        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected member name");
        }
        std::string fieldName = currentToken().value;
        advance();

        auto field =
          std::make_unique<VarDeclaration>(fieldType, fieldName, fieldLine, fieldColumn);
        field->isStructTag = fieldIsStruct;
        field->pointerDepth = pointerDepth;

        // 数组成员：int arr[4];
        if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            field->isArray = true;
            field->arrayDims = 1;
            advance();
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                error("Expected integer constant array size");
            }
            field->arraySize = std::stoi(currentToken().value);
            advance();
            expect(NTokenKind::DELIMITER_RBRACKET);
            while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                ++field->arrayDims;
                advance();
                if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                    error("Expected integer constant array size");
                }
                advance();
                expect(NTokenKind::DELIMITER_RBRACKET);
            }
        }

        if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
            error("struct field initializers are not supported");
        }

        expect(NTokenKind::DELIMITER_SEMICOLON);
        structDecl->fields.push_back(std::move(field));
    }

    expect(NTokenKind::DELIMITER_RBRACE);
    return structDecl;
}

// typedef：typedef int T; / typedef struct Point T; / typedef struct { .. } T;
//          typedef struct Point { .. } T; / typedef int* P;
std::unique_ptr<Decl> Parser::parseTypedefDeclaration() {
    int line = currentToken().line;
    int column = currentToken().column;
    advance(); // 消费 typedef

    auto decl = std::make_unique<TypedefDeclaration>("", line, column);

    if (currentToken().kind == NTokenKind::KEYWORD_STRUCT) {
        int structLine = currentToken().line;
        int structColumn = currentToken().column;
        decl->baseIsStruct = true;
        advance(); // 消费 struct

        if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
            // 匿名定义：内部标签由解析器生成
            std::string anonTag = "__anon_" + std::to_string(m_anonCounter++);
            decl->structDef = parseStructBody(anonTag, structLine, structColumn);
            decl->baseType = anonTag;
        } else if (currentToken().kind == NTokenKind::IDENTIFIER) {
            decl->baseType = currentToken().value;
            advance();
            if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
                // typedef struct Tag { ... } Alias;：定义与别名一体
                decl->structDef =
                  parseStructBody(decl->baseType, structLine, structColumn);
            }
        } else {
            error("Expected struct tag after 'struct'");
        }
    } else if (currentToken().kind == NTokenKind::KEYWORD_INT
               || currentToken().kind == NTokenKind::KEYWORD_CHAR
               || currentToken().kind == NTokenKind::KEYWORD_VOID) {
        decl->baseType = currentToken().value;
        advance();
    } else {
        error("Expected type after 'typedef'");
    }

    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++decl->pointerDepth;
        advance();
    }

    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected typedef name");
    }
    decl->alias = currentToken().value;
    advance();

    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        error("array typedefs are not supported");
    }

    expect(NTokenKind::DELIMITER_SEMICOLON);
    return decl;
}

std::unique_ptr<VarDeclaration> Parser::parseVarDeclaration() {
    // 获取类型（builtin 或 struct Tag）
    bool isStructTag = false;
    int line = 0;
    int column = 0;
    std::string type = parseTypePrefix(isStructTag, line, column);

    // struct 定义/前向声明不允许出现在变量声明位置
    if (isStructTag
        && (currentToken().kind == NTokenKind::DELIMITER_LBRACE
            || currentToken().kind == NTokenKind::DELIMITER_SEMICOLON)) {
        error("struct definition is only allowed at file scope");
    }

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
    varDecl->isStructTag = isStructTag;
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

    // 检查是否有初始化（{ ... } → 逐成员初始化器）
    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseInitializer();
    }

    // 期望分号
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return varDecl;
}

// 声明初始化：`{ e1, e2 }` → InitListExpr，否则普通表达式
std::unique_ptr<Expr> Parser::parseInitializer() {
    if (currentToken().kind != NTokenKind::DELIMITER_LBRACE) {
        return parseExpression();
    }
    int line = currentToken().line;
    int column = currentToken().column;
    advance();
    auto init = std::make_unique<InitListExpr>(line, column);
    if (currentToken().kind != NTokenKind::DELIMITER_RBRACE) {
        do {
            init->values.push_back(parseExpression());
        } while (match(NTokenKind::DELIMITER_COMMA));
    }
    expect(NTokenKind::DELIMITER_RBRACE);
    return init;
}

std::unique_ptr<Stmt> Parser::parseVarDeclarationStmt() {
    // 获取类型（builtin 或 struct Tag）
    bool isStructTag = false;
    int line = 0;
    int column = 0;
    std::string type = parseTypePrefix(isStructTag, line, column);

    // 函数体内不允许 struct 定义/前向声明（局部 struct 变量引用全局标签合法）
    if (isStructTag
        && (currentToken().kind == NTokenKind::DELIMITER_LBRACE
            || currentToken().kind == NTokenKind::DELIMITER_SEMICOLON)) {
        error("struct definition is only allowed at file scope");
    }

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
    varDecl->isStructTag = isStructTag;
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

    // 检查是否有初始化（{ ... } → 逐成员初始化器）
    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseInitializer();
    }

    // 期望分号
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return varDecl;
}

std::unique_ptr<FuncDeclaration> Parser::parseFuncDeclaration() {
    // 获取返回类型（builtin 或 struct Tag）
    bool returnIsStruct = false;
    int line = 0;
    int column = 0;
    std::string returnType = parseTypePrefix(returnIsStruct, line, column);

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
    funcDecl->returnIsStruct = returnIsStruct;
    funcDecl->returnPointerDepth = returnPointerDepth;

    // 解析参数列表
    expect(NTokenKind::DELIMITER_LPAREN);

    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        do {
            // 解析参数（builtin 或 struct Tag）
            bool paramIsStruct = false;
            int paramLine = 0;
            int paramColumn = 0;
            std::string paramType =
              parseTypePrefix(paramIsStruct, paramLine, paramColumn);
            (void)paramLine;
            (void)paramColumn;

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

            // 参数沿用既有约定记录函数声明位置（诊断落点与既有负例一致）
            auto param =
              std::make_unique<VarDeclaration>(paramType, paramName, line, column);
            param->isStructTag = paramIsStruct;
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
    case NTokenKind::KEYWORD_STRUCT:
        // 局部 struct 变量声明（引用全局标签）合法；定义/前向声明在
        // parseVarDeclarationStmt 中报错
        return parseVarDeclarationStmt();
    case NTokenKind::KEYWORD_TYPEDEF:
        error("typedef declarations are only allowed at file scope");
        return nullptr;
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
        // 左值形式：标识符（x = v）、下标（a[i] = v）、解引用（*p = v）、
        // 成员访问（p.x = v / p->x = v）
        const bool validTarget = expr->type == ASTNodeType::IDENTIFIER_EXPR
                                 || expr->type == ASTNodeType::INDEX_EXPR
                                 || expr->type == ASTNodeType::MEMBER_EXPR
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

    // 后缀下标与成员访问：a[i]、p[i]、p.x、p->x（多维下标由语义分析报不支持）
    while (true) {
        if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            int line = currentToken().line;
            int column = currentToken().column;
            advance();

            auto indexExpr = std::make_unique<IndexExpr>(std::move(expr), line, column);
            indexExpr->index = parseExpression();
            expect(NTokenKind::DELIMITER_RBRACKET);

            expr = std::move(indexExpr);
            continue;
        }
        if (currentToken().kind == NTokenKind::OPERATOR_DOT
            || currentToken().kind == NTokenKind::OPERATOR_ARROW) {
            const bool arrow = currentToken().kind == NTokenKind::OPERATOR_ARROW;
            const int line = currentToken().line;
            const int column = currentToken().column;
            advance();

            if (currentToken().kind != NTokenKind::IDENTIFIER) {
                error(arrow ? "Expected member name after '->'"
                            : "Expected member name after '.'");
            }
            std::string member = currentToken().value;
            advance();

            expr = std::make_unique<MemberExpr>(std::move(expr), member, arrow, line, column);
            continue;
        }
        break;
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