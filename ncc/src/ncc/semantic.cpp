#include "ncc/semantic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

// 条件上下文可用的类型：标量/指针/NULL（非零为真）
static bool isConditionType(SemanticType type) {
    return type == SemanticType::Int || type == SemanticType::Char
           || type == SemanticType::IntPtr || type == SemanticType::CharPtr
           || type == SemanticType::Null;
}

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
    // 返回类型在此处唯一校验（void* 等），诊断落在函数声明位置
    symbol.type = declaredType(decl.returnType,
                               decl.returnPointerDepth,
                               false,
                               0,
                               0,
                               decl.line,
                               decl.column,
                               true);
    symbol.line = decl.line;
    symbol.column = decl.column;
    for (const auto& param : decl.parameters) {
        // 参数类型的诊断在 checkFunctionBody 中统一报告，此处静默计算
        symbol.paramTypes.add(declaredType(param->type,
                                           param->pointerDepth,
                                           param->isArray,
                                           param->arraySize,
                                           param->arrayDims,
                                           param->line,
                                           param->column,
                                           false));
    }
    if (declareFunction(symbol)) {
        appendGlobalSummary(symbol);
    }
}

void SemanticAnalyzer::checkGlobalVariable(const VarDeclaration& decl) {
    SemanticType declared = declaredType(decl.type,
                                         decl.pointerDepth,
                                         decl.isArray,
                                         decl.arraySize,
                                         decl.arrayDims,
                                         decl.line,
                                         decl.column,
                                         true);
    if (typeFromName(decl.type) == SemanticType::Void && decl.pointerDepth == 0
        && !decl.isArray) {
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
        SemanticType paramType = declaredType(param->type,
                                              param->pointerDepth,
                                              param->isArray,
                                              param->arraySize,
                                              param->arrayDims,
                                              param->line,
                                              param->column,
                                              true);
        if (typeFromName(param->type) == SemanticType::Void && param->pointerDepth == 0
            && !param->isArray) {
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
    SemanticType declared = declaredType(decl.type,
                                         decl.pointerDepth,
                                         decl.isArray,
                                         decl.arraySize,
                                         decl.arrayDims,
                                         decl.line,
                                         decl.column,
                                         true);
    if (typeFromName(decl.type) == SemanticType::Void && decl.pointerDepth == 0
        && !decl.isArray) {
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
    // 返回类型已在 registerFunctionSignature 中校验，此处静默重算
    const SemanticType returnType = declaredType(m_currentFunction->returnType,
                                                 m_currentFunction->returnPointerDepth,
                                                 false,
                                                 0,
                                                 0,
                                                 stmt.line,
                                                 stmt.column,
                                                 false);
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
    case ASTNodeType::INDEX_EXPR:
        return checkIndex(static_cast<const IndexExpr&>(expr));
    case ASTNodeType::INTEGER_LITERAL:
        return SemanticType::Int;
    case ASTNodeType::CHAR_LITERAL:
        return SemanticType::Char;
    case ASTNodeType::STRING_LITERAL:
        return SemanticType::CharPtr; // 字符串字面量：数据段常量的地址
    case ASTNodeType::NULL_LITERAL:
        return SemanticType::Null;
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
    SemanticType targetType = SemanticType::Error;
    std::string context = "assignment";

    switch (expr.target->type) {
    case ASTNodeType::IDENTIFIER_EXPR: {
        const auto& ident = static_cast<const IdentifierExpr&>(*expr.target);
        context = "assignment to '" + ident.name + "'";
        const Symbol* symbol = lookupSymbol(ident.name);
        if (symbol == nullptr) {
            reportError(ident.line,
                        ident.column,
                        "use of undeclared identifier '" + ident.name + "'");
            break;
        }
        if (symbol->kind == SymbolKind::Function) {
            reportError(ident.line,
                        ident.column,
                        "cannot assign to function '" + ident.name + "'");
            break;
        }
        if (isArrayType(symbol->type)) {
            // 数组整体赋值不支持（PRD R1.2）：数组不是可拷贝的左值
            reportError(ident.line,
                        ident.column,
                        "cannot assign to array '" + ident.name
                          + "' (arrays are not copyable)");
            break;
        }
        targetType = symbol->type;
        break;
    }
    case ASTNodeType::INDEX_EXPR:
        // a[i] = v / p[i] = v：下标检查给出元素类型（内部已报告下标错误）
        targetType = checkIndex(static_cast<const IndexExpr&>(*expr.target));
        break;
    case ASTNodeType::UNARY_EXPR:
        // *p = v：解引用检查给出逐引用类型
        targetType = checkUnary(static_cast<const UnaryExpr&>(*expr.target));
        break;
    default:
        reportError(expr.line, expr.column, "expression is not assignable");
        break;
    }

    checkConversion(valueType, targetType, expr.line, expr.column, context);
    // 赋值表达式的值类型 = 左值类型（读回的是赋值后的左值）
    return targetType;
}

SemanticType SemanticAnalyzer::checkBinary(const BinaryExpr& expr) {
    SemanticType left = checkExpr(*expr.left);
    SemanticType right = checkExpr(*expr.right);

    if (left == SemanticType::Error || right == SemanticType::Error) {
        return SemanticType::Error; // 操作数已报错，抑制级联
    }
    const std::string& op = expr.op;

    // 比较：标量之间（char 提升）；同类型指针之间；指针与 NULL 之间
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        SemanticType l = decayed(left);
        SemanticType r = decayed(right);
        if (isScalar(l) && isScalar(r)) {
            return SemanticType::Int;
        }
        if (l == r && (isPointer(l) || l == SemanticType::Null)) {
            return SemanticType::Int;
        }
        // 指针与 NULL 比较合法
        if ((isPointer(l) && r == SemanticType::Null)
            || (l == SemanticType::Null && isPointer(r))) {
            return SemanticType::Int;
        }
        if ((isPointer(l) || l == SemanticType::Null)
            && (isPointer(r) || r == SemanticType::Null)) {
            reportError(expr.line,
                        expr.column,
                        "comparison between distinct pointer types '" + typeName(left)
                          + "' and '" + typeName(right) + "'");
            return SemanticType::Error;
        }
        reportError(expr.line, expr.column, "invalid operands to binary '" + op + "'");
        return SemanticType::Error;
    }

    // 逻辑运算：操作数为可判真伪的类型（标量/指针/NULL，非零为真）
    if (op == "&&" || op == "||") {
        SemanticType l = decayed(left);
        SemanticType r = decayed(right);
        if (!isConditionType(l) || !isConditionType(r)) {
            reportError(expr.line,
                        expr.column,
                        "invalid operands to binary '" + op + "'");
            return SemanticType::Error;
        }
        return SemanticType::Int;
    }

    // 算术运算（数组名在此退化为指针参与运算）
    SemanticType l = decayed(left);
    SemanticType r = decayed(right);

    if (op == "+" || op == "-") {
        // 标量算术：char 提升，结果 int
        if (isScalar(l) && isScalar(r)) {
            return SemanticType::Int;
        }
        // 指针 ± 整数：按指向类型大小缩放（当前全部 4 字节）
        if (op == "+" && isPointer(l) && isScalar(r)) {
            return l;
        }
        if (op == "+" && isScalar(l) && isPointer(r)) {
            return r;
        }
        if (op == "-" && isPointer(l) && isScalar(r)) {
            return l;
        }
        if (op == "-" && isPointer(l) && isPointer(r)) {
            reportError(expr.line, expr.column, "pointer subtraction is not supported");
            return SemanticType::Error;
        }
        reportError(expr.line, expr.column, "invalid operands to binary '" + op + "'");
        return SemanticType::Error;
    }

    // 乘除模：仅标量
    if (isScalar(l) && isScalar(r)) {
        return SemanticType::Int;
    }
    reportError(expr.line, expr.column, "invalid operands to binary '" + op + "'");
    return SemanticType::Error;
}

SemanticType SemanticAnalyzer::checkUnary(const UnaryExpr& expr) {
    if (expr.op == "&") {
        return checkAddressOf(expr);
    }
    if (expr.op == "*") {
        return checkDereference(expr);
    }

    SemanticType operand = checkExpr(*expr.operand);
    if (operand == SemanticType::Error) {
        return SemanticType::Error;
    }
    if (expr.op == "!") {
        // 逻辑非：标量/指针/NULL 均可判真伪，结果 int
        if (!isConditionType(decayed(operand))) {
            reportError(expr.line, expr.column, "invalid operand to unary '!'");
            return SemanticType::Error;
        }
        return SemanticType::Int;
    }
    // 一元 -：仅标量（char 提升），结果 int；指针取负无意义
    if (!isScalar(decayed(operand))) {
        reportError(expr.line, expr.column, "invalid operand to unary '-'");
        return SemanticType::Error;
    }
    return SemanticType::Int;
}

// &x：操作数必须是左值（x / a[i] / *p），结果为 pointer-to-T
SemanticType SemanticAnalyzer::checkAddressOf(const UnaryExpr& expr) {
    const Expr& operand = *expr.operand;

    // 函数名不能取址（无函数指针）
    if (operand.type == ASTNodeType::IDENTIFIER_EXPR) {
        const auto& ident = static_cast<const IdentifierExpr&>(operand);
        const Symbol* symbol = lookupSymbol(ident.name);
        if (symbol != nullptr && symbol->kind == SymbolKind::Function) {
            reportError(operand.line,
                        operand.column,
                        "cannot take the address of function '" + ident.name + "'");
            return SemanticType::Error;
        }
    }

    SemanticType type = checkExpr(operand);
    if (type == SemanticType::Error) {
        return SemanticType::Error;
    }
    if (!isLValueExpr(operand)) {
        reportError(operand.line, operand.column, "cannot take the address of an rvalue");
        return SemanticType::Error;
    }
    if (isArrayType(type)) {
        reportError(
          operand.line,
          operand.column,
          "cannot take the address of an array (it already decays to a pointer)");
        return SemanticType::Error;
    }
    if (!isScalar(type) && !isPointer(type)) {
        reportError(operand.line,
                    operand.column,
                    "cannot take the address of this expression");
        return SemanticType::Error;
    }
    SemanticType result = pointerTo(type);
    if (result == SemanticType::Error) {
        // & 一级指针 → 二级指针，本里程碑不支持
        reportError(expr.line, expr.column, "multi-level pointers are not supported");
        return SemanticType::Error;
    }
    return result;
}

// *p：操作数必须是指针，结果为其指向类型；char* 受字节打包限制不可解引用
SemanticType SemanticAnalyzer::checkDereference(const UnaryExpr& expr) {
    SemanticType operand = decayed(checkExpr(*expr.operand));
    if (operand == SemanticType::Error) {
        return SemanticType::Error;
    }
    if (operand == SemanticType::IntPtr) {
        return SemanticType::Int;
    }
    if (operand == SemanticType::CharPtr) {
        reportError(expr.operand->line,
                    expr.operand->column,
                    "cannot dereference 'char*' (string literals are byte-packed; "
                    "copy into a char array via a host function instead)");
        return SemanticType::Error;
    }
    if (operand == SemanticType::Null) {
        reportError(expr.operand->line,
                    expr.operand->column,
                    "cannot dereference 'NULL'");
        return SemanticType::Error;
    }
    reportError(expr.operand->line,
                expr.operand->column,
                "cannot dereference non-pointer type '" + typeName(operand) + "'");
    return SemanticType::Error;
}

// a[i] / p[i]：base 为数组（不退化，元素按 4 字节槽存放）或指针；下标必须是标量
SemanticType SemanticAnalyzer::checkIndex(const IndexExpr& expr) {
    SemanticType base = checkExpr(*expr.base);
    SemanticType index = checkExpr(*expr.index);

    if (index != SemanticType::Error && !isScalar(index)) {
        reportError(expr.index->line,
                    expr.index->column,
                    "array subscript is not an integer");
    }

    if (base == SemanticType::Error) {
        return SemanticType::Error;
    }
    switch (base) {
    case SemanticType::IntPtr:
    case SemanticType::IntArray:
        return SemanticType::Int;
    case SemanticType::CharArray:
        return SemanticType::Char;
    case SemanticType::CharPtr:
        reportError(expr.base->line,
                    expr.base->column,
                    "cannot index through 'char*' (string literals are byte-packed; "
                    "copy into a char array via a host function instead)");
        return SemanticType::Error;
    default:
        reportError(expr.base->line,
                    expr.base->column,
                    "subscripted value is not an array or pointer");
        return SemanticType::Error;
    }
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
    SemanticType type = decayed(checkExpr(expr));
    if (type == SemanticType::Void) {
        reportError(expr.line, expr.column, "void value used as condition");
    }
    // Error：子表达式已报错，静默；标量/指针/NULL：非零为真，均可
}

void SemanticAnalyzer::checkConversion(
  SemanticType from, SemanticType to, int line, int column, const std::string& context) {
    if (from == SemanticType::Error || to == SemanticType::Error) {
        return; // 前序错误已报告，抑制级联
    }
    if (from == to) {
        if (isArrayType(from)) {
            // 数组整体拷贝不支持（PRD R1.2）：初始化/赋值/传参中数组只能退化
            reportError(line,
                        column,
                        "cannot copy array of type '" + typeName(from) + "' in "
                          + context);
        }
        return; // int→int、char→char、同型指针
    }
    if (from == SemanticType::Char && to == SemanticType::Int) {
        return; // 提升：char 在需要 int 的场合无损加宽
    }
    if (from == SemanticType::IntArray && to == SemanticType::IntPtr) {
        return; // 数组退化：int[] → int*（传参/赋值/返回）
    }
    if (from == SemanticType::CharArray && to == SemanticType::CharPtr) {
        return; // 数组退化：char[] → char*
    }
    if (from == SemanticType::Null && isPointer(to)) {
        return; // NULL 可赋给任意指针类型
    }
    if (from == SemanticType::Void) {
        reportError(line, column, "void value used in " + context);
        return;
    }
    if (from == SemanticType::Int && to == SemanticType::Char) {
        // 窄化转换：当前语言没有显式转换语法，为避免静默截断一律拒绝
        reportError(line, column, "cannot implicitly convert int to char in " + context);
        return;
    }
    if (isPointer(from) && isPointer(to)) {
        reportError(line,
                    column,
                    "incompatible pointer types ('" + typeName(from) + "' to '"
                      + typeName(to) + "') in " + context);
        return;
    }
    // 其余一律拒绝：指针与标量互转、数组与标量、NULL 与标量等
    reportError(line,
                column,
                "cannot convert '" + typeName(from) + "' to '" + typeName(to) + "' in "
                  + context);
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
    case SemanticType::IntPtr:
        return "int*";
    case SemanticType::CharPtr:
        return "char*";
    case SemanticType::IntArray:
        return "int[]";
    case SemanticType::CharArray:
        return "char[]";
    case SemanticType::Null:
        return "NULL";
    }
    return "<error>";
}

bool SemanticAnalyzer::isScalar(SemanticType type) {
    return type == SemanticType::Int || type == SemanticType::Char;
}

// ---- 类型工具（PRD R1.2） ----

SemanticType SemanticAnalyzer::pointerTo(SemanticType t) {
    switch (t) {
    case SemanticType::Int:
        return SemanticType::IntPtr;
    case SemanticType::Char:
        return SemanticType::CharPtr;
    default:
        return SemanticType::Error; // void*/多级指针不可构造
    }
}

SemanticType SemanticAnalyzer::arrayOf(SemanticType t) {
    switch (t) {
    case SemanticType::Int:
        return SemanticType::IntArray;
    case SemanticType::Char:
        return SemanticType::CharArray;
    default:
        return SemanticType::Error;
    }
}

SemanticType SemanticAnalyzer::decayed(SemanticType t) {
    switch (t) {
    case SemanticType::IntArray:
        return SemanticType::IntPtr;
    case SemanticType::CharArray:
        return SemanticType::CharPtr;
    default:
        return t;
    }
}

bool SemanticAnalyzer::isPointer(SemanticType type) {
    return type == SemanticType::IntPtr || type == SemanticType::CharPtr;
}

bool SemanticAnalyzer::isArrayType(SemanticType type) {
    return type == SemanticType::IntArray || type == SemanticType::CharArray;
}

bool SemanticAnalyzer::isLValueExpr(const Expr& expr) {
    switch (expr.type) {
    case ASTNodeType::IDENTIFIER_EXPR:
    case ASTNodeType::INDEX_EXPR:
        return true;
    case ASTNodeType::UNARY_EXPR:
        return static_cast<const UnaryExpr&>(expr).op == "*";
    default:
        return false;
    }
}

// 由声明的类型要素计算语义类型；不合法组合（多级指针/多维数组/指针数组/
// 越界长度/void* /void[]）按 report 决定是否报错，返回 Error 毒类型
SemanticType SemanticAnalyzer::declaredType(const std::string& baseName,
                                            int pointerDepth,
                                            bool isArray,
                                            int arraySize,
                                            int arrayDims,
                                            int line,
                                            int column,
                                            bool report) {
    const std::string stars(static_cast<size_t>(pointerDepth), '*');
    const std::string typeText = baseName + stars;

    SemanticType base = typeFromName(baseName);
    if (base == SemanticType::Error) {
        if (report) {
            reportError(line, column, "unknown type '" + baseName + "'");
        }
        return SemanticType::Error;
    }
    if (pointerDepth > 1) {
        if (report) {
            reportError(line,
                        column,
                        "multi-level pointers are not supported ('" + typeText + "')");
        }
        return SemanticType::Error;
    }
    if (isArray) {
        if (pointerDepth > 0) {
            if (report) {
                reportError(line,
                            column,
                            "arrays of pointers are not supported ('" + typeText
                              + "[...]')");
            }
            return SemanticType::Error;
        }
        if (arrayDims > 1) {
            if (report) {
                reportError(line, column, "multidimensional arrays are not supported");
            }
            return SemanticType::Error;
        }
        if (arraySize <= 0) {
            if (report) {
                reportError(line, column, "array size must be positive");
            }
            return SemanticType::Error;
        }
        if (base == SemanticType::Void) {
            if (report) {
                reportError(line, column, "cannot declare array of 'void'");
            }
            return SemanticType::Error;
        }
        return arrayOf(base);
    }
    if (pointerDepth == 1) {
        SemanticType result = pointerTo(base);
        if (result == SemanticType::Error && report) {
            reportError(line, column, "'" + typeText + "' is not supported");
        }
        return result;
    }
    return base;
}
