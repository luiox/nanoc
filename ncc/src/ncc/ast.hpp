#ifndef NCC_AST_H
#define NCC_AST_H

#include <memory>
#include <string>
#include <vector>

// AST节点类型枚举
enum class ASTNodeType {
    // 程序
    PROGRAM,

    // 声明
    VAR_DECLARATION,
    FUNC_DECLARATION,
    STRUCT_DECLARATION, // struct 定义/前向声明（PRD R1.2 第二批）
    TYPEDEF_DECLARATION,

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
    STRING_LITERAL,
    NULL_LITERAL,
    INDEX_EXPR,
    MEMBER_EXPR,    // p.x / p->x 成员访问（PRD R1.2 第二批）
    INIT_LIST_EXPR, // { e1, e2, ... } 逐成员初始化器（PRD R1.2 第二批）

    // 其他
    PARAMETER,
    ARGUMENT
};

// 前向声明
class ASTVisitor;

// import 指令（PRD R2a 多文件整体编译）：
// - `import math;` → target = "math"，quoted = false（装载器解析为导入者同目录 math.nc）
// - `import "util/helpers.nc";` → target = "util/helpers.nc"，quoted =
// true（相对导入者目录） 仅允许出现在文件顶部（其他位置由解析器报错，带行号）
struct ImportDirective {
    std::string target;
    bool quoted = false;
    int line = 0;
    int column = 0;
};

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
    // 定义所在文件（PRD R2a）：多文件装载器合并编译单元时逐条标注；
    // 空 = 单文件模式（直接经 Parser 使用，如既有测试），语义/代码生成按
    // 既有单文件行为处理
    std::string sourceFile;

    Decl(ASTNodeType t, int l, int c) : ASTNode(t, l, c) {}
};

// 程序节点
class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Decl>> declarations;
    std::vector<ImportDirective> imports; // 文件顶部 import 指令（按出现顺序）

    Program(int l, int c) : ASTNode(ASTNodeType::PROGRAM, l, c) {}

    void accept(ASTVisitor& visitor) override;
};

// 变量声明节点
// 类型字段（PRD R1.2）：
// - type：基础类型名（int/char/void、struct 标签或 typedef 别名）
// - isStructTag：type 带 struct 前缀（struct Point p），type 存标签名
// - pointerDepth：指针层级（0=值，1=一级指针；≥2 由语义分析显式报不支持）
// - isArray/arraySize/arrayDims：一维数组（arrayDims≥2 由语义分析显式报不支持）
class VarDeclaration : public Decl {
public:
    std::string type;
    std::string name;
    bool isStructTag = false;
    int pointerDepth = 0;
    bool isArray = false;
    int arraySize = 0;
    int arrayDims = 0;
    bool isExported = false; // export 修饰的顶层全局变量（PRD R2a；局部/成员恒为 false）
    std::unique_ptr<Expr> initializer;

    VarDeclaration(const std::string& t, const std::string& n, int l, int c)
      : Decl(ASTNodeType::VAR_DECLARATION, l, c), type(t), name(n) {}

    void accept(ASTVisitor& visitor) override;
};

// 函数声明节点
class FuncDeclaration : public Decl {
public:
    std::string returnType;
    bool returnIsStruct = false; // 返回类型带 struct 前缀（struct Point f()）
    int returnPointerDepth = 0;  // 返回类型指针层级（0=值，1=指针）
    std::string name;
    std::vector<std::unique_ptr<VarDeclaration>> parameters;
    bool isExported = false;    // export 修饰的顶层函数（PRD R2a）
    bool isExtern = false;      // extern 声明（PRD R3）：无函数体，符号由宿主提供
    bool isVariadic = false;    // 参数表带 ...（仅 extern 声明，PRD R3）
    std::unique_ptr<Stmt> body; // extern 声明为 nullptr

    FuncDeclaration(const std::string& rt, const std::string& n, int l, int c)
      : Decl(ASTNodeType::FUNC_DECLARATION, l, c), returnType(rt), name(n) {}

    void accept(ASTVisitor& visitor) override;
};

// struct 定义或前向声明节点（PRD R1.2 第二批）
// - `struct Point { int x; int y; };` → tag="Point"，fields 非空
// - `struct Node;` → isForward=true
// - 匿名定义仅经 typedef 出现（typedef struct { ... } Alias;），tag 为空，
//   由解析器生成内部标签 __anon_N 挂到 TypedefDeclaration::structDef 上
// 成员复用 VarDeclaration（不含初始化器，isStructTag 引用已定义的 struct 标签）
class StructDeclaration : public Decl {
public:
    std::string tag;
    bool isForward = false;
    std::vector<std::unique_ptr<VarDeclaration>> fields;

    StructDeclaration(const std::string& t, int l, int c)
      : Decl(ASTNodeType::STRUCT_DECLARATION, l, c), tag(t) {}

    void accept(ASTVisitor& visitor) override;
};

// typedef 声明节点（PRD R1.2 第二批）
// 形态：typedef int MyInt; / typedef struct Point PointT;
//       typedef struct { ... } Anonymous;（structDef 非空）/ typedef int* IntPtr;
class TypedefDeclaration : public Decl {
public:
    std::string baseType;                         // 基础类型名（int/char/标签/别名）
    bool baseIsStruct = false;                    // baseType 带 struct 前缀
    int pointerDepth = 0;                         // 别名上的指针层级
    std::string alias;                            // 别名
    std::unique_ptr<StructDeclaration> structDef; // 内联 struct 定义（可空）

    TypedefDeclaration(const std::string& aliasName, int l, int c)
      : Decl(ASTNodeType::TYPEDEF_DECLARATION, l, c), alias(aliasName) {}

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

// 变量声明语句节点（用于在语句上下文中声明变量）
// 字段语义与 VarDeclaration 相同（指针/数组/struct 扩展见其注释）
class StmtVarDeclaration : public Stmt {
public:
    std::string type;
    std::string name;
    bool isStructTag = false;
    int pointerDepth = 0;
    bool isArray = false;
    int arraySize = 0;
    int arrayDims = 0;
    std::unique_ptr<Expr> initializer;

    StmtVarDeclaration(const std::string& t, const std::string& n, int l, int c)
      : Stmt(ASTNodeType::VAR_DECLARATION, l, c), type(t), name(n) {}

    void accept(ASTVisitor& visitor) override;
};

// 二元表达式节点
class BinaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;

    BinaryExpr(const std::string& o, int l, int c)
      : Expr(ASTNodeType::BINARY_EXPR, l, c), op(o) {}

    void accept(ASTVisitor& visitor) override;
};

// 一元表达式节点
class UnaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(const std::string& o, int l, int c)
      : Expr(ASTNodeType::UNARY_EXPR, l, c), op(o) {}

    void accept(ASTVisitor& visitor) override;
};

// 赋值表达式节点
// target 是左值表达式：IdentifierExpr（x = v）、IndexExpr（a[i] = v）或
// 解引用 UnaryExpr（*p = v）；解析器只接受这三种形式
class AssignExpr : public Expr {
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;

    AssignExpr(std::unique_ptr<Expr> t, int l, int c)
      : Expr(ASTNodeType::ASSIGN_EXPR, l, c), target(std::move(t)) {}

    void accept(ASTVisitor& visitor) override;
};

// 函数调用表达式节点
class CallExpr : public Expr {
public:
    std::string callee;
    std::vector<std::unique_ptr<Expr>> arguments;

    CallExpr(const std::string& calleeName, int l, int c)
      : Expr(ASTNodeType::CALL_EXPR, l, c), callee(calleeName) {}

    void accept(ASTVisitor& visitor) override;
};

// 标识符表达式节点
class IdentifierExpr : public Expr {
public:
    std::string name;

    IdentifierExpr(const std::string& n, int l, int c)
      : Expr(ASTNodeType::IDENTIFIER_EXPR, l, c), name(n) {}

    void accept(ASTVisitor& visitor) override;
};

// 整数字面量节点
class IntegerLiteral : public Expr {
public:
    int value;

    IntegerLiteral(int v, int l, int c)
      : Expr(ASTNodeType::INTEGER_LITERAL, l, c), value(v) {}

    void accept(ASTVisitor& visitor) override;
};

// 字符字面量节点
class CharLiteral : public Expr {
public:
    char value;

    CharLiteral(char v, int l, int c) : Expr(ASTNodeType::CHAR_LITERAL, l, c), value(v) {}

    void accept(ASTVisitor& visitor) override;
};

// 字符串字面量节点（PRD R1.2）：语义类型为 char*，codegen 落数据段
// value 保留引号内的原文，转义序列原样保留，由汇编器解码
class StringLiteral : public Expr {
public:
    std::string value;

    StringLiteral(const std::string& v, int l, int c)
      : Expr(ASTNodeType::STRING_LITERAL, l, c), value(v) {}

    void accept(ASTVisitor& visitor) override;
};

// NULL 空指针常量节点（PRD R1.2）：语义类型为 Null，可赋给任意指针类型
class NullLiteral : public Expr {
public:
    NullLiteral(int l, int c) : Expr(ASTNodeType::NULL_LITERAL, l, c) {}

    void accept(ASTVisitor& visitor) override;
};

// 下标表达式节点：base[index]，base 为数组或指针表达式
// 既作右值（a[i]）也作左值（a[i] = v，经 AssignExpr.target）
class IndexExpr : public Expr {
public:
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;

    IndexExpr(std::unique_ptr<Expr> b, int l, int c)
      : Expr(ASTNodeType::INDEX_EXPR, l, c), base(std::move(b)) {}

    void accept(ASTVisitor& visitor) override;
};

// 成员访问表达式节点（PRD R1.2 第二批）：p.x（dot）或 p->x（arrow）
// arrow 形态等价 (*p).x；既作右值也作左值（p.x = v，经 AssignExpr.target）
class MemberExpr : public Expr {
public:
    std::unique_ptr<Expr> base;
    std::string member;
    bool arrow = false;

    MemberExpr(std::unique_ptr<Expr> b, const std::string& m, bool isArrow, int l, int c)
      : Expr(ASTNodeType::MEMBER_EXPR, l, c), base(std::move(b)), member(m),
        arrow(isArrow) {}

    void accept(ASTVisitor& visitor) override;
};

// 逐成员初始化器节点（PRD R1.2 第二批）：{ e1, e2, ... }
// 仅允许作为 struct 变量声明的初始化器；不支持嵌套初始化器
class InitListExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> values;

    InitListExpr(int l, int c) : Expr(ASTNodeType::INIT_LIST_EXPR, l, c) {}

    void accept(ASTVisitor& visitor) override;
};

// AST访问者接口
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Program& node) = 0;
    virtual void visit(VarDeclaration& node) = 0;
    virtual void visit(FuncDeclaration& node) = 0;
    virtual void visit(StructDeclaration& node) = 0;
    virtual void visit(TypedefDeclaration& node) = 0;
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
    virtual void visit(StringLiteral& node) = 0;
    virtual void visit(NullLiteral& node) = 0;
    virtual void visit(IndexExpr& node) = 0;
    virtual void visit(MemberExpr& node) = 0;
    virtual void visit(InitListExpr& node) = 0;
    virtual void visit(StmtVarDeclaration& node) = 0;
};

#endif // NCC_AST_H