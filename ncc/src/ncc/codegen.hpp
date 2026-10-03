#ifndef NCC_CODEGEN_H
#define NCC_CODEGEN_H

#include "ncc/ast.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// NCI v2.1 代码生成器：产出 nas 可汇编的 v2.1 汇编文本。
//
// 目标机器约定（doc/Bytecode Format Specification v2.1.md §3/§4.2）：
// - R4=SP、R5=BP；R0-R3 为参数/返回值寄存器（fastcall 前 4 个参数）
// - 函数序言 enter <locals>、尾声 leave + ret；返回值统一在 R0
// - 调用约定：前 4 个参数走 R0-R3，第 5 个起压栈（调用者求值后 addi R4 清栈）
// - 条件跳转仅 JZ/JNZ/JN/JP，flags 由 CMP/CMPI/TEST 置位（bit0=Z bit1=N bit2=P）
// - main 顶层 leave/ret 由 VM 栈底哨兵终止执行，无需 trap/exit
class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator();

    // 生成汇编代码
    std::string generate(Program& program);

    // 访问者模式实现
    void visit(Program& node) override;
    void visit(VarDeclaration& node) override;
    void visit(FuncDeclaration& node) override;
    void visit(CompoundStmt& node) override;
    void visit(IfStmt& node) override;
    void visit(WhileStmt& node) override;
    void visit(ForStmt& node) override;
    void visit(ReturnStmt& node) override;
    void visit(BreakStmt& node) override;
    void visit(ContinueStmt& node) override;
    void visit(ExprStmt& node) override;
    void visit(BinaryExpr& node) override;
    void visit(UnaryExpr& node) override;
    void visit(AssignExpr& node) override;
    void visit(CallExpr& node) override;
    void visit(IdentifierExpr& node) override;
    void visit(IntegerLiteral& node) override;
    void visit(CharLiteral& node) override;
    void visit(StmtVarDeclaration& node) override;

private:
    // 符号类别：局部 / 栈参（>4 的参数在调用者栈上）/ 全局（数据段标号）
    enum class SymKind { Local, StackArg, Global };

    struct Symbol {
        SymKind kind = SymKind::Local;
        int slot = 0;      // Local：帧槽位（1 起，地址 = BP - 4*slot）
        int argIndex = 0;  // StackArg：参数序号（1 起，地址 = BP + 4*(argIndex-3)）
        std::string label; // Global：数据段标号
        std::string type;
    };

    struct GlobalInit {
        std::string label; // 目标数据标号
        Expr* expr;        // 初始化表达式（借用 AST 所有权）
    };

    // 输出缓冲：函数体与数据段分开收集，最后统一拼装
    // （extern 指令在函数体生成期间才收集完毕，需插在文件头）
    std::string m_code;
    std::string m_data;
    std::string* m_sink = nullptr;

    int m_labelCounter = 0;
    std::vector<std::string> m_breakLabels;
    std::vector<std::string> m_continueLabels;

    // 符号表：函数名集合 + 全局/局部变量（局部优先于全局）
    std::set<std::string> m_functions;
    std::map<std::string, Symbol> m_globalSymbols;
    std::map<std::string, Symbol> m_localSymbols;
    std::vector<std::string> m_globalOrder; // 数据段发射顺序
    int m_nextSlot = 0;                     // 当前函数已分配的最大槽位

    std::vector<GlobalInit> m_globalInits;         // 进入 main 后统一执行
    std::set<VarDeclaration*> m_registeredGlobals; // 防止重复登记同一声明
    std::vector<std::string> m_externs;            // 未定义的被调函数 → 宿主符号（callx）
    std::set<std::string> m_externSet;

    // 输出
    void emit(const std::string& code);
    void emitLabel(const std::string& label);
    std::string newLabel();

    // 符号
    const Symbol* findSymbol(const std::string& name) const;
    void registerGlobal(VarDeclaration& node);
    void emitGlobalInits();

    // 变量读写（v2.1 无 BP 相对寻址：地址先入寄存器，再 LOAD/STORE）
    void emitLoadVar(const Symbol& sym);
    void emitStoreVar(const Symbol& sym);

    // 条件分支：expr 为假/为真时跳 target（比较直接走 flags，不物化 0/1）
    void emitBranch(Expr& expr, const std::string& target, bool branchOnTrue);
    void emitBranchFalse(Expr& expr, const std::string& target);
    void emitBranchTrue(Expr& expr, const std::string& target);
    void emitCompareBranch(const std::string& op,
                           const std::string& target,
                           bool branchOnTrue);
    void emitTestBranch(const std::string& target, bool branchOnTrue);

    // 比较结果物化为 0/1 压栈（R0=左值、R1=右值，结果写 R2）
    void emitCompareValue(const std::string& op);

    // 函数帧：统计函数体声明的局部变量槽数
    int countLocalSlots(Stmt* stmt) const;
};

#endif // NCC_CODEGEN_H
