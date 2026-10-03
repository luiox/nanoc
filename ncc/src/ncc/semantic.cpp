#include "ncc/semantic.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

// 条件上下文可用的类型：标量/指针/NULL（非零为真）
static bool isConditionType(const SemanticType& type) {
    return type.kind == SemanticType::Kind::Int || type.kind == SemanticType::Kind::Char
           || type.kind == SemanticType::Kind::Pointer
           || type.kind == SemanticType::Kind::Null;
}

// 子树是否含 match 表达式（PRD R11）：match 降解为 If 链，只能在函数体内
// 的语句位置出现；用于拒绝全局初始化器中的 match
static bool containsMatchExpr(const Expr& expr) {
    if (expr.type == ASTNodeType::MATCH_EXPR) {
        return true;
    }
    switch (expr.type) {
    case ASTNodeType::BINARY_EXPR: {
        const auto& binary = static_cast<const BinaryExpr&>(expr);
        return (binary.left != nullptr && containsMatchExpr(*binary.left))
               || (binary.right != nullptr && containsMatchExpr(*binary.right));
    }
    case ASTNodeType::UNARY_EXPR: {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        return unary.operand != nullptr && containsMatchExpr(*unary.operand);
    }
    case ASTNodeType::ASSIGN_EXPR: {
        const auto& assign = static_cast<const AssignExpr&>(expr);
        return (assign.target != nullptr && containsMatchExpr(*assign.target))
               || (assign.value != nullptr && containsMatchExpr(*assign.value));
    }
    case ASTNodeType::CALL_EXPR: {
        const auto& call = static_cast<const CallExpr&>(expr);
        for (const auto& argument : call.arguments) {
            if (argument != nullptr && containsMatchExpr(*argument)) {
                return true;
            }
        }
        return false;
    }
    case ASTNodeType::INDEX_EXPR: {
        const auto& index = static_cast<const IndexExpr&>(expr);
        return (index.base != nullptr && containsMatchExpr(*index.base))
               || (index.index != nullptr && containsMatchExpr(*index.index));
    }
    case ASTNodeType::MEMBER_EXPR: {
        const auto& member = static_cast<const MemberExpr&>(expr);
        return member.base != nullptr && containsMatchExpr(*member.base);
    }
    case ASTNodeType::INIT_LIST_EXPR: {
        const auto& init = static_cast<const InitListExpr&>(expr);
        for (const auto& value : init.values) {
            if (value != nullptr && containsMatchExpr(*value)) {
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// 语义类型（R1.2 第二批：递归值类型）
// ---------------------------------------------------------------------------

bool SemanticType::operator==(const SemanticType& other) const {
    if (kind != other.kind || tag != other.tag) {
        return false;
    }
    if (element == nullptr || other.element == nullptr) {
        return element == other.element;
    }
    return *element == *other.element;
}

const SemanticType SemanticType::Int{ Kind::Int, "", nullptr };
const SemanticType SemanticType::Char{ Kind::Char, "", nullptr };
const SemanticType SemanticType::Void{ Kind::Void, "", nullptr };
const SemanticType SemanticType::Error{ Kind::Error, "", nullptr };
const SemanticType SemanticType::Null{ Kind::Null, "", nullptr };
const SemanticType SemanticType::IntPtr = pointerTo(Int);
const SemanticType SemanticType::CharPtr = pointerTo(Char);
const SemanticType SemanticType::IntArray = arrayOf(Int);
const SemanticType SemanticType::CharArray = arrayOf(Char);

SemanticType SemanticType::structOf(std::string structTag) {
    return SemanticType{ Kind::Struct, std::move(structTag), nullptr };
}

SemanticType SemanticType::pointerTo(SemanticType pointee) {
    return SemanticType{ Kind::Pointer,
                         "",
                         std::make_shared<const SemanticType>(std::move(pointee)) };
}

SemanticType SemanticType::arrayOf(SemanticType elem) {
    return SemanticType{ Kind::Array,
                         "",
                         std::make_shared<const SemanticType>(std::move(elem)) };
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
  : m_fileName(std::move(fileName)), m_currentFunction(nullptr), m_loopDepth(0),
    m_deferDepth(0) {}

ca::Result<SemanticResult, std::string>
SemanticAnalyzer::analyze(const Program& program) {
    // 结构契约校验：仅当 AST 违反解析器产出约定时返回 Err（正常解析结果不会触发）
    if (program.type != ASTNodeType::PROGRAM) {
        return ca::Err(std::string("semantic analysis requires a Program root node"));
    }
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            const auto& func = static_cast<const FuncDeclaration&>(*decl);
            // extern 声明无函数体（PRD R3）：body == nullptr 是合法形态；
            // 普通函数仍要求复合语句体
            if (func.isExtern) {
                continue;
            }
            if (func.body == nullptr || func.body->type != ASTNodeType::COMPOUND_STMT) {
                return ca::Err("function '" + func.name + "' has no valid compound body");
            }
        }
    }

    m_result = SemanticResult{};
    m_scopes.clear();
    m_structs.clear();
    m_typedefs.clear();
    m_globalSymbols.clear();
    m_globalByName.clear();
    m_currentFile.clear();
    m_scopes.emplace_back(); // 作用域 0：全局（顶层符号另登记于 m_globalSymbols）
    m_currentFunction = nullptr;
    m_loopDepth = 0;
    m_deferDepth = 0;

    // 第一遍之一：struct 定义/前向声明与 typedef 按声明顺序登记。类型命名空间
    // 全编译单元内可见（不强制文本先序，决策见 semantic.hpp 类注释）
    for (const auto& decl : program.declarations) {
        m_currentFile = decl->sourceFile;
        if (decl->type == ASTNodeType::STRUCT_DECLARATION) {
            registerStructDeclaration(static_cast<const StructDeclaration&>(*decl));
        } else if (decl->type == ASTNodeType::TYPEDEF_DECLARATION) {
            registerTypedefDeclaration(static_cast<const TypedefDeclaration&>(*decl));
        }
    }

    // 第一遍之二：登记全部顶层函数签名，使调用点可以前向引用（先调用后定义/
    // 相互递归）
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            m_currentFile = decl->sourceFile;
            registerFunctionSignature(static_cast<const FuncDeclaration&>(*decl));
        }
    }

    // 第二遍：按声明顺序处理——全局变量"先声明后可见"（声明顺序即可见顺序），
    // 函数体逐一检查
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION
            || decl->type == ASTNodeType::VAR_DECLARATION) {
            m_currentFile = decl->sourceFile;
        }
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            // extern 声明无函数体（PRD R3）：登记签名后跳过函数体检查
            if (static_cast<const FuncDeclaration&>(*decl).isExtern) {
                continue;
            }
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
    // 局部作用域未命中 → 顶层符号表（受跨文件可见性约束）
    return lookupGlobal(name);
}

// ---- 顶层符号表（PRD R2a 多文件可见性）----

bool SemanticAnalyzer::declareGlobal(const Symbol& symbol) {
    std::vector<std::size_t>& slots = m_globalByName[symbol.name];
    for (const std::size_t index : slots) {
        const Symbol& existing = m_globalSymbols[index];
        // 重复定义规则（决策记录）：
        // - 同文件同名（函数/变量混用同命名空间）→ 沿用既有 redefinition 报错
        // - main 全局唯一：跨文件多个 main 报错（VM 入口标号不参与 mangle）
        // - 任一方导出 → 导出名全编译单元唯一，跨文件冲突报错
        // - 双方皆私有且跨文件 → 合法（各文件各一份，codegen 标号 mangle 隔离）
        const bool sameFile = existing.definedIn == symbol.definedIn;
        if (sameFile || symbol.name == "main" || existing.isExported
            || symbol.isExported) {
            reportError(symbol.line,
                        symbol.column,
                        "redefinition of '" + symbol.name + "'");
            return false;
        }
    }
    slots.push_back(m_globalSymbols.size());
    m_globalSymbols.push_back(symbol);
    appendGlobalSummary(symbol);
    return true;
}

const Symbol* SemanticAnalyzer::lookupGlobal(const std::string& name) const {
    const auto it = m_globalByName.find(name);
    if (it == m_globalByName.end()) {
        return nullptr;
    }
    // 可见性解析（use 处文件 = m_currentFile）：
    // 1. 当前文件定义（私有或导出）；2. 任意文件的导出定义。
    // 单文件模式（definedIn 与 m_currentFile 皆为空）在 1 即命中，行为与
    // 既有单文件语义一致
    for (const std::size_t index : it->second) {
        if (m_globalSymbols[index].definedIn == m_currentFile) {
            return &m_globalSymbols[index];
        }
    }
    for (const std::size_t index : it->second) {
        if (m_globalSymbols[index].isExported) {
            return &m_globalSymbols[index];
        }
    }
    return nullptr;
}

std::string SemanticAnalyzer::hiddenGlobalHint(const std::string& name) const {
    const auto it = m_globalByName.find(name);
    if (it == m_globalByName.end()) {
        return "";
    }
    for (const std::size_t index : it->second) {
        const Symbol& symbol = m_globalSymbols[index];
        if (!symbol.definedIn.empty()) {
            return "'" + name + "' is defined in '" + symbol.definedIn
                   + "' but not exported";
        }
    }
    return "";
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
    summary.definedIn = symbol.definedIn;
    summary.isExported = symbol.isExported;
    summary.isExtern = symbol.isExtern;
    for (const auto& paramType : symbol.paramTypes) {
        summary.paramTypes.add(typeName(paramType));
    }
    m_result.globals.add(std::move(summary));
}

// ---- struct / typedef 登记（PRD R1.2 第二批） ----

void SemanticAnalyzer::registerStructDeclaration(const StructDeclaration& decl) {
    // 前向声明：登记 incomplete 标签；对已完整标签重复前向声明幂等合法
    if (decl.isForward) {
        if (m_structs.find(decl.tag) == m_structs.end()) {
            StructInfo info;
            info.tag = decl.tag;
            info.complete = false;
            info.line = decl.line;
            info.column = decl.column;
            m_structs.emplace(decl.tag, std::move(info));
        }
        return;
    }

    auto existing = m_structs.find(decl.tag);
    if (existing != m_structs.end() && existing->second.complete) {
        reportError(decl.line, decl.column, "redefinition of 'struct " + decl.tag + "'");
        return;
    }

    // 先登记 incomplete 标签：字段解析期间自引用（struct A { struct A a; } 与
    // struct A { struct A* next; }）才能正确判型
    {
        StructInfo placeholder;
        placeholder.tag = decl.tag;
        placeholder.complete = false;
        placeholder.line = decl.line;
        placeholder.column = decl.column;
        m_structs[decl.tag] = std::move(placeholder);
    }

    StructInfo info;
    info.tag = decl.tag;
    info.line = decl.line;
    info.column = decl.column;
    info.complete = true;

    // 布局：成员按声明顺序排布，4 字节对齐、无填充（决策见 semantic.hpp）
    int offset = 0;
    std::set<std::string> seenMembers;
    for (const auto& field : decl.fields) {
        SemanticType fieldType = declaredType(field->type,
                                              field->isStructTag,
                                              field->pointerDepth,
                                              field->isArray,
                                              field->arraySize,
                                              field->arrayDims,
                                              field->line,
                                              field->column,
                                              true);

        // void 成员
        if (fieldType.kind == SemanticType::Kind::Void) {
            reportError(field->line,
                        field->column,
                        "field '" + field->name + "' cannot have void type");
        }
        // incomplete struct 值成员（含数组元素；自引用仅允许经指针）
        bool incompleteValue = false;
        if (fieldType.kind == SemanticType::Kind::Struct) {
            incompleteValue = !isCompleteStruct(fieldType.tag);
        } else if (fieldType.kind == SemanticType::Kind::Array
                   && fieldType.element->kind == SemanticType::Kind::Struct) {
            incompleteValue = !isCompleteStruct(fieldType.element->tag);
        }
        if (incompleteValue) {
            const std::string& tag = fieldType.kind == SemanticType::Kind::Struct
                                       ? fieldType.tag
                                       : fieldType.element->tag;
            reportError(field->line,
                        field->column,
                        "field '" + field->name + "' has incomplete type 'struct " + tag
                          + "'");
        }

        if (!seenMembers.insert(field->name).second) {
            reportError(field->line,
                        field->column,
                        "duplicate member '" + field->name + "' in 'struct " + decl.tag
                          + "'");
        }

        // 成员大小：数组按元素数 × 元素大小；其余按类型大小
        int size = 0;
        if (field->isArray) {
            int elementSize = 4;
            if (fieldType.kind == SemanticType::Kind::Array
                && fieldType.element->kind == SemanticType::Kind::Struct) {
                const StructInfo* elemInfo = lookupStruct(fieldType.element->tag);
                elementSize = elemInfo != nullptr ? elemInfo->size : 4;
            }
            size = std::max(field->arraySize, 1) * elementSize;
        } else {
            size = typeSize(fieldType);
        }
        if (size <= 0) {
            size = 4; // 已报错的字段占位，保持后续偏移对齐
        }

        StructField entry;
        entry.name = field->name;
        entry.type = std::move(fieldType);
        entry.offset = offset;
        entry.size = size;
        entry.line = field->line;
        entry.column = field->column;
        offset += size;
        info.fields.push_back(std::move(entry));
    }

    if (info.fields.empty()) {
        // 空 struct 无意义：报错并保持 incomplete，后续使用由 incomplete 检查兜底
        reportError(decl.line, decl.column, "struct '" + decl.tag + "' has no members");
        info.complete = false;
    }
    info.size = offset;
    m_structs[decl.tag] = std::move(info);
}

void SemanticAnalyzer::registerTypedefDeclaration(const TypedefDeclaration& decl) {
    // 内联 struct 定义先登记（typedef struct { ... } Alias;）
    if (decl.structDef) {
        registerStructDeclaration(*decl.structDef);
    }

    SemanticType resolved = declaredType(decl.baseType,
                                         decl.baseIsStruct,
                                         decl.pointerDepth,
                                         false,
                                         0,
                                         0,
                                         decl.line,
                                         decl.column,
                                         true);

    // typedef struct X X; 幂等（C 常见自引用别名写法）：登记别名后返回
    if (resolved.kind == SemanticType::Kind::Struct && resolved.tag == decl.alias) {
        m_typedefs.emplace(decl.alias, std::move(resolved));
        return;
    }

    // 类型命名空间冲突：别名与已有 typedef/struct 标签同名（决策见 semantic.hpp）
    if (m_typedefs.find(decl.alias) != m_typedefs.end()
        || m_structs.find(decl.alias) != m_structs.end()) {
        reportError(decl.line, decl.column, "redefinition of '" + decl.alias + "'");
        return;
    }

    m_typedefs.emplace(decl.alias, std::move(resolved));
}

// ---- 声明登记 ----

void SemanticAnalyzer::registerFunctionSignature(const FuncDeclaration& decl) {
    Symbol symbol;
    symbol.kind = SymbolKind::Function;
    symbol.name = decl.name;
    // 返回类型在此处唯一校验（void* 等），诊断落在函数声明位置
    SemanticType returnType = declaredType(decl.returnType,
                                           decl.returnIsStruct,
                                           decl.returnPointerDepth,
                                           false,
                                           0,
                                           0,
                                           decl.line,
                                           decl.column,
                                           true);
    if (returnType.kind == SemanticType::Kind::Struct
        && !isCompleteStruct(returnType.tag)) {
        reportError(decl.line,
                    decl.column,
                    "function '" + decl.name + "' has incomplete return type 'struct "
                      + returnType.tag + "'");
    }
    symbol.type = std::move(returnType);
    symbol.line = decl.line;
    symbol.column = decl.column;
    symbol.definedIn = decl.sourceFile;
    symbol.isExported = decl.isExported;
    symbol.isExtern = decl.isExtern;
    symbol.isVariadic = decl.isVariadic;
    for (const auto& param : decl.parameters) {
        // 参数类型的诊断在 checkFunctionBody 中统一报告，此处静默计算
        symbol.paramTypes.add(declaredType(param->type,
                                           param->isStructTag,
                                           param->pointerDepth,
                                           param->isArray,
                                           param->arraySize,
                                           param->arrayDims,
                                           param->line,
                                           param->column,
                                           false));
    }
    declareGlobal(symbol);
}

void SemanticAnalyzer::checkGlobalVariable(const VarDeclaration& decl) {
    // match 是语句化表达式（lower 层降解为 If 链），只能出现在函数体内的
    // 语句位置；全局初始化器没有语句边界，显式拒绝（含嵌套子表达式）
    if (decl.initializer != nullptr && containsMatchExpr(*decl.initializer)) {
        reportError(decl.line,
                    decl.column,
                    "match expression is not allowed in the initializer of '" + decl.name
                      + "' (function scope only)");
    }
    SemanticType declared = declaredType(decl.type,
                                         decl.isStructTag,
                                         decl.pointerDepth,
                                         decl.isArray,
                                         decl.arraySize,
                                         decl.arrayDims,
                                         decl.line,
                                         decl.column,
                                         true);
    if (declared.kind == SemanticType::Kind::Void) {
        reportError(decl.line,
                    decl.column,
                    "variable '" + decl.name + "' cannot have void type");
    }
    // incomplete struct 值（含数组元素）不可实例化
    bool incompleteValue = false;
    if (declared.kind == SemanticType::Kind::Struct) {
        incompleteValue = !isCompleteStruct(declared.tag);
    } else if (declared.kind == SemanticType::Kind::Array
               && declared.element->kind == SemanticType::Kind::Struct) {
        incompleteValue = !isCompleteStruct(declared.element->tag);
    }
    if (incompleteValue) {
        const std::string& tag = declared.kind == SemanticType::Kind::Struct
                                   ? declared.tag
                                   : declared.element->tag;
        reportError(decl.line,
                    decl.column,
                    "variable '" + decl.name + "' has incomplete type 'struct " + tag
                      + "'");
    }
    Symbol symbol;
    symbol.kind = SymbolKind::Variable;
    symbol.name = decl.name;
    symbol.type = declared;
    symbol.line = decl.line;
    symbol.column = decl.column;
    symbol.definedIn = decl.sourceFile;
    symbol.isExported = decl.isExported;
    declareGlobal(symbol);
    if (decl.initializer) {
        checkInitializer(*decl.initializer, declared, decl.name, decl.line, decl.column);
    }
}

void SemanticAnalyzer::checkFunctionBody(const FuncDeclaration& decl) {
    m_currentFunction = &decl;
    pushScope();

    // 返回类型：用于 return 检查与缺 return 判定（签名阶段已校验，此处静默重算）
    const SemanticType fnReturnType = declaredType(decl.returnType,
                                                   decl.returnIsStruct,
                                                   decl.returnPointerDepth,
                                                   false,
                                                   0,
                                                   0,
                                                   decl.line,
                                                   decl.column,
                                                   false);

    // 参数登记在函数作用域内；解析器目前给参数记录的是函数的位置
    for (const auto& param : decl.parameters) {
        SemanticType paramType = declaredType(param->type,
                                              param->isStructTag,
                                              param->pointerDepth,
                                              param->isArray,
                                              param->arraySize,
                                              param->arrayDims,
                                              param->line,
                                              param->column,
                                              true);
        if (paramType.kind == SemanticType::Kind::Void) {
            reportError(param->line,
                        param->column,
                        "parameter '" + param->name + "' cannot have void type");
        }
        if (paramType.kind == SemanticType::Kind::Struct
            && !isCompleteStruct(paramType.tag)) {
            reportError(param->line,
                        param->column,
                        "parameter '" + param->name + "' has incomplete type 'struct "
                          + paramType.tag + "'");
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
    if (fnReturnType.kind != SemanticType::Kind::Void && !definitelyReturns(*decl.body)) {
        reportError(decl.line,
                    decl.column,
                    "missing return statement in non-void function '" + decl.name + "'");
    }

    popScope();
    m_currentFunction = nullptr;
}

void SemanticAnalyzer::checkLocalVariable(const StmtVarDeclaration& decl) {
    SemanticType declared = declaredType(decl.type,
                                         decl.isStructTag,
                                         decl.pointerDepth,
                                         decl.isArray,
                                         decl.arraySize,
                                         decl.arrayDims,
                                         decl.line,
                                         decl.column,
                                         true);
    if (declared.kind == SemanticType::Kind::Void) {
        reportError(decl.line,
                    decl.column,
                    "variable '" + decl.name + "' cannot have void type");
    }
    bool incompleteValue = false;
    if (declared.kind == SemanticType::Kind::Struct) {
        incompleteValue = !isCompleteStruct(declared.tag);
    } else if (declared.kind == SemanticType::Kind::Array
               && declared.element->kind == SemanticType::Kind::Struct) {
        incompleteValue = !isCompleteStruct(declared.element->tag);
    }
    if (incompleteValue) {
        const std::string& tag = declared.kind == SemanticType::Kind::Struct
                                   ? declared.tag
                                   : declared.element->tag;
        reportError(decl.line,
                    decl.column,
                    "variable '" + decl.name + "' has incomplete type 'struct " + tag
                      + "'");
    }
    Symbol symbol;
    symbol.kind = SymbolKind::Variable;
    symbol.name = decl.name;
    symbol.type = declared;
    symbol.line = decl.line;
    symbol.column = decl.column;
    declareVariable(symbol); // 冲突时已报错；登记失败则查找命中先登记的符号
    if (decl.initializer) {
        checkInitializer(*decl.initializer, declared, decl.name, decl.line, decl.column);
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
    case ASTNodeType::DEFER_STMT:
        // defer 语句（PRD R10；追加在既有语句分发链之后）
        checkDefer(static_cast<const DeferStmt&>(stmt));
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
    // PRD R10 硬规格：defer 体内再 return → 编译错误（defer 在作用域退出时
    // 执行，其中 return 无可行目标作用域）
    if (m_deferDepth > 0) {
        reportError(stmt.line, stmt.column, "'return' cannot appear inside a defer body");
        if (stmt.value != nullptr) {
            checkExpr(*stmt.value); // 仍检查值表达式，尽量多收集错误
        }
        return;
    }
    // 解析器保证 return 只出现在函数体内；防御式判空
    if (m_currentFunction == nullptr) {
        return;
    }
    // 返回类型已在 registerFunctionSignature 中校验，此处静默重算
    const SemanticType returnType = declaredType(m_currentFunction->returnType,
                                                 m_currentFunction->returnIsStruct,
                                                 m_currentFunction->returnPointerDepth,
                                                 false,
                                                 0,
                                                 0,
                                                 stmt.line,
                                                 stmt.column,
                                                 false);
    if (stmt.value != nullptr) {
        if (returnType.kind == SemanticType::Kind::Void) {
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
        if (returnType.kind != SemanticType::Kind::Void) {
            reportError(stmt.line,
                        stmt.column,
                        "non-void function '" + m_currentFunction->name
                          + "' must return a value");
        }
    }
}

void SemanticAnalyzer::checkBreak(const BreakStmt& stmt) {
    // defer 体在作用域退出时执行，不在任何循环体内（PRD R10 限制的对称延伸）
    if (m_deferDepth > 0) {
        reportError(stmt.line, stmt.column, "'break' cannot appear inside a defer body");
        return;
    }
    if (m_loopDepth == 0) {
        reportError(stmt.line, stmt.column, "'break' outside of a loop");
    }
}

void SemanticAnalyzer::checkContinue(const ContinueStmt& stmt) {
    if (m_deferDepth > 0) {
        reportError(stmt.line,
                    stmt.column,
                    "'continue' cannot appear inside a defer body");
        return;
    }
    if (m_loopDepth == 0) {
        reportError(stmt.line, stmt.column, "'continue' outside of a loop");
    }
}

// ---- defer / match（PRD R10/R11；追加在既有检查函数之后） ----

void SemanticAnalyzer::checkDefer(const DeferStmt& stmt) {
    // defer 内再 defer → 编译错误（任务规格；PRD R10 限制的闭合）
    if (m_deferDepth > 0) {
        reportError(stmt.line, stmt.column, "'defer' cannot appear inside a defer body");
        return;
    }
    // body 限定为表达式语句：注册时求值语义（PRD 硬规格）只对表达式形态
    // 良定义（值捕获）；块/控制流语句无注册时求值含义，显式拒绝而非误译
    if (stmt.body == nullptr || stmt.body->type != ASTNodeType::EXPR_STMT) {
        reportError(stmt.line,
                    stmt.column,
                    "'defer' body must be an expression statement");
        return;
    }
    const auto& exprStmt = static_cast<const ExprStmt&>(*stmt.body);
    if (exprStmt.expression == nullptr) {
        return; // 解析器产出契约保证非空；防御式返回
    }
    m_deferDepth++;
    checkExpr(*exprStmt.expression);
    m_deferDepth--;
}

SemanticType SemanticAnalyzer::checkMatch(const MatchExpr& expr) {
    // 主体：仅 int/char（字符串/指针/struct 值 → 错误，PRD R11）
    SemanticType subject = decayed(checkExpr(*expr.subject));
    if (subject != SemanticType::Error && !isScalar(subject)) {
        reportError(expr.subject->line,
                    expr.subject->column,
                    "match subject must be int or char, not '" + typeName(subject) + "'");
        subject = SemanticType::Error;
    }

    bool hasWildcard = false;
    for (const auto& arm : expr.arms) {
        if (arm == nullptr) {
            continue;
        }
        if (hasWildcard) {
            // 通配之后的分支永不可达（按序求值决策的自然推论）
            reportWarning(arm->line, arm->column, "unreachable match arm after wildcard");
        }

        // 守卫绑定作用域 = 所在分支（PRD R11）：分支内可遮蔽外层同名变量
        pushScope();
        for (const auto& pattern : arm->patterns) {
            if (pattern == nullptr) {
                continue;
            }
            if (pattern->kind == MatchPattern::Kind::Wildcard) {
                hasWildcard = true;
                continue;
            }
            if (pattern->kind == MatchPattern::Kind::Range) {
                // 区间含端点（决策记录：闭区间）；空区间报错
                if (pattern->lo > pattern->hi) {
                    reportError(pattern->line,
                                pattern->column,
                                "invalid range pattern '" + std::to_string(pattern->lo)
                                  + ".." + std::to_string(pattern->hi)
                                  + "': lower bound exceeds upper bound");
                }
                continue;
            }
            if (pattern->kind == MatchPattern::Kind::Guard) {
                Symbol binding;
                binding.kind = SymbolKind::Variable;
                binding.name = pattern->binding;
                binding.type = subject; // 绑定类型 = 主体类型（char/int）
                binding.line = pattern->line;
                binding.column = pattern->column;
                declareVariable(binding);
                if (pattern->guard != nullptr) {
                    checkCondition(*pattern->guard);
                }
            }
        }

        // 分支体：表达式形态须为 int/char（块形态值为 0，无类型约束）
        if (arm->exprBody != nullptr) {
            SemanticType bodyType = checkExpr(*arm->exprBody);
            if (bodyType != SemanticType::Error && !isScalar(bodyType)) {
                reportError(arm->exprBody->line,
                            arm->exprBody->column,
                            "match arm value must be int or char, not '"
                              + typeName(bodyType) + "'");
            }
        }
        if (arm->blockBody != nullptr) {
            checkStmt(*arm->blockBody);
        }
        popScope();
    }

    // 未穷尽检查（PRD R11：无 `_` → 警告而非错误；守卫不视作穷尽——条件动态）
    if (!hasWildcard) {
        reportWarning(expr.line,
                      expr.column,
                      "match has no wildcard ('_') arm; unmatched values produce 0");
    }

    // match 表达式结果类型恒为 int（char 分支值提升；块分支值为 0）
    return SemanticType::Int;
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
    case ASTNodeType::MEMBER_EXPR:
        return checkMember(static_cast<const MemberExpr&>(expr));
    case ASTNodeType::INTEGER_LITERAL:
        return SemanticType::Int;
    case ASTNodeType::CHAR_LITERAL:
        return SemanticType::Char;
    case ASTNodeType::STRING_LITERAL:
        return SemanticType::CharPtr; // 字符串字面量：数据段常量的地址
    case ASTNodeType::NULL_LITERAL:
        return SemanticType::Null;
    case ASTNodeType::MATCH_EXPR:
        // match 表达式（PRD R11；追加在既有表达式分发链之后）
        return checkMatch(static_cast<const MatchExpr&>(expr));
    default:
        return SemanticType::Error; // 不可达：表达式节点类型已穷举
    }
}

SemanticType SemanticAnalyzer::checkIdentifier(const IdentifierExpr& expr) {
    const Symbol* symbol = lookupSymbol(expr.name);
    if (symbol == nullptr) {
        const std::string hint = hiddenGlobalHint(expr.name);
        reportError(expr.line,
                    expr.column,
                    hint.empty() ? "use of undeclared identifier '" + expr.name + "'"
                                 : hint);
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
            const std::string hint = hiddenGlobalHint(ident.name);
            reportError(ident.line,
                        ident.column,
                        hint.empty() ? "use of undeclared identifier '" + ident.name + "'"
                                     : hint);
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
    case ASTNodeType::MEMBER_EXPR: {
        // p.x = v / p->x = v：成员检查给出成员类型（内部已报告成员错误）
        const auto& member = static_cast<const MemberExpr&>(*expr.target);
        context = "assignment to member '" + member.member + "'";
        targetType = checkMember(member);
        if (isArrayType(targetType)) {
            reportError(member.line,
                        member.column,
                        "cannot assign to array member '" + member.member
                          + "' (arrays are not copyable)");
            targetType = SemanticType::Error;
        }
        break;
    }
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
        if (l == r && (isPointer(l) || l.kind == SemanticType::Kind::Null)) {
            return SemanticType::Int;
        }
        // 指针与 NULL 比较合法
        if ((isPointer(l) && r.kind == SemanticType::Kind::Null)
            || (l.kind == SemanticType::Kind::Null && isPointer(r))) {
            return SemanticType::Int;
        }
        if ((isPointer(l) || l.kind == SemanticType::Kind::Null)
            && (isPointer(r) || r.kind == SemanticType::Kind::Null)) {
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
        // 指针 ± 整数：按指向类型大小缩放（标量/指针 4 字节，struct 按布局）
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

// &x：操作数必须是左值（x / a[i] / *p / p.x），结果为 pointer-to-T
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
    if (isPointer(type)) {
        // & 一级指针 → 二级指针，本里程碑不支持
        reportError(expr.line, expr.column, "multi-level pointers are not supported");
        return SemanticType::Error;
    }
    if (!isScalar(type) && type.kind != SemanticType::Kind::Struct) {
        reportError(operand.line,
                    operand.column,
                    "cannot take the address of this expression");
        return SemanticType::Error;
    }
    return SemanticType::pointerTo(type);
}

// *p：操作数必须是指针，结果为其指向类型；char* 受字节打包限制不可解引用
SemanticType SemanticAnalyzer::checkDereference(const UnaryExpr& expr) {
    SemanticType operand = decayed(checkExpr(*expr.operand));
    if (operand == SemanticType::Error) {
        return SemanticType::Error;
    }
    if (operand.kind == SemanticType::Kind::Pointer) {
        if (operand.element->kind == SemanticType::Kind::Char) {
            reportError(expr.operand->line,
                        expr.operand->column,
                        "cannot dereference 'char*' (string literals are byte-packed; "
                        "copy into a char array via a host function instead)");
            return SemanticType::Error;
        }
        return *operand.element;
    }
    if (operand.kind == SemanticType::Kind::Null) {
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

// a[i] / p[i]：base 为数组（不退化）或指针；下标必须是标量。
// char* 受字节打包限制不可下标；struct 数组/struct 指针下标得 struct 值
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
    if (base.kind == SemanticType::Kind::Array) {
        return *base.element;
    }
    if (base.kind == SemanticType::Kind::Pointer) {
        if (base.element->kind == SemanticType::Kind::Char) {
            reportError(expr.base->line,
                        expr.base->column,
                        "cannot index through 'char*' (string literals are byte-packed; "
                        "copy into a char array via a host function instead)");
            return SemanticType::Error;
        }
        return *base.element;
    }
    reportError(expr.base->line,
                expr.base->column,
                "subscripted value is not an array or pointer");
    return SemanticType::Error;
}

// p.x / p->x：dot 要求 base 为 struct 值，arrow 要求 base 为 struct 指针
// （arrow 等价 (*p).x）；成员类型查 struct 布局表
SemanticType SemanticAnalyzer::checkMember(const MemberExpr& expr) {
    SemanticType base = checkExpr(*expr.base);
    if (base == SemanticType::Error) {
        return SemanticType::Error;
    }

    if (expr.arrow) {
        if (base.kind != SemanticType::Kind::Pointer
            || base.element->kind != SemanticType::Kind::Struct) {
            reportError(expr.base->line,
                        expr.base->column,
                        "'->' requires a pointer to struct, but operand has type '"
                          + typeName(base) + "'");
            return SemanticType::Error;
        }
        base = *base.element;
    } else {
        if (base.kind == SemanticType::Kind::Pointer
            && base.element->kind == SemanticType::Kind::Struct) {
            reportError(expr.base->line,
                        expr.base->column,
                        "member access through pointer type '" + typeName(base)
                          + "'; use '->'");
            return SemanticType::Error;
        }
        if (base.kind != SemanticType::Kind::Struct) {
            reportError(expr.base->line,
                        expr.base->column,
                        "member access on non-struct type '" + typeName(base) + "'");
            return SemanticType::Error;
        }
    }

    const StructInfo* info = lookupStruct(base.tag);
    if (info == nullptr || !info->complete) {
        reportError(expr.base->line,
                    expr.base->column,
                    "member access into incomplete type 'struct " + base.tag + "'");
        return SemanticType::Error;
    }
    for (const auto& field : info->fields) {
        if (field.name == expr.member) {
            return field.type;
        }
    }
    reportError(expr.line,
                expr.column,
                "struct '" + base.tag + "' has no member named '" + expr.member + "'");
    return SemanticType::Error;
}

// 声明初始化器：普通表达式走 checkConversion；{ ... } 仅限完整 struct 变量
// （逐成员扁平、长度与成员数一致）；数组与标量目标的初始化列表报错
void SemanticAnalyzer::checkInitializer(const Expr& initializer,
                                        const SemanticType& declared,
                                        const std::string& declName,
                                        int line,
                                        int column) {
    if (initializer.type != ASTNodeType::INIT_LIST_EXPR) {
        SemanticType initType = checkExpr(initializer);
        checkConversion(initType,
                        declared,
                        line,
                        column,
                        "initialization of '" + declName + "'");
        return;
    }

    if (declared.kind == SemanticType::Kind::Array) {
        reportError(line, column, "array initializers are not supported");
        return;
    }
    if (declared.kind != SemanticType::Kind::Struct) {
        reportError(line, column, "brace initializer is only supported for struct types");
        return;
    }
    const StructInfo* info = lookupStruct(declared.tag);
    if (info == nullptr || !info->complete) {
        return; // incomplete 已在声明处报错
    }

    const auto& initList = static_cast<const InitListExpr&>(initializer);
    if (initList.values.size() != info->fields.size()) {
        reportError(line,
                    column,
                    "initializer for struct '" + declared.tag + "' expects "
                      + std::to_string(info->fields.size()) + " value(s), but got "
                      + std::to_string(initList.values.size()));
        // 仍逐个检查已提供的值，尽量多收集错误
    }
    const ca::usize checkCount =
      std::min(initList.values.size(), static_cast<ca::usize>(info->fields.size()));
    for (ca::usize i = 0; i < checkCount; ++i) {
        const StructField& field = info->fields[static_cast<size_t>(i)];
        SemanticType valueType = checkExpr(*initList.values[i]);
        checkConversion(valueType,
                        field.type,
                        initList.values[i]->line,
                        initList.values[i]->column,
                        "initialization of field '" + field.name + "' of '" + declName
                          + "'");
    }
}

SemanticType SemanticAnalyzer::checkCall(const CallExpr& expr) {
    const Symbol* symbol = lookupSymbol(expr.callee);
    if (symbol == nullptr) {
        const std::string hint = hiddenGlobalHint(expr.callee);
        reportError(expr.line,
                    expr.column,
                    hint.empty() ? "call to undeclared function '" + expr.callee + "'"
                                 : hint);
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

    // 参数个数：非 varargs 严格相等；varargs（PRD R3）实参数 ≥ 命名参数数
    const bool variadic = symbol->isVariadic;
    if (variadic ? expr.arguments.size() < symbol->paramTypes.len()
                 : expr.arguments.size() != symbol->paramTypes.len()) {
        reportError(expr.line,
                    expr.column,
                    "function '" + expr.callee + "' expects "
                      + (variadic ? "at least " : "")
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
    // varargs 可变部分（命名参数之后）：不与具体形参比对，但须为标量/指针
    // （数组退化后判断；void/struct 值报错——经宿主 ABI 无法传递）
    for (ca::usize i = checkCount; variadic && i < expr.arguments.size(); ++i) {
        const SemanticType argumentType = decayed(checkExpr(*expr.arguments[i]));
        if (argumentType == SemanticType::Error) {
            continue; // 子表达式已报错，抑制级联
        }
        if (argumentType.kind == SemanticType::Kind::Void) {
            reportError(expr.line,
                        expr.column,
                        "void value passed as variadic argument "
                          + std::to_string(static_cast<int>(i) + 1) + " of call to '"
                          + expr.callee + "'");
        } else if (argumentType.kind == SemanticType::Kind::Struct) {
            reportError(expr.line,
                        expr.column,
                        "struct value passed as variadic argument "
                          + std::to_string(static_cast<int>(i) + 1) + " of call to '"
                          + expr.callee + "' ('" + typeName(argumentType) + "')");
        }
    }
    return symbol->type;
}

void SemanticAnalyzer::checkCondition(const Expr& expr) {
    SemanticType type = decayed(checkExpr(expr));
    if (type.kind == SemanticType::Kind::Void) {
        reportError(expr.line, expr.column, "void value used as condition");
        return;
    }
    if (type.kind == SemanticType::Kind::Struct) {
        reportError(expr.line,
                    expr.column,
                    "struct value used as condition ('" + typeName(type) + "')");
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
            // 数组整体拷贝不支持（PRD R1.2）：初始化/赋值/传参中数组只能退化；
            // struct 值相等则逐字拷贝，合法
            reportError(line,
                        column,
                        "cannot copy array of type '" + typeName(from) + "' in "
                          + context);
        }
        return; // int→int、char→char、同型指针、同标签 struct
    }
    if (from.kind == SemanticType::Kind::Char && to.kind == SemanticType::Kind::Int) {
        return; // 提升：char 在需要 int 的场合无损加宽
    }
    // 数组退化：Array(elem) → Pointer(elem)（int[]/char[]/struct T[]）
    if (from.kind == SemanticType::Kind::Array && to.kind == SemanticType::Kind::Pointer
        && *from.element == *to.element) {
        return;
    }
    if (from.kind == SemanticType::Kind::Null && isPointer(to)) {
        return; // NULL 可赋给任意指针类型
    }
    if (from.kind == SemanticType::Kind::Void) {
        reportError(line, column, "void value used in " + context);
        return;
    }
    if (from.kind == SemanticType::Kind::Int && to.kind == SemanticType::Kind::Char) {
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
    // 其余一律拒绝：指针与标量互转、数组与标量、NULL 与标量、struct 与标量、
    // 不同 struct 之间等
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

void SemanticAnalyzer::reportWarning(int line, int column, const std::string& message) {
    Diagnostic diagnostic;
    diagnostic.file = m_fileName;
    diagnostic.line = line;
    diagnostic.column = column;
    diagnostic.severity = DiagnosticSeverity::Warning;
    diagnostic.message = message;
    m_result.diagnostics.add(std::move(diagnostic));
}

std::string SemanticAnalyzer::typeName(const SemanticType& type) {
    switch (type.kind) {
    case SemanticType::Kind::Int:
        return "int";
    case SemanticType::Kind::Char:
        return "char";
    case SemanticType::Kind::Void:
        return "void";
    case SemanticType::Kind::Error:
        return "<error>";
    case SemanticType::Kind::Null:
        return "NULL";
    case SemanticType::Kind::Pointer:
        return typeName(*type.element) + "*";
    case SemanticType::Kind::Array:
        return typeName(*type.element) + "[]";
    case SemanticType::Kind::Struct:
        return "struct " + type.tag;
    }
    return "<error>";
}

bool SemanticAnalyzer::isScalar(const SemanticType& type) {
    return type.kind == SemanticType::Kind::Int || type.kind == SemanticType::Kind::Char;
}

// ---- 类型工具（PRD R1.2） ----

SemanticType SemanticAnalyzer::pointerTo(SemanticType t) {
    return SemanticType::pointerTo(std::move(t));
}

SemanticType SemanticAnalyzer::arrayOf(SemanticType t) {
    return SemanticType::arrayOf(std::move(t));
}

SemanticType SemanticAnalyzer::decayed(SemanticType t) {
    if (t.kind == SemanticType::Kind::Array) {
        return SemanticType::pointerTo(*t.element);
    }
    return t;
}

bool SemanticAnalyzer::isPointer(SemanticType type) {
    return type.kind == SemanticType::Kind::Pointer;
}

bool SemanticAnalyzer::isArrayType(SemanticType type) {
    return type.kind == SemanticType::Kind::Array;
}

bool SemanticAnalyzer::isLValueExpr(const Expr& expr) {
    switch (expr.type) {
    case ASTNodeType::IDENTIFIER_EXPR:
    case ASTNodeType::INDEX_EXPR:
        return true;
    case ASTNodeType::UNARY_EXPR:
        return static_cast<const UnaryExpr&>(expr).op == "*";
    case ASTNodeType::MEMBER_EXPR: {
        const auto& member = static_cast<const MemberExpr&>(expr);
        // p->x 等价 (*p).x，恒为左值；p.x 取决于 p 是否左值
        return member.arrow || isLValueExpr(*member.base);
    }
    default:
        return false;
    }
}

// ---- struct 布局查询 ----

const StructInfo* SemanticAnalyzer::lookupStruct(const std::string& tag) const {
    auto it = m_structs.find(tag);
    return it != m_structs.end() ? &it->second : nullptr;
}

int SemanticAnalyzer::typeSize(const SemanticType& type) const {
    switch (type.kind) {
    case SemanticType::Kind::Int:
    case SemanticType::Kind::Char:
    case SemanticType::Kind::Void:
    case SemanticType::Kind::Error:
    case SemanticType::Kind::Null:
    case SemanticType::Kind::Pointer:
        return 4;
    case SemanticType::Kind::Struct: {
        const StructInfo* info = lookupStruct(type.tag);
        return info != nullptr ? info->size : 0;
    }
    case SemanticType::Kind::Array:
        // 数组长度不在类型内：由声明处结合 arraySize 计算，此处不使用
        return 0;
    }
    return 0;
}

bool SemanticAnalyzer::isCompleteStruct(const std::string& tag) const {
    const StructInfo* info = lookupStruct(tag);
    return info != nullptr && info->complete;
}

// 由声明的类型要素计算语义类型；不合法组合（未知类型/多级指针/多维数组/
// 指针数组/越界长度/void* /void[]）按 report 决定是否报错，返回 Error 毒类型
SemanticType SemanticAnalyzer::declaredType(const std::string& baseName,
                                            bool isStructTag,
                                            int pointerDepth,
                                            bool isArray,
                                            int arraySize,
                                            int arrayDims,
                                            int line,
                                            int column,
                                            bool report) {
    const std::string stars(static_cast<size_t>(pointerDepth), '*');
    const std::string typeText = (isStructTag ? "struct " : "") + baseName + stars;

    // 解析基型：builtin → typedef 别名 → struct 标签（合并的类型命名空间）
    SemanticType base;
    bool known = false;
    if (!isStructTag) {
        if (baseName == "int") {
            base = SemanticType::Int;
            known = true;
        } else if (baseName == "char") {
            base = SemanticType::Char;
            known = true;
        } else if (baseName == "void") {
            base = SemanticType::Void;
            known = true;
        } else {
            auto it = m_typedefs.find(baseName);
            if (it != m_typedefs.end()) {
                base = it->second;
                known = true;
            }
        }
    } else if (m_structs.find(baseName) != m_structs.end()) {
        base = SemanticType::structOf(baseName);
        known = true;
    }
    if (!known) {
        if (report) {
            reportError(line, column, "unknown type '" + typeText + "'");
        }
        return SemanticType::Error;
    }

    if (pointerDepth > 1
        || (pointerDepth == 1 && base.kind == SemanticType::Kind::Pointer)) {
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
        if (base.kind == SemanticType::Kind::Void) {
            if (report) {
                reportError(line, column, "cannot declare array of 'void'");
            }
            return SemanticType::Error;
        }
        return arrayOf(base);
    }

    if (pointerDepth == 1) {
        if (base.kind == SemanticType::Kind::Void) {
            if (report) {
                reportError(line, column, "'" + typeText + "' is not supported");
            }
            return SemanticType::Error;
        }
        return pointerTo(base);
    }
    return base;
}
