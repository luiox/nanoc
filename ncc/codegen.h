#ifndef NCC_CODEGEN_H
#define NCC_CODEGEN_H

#include "ast.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

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
    
private:
    std::string m_output;
    int m_labelCounter;
    std::vector<std::string> m_breakLabels;
    std::vector<std::string> m_continueLabels;
    
    // 符号表
    struct Symbol {
        std::string name;
        std::string type;
        int offset; // 相对于栈帧的偏移
    };
    
    std::map<std::string, Symbol> m_symbolTable;
    int m_currentOffset;
    
    // 辅助函数
    void emit(const std::string& code);
    void emitLabel(const std::string& label);
    std::string newLabel();
    void pushScope();
    void popScope();
    
    // 寄存器分配
    std::vector<bool> m_registers;
    int allocateRegister();
    void freeRegister(int reg);
    
    // 栈管理
    void pushRegister(int reg);
    void popRegister(int reg);
    
    // 表达式求值
    int evaluateExpression(Expr& expr);
    
    // 类型检查
    std::string getType(Expr& expr);
};

#endif // NCC_CODEGEN_H