#ifndef NCC_CODEGEN_H
#define NCC_CODEGEN_H

#include "ncc/ast.hpp"
#include "ncc/ir.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

// 顶层符号链接信息（多文件标号 mangle 所需）。
//
// IR 缺口绕过说明：ir::Module 的 IrFunction/IrGlobal 未建模 Decl::sourceFile /
// isExported（lower 丢弃了这两项），而 PRD R2a 的私有顶层符号 mangle
// （".f_<stem>_<name>"）与跨文件调用解析必须依赖它们。本期不改 ir.*（接口
// 只读约定），以位置对齐的 side-table 绕过：buildLinkageTable 按声明序遍历
// AST，与 ir::lower 第二遍的 functions/globals 导出顺序严格一一对应
// （两者同序遍历 program.declarations 并按同类过滤），main.cpp 在 lower 后
// 填充并传入 generate。多文件扩展落地时应把链接信息提升为 Module 的一等
// 字段，届时删除本 side-table。
struct LinkageEntry {
    std::string name; // 展示名（与 IR 节点名字一致，冗余携带便于核对）
    std::string file; // 定义所在文件（空 = 单文件模式）
    bool isExported = false;
};

struct LinkageTable {
    std::vector<LinkageEntry> functions; // 与 ir::Module::functions 按下标对齐
    std::vector<LinkageEntry> globals;   // 与 ir::Module::globals 按下标对齐
};

// 按声明序收集顶层函数/全局变量的链接信息（与 ir::lower 的导出顺序对齐，
// 见 LinkageEntry 注释）。AST 是唯一仍携带 sourceFile/isExported 的结构。
LinkageTable buildLinkageTable(const Program& program);

// NCI v2.1 代码生成器：消费带类型 IR（ir::Module），产出 nas 可汇编的
// v2.1 汇编文本（PRD R1.4 后端迁移：发射逻辑自 AST 直发改为遍历 IR 节点，
// 机器约定与 R1.3 前的实现逐条保持）。
//
// 目标机器约定（doc/Bytecode Format Specification v2.1.md §3/§4.2）：
// - R4=SP、R5=BP；R0-R3 为参数/返回值寄存器（fastcall 前 4 个参数）
// - 函数序言 enter <locals>、尾声 leave + ret；返回值统一在 R0
// - 调用约定：前 4 个参数走 R0-R3，第 5 个起压栈（调用者求值后 addi R4 清栈）
// - 条件跳转仅 JZ/JNZ/JN/JP，flags 由 CMP/CMPI/TEST 置位（bit0=Z bit1=N bit2=P）
// - main 顶层 leave/ret 由 VM 栈底哨兵终止执行，无需 trap/exit
//
// struct 约定（与 semantic 的布局规则一致：4 字节对齐、无填充）：
// - struct 值表达式的求值结果 = 对象地址压栈（左值：槽地址；右值：sret 临时地址）
// - 传参：struct 实参逐字拷贝到调用者帧内临时槽，副本地址作为 fastcall 实参；
//   被调函数中 struct 形参槽位存地址（isStructParam），成员访问经间接寻址
// - 返回：sret——调用者分配接收槽并经 R7 传入地址（mov R7, R5 + subi R7, off）；
//   返回 struct 的函数在序言把 R7 溢出到专用帧槽（防嵌套 struct 调用覆盖），
//   return 时把返回值逐字拷到 [R7]
// - struct 临时区（实参副本/接收槽）在函数帧尾部，codegen 静态统计并在越界时
//   抛异常防御
//
// 类型信息来源：IR 节点自带 IrType（lower 已完成推导），codegen 仅将其映射为
// 规范类型名字符串（typeName）驱动既有发射判定，不再自行从 AST 推导。
class CodeGenerator {
public:
    CodeGenerator();

    // 生成汇编代码：输入 IR 模块；linkage 提供顶层符号的文件/导出信息
    // （多文件 mangle 必需，见 LinkageEntry 注释；nullptr = 单文件模式）。
    // 发射错误（内部契约破坏）抛 std::runtime_error。
    std::string generate(const ir::Module& module, const LinkageTable* linkage = nullptr);

    // 兼容入口：AST → ir::lower → IR 发射（一条管线，无独立 AST 路径）。
    // 供尚未接入 semantic/lower 的调用方（golden e2e、multifile 等回归测试与
    // compile_examples）继续以 AST 为入口，实际发射全部经 IR。
    std::string generate(Program& program);

private:
    // 符号类别：局部 / 栈参（>4 的参数在调用者栈上）/ 全局（数据段标号）
    enum class SymKind { Local, StackArg, Global };

    struct Symbol {
        SymKind kind = SymKind::Local;
        int slot = 0;      // Local：帧槽位（1 起，地址 = BP - 4*slot）
        int argIndex = 0;  // StackArg：参数序号（1 起，地址 = BP + 4*(argIndex-3)）
        std::string label; // Global：数据段标号
        std::string type;  // 规范类型名：int/char/int*/char*/int[]/char[]/
                           // struct T / struct T* / struct T[]
        bool isArray = false;
        int arraySize = 0;          // 数组元素数（isArray 时有效）
        bool isStructParam = false; // struct 形参：槽位存副本地址（type 为 struct T*）
    };

    // struct 布局（规则与 semantic 一致：按声明顺序累加、4 字节对齐、无填充；
    // 偏移/大小均以字为单位）
    struct FieldLayout {
        std::string name;
        int offsetWords = 0;
        int sizeWords = 0;
        std::string type; // 规范类型名
    };
    struct StructLayout {
        std::vector<FieldLayout> fields;
        int sizeWords = 0;
        bool complete = false;
    };

    struct GlobalInit {
        std::string label;      // 目标数据标号
        int offsetWords = 0;    // struct 逐成员初始化的字偏移（标量为 0）
        const ir::IrExpr* expr; // 初始化表达式（借用 IR 所有权）
    };

    // 顶层函数条目（PRD R2a 多文件）：展示名 → 候选列表（跨文件私有同名
    // 函数共存）。调用点解析与语义同规则：当前文件定义优先，其次导出定义。
    struct FunctionEntry {
        std::string file;       // 定义所在文件（空 = 单文件模式）
        std::string label;      // 汇编标号（main/导出名不变；私有 .f_<stem>_<name>）
        std::string returnType; // 规范返回类型名
        bool isExported = false;
    };

    // 输出缓冲：函数体与数据段分开收集，最后统一拼装
    // （extern 指令在函数体生成期间才收集完毕，需插在文件头）
    std::string m_code;
    std::string m_data;
    std::string* m_sink = nullptr;

    int m_labelCounter = 0;
    std::vector<std::string> m_breakLabels;
    std::vector<std::string> m_continueLabels;

    // 符号表：函数条目（名字 → 候选）+ 全局/局部变量（局部优先于全局）。
    // m_globalSymbols 的键 = 存储键：导出符号与单文件模式用展示名，未导出
    // 顶层变量用 "文件\x01名字"（跨文件私有同名共存，PRD R2a）
    std::map<std::string, std::vector<FunctionEntry>> m_functionTable;
    std::map<std::string, Symbol> m_globalSymbols;
    std::map<std::string, Symbol> m_localSymbols;
    std::vector<std::string> m_globalOrder; // 数据段发射顺序（存储键）
    int m_nextSlot = 0;                     // 当前函数已分配的最大槽位
    std::string m_currentFile;              // 当前生成函数的定义文件（解析私有标号用）

    // 文件 stem → 汇编标号前缀（mangle 用）：同 stem 的不同文件追加 _2/_3...
    std::map<std::string, std::string> m_modulePrefixes;
    std::set<std::string> m_usedPrefixes;

    // struct 布局表（自 IR 的 IrStructDef 重建；IrType 已完成 typedef 展开）
    std::map<std::string, StructLayout> m_structs;

    std::vector<GlobalInit> m_globalInits; // 进入 main 后统一执行
    std::vector<std::string> m_externs;    // 未定义的被调函数 → 宿主符号（callx）
    std::set<std::string> m_externSet;
    std::map<std::string, std::string> m_stringLiterals; // 字面量内容 → 数据标号（去重）

    // 当前函数的 struct 返回信息（sret 方案）
    std::string m_structReturnTag; // 非空 = 当前函数返回 struct，值为标签
    int m_sretSaveSlot = 0;        // R7 溢出槽（struct 返回函数专用）
    int m_tempCursor = 0;          // struct 临时区游标（帧槽位）
    int m_tempLimit = 0;           // 临时区上界（enter 大小依据，越界抛异常）

    // 输出
    void emit(const std::string& code);
    void emitLabel(const std::string& label);
    std::string newLabel();

    // 符号
    const Symbol* findSymbol(const std::string& name) const;
    void registerGlobal(const ir::IrGlobal& global, const LinkageEntry& link);
    void emitGlobalInits();

    // ---- 顶层符号标号（PRD R2a mangle 决策）----
    // main 恒为 "main"；导出符号用原名（函数 = 名字，全局变量 = ".g_" + 名字，
    // 与既有单文件产物一致）；未导出的顶层符号加文件 stem 前缀
    // （".f_<stem>_<name>"），同名私有符号跨文件隔离。
    // 单文件模式（file 为空）完全不 mangle，产物与既有基线逐字节一致。
    std::string functionLabel(const std::string& name, const LinkageEntry& link);
    std::string modulePrefix(const std::string& file);
    // 调用点函数解析：当前文件定义优先，其次导出定义；未命中 = 宿主外部符号
    const FunctionEntry* resolveFunction(const std::string& name) const;
    // 全局变量的存储键（跨文件私有同名共存）
    static std::string
    globalKey(const std::string& name, const std::string& file, bool isExported);

    // ---- IR 类型 → 规范类型名 ----
    // IrType → 规范名（int/char/void/null/<error>/struct T/T*/T[]；数组不带
    // 长度，与既有发射判定兼容；typedef 已由 lower 展开为 IrType）
    static std::string typeName(const ir::IrType& type);
    // 声明槽位字数：struct 按布局，数组按元素数 × 元素字数，标量 1
    int declWords(const ir::IrType& type) const;

    // struct 布局查询
    const StructLayout* structLayoutOf(const std::string& type) const;
    const FieldLayout* findField(const StructLayout& layout,
                                 const std::string& name) const;
    void registerStructLayout(const ir::IrStructDef& def);

    // 类型字数：标量/指针 1，struct 值按布局（数组长度由声明侧另行计算）
    int typeSizeWords(const std::string& type) const;

    // ---- 模块与函数 ----
    void generateFunction(const ir::IrFunction& func, const LinkageEntry& link);

    // ---- 语句 ----
    void generateStmt(const ir::IrStmt& stmt);
    // 语句级赋值（三地址形态）：值入栈后按目标形态存回，不回填表达式结果
    void emitStoreStmt(const ir::IrStoreStmt& stmt);

    // ---- 表达式（求值结果压栈）----
    void evalExpr(const ir::IrExpr& expr);
    void emitAssignExpr(const ir::IrAssignExpr& node);

    // 变量读写（v2.1 无 BP 相对寻址：地址先入寄存器，再 LOAD/STORE）
    void emitLoadVar(const Symbol& sym);
    void emitStoreVar(const Symbol& sym);
    // struct 对象地址 → R0（形参：槽值即地址；局部/全局：槽地址）
    void emitStructAddressOfSymbol(const Symbol& sym);

    // 条件分支：expr 为假/为真时跳 target（比较直接走 flags，不物化 0/1）
    void
    emitBranchExpr(const ir::IrExpr& expr, const std::string& target, bool branchOnTrue);
    void emitCompareBranch(const std::string& op,
                           const std::string& target,
                           bool branchOnTrue);
    void emitTestBranch(const std::string& target, bool branchOnTrue);

    // 比较结果物化为 0/1 压栈（R0=左值、R1=右值，结果写 R2）
    void emitCompareValue(const std::string& op);

    // 地址计算：目标地址放入 R0（变量/数组首元素/a[i]/*p/p.x）
    void emitAddressOf(const ir::IrExpr& expr);
    void emitAddressOfSymbol(const Symbol& sym);
    void emitElementAddress(const ir::IrIndexExpr& node);
    // struct 对象地址 → R0：标识符/成员/下标/解引用/调用返回的临时
    void emitStructValueAddress(const ir::IrExpr& expr);
    // 成员地址 → R0（出参返回字段布局，供 load 判定）
    void emitMemberAddress(const ir::IrMemberExpr& node, const FieldLayout** outField);

    // struct 临时槽：帧尾临时区分配 sizeWords 字的连续块，返回块内最高槽号
    // （拷贝自该地址向高地址延伸，恰好覆盖块内全部槽位）；越界防御性抛异常
    int allocStructTemp(int sizeWords);
    // 栈顶=源地址、R0=目的地址 → 逐字拷贝 sizeWords 字（不回填结果）
    void emitPopCopy(int sizeWords);
    // 栈顶=源地址、R0=目的地址 → 逐字拷贝 sizeWords 字，push 目的地址
    void emitPopCopyPush(int sizeWords);
    // R1=源地址、R2=目的地址 → 逐字拷贝 sizeWords 字
    void emitCopyWords(int sizeWords);
    // 栈顶=源 struct 地址 → 拷贝到新临时槽，push 临时槽地址（struct 实参按值传递）
    void emitStructArgCopy(const std::string& structType);

    // struct 临时空间统计：累计函数体内 struct 实参副本与 struct 返回接收槽
    // 的词数（IR 节点自带类型，无需预登记符号表）
    int countStructTemps(const ir::IrStmt* stmt);
    int structTempWords(const ir::IrExpr* expr);

    // 字符串字面量去重入数据段，返回标号
    std::string internString(const std::string& content);

    // 函数帧：统计函数体声明的局部变量槽数
    int countLocalSlots(const ir::IrStmt* stmt) const;
};

#endif // NCC_CODEGEN_H
