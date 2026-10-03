#ifndef NCC_IR_H
#define NCC_IR_H

// IR 层数据模型 + AST 降级器 + dump（PRD R1.3）。
//
// 定位：带类型的树/三地址混合表示，非 SSA。覆盖当前全部语言结构（PRD R1.2：
// 标量/指针/一维数组/struct/typedef/字符串/控制流/函数调用），作为 R1.4 后端
// 迁移（codegen 改为 IR → 文本汇编）与后续 C/LLVM 后端的统一消费形态。
//
// 类型表示决策（二选一裁决：独立最小 IrType，不复用 semantic::SemanticType）：
// 1. SemanticAnalyzer 是只读 pass，不回填表达式类型（semantic.hpp 类注释
//    "纯只读 pass：不改写 AST"），SemanticResult 仅含字符串摘要——无论选哪种
//    表示，lower 都必须在遍历中自行重算表达式类型，复用 SemanticType 省不掉
//    任何推导逻辑，只省一个结构体定义；
// 2. IR 要作为三后端的统一输入，自带的 IrType 使 ir.hpp 不反向依赖
//    semantic.hpp，后端只消费 IR 即可（先例：codegen.cpp 同样独立重建类型
//    信息而非消费 semantic 产物）；
// 3. IrType 形态与 SemanticType 同构（kind + tag + element 递归值类型，
//    operator== 深度同一性比较、数组长度不参与），规则变化时两侧同步成本低。
//
// lower 输入契约：已通过 SemanticAnalyzer 的单 Program（多文件 import/export
// 归并出的单编译单元；多模块扩展在命名上留空间，见 lower 注释）。类型层面
// 的非法输入不产生诊断（语义层已报），统一降级为 Kind::Error 抑制级联；
// 仅当 AST 违反解析器产出契约（结构缺失）时按 ca::Result 返回 Err（与
// SemanticAnalyzer::analyze 的契约校验同策略）。
//
// R10-R12 预留挂接点（#45 注释的兑现情况）：
// - R10 defer（已落地）：展开点在 lower——IrBlockStmt/Return/Break/Continue 的
//   所有退出路径尾部逆序插入已注册 defer 语句（"IR 层展开，三后端免费获得"，
//   见 PRD R10）；defer 内 return 的拒绝语义留在语义层。展开后的 IR 只由既有
//   语句形态组成，后端零改动；带 defer 的作用域块以 IrDeferScopeStmt（Kind 仍
//   为 Block）承载，见其注释。
// - R11 match（已落地）：lower 降解为比较+跳转链（If 链）；密集值的跳转表生成
//   属后端优化，不在降解层做（PRD R11）。匹配主体/结果各占一个隐藏局部变量，
//   分支体经结果变量回填——IR 中不存在独立 Match 节点。
// - R12 coro/yield（已落地）：lower 产出含 IrYieldStmt 的 coro 函数 IR
//   （IrFunction::isCoro），文件尾的状态机变换随即降解（PRD R12，决策 A2）：
//   每个 coro 函数变换为「(int __coro_h) 签名的状态机函数」（名字/functions
//   槽位不变），帧 = 每 coro 函数一个 __coro_frame_<name> struct + 全局帧数
//   组/槽计数器；句柄分发给注入的 __coro_resume/__coro_done。变换产物只用
//   既有 IR 构造（If 链/While/Return/struct 成员/数组下标），三后端零改动。
//   硬约束"yield 不得出现在 pending defer 作用域"由语义层裁决（m_deferDepth），
//   变换前对 IrDeferScopeStmt 子树内的 Yield 再做防御性检查。
//
// libca：作用域表用 ca::collection::HashMap（与 SemanticAnalyzer::Scope 一致），
// 有序集合用 std::vector（与 ast.hpp 一致）；契约错误走 ca::Result。

#include "ncc/ast.hpp"

#include <libca/collection/hash_map.hpp>
#include <libca/core/result.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ir {

    // ---------------------------------------------------------------------------
    // IrType：递归值类型（形态与 semantic::SemanticType 同构，见文件头决策注释）
    // - Pointer / Array：element 指向目标/元素类型（语言限制：一级指针、一维数组）
    // - Struct：tag 为结构体标签（匿名 struct 用解析器生成的 __anon_N）
    // - Array 额外携带声明长度 length（后端布局需要；不参与同一性比较，
    //   与 semantic "长度不参与数组类型同一性"的策略一致）
    // ---------------------------------------------------------------------------
    struct IrType {
        enum class Kind { Int, Char, Void, Null, Error, Pointer, Array, Struct };

        Kind kind = Kind::Error;
        std::string tag;                       // 仅 Kind::Struct 有效
        int length = 0;                        // 仅 Kind::Array 有效（>0 为已知长度）
        std::shared_ptr<const IrType> element; // 仅 Pointer/Array 有效

        // 深度同一性比较（length 不参与）
        bool operator==(const IrType& other) const;
        bool operator!=(const IrType& other) const { return !(*this == other); }

        // 可读名：int / char / void / null / <error> / int* / char* / int[10] /
        // struct Point / struct Node*（与 codegen 的规范类型名一致）
        std::string toString() const;

        static const IrType Int;
        static const IrType Char;
        static const IrType Void;
        static const IrType Null;
        static const IrType Error;

        static IrType pointerTo(IrType pointee);
        static IrType arrayOf(IrType elem, int length);
        static IrType structOf(std::string structTag);
    };

    // ---------------------------------------------------------------------------
    // 表达式（全部带类型注记；既作右值也作左值的节点由父节点语境区分）
    // ---------------------------------------------------------------------------
    struct IrExpr {
        enum class Kind {
            IntConst,
            CharConst,
            StringConst,
            NullConst,
            Var,     // 变量引用（数组名保持 Array 类型，退化发生在使用点）
            Unary,   // 一元 - 与 !（取址/解引用独立成节点）
            Binary,  // 算术 + - * / % 与比较 == != < <= > >=（结果 int / 指针算术）
            Logical, // && ||（短路求值，后端需分支实现，独立于 Binary）
            Assign,  // 赋值表达式（嵌套场景）；语句级赋值降为 IrStoreStmt
            Index,   // a[i]
            Member,  // p.x / p->x
            AddrOf,  // &e
            Deref,   // *p
            Call,    // f(args)
            InitList // { e1, e2, ... }（仅 struct 声明初始化器）
        };

        Kind kind;
        IrType type;
        int line = 0;
        int column = 0;

        IrExpr(Kind k, IrType t, int l, int c)
          : kind(k), type(std::move(t)), line(l), column(c) {}
        virtual ~IrExpr() = default;
    };

    struct IrIntConst : IrExpr {
        int value = 0;
        IrIntConst(int v, int l, int c)
          : IrExpr(Kind::IntConst, IrType::Int, l, c), value(v) {}
    };

    struct IrCharConst : IrExpr {
        char value = '\0';
        IrCharConst(char v, int l, int c)
          : IrExpr(Kind::CharConst, IrType::Char, l, c), value(v) {}
    };

    // value 为引号内原文（转义序列原样保留，与 AST StringLiteral 约定一致）
    struct IrStringConst : IrExpr {
        std::string value;
        IrStringConst(std::string v, int l, int c)
          : IrExpr(Kind::StringConst, IrType::pointerTo(IrType::Char), l, c),
            value(std::move(v)) {}
    };

    struct IrNullConst : IrExpr {
        IrNullConst(int l, int c) : IrExpr(Kind::NullConst, IrType::Null, l, c) {}
    };

    struct IrVarRef : IrExpr {
        std::string name;
        IrVarRef(std::string n, IrType t, int l, int c)
          : IrExpr(Kind::Var, std::move(t), l, c), name(std::move(n)) {}
    };

    struct IrUnaryExpr : IrExpr {
        std::string op; // "-" 或 "!"
        std::unique_ptr<IrExpr> operand;
        IrUnaryExpr(std::string o, std::unique_ptr<IrExpr> e, int l, int c)
          : IrExpr(Kind::Unary, IrType::Int, l, c), op(std::move(o)),
            operand(std::move(e)) {}
    };

    struct IrBinaryExpr : IrExpr {
        std::string op; // 源算符拼写：+ - * / % == != < <= > >=
        std::unique_ptr<IrExpr> left;
        std::unique_ptr<IrExpr> right;
        IrBinaryExpr(std::string o,
                     std::unique_ptr<IrExpr> l,
                     std::unique_ptr<IrExpr> r,
                     IrType t,
                     int ln,
                     int c)
          : IrExpr(Kind::Binary, std::move(t), ln, c), op(std::move(o)),
            left(std::move(l)), right(std::move(r)) {}
    };

    struct IrLogicalExpr : IrExpr {
        std::string op; // "&&" 或 "||"
        std::unique_ptr<IrExpr> left;
        std::unique_ptr<IrExpr> right;
        IrLogicalExpr(std::string o,
                      std::unique_ptr<IrExpr> l,
                      std::unique_ptr<IrExpr> r,
                      int ln,
                      int c)
          : IrExpr(Kind::Logical, IrType::Int, ln, c), op(std::move(o)),
            left(std::move(l)), right(std::move(r)) {}
    };

    struct IrAssignExpr : IrExpr {
        std::unique_ptr<IrExpr> target; // 左值：Var / Index / Deref / Member
        std::unique_ptr<IrExpr> value;
        IrAssignExpr(std::unique_ptr<IrExpr> t, std::unique_ptr<IrExpr> v, int l, int c)
          : IrExpr(Kind::Assign, t->type, l, c), target(std::move(t)),
            value(std::move(v)) {}
    };

    struct IrIndexExpr : IrExpr {
        std::unique_ptr<IrExpr> base;
        std::unique_ptr<IrExpr> index;
        IrIndexExpr(
          std::unique_ptr<IrExpr> b, std::unique_ptr<IrExpr> i, IrType t, int l, int c)
          : IrExpr(Kind::Index, std::move(t), l, c), base(std::move(b)),
            index(std::move(i)) {}
    };

    struct IrMemberExpr : IrExpr {
        std::unique_ptr<IrExpr> base;
        std::string member;
        bool arrow = false;
        IrMemberExpr(
          std::unique_ptr<IrExpr> b, std::string m, bool isArrow, IrType t, int l, int c)
          : IrExpr(Kind::Member, std::move(t), l, c), base(std::move(b)),
            member(std::move(m)), arrow(isArrow) {}
    };

    struct IrAddrOfExpr : IrExpr {
        std::unique_ptr<IrExpr> operand;
        IrAddrOfExpr(std::unique_ptr<IrExpr> e, IrType t, int l, int c)
          : IrExpr(Kind::AddrOf, std::move(t), l, c), operand(std::move(e)) {}
    };

    struct IrDerefExpr : IrExpr {
        std::unique_ptr<IrExpr> operand;
        IrDerefExpr(std::unique_ptr<IrExpr> e, IrType t, int l, int c)
          : IrExpr(Kind::Deref, std::move(t), l, c), operand(std::move(e)) {}
    };

    struct IrCallExpr : IrExpr {
        std::string callee;
        std::vector<std::unique_ptr<IrExpr>> arguments;
        IrCallExpr(std::string name, IrType returnType, int l, int c)
          : IrExpr(Kind::Call, std::move(returnType), l, c), callee(std::move(name)) {}
    };

    struct IrInitListExpr : IrExpr {
        std::vector<std::unique_ptr<IrExpr>> values;
        IrInitListExpr(IrType t, int l, int c)
          : IrExpr(Kind::InitList, std::move(t), l, c) {}
    };

    // ---------------------------------------------------------------------------
    // 语句。R10-R12 扩展位见文件头"预留挂接点"。
    // ---------------------------------------------------------------------------
    struct IrStmt {
        enum class Kind {
            Let,   // 局部变量声明（带类型与可选初始化器）
            Store, // 语句级赋值（表达式语境的赋值保留为 IrAssignExpr）
            Eval,  // 表达式语句
            If,
            While,
            For,
            Return,
            Break,
            Continue,
            Block, // 复合语句（独立作用域）
            // ---- 预留扩展位说明（R10/R11 已落地，未新增枚举值）----
            // defer 与 match 在 lower 层全部降解为既有语句形态（退出点插入 /
            // If 链），后端只见既有 Kind；带 defer 注册记录的作用域用
            // IrDeferScopeStmt（Kind::Block 的子类）承载。若后续特性需要
            // 后端可见的新形态，再在此追加枚举值。
            Yield, // R12：协程 yield 点（PRD R12）。仅出现在 coro 函数的
                   // lower 产物中，随即被状态机变换降解为 Store+Return——
                   // 后端不消费本 Kind（见到即内部契约破坏）
        };

        Kind kind;
        int line = 0;
        int column = 0;

        IrStmt(Kind k, int l, int c) : kind(k), line(l), column(c) {}
        virtual ~IrStmt() = default;
    };

    struct IrLetStmt : IrStmt {
        std::string name;
        IrType type;
        std::unique_ptr<IrExpr> init; // 可空
        IrLetStmt(std::string n, IrType t, std::unique_ptr<IrExpr> i, int l, int c)
          : IrStmt(Kind::Let, l, c), name(std::move(n)), type(std::move(t)),
            init(std::move(i)) {}
    };

    struct IrStoreStmt : IrStmt {
        std::unique_ptr<IrExpr> target; // 左值
        std::unique_ptr<IrExpr> value;
        IrStoreStmt(std::unique_ptr<IrExpr> t, std::unique_ptr<IrExpr> v, int l, int c)
          : IrStmt(Kind::Store, l, c), target(std::move(t)), value(std::move(v)) {}
    };

    struct IrEvalStmt : IrStmt {
        std::unique_ptr<IrExpr> expression;
        IrEvalStmt(std::unique_ptr<IrExpr> e, int l, int c)
          : IrStmt(Kind::Eval, l, c), expression(std::move(e)) {}
    };

    struct IrIfStmt : IrStmt {
        std::unique_ptr<IrExpr> condition;
        std::unique_ptr<IrStmt> thenBranch; // Block 或隐式包裹的 Block（单语句分支）
        std::unique_ptr<IrStmt> elseBranch; // 可空
        IrIfStmt(std::unique_ptr<IrExpr> cond,
                 std::unique_ptr<IrStmt> thenS,
                 std::unique_ptr<IrStmt> elseS,
                 int l,
                 int c)
          : IrStmt(Kind::If, l, c), condition(std::move(cond)),
            thenBranch(std::move(thenS)), elseBranch(std::move(elseS)) {}
    };

    struct IrWhileStmt : IrStmt {
        std::unique_ptr<IrExpr> condition;
        std::unique_ptr<IrStmt> body;
        IrWhileStmt(std::unique_ptr<IrExpr> cond, std::unique_ptr<IrStmt> b, int l, int c)
          : IrStmt(Kind::While, l, c), condition(std::move(cond)), body(std::move(b)) {}
    };

    struct IrForStmt : IrStmt {
        std::unique_ptr<IrStmt> init;      // 可空（声明或赋值，挂在 for 自身作用域）
        std::unique_ptr<IrExpr> condition; // 可空
        std::unique_ptr<IrExpr> step;      // 可空
        std::unique_ptr<IrStmt> body;
        IrForStmt(std::unique_ptr<IrStmt> i,
                  std::unique_ptr<IrExpr> cond,
                  std::unique_ptr<IrExpr> s,
                  std::unique_ptr<IrStmt> b,
                  int l,
                  int c)
          : IrStmt(Kind::For, l, c), init(std::move(i)), condition(std::move(cond)),
            step(std::move(s)), body(std::move(b)) {}
    };

    struct IrReturnStmt : IrStmt {
        std::unique_ptr<IrExpr> value; // 可空（void 函数）
        IrReturnStmt(std::unique_ptr<IrExpr> v, int l, int c)
          : IrStmt(Kind::Return, l, c), value(std::move(v)) {}
    };

    struct IrBreakStmt : IrStmt {
        IrBreakStmt(int l, int c) : IrStmt(Kind::Break, l, c) {}
    };

    struct IrContinueStmt : IrStmt {
        IrContinueStmt(int l, int c) : IrStmt(Kind::Continue, l, c) {}
    };

    struct IrBlockStmt : IrStmt {
        std::vector<std::unique_ptr<IrStmt>> statements;
        IrBlockStmt(int l, int c) : IrStmt(Kind::Block, l, c) {}
    };

    // R10 defer：带 defer 注册记录的作用域块（coro 挂接点，PRD R12 前置）。
    // 仅当作用域内注册了 defer 时，lower 用本节点替换普通 IrBlockStmt：
    // - 语义展开已就地完成（所有退出点逆序插入注册的退出动作），后端按
    //   Kind::Block 处理即可——子类与基类布局兼容，零改动；
    // - defers 保存注册序的退出动作（与展开所用语句同构，未逆序），供 R12
    //   coro 变换检测硬约束"yield 不得出现在 pending defer 作用域"：凡
    //   yield 出现在非空 defers 的本节点子树内即违规。
    struct IrDeferScopeStmt : IrBlockStmt {
        std::vector<std::unique_ptr<IrStmt>> defers; // 注册序（未逆序）
        IrDeferScopeStmt(int l, int c) : IrBlockStmt(l, c) {}
    };

    // R12 coro：yield 点（PRD R12）。lower 产出、状态机变换消费的中间形态：
    // - 语义层保证仅出现在 coro 函数体内且不在 pending defer 作用域内；
    // - 变换降解为「帧 __state 写恢复点 + return 产出值」（决策 A2 状态机），
    //   变换后 IR 不再含本节点，后端零改动
    struct IrYieldStmt : IrStmt {
        std::unique_ptr<IrExpr> value; // 产出值（coro 返回类型一期限定 int）
        IrYieldStmt(std::unique_ptr<IrExpr> v, int l, int c)
          : IrStmt(Kind::Yield, l, c), value(std::move(v)) {}
    };

    // ---------------------------------------------------------------------------
    // 模块级实体
    // ---------------------------------------------------------------------------
    struct IrParam {
        std::string name;
        IrType type;
    };

    struct IrLocal {
        std::string name;
        IrType type;
    };

    // struct 布局的最小记录（成员类型；偏移/大小由后端按 4 字节对齐无填充规则
    // 自行计算，semantic::StructInfo 仍是布局权威）
    struct IrField {
        std::string name;
        IrType type;
    };

    struct IrStructDef {
        std::string tag;
        std::vector<IrField> fields;
        bool complete = false; // 前向声明 incomplete
    };

    struct IrTypedef {
        std::string alias;
        IrType type;
    };

    struct IrGlobal {
        std::string name;
        IrType type;
        std::unique_ptr<IrExpr> init; // 可空；struct 逐成员初始化为 IrInitListExpr
    };

    struct IrFunction {
        std::string name;
        IrType returnType;
        std::vector<IrParam> params;
        std::vector<IrLocal> locals; // 函数体内声明的局部变量（按声明顺序，含嵌套块）
        std::unique_ptr<IrBlockStmt> body;
        // coro 函数标志（PRD R12）：lower 产物的 body 可能含 IrYieldStmt；
        // 状态机变换后 body 只由既有语句形态组成，标志保留供 dump 识别。
        // 变换会把签名改写为 (int __coro_h)（句柄 = 帧槽编码），名字与
        // functions 槽位不变（LinkageTable 位置对齐约定，见 codegen.hpp）
        bool isCoro = false;
    };

    // extern 声明（PRD R3）：宿主提供的 C 函数签名。不进入 functions（无函数
    // 体、不发射定义），后端据其发射外部调用：NAS 侧 `extern 符号` 指令 +
    // varargs 的 cdecl 调用序列；C 侧带签名原型
    struct IrExternDecl {
        std::string name;
        IrType returnType;
        std::vector<IrParam> params;
        bool isVariadic = false;
    };

    // ---------------------------------------------------------------------------
    // IR 模块 + 降级器 + dump
    // ---------------------------------------------------------------------------
    struct Module {
        std::vector<IrStructDef> structs;                   // 声明序
        std::vector<IrTypedef> typedefs;                    // 声明序
        std::vector<IrGlobal> globals;                      // 声明序（"先声明后可见"）
        std::vector<std::unique_ptr<IrFunction>> functions; // 声明序
        std::vector<IrExternDecl> externs; // extern 声明序（PRD R3，按名去重）

        // 文本输出：缩进分层，每个表达式带类型注记（dump 样例见 test_ir.cpp 黄金）
        std::string dump() const;
    };

    // AST → IR 降级器。输入契约与错误策略见文件头注释。
    // 命名留空间：多模块（R2a/R7）将来可加 lowerMulti(const std::vector<const
    // Program*>&)，本接口只处理单 Program 输入。
    ca::Result<Module, std::string> lower(const Program& program);

} // namespace ir

#endif // NCC_IR_H
