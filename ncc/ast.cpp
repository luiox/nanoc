#include "ast.hpp"

// Program节点的accept实现
void Program::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// VarDeclaration节点的accept实现
void VarDeclaration::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// FuncDeclaration节点的accept实现
void FuncDeclaration::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// CompoundStmt节点的accept实现
void CompoundStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// IfStmt节点的accept实现
void IfStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// WhileStmt节点的accept实现
void WhileStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ForStmt节点的accept实现
void ForStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ReturnStmt节点的accept实现
void ReturnStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// BreakStmt节点的accept实现
void BreakStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ContinueStmt节点的accept实现
void ContinueStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ExprStmt节点的accept实现
void ExprStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// BinaryExpr节点的accept实现
void BinaryExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// UnaryExpr节点的accept实现
void UnaryExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// AssignExpr节点的accept实现
void AssignExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// CallExpr节点的accept实现
void CallExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// IdentifierExpr节点的accept实现
void IdentifierExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// IntegerLiteral节点的accept实现
void IntegerLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// CharLiteral节点的accept实现
void CharLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// StmtVarDeclaration节点的accept实现
void StmtVarDeclaration::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}