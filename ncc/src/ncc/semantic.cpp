#include "ncc/semantic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

// ---------------------------------------------------------------------------
// 诊断与结果
// ---------------------------------------------------------------------------

std::string Diagnostic::toString() const {
    std::ostringstream oss;
    oss << file << ':' << line << ':' << column << ": "
        << (severity == DiagnosticSeverity::Error ? "error" : "warning") << ": "
        << message;
    return oss.str();
}

bool SemanticResult::hasErrors() const { return errorCount() > 0; }

ca::usize SemanticResult::errorCount() const {
    ca::usize count = 0;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// 分析器
// ---------------------------------------------------------------------------

SemanticAnalyzer::SemanticAnalyzer(std::string fileName)
  : m_fileName(std::move(fileName)), m_currentFunction(nullptr), m_loopDepth(0) {}

ca::Result<SemanticResult, std::string>
SemanticAnalyzer::analyze(const Program& program) {
    // 结构契约校验：仅当 AST 违反解析器产出约定时返回 Err（正常解析结果不会触发）
    if (program.type != ASTNodeType::PROGRAM) {
        return ca::Err(std::string("semantic analysis requires a Program root node"));
    }
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            const auto& func = static_cast<const FuncDeclaration&>(*decl);
            if (func.body == nullptr || func.body->type != ASTNodeType::COMPOUND_STMT) {
                return ca::Err("function '" + func.name + "' has no valid compound body");
            }
        }
    }

    m_result = SemanticResult{};
    m_scopes.clear();
    m_scopes.emplace_back(); // 作用域 0：全局
    m_currentFunction = nullptr;
    m_loopDepth = 0;

    // 第一遍：登记全部顶层函数签名，使调用点可以前向引用（先调用后定义/相互递归）
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            registerFunctionSignature(static_cast<const FuncDeclaration&>(*decl));
        }
    }

    // 第二遍：按声明顺序处理——全局变量"先声明后可见"（声明顺序即可见顺序），
    // 函数体逐一检查
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            checkFunctionBody(static_cast<const FuncDeclaration&>(*decl));
        } else if (decl->type == ASTNodeType::VAR_DECLARATION) {
            checkGlobalVariable(static_cast<const VarDeclaration&>(*decl));
        }
    }

    return ca::Ok(std::move(m_result));
}

// ---- 符号表辅助 ----

SemanticAnalyzer::Scope& SemanticAnalyzer::currentScope() { return m_scopes.back(); }

void SemanticAnalyzer::pushScope() { m_scopes.emplace_back(); }

void SemanticAnalyzer::popScope() { m_scopes.pop_back(); }

const Symbol* SemanticAnalyzer::lookupSymbol(const std::string& name) const {
    // 由内向外逐层查找；每层先查变量表再查函数表
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (const Symbol* variable = it->variables.get(name)) {
            return variable;
        }
        if (const Symbol* function = it->functions.get(name)) {
            return function;
        }
    }
    return nullptr;
}

bool SemanticAnalyzer::declareVariable(const Symbol& symbol) {
    Scope& scope = currentScope();
    if (scope.variables.contains_key(symbol.name)
        || scope.functions.contains_key(symbol.name)) {
        reportError(symbol.line, symbol.column, "redefinition of '" + symbol.name + "'");
        return false;
    }
    scope.variables.put(symbol.name, symbol);
    return true;
}

bool SemanticAnalyzer::declareFunction(const Symbol& symbol) {
    Scope& scope = currentScope();
    if (scope.variables.contains_key(symbol.name)
        || scope.functions.contains_key(symbol.name)) {
        reportError(symbol.line, symbol.column, "redefinition of '" + symbol.name + "'");
        return false;
    }
    scope.functions.put(symbol.name, symbol);
    return true;
}

void SemanticAnalyzer::appendGlobalSummary(const Symbol& symbol) {
    SymbolSummary summary;
    summary.name = symbol.name;
    summary.kind = symbol.kind;
    summary.type = typeName(symbol.type);
    summary.line = symbol.line;
    summary.column = symbol.column;
    for (const auto& paramType : symbol.paramTypes) {
        summary.paramTypes.add(typeName(paramType));
    }
    m_result.globals.add(std::move(summary));
}

// ---- 声明登记 ----

void SemanticAnalyzer::registerFunctionSignature(const FuncDeclaration& decl) {
    Symbol symbol;
    symbol.kind = SymbolKind::Function;
    symbol.name = decl.name;
    symbol.type = typeFromName(decl.returnType);
    symbol.line = decl.line;
    symbol.column = decl.column;
    for (const auto& param : decl.parameters) {
        symbol.paramTypes.add(typeFromName(param->type));
    }
    if (declareFunction(symbol)) {
        appendGlobalSummary(symbol);
    }
}

void SemanticAnalyzer::checkGlobalVariable(const VarDeclaration& decl) {
    SemanticType declared = typeFromName(decl.type);
    if (declared == SemanticType::Void) {
        reportError(decl.line,
                    decl.column,
                    "variable '" + decl.name + "' cannot have void type");
    }
    Symbol symbol;
    symbol.kind = SymbolKind::Variable;
    symbol.name = decl.name;
    symbol.type = declared;
    symbol.line = decl.line;
    symbol.column = decl.column;
    if (declareVariable(symbol)) {
        appendGlobalSummary(symbol);
    }
    if (decl.initializer) {
        SemanticType initType = checkExpr(*decl.initializer);
        checkConversion(initType,
                        declared,
                        decl.line,
                        decl.column,
                        "initialization of '" + decl.name + "'");
    }
}

void SemanticAnalyzer::checkFunctionBody(const FuncDeclaration& decl) {
    m_currentFunction = &decl;
    pushScope();

    // 参数登记在函数作用域内；解析器目前给参数记录的是函数的位置
    for (const auto& param : decl.parameters) {
        SemanticType paramType = typeFromName(param->type);
        if (paramType == SemanticType::Void) {
            reportError(param->line,
                        param->column,
                        "parameter '" + param->name + "' cannot have void type");
        }
        Symbol symbol;
        symbol.kind = SymbolKind::Parameter;
        symbol.name = param->name;
        symbol.type = paramType;
        symbol.line = param->line;
        symbol.column = param->column;
        declareVariable(symbol); // 与其他参数/顶层局部变量同名 → redefinition
    }

    // 函数体最外层块与参数共用当前作用域，因此只遍历语句而不额外压栈；
    // 内层块由 checkCompound 各自压栈
    const auto& body = static_cast<const CompoundStmt&>(*decl.body);
    for (const auto& stmt : body.statements) {
        checkStmt(*stmt);
    }

    // return 覆盖检查（保守可达性，策略见 definitelyReturns 注释）
    if (typeFromName(decl.returnType) != SemanticType::Void
        && !definitelyReturns(*decl.body)) {
        reportError(decl.line,
                    decl.column,
                    "missing return statement in non-void function '" + decl.name + "'");
    }

    popScope();
    m_currentFunction = nullptr;
}

void SemanticAnalyzer::checkLocalVariable(const StmtVarDeclaration& decl) {
    SemanticType declared = typeFromName(decl.type);
    if (declared == SemanticType::Void) {
        reportError(decl.line,
                    decl.column,
                    "variable '" + decl.name + "' cannot have void type");
    }
    Symbol symbol;
    symbol.kind = SymbolKind::Variable;
    symbol.name = decl.name;
    symbol.type = declared;
    symbol.line = decl.line;
    symbol.column = decl.column;
    declareVariable(symbol); // 冲突时已报错；登记失败则查找命中先登记的符号
    if (decl.initializer) {
        SemanticType initType = checkExpr(*decl.initializer);
        checkConversion(initType,
                        declared,
                        decl.line,
                        decl.column,
                        "initialization of '" + decl.name + "'");
    }
}

// ---- 语句检查 ----

void SemanticAnalyzer::checkStmt(const Stmt& stmt) {
    switch (stmt.type) {
    case ASTNodeType::COMPOUND_STMT:
        checkCompound(static_cast<const CompoundStmt&>(stmt));
        break;
    case ASTNodeType::IF_STMT:
        checkIf(static_cast<const IfStmt&>(stmt));
        break;
    case ASTNodeType::WHILE_STMT:
        checkWhile(static_cast<const WhileStmt&>(stmt));
        break;
    case ASTNodeType::FOR_STMT:
        checkFor(static_cast<const ForStmt&>(stmt));
        break;
    case ASTNodeType::RETURN_STMT:
        checkReturn(static_cast<const ReturnStmt&>(stmt));
        break;
    case ASTNodeType::BREAK_STMT:
        checkBreak(static_cast<const BreakStmt&>(stmt));
        break;
    case ASTNodeType::CONTINUE_STMT:
        checkContinue(static_cast<const ContinueStmt&>(stmt));
        break;
    case ASTNodeType::EXPR_STMT:
        checkExpr(*static_cast<const ExprStmt&>(stmt).expression);
        break;
    case ASTNodeType::VAR_DECLARATION:
        // 语句列表里的声明只会是 StmtVarDeclaration（解析器约定）
        checkLocalVariable(static_cast<const StmtVarDeclaration&>(stmt));
        break;
    default:
        break;
    }
}

void SemanticAnalyzer::checkNestedStmt(const Stmt& stmt) {
    if (stmt.type == ASTNodeType::VAR_DECLARATION) {
        pushScope();
        checkStmt(stmt);
        popScope();
    } else {
        checkStmt(stmt);
    }
}

void SemanticAnalyzer::checkCompound(const CompoundStmt& stmt) {
    pushScope();
    for (const auto& sub : stmt.statements) {
        checkStmt(*sub);
    }
    popScope();
}

void SemanticAnalyzer::checkIf(const IfStmt& stmt) {
    checkCondition(*stmt.condition);
    checkNestedStmt(*stmt.thenBranch);
    if (stmt.elseBranch != nullptr) {
        checkNestedStmt(*stmt.elseBranch);
    }
}

void SemanticAnalyzer::checkWhile(const WhileStmt& stmt) {
    checkCondition(*stmt.condition);
    m_loopDepth++;
    checkNestedStmt(*stmt.body);
    m_loopDepth--;
}

void SemanticAnalyzer::checkFor(const ForStmt& stmt) {
    // for 整体一个作用域：init 中声明的变量只在循环内可见，循环外不泄漏
    pushScope();
    if (stmt.init != nullptr) {
        checkStmt(*stmt.init);
    }
    if (stmt.condition != nullptr) {
        checkCondition(*stmt.condition);
    }
    if (stmt.increment != nullptr) {
        checkExpr(*stmt.increment);
    }
    m_loopDepth++;
    if (stmt.body != nullptr) {
        checkNestedStmt(*stmt.body);
    }
    m_loopDepth--;
    popScope();
}

void SemanticAnalyzer::checkReturn(const ReturnStmt& stmt) {
    // 解析器保证 return 只出现在函数体内；防御式判空
    if (m_currentFunction == nullptr) {
        return;
    }
    SemanticType returnType = typeFromName(m_currentFunction->returnType);
    if (stmt.value != nullptr) {
        if (returnType == SemanticType::Void) {
            reportError(stmt.line,
                        stmt.column,
                        "void function '" + m_currentFunction->name
                          + "' should not return a value");
            return;
        }
        SemanticType valueType = checkExpr(*stmt.value);
        checkConversion(valueType,
                        returnType,
                        stmt.line,
                        stmt.column,
                        "return statement");
    } else {
        if (returnType != SemanticType::Void) {
            reportError(stmt.line,
                        stmt.column,
                        "non-void function '" + m_currentFunction->name
                          + "' must return a value");
        }
    }
}

void SemanticAnalyzer::checkBreak(const BreakStmt& stmt) {
    if (m_loopDepth == 0) {
        reportError(stmt.line, stmt.column, "'break' outside of a loop");
    }
}

void SemanticAnalyzer::checkContinue(const ContinueStmt& stmt) {
    if (m_loopDepth == 0) {
        reportError(stmt.line, stmt.column, "'continue' outside of a loop");
    }
}

// ---- 表达式检查 ----

SemanticType SemanticAnalyzer::checkExpr(const Expr& expr) {
    switch (expr.type) {
    case ASTNodeType::BINARY_EXPR:
        return checkBinary(static_cast<const BinaryExpr&>(expr));
    case ASTNodeType::UNARY_EXPR:
        return checkUnary(static_cast<const UnaryExpr&>(expr));
    case ASTNodeType::ASSIGN_EXPR:
        return checkAssign(static_cast<const AssignExpr&>(expr));
    case ASTNodeType::CALL_EXPR:
        return checkCall(static_cast<const CallExpr&>(expr));
    case ASTNodeType::IDENTIFIER_EXPR:
        return checkIdentifier(static_cast<const IdentifierExpr&>(expr));
    case ASTNodeType::INTEGER_LITERAL:
        return SemanticType::Int;
    case ASTNodeType::CHAR_LITERAL:
        return SemanticType::Char;
    default:
        return SemanticType::Error; // 不可达：表达式节点类型已穷举
    }
}

SemanticType SemanticAnalyzer::checkIdentifier(const IdentifierExpr& expr) {
    const Symbol* symbol = lookupSymbol(expr.name);
    if (symbol == nullptr) {
        reportError(expr.line,
                    expr.column,
                    "use of undeclared identifier '" + expr.name + "'");
        return SemanticType::Error;
    }
    if (symbol->kind == SymbolKind::Function) {
        // 当前语言无函数指针/取址，函数名不能作为值使用
        reportError(expr.line,
                    expr.column,
                    "function '" + expr.name + "' used as a value");
        return SemanticType::Error;
    }
    return symbol->type;
}

SemanticType SemanticAnalyzer::checkAssign(const AssignExpr& expr) {
    // 先检查右侧表达式，尽量多收集错误
    SemanticType valueType = checkExpr(*expr.value);
    const Symbol* symbol = lookupSymbol(expr.name);
    if (symbol == nullptr) {
        reportError(expr.line,
                    expr.column,
                    "use of undeclared identifier '" + expr.name + "'");
        return SemanticType::Error;
    }
    if (symbol->kind == SymbolKind::Function) {
        reportError(expr.line,
                    expr.column,
                    "cannot assign to function '" + expr.name + "'");
        return SemanticType::Error;
    }
    checkConversion(valueType,
                    symbol->type,
                    expr.line,
                    expr.column,
                    "assignment to '" + expr.name + "'");
    // 赋值表达式的值类型 = 左值类型（读回的是赋值后的左值）
    return symbol->type;
}

SemanticType SemanticAnalyzer::checkBinary(const BinaryExpr& expr) {
    SemanticType left = checkExpr(*expr.left);
    SemanticType right = checkExpr(*expr.right);

    if (left == SemanticType::Error || right == SemanticType::Error) {
        return SemanticType::Error; // 操作数已报错，抑制级联
    }
    if (!isScalar(left) || !isScalar(right)) {
        // 目前唯一的非标量来源是 void 表达式（void 函数调用的结果）
        reportError(expr.line,
                    expr.column,
                    "invalid operands to binary '" + expr.op + "'");
        return SemanticType::Error;
    }
    // 算术/比较/逻辑运算统一规则：char 操作数先提升为 int，结果一律 int
    // （比较与逻辑运算的结果是 0/1）
    return SemanticType::Int;
}

SemanticType SemanticAnalyzer::checkUnary(const UnaryExpr& expr) {
    SemanticType operand = checkExpr(*expr.operand);
    if (operand == SemanticType::Error) {
        return SemanticType::Error;
    }
    if (!isScalar(operand)) {
        reportError(expr.line, expr.column, "invalid operand to unary '" + expr.op + "'");
        return SemanticType::Error;
    }
    // 一元 - 与 ! 的结果一律是 int（char 操作数先提升为 int）
    return SemanticType::Int;
}

SemanticType SemanticAnalyzer::checkCall(const CallExpr& expr) {
    const Symbol* symbol = lookupSymbol(expr.callee);
    if (symbol == nullptr) {
        reportError(expr.line,
                    expr.column,
                    "call to undeclared function '" + expr.callee + "'");
        // 仍检查实参表达式本身，收集其中可能存在的错误
        for (const auto& argument : expr.arguments) {
            checkExpr(*argument);
        }
        return SemanticType::Error;
    }
    if (symbol->kind != SymbolKind::Function) {
        reportError(expr.line, expr.column, "'" + expr.callee + "' is not a function");
        for (const auto& argument : expr.arguments) {
            checkExpr(*argument);
        }
        return SemanticType::Error;
    }

    // 参数个数一致性
    if (expr.arguments.size() != symbol->paramTypes.len()) {
        reportError(expr.line,
                    expr.column,
                    "function '" + expr.callee + "' expects "
                      + std::to_string(symbol->paramTypes.len())
                      + " argument(s), but got " + std::to_string(expr.arguments.size()));
    }
    // 个数不符时仍逐个检查前 min 个实参，尽量多收集错误
    const ca::usize checkCount =
      std::min(expr.arguments.size(), symbol->paramTypes.len());
    for (ca::usize i = 0; i < checkCount; ++i) {
        SemanticType argumentType = checkExpr(*expr.arguments[i]);
        checkConversion(argumentType,
                        symbol->paramTypes[i],
                        expr.line,
                        expr.column,
                        "argument " + std::to_string(static_cast<int>(i) + 1)
                          + " of call to '" + expr.callee + "'");
    }
    return symbol->type;
}

void SemanticAnalyzer::checkCondition(const Expr& expr) {
    SemanticType type = checkExpr(expr);
    if (type == SemanticType::Void) {
        reportError(expr.line, expr.column, "void value used as condition");
    }
    // Error：子表达式已报错，静默；Int/Char：非零为真，均可
}

void SemanticAnalyzer::checkConversion(
  SemanticType from, SemanticType to, int line, int column, const std::string& context) {
    if (from == SemanticType::Error || to == SemanticType::Error) {
        return; // 前序错误已报告，抑制级联
    }
    if (from == to) {
        return; // int→int、char→char
    }
    if (from == SemanticType::Char && to == SemanticType::Int) {
        return; // 提升：char 在需要 int 的场合无损加宽
    }
    if (from == SemanticType::Void) {
        reportError(line, column, "void value used in " + context);
        return;
    }
    // 剩余唯一路径：int → char。这是窄化转换，当前语言没有显式转换语法，
    // 为避免静默截断一律拒绝。
    reportError(line, column, "cannot implicitly convert int to char in " + context);
}

// ---- return 覆盖检查 ----

bool SemanticAnalyzer::definitelyReturns(const Stmt& stmt) const {
    switch (stmt.type) {
    case ASTNodeType::RETURN_STMT:
        return true;
    case ASTNodeType::COMPOUND_STMT: {
        const auto& block = static_cast<const CompoundStmt&>(stmt);
        return !block.statements.empty() && definitelyReturns(*block.statements.back());
    }
    case ASTNodeType::IF_STMT: {
        const auto& ifStmt = static_cast<const IfStmt&>(stmt);
        return ifStmt.elseBranch != nullptr && definitelyReturns(*ifStmt.thenBranch)
               && definitelyReturns(*ifStmt.elseBranch);
    }
    default:
        // 循环（条件可能一次都不满足）、break/continue、表达式语句均不提供
        // 必然返回保证
        return false;
    }
}

// ---- 工具 ----

void SemanticAnalyzer::reportError(int line, int column, const std::string& message) {
    Diagnostic diagnostic;
    diagnostic.file = m_fileName;
    diagnostic.line = line;
    diagnostic.column = column;
    diagnostic.severity = DiagnosticSeverity::Error;
    diagnostic.message = message;
    m_result.diagnostics.add(std::move(diagnostic));
}

SemanticType SemanticAnalyzer::typeFromName(const std::string& name) {
    if (name == "int") {
        return SemanticType::Int;
    }
    if (name == "char") {
        return SemanticType::Char;
    }
    if (name == "void") {
        return SemanticType::Void;
    }
    return SemanticType::Error; // 解析器只产出 int/char/void；防御式兜底
}

std::string SemanticAnalyzer::typeName(SemanticType type) {
    switch (type) {
    case SemanticType::Int:
        return "int";
    case SemanticType::Char:
        return "char";
    case SemanticType::Void:
        return "void";
    case SemanticType::Error:
        return "<error>";
    }
    return "<error>";
}

bool SemanticAnalyzer::isScalar(SemanticType type) {
    return type == SemanticType::Int || type == SemanticType::Char;
}
