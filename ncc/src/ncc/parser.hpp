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
    int m_anonCounter = 0;              // 匿名 struct 内部标签计数（__anon_N）
    std::set<std::string> m_typedefNames; // 已解析的 typedef 别名（文件作用域）

    // 辅助函数
    Token currentToken() const;
    Token peekToken() const;
    Token peekTokenAt(size_t offset) const;
    void advance();
    bool match(NTokenKind kind);
    bool expect(NTokenKind kind);
    // 是否已登记的 typedef 别名（解析器按声明顺序维护，供语句分发消歧）
    bool isTypedefName(const std::string& name) const;

    // 解析函数
    std::unique_ptr<Decl> parseDeclaration();
    std::unique_ptr<VarDeclaration> parseVarDeclaration();
    std::unique_ptr<FuncDeclaration> parseFuncDeclaration();
    std::unique_ptr<Decl> parseTypedefDeclaration();
    std::unique_ptr<StructDeclaration> parseStructBody(const std::string& tag,
                                                       int line,
                                                       int column);
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

    // 类型前缀：builtin 关键字或 `struct Tag`；line/column 返回首个 token 位置
    std::string parseTypePrefix(bool& isStructTag, int& line, int& column);
    // 声明初始化：`{ e1, e2 }` → InitListExpr，否则普通表达式
    std::unique_ptr<Expr> parseInitializer();

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