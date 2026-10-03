#ifndef NCC_SEMANTIC_H
#define NCC_SEMANTIC_H

#include "ncc/ast.hpp"

#include <libca/collection/array_list.hpp>
#include <libca/collection/hash_map.hpp>
#include <libca/core/result.hpp>

#include <map>
#include <memory>
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

// 语义类型（PRD R1.2，第二批重构为递归值类型）。
//
// R1.2 第一批用扁平枚举（Int/IntPtr/IntArray...）；第二批引入 struct 后，
// 指针/数组必须携带目标类型的同一性（struct Node* 与 struct Rect* 互不相容、
// struct 数组元素需要布局），故改为 kind + tag + element 的递归值类型：
// - Kind::Pointer / Kind::Array：element 指向目标/元素类型（一级指针与一维数组，
//   多级/多维由语义显式拒绝；元素可为标量、struct——不支持指针/数组元素）
// - Kind::Struct：tag 为结构体标签（匿名 struct 用解析器生成的 __anon_N），
//   布局（成员偏移/大小）查 SemanticAnalyzer::m_structs
// - 静态常量保留第一批的名字（Int/IntPtr/IntArray...），既有比较代码不变；
//   operator== 做深度同一性比较（长度不参与数组类型同一性）
struct SemanticType {
    enum class Kind { Int, Char, Void, Error, Null, Pointer, Array, Struct };

    Kind kind = Kind::Error;
    std::string tag;                             // 仅 Kind::Struct 有效
    std::shared_ptr<const SemanticType> element; // 仅 Pointer/Array 有效

    SemanticType() = default;
    SemanticType(Kind k, std::string t, std::shared_ptr<const SemanticType> e)
      : kind(k), tag(std::move(t)), element(std::move(e)) {}

    bool operator==(const SemanticType& other) const;
    bool operator!=(const SemanticType& other) const { return !(*this == other); }

    static const SemanticType Int;
    static const SemanticType Char;
    static const SemanticType Void;
    static const SemanticType Error;
    static const SemanticType Null;
    static const SemanticType IntPtr;
    static const SemanticType CharPtr;
    static const SemanticType IntArray;
    static const SemanticType CharArray;

    // 构造工具
    static SemanticType structOf(std::string structTag);
    static SemanticType pointerTo(SemanticType pointee);
    static SemanticType arrayOf(SemanticType elem);
};

// struct 成员布局条目（PRD R1.2 第二批）。布局/对齐决策：
// 全部成员按 4 字节对齐、无内部填充——标量/指针 4 字节、数组 n×4 字节、
// 嵌套 struct 大小归纳为 4 的倍数，因此偏移 = 前序成员大小之和，
// struct 总大小也天然是 4 的倍数（VM 全字访问，无更细对齐需求）。
struct StructField {
    std::string name;
    SemanticType type;
    int offset = 0; // 字节偏移（4 的倍数）
    int size = 0;   // 字节大小（4 的倍数）
    int line = 0;
    int column = 0;
};

// struct 布局表条目：tag → 成员序列 + 总大小；complete=false 为前向声明
struct StructInfo {
    std::string tag;
    std::vector<StructField> fields;
    int size = 0; // 字节大小（4 的倍数；incomplete 时为 0）
    bool complete = false;
    int line = 0;
    int column = 0;
};

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

// 语义分析器（PRD R1.1/R1.2）。纯只读 pass：不改写 AST，不接线 main。
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
// struct 与 typedef（PRD R1.2 第二批）的命名空间决策：
// - struct 标签与 typedef 名合并进同一个"类型命名空间"，与变量/函数命名空间
//   相互独立：`typedef int T; int T;` 合法（类型名与值名互不冲突），
//   `struct P {}; typedef int P;` 非法（类型命名空间内冲突）。
// - 类型名只允许文件作用域声明；整个编译单元内可见（登记于第一遍，不强制
//   文本先序），前向声明 `struct Node;` 登记为 incomplete，供自引用指针使用。
// - typedef 不可重定义；struct 标签不可重复完整定义（对已 complete 的标签
//   再前向声明幂等合法）。
//
// struct 值语义：整体赋值/初始化/传参/返回均逐字拷贝（不同于数组的不可拷贝）。
//
// 类型提升规则（详见 checkConversion）：
// - char 在需要 int 的场合（算术/关系/逻辑运算、int 目标的赋值/初始化/传参/
//   返回）隐式提升为 int（加宽，无损）。
// - int 到 char 是窄化转换：语言没有显式转换语法，一律拒绝并报错。
// - 算术运算结果为 int；比较与逻辑运算结果为 int（0/1）；一元 - 与 ! 的
//   结果为 int（char 操作数先提升）。
// - 数组名在赋值/初始化/传参/返回/比较场合退化为 pointer-to-T（decay）。
// - NULL（Null 类型）可赋给任意指针类型、与任意指针比较；不得转为标量。
// - char* 与 int*、struct A* 与 struct B* 互不相容；指针不得与标量互转；
//   struct 值不得与标量/指针互转，不同 struct 类型互不相容。
// - void 只能作函数返回类型；void 值（void 函数调用的结果）不允许出现在
//   任何需要值的位置。
//
// 已知限制（本里程碑显式拒绝而非静默错译）：
// - char* 不得解引用/下标：字符串字面量按字节打包落数据段（宿主 strlen 等
//   按字节读），而 VM 无字节级 LOAD；char 数组元素仍按 4 字节槽存放。
// - 多级指针（int**）、多维数组（int a[2][3]）、指针数组（int* a[3]）不支持。
// - struct 定义/typedef 只允许文件作用域；成员初始化器与嵌套初始化器不支持
//   （逐成员扁平初始化器 { e1, e2, ... } 仅用于 struct 变量，长度须与成员数
//   一致）。
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

    // ---- struct / typedef 登记（PRD R1.2 第二批） ----
    // struct 定义：校验成员（未知类型/void/incomplete 值成员/重复成员名），
    // 计算 4 字节对齐布局；前向声明登记 incomplete 标签
    void registerStructDeclaration(const StructDeclaration& decl);
    // typedef：可携带内联 struct 定义；别名登记进类型命名空间（不可重定义）
    void registerTypedefDeclaration(const TypedefDeclaration& decl);

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
    SemanticType checkAddressOf(const UnaryExpr& expr);   // &x
    SemanticType checkDereference(const UnaryExpr& expr); // *p
    SemanticType checkCall(const CallExpr& expr);
    SemanticType checkIndex(const IndexExpr& expr);
    SemanticType checkMember(const MemberExpr& expr); // p.x / p->x

    // 声明初始化器：普通表达式走 checkConversion；{ ... } 仅限 struct 变量
    // （逐成员扁平、长度与成员数一致），数组/标量目标的初始化列表报错
    void checkInitializer(const Expr& initializer,
                          const SemanticType& declared,
                          const std::string& declName,
                          int line,
                          int column);

    // 条件上下文（if/while/for 条件）：标量/指针/NULL 均可（非零为真），
    // void 与 struct 值报错
    void checkCondition(const Expr& expr);

    // 检查 from 是否可隐式转换（含提升/退化）为 to，不可则报错。
    // context 描述使用场景，如 "initialization of 'c'" / "return statement"。
    void checkConversion(SemanticType from,
                         SemanticType to,
                         int line,
                         int column,
                         const std::string& context);

    // ---- 类型工具 ----
    // 从声明的 类型名+指针层级+数组标记 计算语义类型；不合法组合（未知类型/
    // 多级指针/多维数组/指针数组/越界长度/void*/void[]）报错并返回 Error
    SemanticType declaredType(const std::string& baseName,
                              bool isStructTag,
                              int pointerDepth,
                              bool isArray,
                              int arraySize,
                              int arrayDims,
                              int line,
                              int column,
                              bool report);
    static SemanticType pointerTo(SemanticType t); // T → T*
    static SemanticType arrayOf(SemanticType t);   // T → T[]
    static SemanticType decayed(SemanticType t);   // T[] → T*，其余原样
    static bool isPointer(SemanticType type);
    static bool isArrayType(SemanticType type);
    static bool isLValueExpr(const Expr& expr); // x / a[i] / *p / p.x / p->x

    // ---- struct 布局查询 ----
    const StructInfo* lookupStruct(const std::string& tag) const;
    // struct 值类型的字节大小（非 struct/指针返回 4）；incomplete 返回 0
    int typeSize(const SemanticType& type) const;
    bool isCompleteStruct(const std::string& tag) const;

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
    static std::string typeName(const SemanticType& type);
    static bool isScalar(const SemanticType& type); // int 或 char
    std::string m_fileName;
    std::vector<Scope> m_scopes;
    SemanticResult m_result;
    // struct 布局表与 typedef 表：文件作用域单一类型命名空间（决策见类注释）
    std::map<std::string, StructInfo> m_structs;
    std::map<std::string, SemanticType> m_typedefs;
    // 当前正在检查的函数（用于 return 检查）；nullptr 表示不在函数体内
    const FuncDeclaration* m_currentFunction;
    int m_loopDepth;
};

#endif // NCC_SEMANTIC_H
