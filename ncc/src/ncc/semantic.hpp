#ifndef NCC_SEMANTIC_H
#define NCC_SEMANTIC_H

#include "ncc/ast.hpp"

#include <libca/collection/array_list.hpp>
#include <libca/collection/hash_map.hpp>
#include <libca/core/result.hpp>

#include <string>
#include <vector>

// 诊断严重级别
enum class DiagnosticSeverity { Error, Warning };

// 一条诊断信息：位置 + 严重级别 + 消息。
// 文本形式为 "file:line:col: error: message"（行/列均从 1 开始，与 Lexer 一致）。
struct Diagnostic {
    std::string file;
    int line = 0;
    int column = 0;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;

    // 渲染为 "file:line:col: error: message"
    std::string toString() const;
};

// 符号类别。变量（含参数）与函数在同一作用域内分开登记，但共享命名空间：
// 同一作用域内先登记的名字会与后登记的同名变量/函数冲突（报 redefinition）。
enum class SymbolKind { Variable, Parameter, Function };

// 语义类型。当前语言只有 int/char 两个值类型，void 仅作函数返回类型；
// Error 是"毒类型"：前序错误已报告时用它占位，避免同一根源产生级联报错。
enum class SemanticType { Int, Char, Void, Error };

// 符号表条目
struct Symbol {
    SymbolKind kind = SymbolKind::Variable;
    std::string name;
    SemanticType type = SemanticType::Error;
    int line = 0;
    int column = 0;
    // 仅函数使用：参数类型序列
    ca::collection::ArrayList<SemanticType> paramTypes;
};

// 全局符号摘要（analyze 结果的一部分，供调用方与测试核对符号表内容）。
// 按登记顺序排列：函数先于全局变量（两遍分析，函数签名先统一登记）。
struct SymbolSummary {
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    std::string type;                                  // 变量类型或函数返回类型的可读名
    ca::collection::ArrayList<std::string> paramTypes; // 仅函数：参数类型可读名
    int line = 0;
    int column = 0;
};

// 语义分析结果：全部诊断 + 全局符号表摘要
struct SemanticResult {
    ca::collection::ArrayList<Diagnostic> diagnostics;
    ca::collection::ArrayList<SymbolSummary> globals;

    bool hasErrors() const;
    ca::usize errorCount() const;
};

// 语义分析器（PRD R1.1）。纯只读 pass：不改写 AST，不接线 main。
//
// 作用域规则：
// - 作用域栈：全局 / 函数 / 块作用域。函数体最外层块与参数共用一个作用域
//   （参数与顶层局部变量同名即 redefinition）；嵌套块、if/while 单语句分支、
//   for 整体各自有独立作用域。
// - 变量与函数在同一作用域内分开登记（两张表）但共享命名空间，同名冲突报
//   redefinition；跨作用域允许遮蔽（内层变量可遮蔽外层变量/全局函数，出块后
//   外层名字恢复可见）。
// - 全局变量按声明顺序可见（先声明后使用）；函数签名统一先登记，因此允许
//   前向调用与相互递归。
//
// 类型提升规则（详见 checkConversion）：
// - char 在需要 int 的场合（算术/关系/逻辑运算、int 目标的赋值/初始化/传参/
//   返回）隐式提升为 int（加宽，无损）。
// - int 到 char 是窄化转换：语言没有显式转换语法，一律拒绝并报错。
// - 算术运算结果为 int；比较与逻辑运算结果为 int（0/1）；一元 - 与 ! 的
//   结果为 int（char 操作数先提升）。
// - void 只能作函数返回类型；void 值（void 函数调用的结果）不允许出现在
//   任何需要值的位置。
//
// 诊断策略：一次 analyze 收集全部诊断而非首错即停；已报错的子表达式用
// Error 毒类型抑制级联。
class SemanticAnalyzer {
public:
    // fileName 作为诊断前缀；默认占位符便于单测独立使用
    explicit SemanticAnalyzer(std::string fileName = "<input>");

    // 对整棵 AST 做语义分析。正常情况返回 Ok(结果)：诊断（可能为空）与全局
    // 符号摘要；仅当 AST 违反解析器产出契约（如函数体为空指针）时返回 Err。
    ca::Result<SemanticResult, std::string> analyze(const Program& program);

private:
    // 作用域：变量与函数分开登记
    struct Scope {
        ca::collection::HashMap<std::string, Symbol> variables;
        ca::collection::HashMap<std::string, Symbol> functions;
    };

    // ---- 符号表辅助 ----
    Scope& currentScope();
    void pushScope();
    void popScope();
    // 由内向外逐层查找；每层先查变量表再查函数表（同一层内二者不会同名）
    const Symbol* lookupSymbol(const std::string& name) const;

    // ---- 声明登记 ----
    // 当前作用域登记变量；同名冲突（与变量或函数）报 redefinition 并返回 false
    bool declareVariable(const Symbol& symbol);
    bool declareFunction(const Symbol& symbol);
    void appendGlobalSummary(const Symbol& symbol);

    void registerFunctionSignature(const FuncDeclaration& decl);
    void checkGlobalVariable(const VarDeclaration& decl);
    void checkFunctionBody(const FuncDeclaration& decl);
    void checkLocalVariable(const StmtVarDeclaration& decl);

    // ---- 语句/表达式检查 ----
    void checkStmt(const Stmt& stmt);
    // 单条语句作为分支/循环体时，若是变量声明则包一层临时作用域，
    // 避免声明泄漏到外层（如 `if (c) int x = 1;` 之后 x 不可见）
    void checkNestedStmt(const Stmt& stmt);
    void checkCompound(const CompoundStmt& stmt);
    void checkIf(const IfStmt& stmt);
    void checkWhile(const WhileStmt& stmt);
    void checkFor(const ForStmt& stmt);
    void checkReturn(const ReturnStmt& stmt);
    void checkBreak(const BreakStmt& stmt);
    void checkContinue(const ContinueStmt& stmt);

    SemanticType checkExpr(const Expr& expr);
    SemanticType checkIdentifier(const IdentifierExpr& expr);
    SemanticType checkAssign(const AssignExpr& expr);
    SemanticType checkBinary(const BinaryExpr& expr);
    SemanticType checkUnary(const UnaryExpr& expr);
    SemanticType checkCall(const CallExpr& expr);

    // 条件上下文（if/while/for 条件）：int/char 均可（非零为真），void 报错
    void checkCondition(const Expr& expr);

    // 检查 from 是否可隐式转换（含提升）为 to，不可则报错。
    // context 描述使用场景，如 "initialization of 'c'" / "return statement"。
    void checkConversion(SemanticType from,
                         SemanticType to,
                         int line,
                         int column,
                         const std::string& context);

    // ---- return 覆盖检查（保守可达性策略） ----
    // - return 语句：必然返回
    // - 复合语句：最后一个语句必然返回（语句按顺序执行）
    // - if：当且仅当带 else 且两个分支都必然返回
    // - 循环不提供必然返回保证（条件可能一次都不满足）；break/continue/
    //   表达式语句等同样不提供。
    // 该策略宁可漏报（放过可能缺 return 的程序）也不误报，符合"保守"要求。
    bool definitelyReturns(const Stmt& stmt) const;

    // ---- 工具 ----
    void reportError(int line, int column, const std::string& message);
    static SemanticType typeFromName(const std::string& name);
    static std::string typeName(SemanticType type);
    static bool isScalar(SemanticType type); // int 或 char

    std::string m_fileName;
    std::vector<Scope> m_scopes;
    SemanticResult m_result;
    // 当前正在检查的函数（用于 return 检查）；nullptr 表示不在函数体内
    const FuncDeclaration* m_currentFunction;
    int m_loopDepth;
};

#endif // NCC_SEMANTIC_H
