#ifndef NCC_PARSER_H
#define NCC_PARSER_H

#include "ncc/ast.hpp"
#include "ncc/lexer.hpp"
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// 解析错误（PRD R1.1/R2a）：携带位置供装载器转成结构化诊断。
// what() 文案：无文件名时保持既有格式（"Parse error at line L, column C: ..."，
// 既有测试依赖）；有文件名时为 "file:line:column: error: ..."（与语义诊断格式一致）
struct ParseError : std::runtime_error {
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;

    ParseError(std::string fileName, int l, int c, std::string msg)
      : std::runtime_error(format(std::move(fileName), l, c, msg)),
        file(std::move(fileName)), line(l), column(c), message(std::move(msg)) {}

private:
    static std::string
    format(const std::string& fileName, int l, int c, const std::string& msg) {
        if (fileName.empty()) {
            return "Parse error at line " + std::to_string(l) + ", column "
                   + std::to_string(c) + ": " + msg;
        }
        return fileName + ':' + std::to_string(l) + ':' + std::to_string(c)
               + ": error: " + msg;
    }
};

class Parser {
public:
    // fileName 可选：非空时解析错误带文件前缀（多文件装载用），诊断可定位到文件
    // externalTypedefNames：单元级已知 typedef 别名（PRD R9 装载器线程：头文件
    // 先于包含者解析，其别名注入后续文件，跨文件 `PointT p;` 才能按类型解析）
    Parser(const std::vector<Token>& tokens,
           std::string fileName = "",
           const std::set<std::string>& externalTypedefNames = {});

    // 解析程序
    std::unique_ptr<Program> parse();

    // 头文件模式（PRD R9）：接受 C 声明子集扩展——函数原型（无函数体）、
    // 限定符/修饰符链（const/volatile/static/inline/register/unsigned/signed/
    // long/short，语义忽略）、(void) 空参表、数组形参退化。语言本体（.nc）
    // 不受影响（默认关闭）
    void setHeaderMode(bool headerMode) { m_headerMode = headerMode; }

private:
    std::vector<Token> m_tokens;
    size_t m_pos;
    std::string m_fileName;               // 可选文件名（诊断前缀）
    int m_anonCounter = 0;                // 匿名 struct 内部标签计数（__anon_N）
    std::set<std::string> m_typedefNames; // 已解析的 typedef 别名（文件作用域）
    bool m_headerMode = false;            // 头文件模式（PRD R9 声明子集扩展）

    // 辅助函数
    Token currentToken() const;
    Token peekToken() const;
    void advance();
    bool match(NTokenKind kind);
    bool expect(NTokenKind kind);
    // 是否已登记的 typedef 别名（构造时对 token 预扫描收集，供语句分发消歧）
    bool isTypedefName(const std::string& name) const;

    // 解析函数
    // isExported：由 `export` 前缀传入，只修饰顶层函数与全局变量（PRD R2a）
    std::unique_ptr<Decl> parseDeclaration(bool isExported = false);
    // 文件顶部 import 指令：`import math;` / `import "util/helpers.nc";`
    ImportDirective parseImportDirective();
    std::unique_ptr<VarDeclaration> parseVarDeclaration();
    std::unique_ptr<FuncDeclaration> parseFuncDeclaration();
    // extern 声明（PRD R3）：`extern int puts(char* s);`，仅限文件作用域；
    // 函数体位置必须是 ';'，参数表尾部可带 ...
    std::unique_ptr<FuncDeclaration> parseExternDeclaration();
    // 头文件函数原型（PRD R9）：`int add(int a, int b);`，与 extern 声明同构
    // （isPrototype 标记、无函数体）；支持 `(void)` 空参表、无名形参、数组形参
    // 退化（C 语义）、varargs `...`
    std::unique_ptr<FuncDeclaration> parsePrototypeDeclaration();
    std::unique_ptr<Decl> parseTypedefDeclaration();
    std::unique_ptr<StructDeclaration>
    parseStructBody(const std::string& tag, int line, int column);
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

    // ---- R9 头文件模式辅助 ----
    // 消费 C 限定符/修饰符链（const/volatile/static/inline/register 与
    // unsigned/signed/long/short，语义忽略——决策记录见实现注释）。返回是否
    // 消费了 unsigned/signed/long/short（裸修饰符按 int 解释）
    bool skipHeaderQualifiers();
    // 当前 token 是否开始一个类型（builtin/struct/typedef 别名）
    bool isTypeStart() const;
    // 头文件原型形态判定：从 '(' 起扫描 `(...)` 是否后随 ';'（仅词法形态）
    bool isPrototypeForm(std::size_t lparenPos) const;

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

    // 错误处理：抛 ParseError（位置取当前 token）
    void error(const std::string& message);
    // 错误处理：抛 ParseError（位置取指定 token，用于 export 等前置上下文）
    void errorAt(const Token& token, const std::string& message);
};

#endif // NCC_PARSER_H