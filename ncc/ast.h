#ifndef NCC_AST_H
#define NCC_AST_H

#include <string>
#include <vector>
#include <memory>

// AST节点类型枚举
enum class ASTNodeType {
    // 程序
    PROGRAM,
    
    // 声明
    VAR_DECLARATION,
    FUNC_DECLARATION,
    
    // 语句
    COMPOUND_STMT,
    IF_STMT,
    WHILE_STMT,
    FOR_STMT,
    RETURN_STMT,
    BREAK_STMT,
    CONTINUE_STMT,
    EXPR_STMT,
    
    // 表达式
    BINARY_EXPR,
    UNARY_EXPR,
    ASSIGN_EXPR,
    CALL_EXPR,
    IDENTIFIER_EXPR,
    INTEGER_LITERAL,
    CHAR_LITERAL,
    
    // 其他
    PARAMETER,
    ARGUMENT
};

// 前向声明
class ASTVisitor;

// AST节点基类
class ASTNode {
public:
    ASTNodeType type;
    int line;
    int column;
    
    ASTNode(ASTNodeType t, int l, int c) : type(t), line(l), column(c) {}
    virtual ~ASTNode() = default;
    
    virtual void accept(ASTVisitor& visitor) = 0;
};

// 表达式基类
class Expr : public ASTNode {
public:
    Expr(ASTNodeType t, int l, int c) : ASTNode(t, l, c) {}
};

// 语句基类
class Stmt : public ASTNode {
public:
    Stmt(ASTNodeType t, int l, int c) : ASTNode(t, l, c) {}
};

// 声明基类
class Decl : public ASTNode {
public:
    Decl(ASTNodeType t, int l, int c) : ASTNode(t, l, c) {}
};

// 程序节点
class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Decl>> declarations;
    
    Program(int l, int c) : ASTNode(ASTNodeType::PROGRAM, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 变量声明节点
class VarDeclaration : public Decl {
public:
    std::string type;
    std::string name;
    std::unique_ptr<Expr> initializer;
    
    VarDeclaration(const std::string& t, const std::string& n, int l, int c)
        : Decl(ASTNodeType::VAR_DECLARATION, l, c), type(t), name(n) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 函数声明节点
class FuncDeclaration : public Decl {
public:
    std::string returnType;
    std::string name;
    std::vector<std::unique_ptr<VarDeclaration>> parameters;
    std::unique_ptr<Stmt> body;
    
    FuncDeclaration(const std::string& rt, const std::string& n, int l, int c)
        : Decl(ASTNodeType::FUNC_DECLARATION, l, c), returnType(rt), name(n) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 复合语句节点
class CompoundStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;
    
    CompoundStmt(int l, int c) : Stmt(ASTNodeType::COMPOUND_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// if语句节点
class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
    
    IfStmt(int l, int c) : Stmt(ASTNodeType::IF_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// while语句节点
class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
    
    WhileStmt(int l, int c) : Stmt(ASTNodeType::WHILE_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// for语句节点
class ForStmt : public Stmt {
public:
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> increment;
    std::unique_ptr<Stmt> body;
    
    ForStmt(int l, int c) : Stmt(ASTNodeType::FOR_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// return语句节点
class ReturnStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;
    
    ReturnStmt(int l, int c) : Stmt(ASTNodeType::RETURN_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// break语句节点
class BreakStmt : public Stmt {
public:
    BreakStmt(int l, int c) : Stmt(ASTNodeType::BREAK_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// continue语句节点
class ContinueStmt : public Stmt {
public:
    ContinueStmt(int l, int c) : Stmt(ASTNodeType::CONTINUE_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 表达式语句节点
class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expression;
    
    ExprStmt(int l, int c) : Stmt(ASTNodeType::EXPR_STMT, l, c) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 二元表达式节点
class BinaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    
    BinaryExpr(const std::string& o, int l, int c) : Expr(ASTNodeType::BINARY_EXPR, l, c), op(o) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 一元表达式节点
class UnaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> operand;
    
    UnaryExpr(const std::string& o, int l, int c) : Expr(ASTNodeType::UNARY_EXPR, l, c), op(o) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 赋值表达式节点
class AssignExpr : public Expr {
public:
    std::string name;
    std::unique_ptr<Expr> value;
    
    AssignExpr(const std::string& n, int l, int c) : Expr(ASTNodeType::ASSIGN_EXPR, l, c), name(n) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 函数调用表达式节点
class CallExpr : public Expr {
public:
    std::string callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    
    CallExpr(const std::string& calleeName, int l, int c) : Expr(ASTNodeType::CALL_EXPR, l, c), callee(calleeName) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 标识符表达式节点
class IdentifierExpr : public Expr {
public:
    std::string name;
    
    IdentifierExpr(const std::string& n, int l, int c) : Expr(ASTNodeType::IDENTIFIER_EXPR, l, c), name(n) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 整数字面量节点
class IntegerLiteral : public Expr {
public:
    int value;
    
    IntegerLiteral(int v, int l, int c) : Expr(ASTNodeType::INTEGER_LITERAL, l, c), value(v) {}
    
    void accept(ASTVisitor& visitor) override;
};

// 字符字面量节点
class CharLiteral : public Expr {
public:
    char value;
    
    CharLiteral(char v, int l, int c) : Expr(ASTNodeType::CHAR_LITERAL, l, c), value(v) {}
    
    void accept(ASTVisitor& visitor) override;
};

// AST访问者接口
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    
    virtual void visit(Program& node) = 0;
    virtual void visit(VarDeclaration& node) = 0;
    virtual void visit(FuncDeclaration& node) = 0;
    virtual void visit(CompoundStmt& node) = 0;
    virtual void visit(IfStmt& node) = 0;
    virtual void visit(WhileStmt& node) = 0;
    virtual void visit(ForStmt& node) = 0;
    virtual void visit(ReturnStmt& node) = 0;
    virtual void visit(BreakStmt& node) = 0;
    virtual void visit(ContinueStmt& node) = 0;
    virtual void visit(ExprStmt& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(UnaryExpr& node) = 0;
    virtual void visit(AssignExpr& node) = 0;
    virtual void visit(CallExpr& node) = 0;
    virtual void visit(IdentifierExpr& node) = 0;
    virtual void visit(IntegerLiteral& node) = 0;
    virtual void visit(CharLiteral& node) = 0;
};

#endif // NCC_AST_H