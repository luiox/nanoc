#ifndef NCC_PARSER_H
#define NCC_PARSER_H

#include "ncc/ast.hpp"
#include "ncc/lexer.hpp"
#include <memory>
#include <vector>

class Parser {
public:
    Parser(const std::vector<Token>& tokens);

    // 解析程序
    std::unique_ptr<Program> parse();

private:
    std::vector<Token> m_tokens;
    size_t m_pos;

    // 辅助函数
    Token currentToken() const;
    Token peekToken() const;
    void advance();
    bool match(NTokenKind kind);
    bool expect(NTokenKind kind);

    // 解析函数
    std::unique_ptr<Decl> parseDeclaration();
    std::unique_ptr<VarDeclaration> parseVarDeclaration();
    std::unique_ptr<FuncDeclaration> parseFuncDeclaration();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseVarDeclarationStmt();
    std::unique_ptr<CompoundStmt> parseCompoundStatement();
    std::unique_ptr<IfStmt> parseIfStatement();
    std::unique_ptr<WhileStmt> parseWhileStatement();
    std::unique_ptr<ForStmt> parseForStatement();
    std::unique_ptr<ReturnStmt> parseReturnStatement();
    std::unique_ptr<BreakStmt> parseBreakStatement();
    std::unique_ptr<ContinueStmt> parseContinueStatement();
    std::unique_ptr<ExprStmt> parseExprStatement();

    // 表达式解析
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseRelational();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseCall(const std::string& callee);

    // 错误处理
    void error(const std::string& message);
};

#endif // NCC_PARSER_H