#include "ncc/ir.hpp"

#include "ncc/ast.hpp"

#include <map>
#include <set>
#include <sstream>
#include <utility>

// AST → IR 降级器与 dump（设计决策见 ir.hpp 文件头注释）。
//
// lower 遍历策略与 SemanticAnalyzer 保持同构（输入契约 = 已通过语义分析）：
// - 类型表（struct/typedef）按声明顺序单遍登记，全文件可见；
// - 函数签名先统一登记（前向调用/相互递归）；
// - 按声明顺序降级全局变量与函数体（全局"先声明后可见"）；
// - 作用域栈：函数体最外层块与参数共用一个作用域；嵌套块、if/while 单语句
//   分支（隐式包一层 Block）、for 整体各自独立作用域；
// - 表达式类型推导规则镜像 semantic::checkExpr（char 提升、数组退化、指针
//   算术、成员查表）；非法组合（语义层已报）降级为 Kind::Error 抑制级联。
namespace ir {

    // ---------------------------------------------------------------------------
    // IrType
    // ---------------------------------------------------------------------------

    const IrType IrType::Int{ Kind::Int };
    const IrType IrType::Char{ Kind::Char };
    const IrType IrType::Void{ Kind::Void };
    const IrType IrType::Null{ Kind::Null };
    const IrType IrType::Error{ Kind::Error };

    IrType IrType::pointerTo(IrType pointee) {
        IrType t;
        t.kind = Kind::Pointer;
        t.element = std::make_shared<const IrType>(std::move(pointee));
        return t;
    }

    IrType IrType::arrayOf(IrType elem, int length) {
        IrType t;
        t.kind = Kind::Array;
        t.length = length;
        t.element = std::make_shared<const IrType>(std::move(elem));
        return t;
    }

    IrType IrType::structOf(std::string structTag) {
        IrType t;
        t.kind = Kind::Struct;
        t.tag = std::move(structTag);
        return t;
    }

    bool IrType::operator==(const IrType& other) const {
        if (kind != other.kind) {
            return false;
        }
        if (kind == Kind::Struct && tag != other.tag) {
            return false;
        }
        if (kind == Kind::Pointer || kind == Kind::Array) {
            if (!element != !other.element) {
                return false;
            }
            if (element && *element != *other.element) {
                return false;
            }
        }
        return true; // length 不参与同一性（与 semantic 策略一致）
    }

    std::string IrType::toString() const {
        switch (kind) {
        case Kind::Int:
            return "int";
        case Kind::Char:
            return "char";
        case Kind::Void:
            return "void";
        case Kind::Null:
            return "null";
        case Kind::Error:
            return "<error>";
        case Kind::Struct:
            return "struct " + tag;
        case Kind::Pointer:
            return element ? element->toString() + "*" : "<error>*";
        case Kind::Array: {
            const std::string base = element ? element->toString() : "<error>";
            return length > 0 ? base + "[" + std::to_string(length) + "]" : base + "[]";
        }
        }
        return "<error>";
    }

    // ---------------------------------------------------------------------------
    // 降级器
    // ---------------------------------------------------------------------------

    namespace {

        // 数组名在值语境退化为 pointer-to-T（与 semantic::decayed 一致）
        IrType decayed(const IrType& type) {
            if (type.kind == IrType::Kind::Array) {
                return IrType::pointerTo(*type.element);
            }
            return type;
        }

        bool isScalar(const IrType& type) {
            return type.kind == IrType::Kind::Int || type.kind == IrType::Kind::Char;
        }

        bool isPointer(const IrType& type) { return type.kind == IrType::Kind::Pointer; }

        // 源算符 → dump 助记符（算术/比较结果与操作数区分开，后端可直接用作节点名）
        std::string binaryMnemonic(const std::string& op) {
            if (op == "+")
                return "add";
            if (op == "-")
                return "sub";
            if (op == "*")
                return "mul";
            if (op == "/")
                return "div";
            if (op == "%")
                return "mod";
            if (op == "==")
                return "eq";
            if (op == "!=")
                return "ne";
            if (op == "<")
                return "lt";
            if (op == "<=")
                return "le";
            if (op == ">")
                return "gt";
            if (op == ">=")
                return "ge";
            return op;
        }

        class Lowering {
        public:
            ca::Result<Module, std::string> run(const Program& program) {
                // 结构契约校验：与 SemanticAnalyzer::analyze 同策略（仅当 AST 违反
                // 解析器产出约定时返回 Err）
                if (program.type != ASTNodeType::PROGRAM) {
                    return ca::Err(std::string("lowering requires a Program root node"));
                }
                for (const auto& decl : program.declarations) {
                    if (decl->type == ASTNodeType::FUNC_DECLARATION) {
                        const auto& func = static_cast<const FuncDeclaration&>(*decl);
                        // extern 声明（PRD R3）与头文件原型（PRD R9）无函数体：
                        // 合法形态，不查 body
                        if (func.isExtern || func.isPrototype) {
                            continue;
                        }
                        if (func.body == nullptr
                            || func.body->type != ASTNodeType::COMPOUND_STMT) {
                            return ca::Err("function '" + func.name
                                           + "' has no valid compound body");
                        }
                    }
                }

                // 第一遍之一：struct 与 typedef 按声明顺序登记（类型全文件可见）
                for (const auto& decl : program.declarations) {
                    if (decl->type == ASTNodeType::STRUCT_DECLARATION) {
                        registerStruct(static_cast<const StructDeclaration&>(*decl));
                    } else if (decl->type == ASTNodeType::TYPEDEF_DECLARATION) {
                        registerTypedef(static_cast<const TypedefDeclaration&>(*decl));
                    }
                }

                // 第一遍之二：函数签名先统一登记（返回类型供调用点判型）。
                // 同步收集本单元有定义的函数名：头文件原型（PRD R9）与同名
                // 定义合并后不再进 externs（否则产物同时出现定义标号与同名
                // extern 导入，nas 冲突）
                std::set<std::string> definedFunctionNames;
                for (const auto& decl : program.declarations) {
                    if (decl->type == ASTNodeType::FUNC_DECLARATION) {
                        const auto& func = static_cast<const FuncDeclaration&>(*decl);
                        m_functionReturns[func.name] =
                          resolveDeclType(func.returnType,
                                          func.returnIsStruct,
                                          func.returnPointerDepth,
                                          false,
                                          0);
                        if (!func.isExtern && !func.isPrototype) {
                            definedFunctionNames.insert(func.name);
                        }
                    }
                }

                // 第二遍：按声明顺序降级全局变量与函数（全局"先声明后可见"）。
                // 作用域 0 为全局：全局变量登记于此，函数体查名自内向外可见全局
                pushScope();
                for (const auto& decl : program.declarations) {
                    if (decl->type == ASTNodeType::FUNC_DECLARATION) {
                        const auto& func = static_cast<const FuncDeclaration&>(*decl);
                        // extern 声明（PRD R3）与头文件原型（PRD R9）：只收集
                        // 签名，不进入 functions；同名去重（多文件重复 extern
                        // 幂等）；已被本单元定义覆盖的原型跳过（调用点绑定定义）
                        if (func.isExtern || func.isPrototype) {
                            if (definedFunctionNames.count(func.name) > 0) {
                                continue;
                            }
                            bool seen = false;
                            for (const auto& existing : m_module.externs) {
                                if (existing.name == func.name) {
                                    seen = true;
                                    break;
                                }
                            }
                            if (!seen) {
                                IrExternDecl entry;
                                entry.name = func.name;
                                entry.returnType =
                                  resolveDeclType(func.returnType,
                                                  func.returnIsStruct,
                                                  func.returnPointerDepth,
                                                  false,
                                                  0);
                                for (const auto& param : func.parameters) {
                                    IrParam paramEntry;
                                    paramEntry.name = param->name;
                                    paramEntry.type = resolveVarDeclType(*param);
                                    entry.params.push_back(std::move(paramEntry));
                                }
                                entry.isVariadic = func.isVariadic;
                                m_module.externs.push_back(std::move(entry));
                            }
                            continue;
                        }
                        m_module.functions.push_back(lowerFunction(func));
                    } else if (decl->type == ASTNodeType::VAR_DECLARATION) {
                        m_module.globals.push_back(
                          lowerGlobal(static_cast<const VarDeclaration&>(*decl)));
                    }
                }
                popScope();

                // 类型表按声明序导出（dump 顺序稳定）
                for (const auto& tag : m_structOrder) {
                    m_module.structs.push_back(m_structs[tag]);
                }
                m_module.typedefs = std::move(m_typedefList);

                return ca::Ok(std::move(m_module));
            }

        private:
            // 作用域：名字 → 类型（libca HashMap，与 SemanticAnalyzer::Scope 一致）
            struct Scope {
                ca::collection::HashMap<std::string, IrType> variables;
            };

            Module m_module;
            std::map<std::string, IrStructDef> m_structs;
            std::vector<std::string> m_structOrder; // struct 首次登记序（导出顺序）
            std::vector<IrTypedef> m_typedefList;
            std::map<std::string, IrType> m_typedefs;
            std::map<std::string, IrType> m_functionReturns; // 函数名 → 返回类型
            std::vector<Scope> m_scopes;
            std::vector<IrLocal>* m_locals = nullptr; // 当前函数局部表（函数外为 null）

            // ---- 作用域辅助 ----
            void pushScope() { m_scopes.emplace_back(); }
            void popScope() { m_scopes.pop_back(); }

            const IrType* lookupVariable(const std::string& name) const {
                for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
                    if (const IrType* type = it->variables.get(name)) {
                        return type;
                    }
                }
                return nullptr;
            }

            void declareVariable(const std::string& name, IrType type) {
                m_scopes.back().variables.put(name, std::move(type));
            }

            // ---- 类型解析（镜像 semantic::declaredType，非法组合降级 Error 不报错）----
            IrType resolveDeclType(const std::string& baseName,
                                   bool isStructTag,
                                   int pointerDepth,
                                   bool isArray,
                                   int arraySize) {
                IrType base;
                bool known = false;
                if (!isStructTag) {
                    if (baseName == "int") {
                        base = IrType::Int;
                        known = true;
                    } else if (baseName == "char") {
                        base = IrType::Char;
                        known = true;
                    } else if (baseName == "void") {
                        base = IrType::Void;
                        known = true;
                    } else {
                        auto it = m_typedefs.find(baseName);
                        if (it != m_typedefs.end()) {
                            base = it->second;
                            known = true;
                        }
                    }
                } else if (m_structs.find(baseName) != m_structs.end()) {
                    base = IrType::structOf(baseName);
                    known = true;
                }
                if (!known || pointerDepth > 1 || (isArray && pointerDepth > 0)) {
                    return IrType::Error;
                }
                if (isArray) {
                    if (base.kind == IrType::Kind::Void
                        || base.kind == IrType::Kind::Pointer
                        || base.kind == IrType::Kind::Array) {
                        return IrType::Error;
                    }
                    return IrType::arrayOf(std::move(base), arraySize);
                }
                if (pointerDepth == 1) {
                    if (base.kind == IrType::Kind::Void
                        || base.kind == IrType::Kind::Pointer) {
                        return IrType::Error;
                    }
                    return IrType::pointerTo(std::move(base));
                }
                return base;
            }

            IrType resolveVarDeclType(const VarDeclaration& decl) {
                return resolveDeclType(decl.type,
                                       decl.isStructTag,
                                       decl.pointerDepth,
                                       decl.isArray,
                                       decl.arraySize);
            }

            // StmtVarDeclaration 与 VarDeclaration 字段同构但无继承关系，单列重载
            IrType resolveVarDeclType(const StmtVarDeclaration& decl) {
                return resolveDeclType(decl.type,
                                       decl.isStructTag,
                                       decl.pointerDepth,
                                       decl.isArray,
                                       decl.arraySize);
            }

            // ---- 类型表登记（声明序；匿名 struct 经 typedef 内联定义）----
            void registerStruct(const StructDeclaration& decl) {
                if (m_structs.find(decl.tag) == m_structs.end()) {
                    m_structOrder.push_back(decl.tag); // 首次登记序 = 导出顺序
                }
                auto existing = m_structs.find(decl.tag);
                if (existing != m_structs.end() && existing->second.complete) {
                    return; // 重复定义已由语义层报错，此处幂等保留首个
                }
                if (decl.isForward) {
                    if (existing == m_structs.end()) {
                        IrStructDef def;
                        def.tag = decl.tag;
                        def.complete = false;
                        m_structs.emplace(decl.tag, std::move(def));
                    }
                    return;
                }

                // 先登记 incomplete 占位：字段解析期间自引用（struct Node* next）才能判型
                {
                    IrStructDef placeholder;
                    placeholder.tag = decl.tag;
                    placeholder.complete = false;
                    m_structs[decl.tag] = std::move(placeholder);
                }

                IrStructDef def;
                def.tag = decl.tag;
                def.complete =
                  !decl.fields.empty(); // 空 struct 语义层已报，按 incomplete 降级
                for (const auto& field : decl.fields) {
                    IrField entry;
                    entry.name = field->name;
                    entry.type = resolveDeclType(field->type,
                                                 field->isStructTag,
                                                 field->pointerDepth,
                                                 field->isArray,
                                                 field->arraySize);
                    def.fields.push_back(std::move(entry));
                }
                m_structs[decl.tag] = std::move(def);
            }

            void registerTypedef(const TypedefDeclaration& decl) {
                // 内联 struct 定义先登记（typedef struct { ... } Alias;）
                if (decl.structDef) {
                    registerStruct(*decl.structDef);
                }
                if (m_typedefs.find(decl.alias) != m_typedefs.end()) {
                    return; // 重定义已由语义层报错，此处幂等保留首个
                }
                IrType resolved = resolveDeclType(decl.baseType,
                                                  decl.baseIsStruct,
                                                  decl.pointerDepth,
                                                  false,
                                                  0);
                m_typedefs.emplace(decl.alias, resolved);
                IrTypedef entry;
                entry.alias = decl.alias;
                entry.type = std::move(resolved);
                m_typedefList.push_back(std::move(entry));
            }

            // ---- 表达式降级（类型推导镜像 semantic::checkExpr）----
            // 空孩子防御：解析器产出契约保证非空，越界输入降级为 Error 常量不崩溃
            std::unique_ptr<IrExpr> errorExpr(int line, int column) {
                auto node = std::make_unique<IrIntConst>(0, line, column);
                node->type = IrType::Error;
                return node;
            }

            std::unique_ptr<IrExpr> lowerExpr(const Expr& expr) {
                switch (expr.type) {
                case ASTNodeType::INTEGER_LITERAL: {
                    const auto& lit = static_cast<const IntegerLiteral&>(expr);
                    return std::make_unique<IrIntConst>(lit.value, lit.line, lit.column);
                }
                case ASTNodeType::CHAR_LITERAL: {
                    const auto& lit = static_cast<const CharLiteral&>(expr);
                    return std::make_unique<IrCharConst>(lit.value, lit.line, lit.column);
                }
                case ASTNodeType::STRING_LITERAL: {
                    const auto& lit = static_cast<const StringLiteral&>(expr);
                    return std::make_unique<IrStringConst>(lit.value,
                                                           lit.line,
                                                           lit.column);
                }
                case ASTNodeType::NULL_LITERAL:
                    return std::make_unique<IrNullConst>(expr.line, expr.column);

                case ASTNodeType::IDENTIFIER_EXPR: {
                    const auto& ident = static_cast<const IdentifierExpr&>(expr);
                    const IrType* type = lookupVariable(ident.name);
                    return std::make_unique<IrVarRef>(ident.name,
                                                      type ? *type : IrType::Error,
                                                      ident.line,
                                                      ident.column);
                }

                case ASTNodeType::UNARY_EXPR: {
                    const auto& unary = static_cast<const UnaryExpr&>(expr);
                    if (!unary.operand) {
                        return errorExpr(unary.line, unary.column);
                    }
                    std::unique_ptr<IrExpr> operand = lowerExpr(*unary.operand);
                    if (unary.op == "&") {
                        IrType type = IrType::Error;
                        if (operand->type.kind == IrType::Kind::Int
                            || operand->type.kind == IrType::Kind::Char
                            || operand->type.kind == IrType::Kind::Struct) {
                            type = IrType::pointerTo(operand->type);
                        }
                        return std::make_unique<IrAddrOfExpr>(std::move(operand),
                                                              std::move(type),
                                                              unary.line,
                                                              unary.column);
                    }
                    if (unary.op == "*") {
                        IrType type = IrType::Error;
                        const IrType operandType = decayed(operand->type);
                        if (isPointer(operandType)
                            && operandType.element->kind != IrType::Kind::Char) {
                            type = *operandType.element;
                        }
                        return std::make_unique<IrDerefExpr>(std::move(operand),
                                                             std::move(type),
                                                             unary.line,
                                                             unary.column);
                    }
                    // - 与 !：结果 int（char 提升）；操作数已报错（Error）则毒化级联
                    auto node = std::make_unique<IrUnaryExpr>(unary.op,
                                                              std::move(operand),
                                                              unary.line,
                                                              unary.column);
                    if (node->operand->type.kind == IrType::Kind::Error) {
                        node->type = IrType::Error;
                    }
                    return node;
                }

                case ASTNodeType::BINARY_EXPR: {
                    const auto& binary = static_cast<const BinaryExpr&>(expr);
                    if (!binary.left || !binary.right) {
                        return errorExpr(binary.line, binary.column);
                    }
                    std::unique_ptr<IrExpr> left = lowerExpr(*binary.left);
                    std::unique_ptr<IrExpr> right = lowerExpr(*binary.right);
                    if (binary.op == "&&" || binary.op == "||") {
                        return std::make_unique<IrLogicalExpr>(binary.op,
                                                               std::move(left),
                                                               std::move(right),
                                                               binary.line,
                                                               binary.column);
                    }
                    IrType type = IrType::Int;
                    if (left->type.kind == IrType::Kind::Error
                        || right->type.kind == IrType::Kind::Error) {
                        type = IrType::Error; // 操作数已报错，抑制级联（与语义层一致）
                    } else if (binary.op == "+" || binary.op == "-") {
                        const IrType lt = decayed(left->type);
                        const IrType rt = decayed(right->type);
                        // 指针 ± 整数 → 指针类型；整数 + 指针 → 指针类型；其余 int
                        if (isPointer(lt) && isScalar(rt)) {
                            type = lt;
                        } else if (binary.op == "+" && isScalar(lt) && isPointer(rt)) {
                            type = rt;
                        }
                    }
                    return std::make_unique<IrBinaryExpr>(binary.op,
                                                          std::move(left),
                                                          std::move(right),
                                                          std::move(type),
                                                          binary.line,
                                                          binary.column);
                }

                case ASTNodeType::ASSIGN_EXPR: {
                    const auto& assign = static_cast<const AssignExpr&>(expr);
                    if (!assign.target || !assign.value) {
                        return errorExpr(assign.line, assign.column);
                    }
                    std::unique_ptr<IrExpr> target = lowerExpr(*assign.target);
                    std::unique_ptr<IrExpr> value = lowerExpr(*assign.value);
                    return std::make_unique<IrAssignExpr>(std::move(target),
                                                          std::move(value),
                                                          assign.line,
                                                          assign.column);
                }

                case ASTNodeType::CALL_EXPR: {
                    const auto& call = static_cast<const CallExpr&>(expr);
                    auto node =
                      std::make_unique<IrCallExpr>(call.callee,
                                                   m_functionReturns.count(call.callee)
                                                     ? m_functionReturns[call.callee]
                                                     : IrType::Error,
                                                   call.line,
                                                   call.column);
                    for (const auto& argument : call.arguments) {
                        if (argument) {
                            node->arguments.push_back(lowerExpr(*argument));
                        }
                    }
                    return node;
                }

                case ASTNodeType::INDEX_EXPR: {
                    const auto& index = static_cast<const IndexExpr&>(expr);
                    if (!index.base || !index.index) {
                        return errorExpr(index.line, index.column);
                    }
                    std::unique_ptr<IrExpr> base = lowerExpr(*index.base);
                    std::unique_ptr<IrExpr> subscript = lowerExpr(*index.index);
                    IrType type = IrType::Error;
                    // 数组下标：取元素类型（char 数组合法）；指针下标与 *p 同规则
                    // （char* 禁止，字符串字节打包限制，语义层已报）
                    if (base->type.kind == IrType::Kind::Array && base->type.element) {
                        type = *base->type.element;
                    } else {
                        const IrType baseType = decayed(base->type);
                        if (isPointer(baseType) && baseType.element
                            && baseType.element->kind != IrType::Kind::Char) {
                            type = *baseType.element;
                        }
                    }
                    return std::make_unique<IrIndexExpr>(std::move(base),
                                                         std::move(subscript),
                                                         std::move(type),
                                                         index.line,
                                                         index.column);
                }

                case ASTNodeType::MEMBER_EXPR: {
                    const auto& member = static_cast<const MemberExpr&>(expr);
                    if (!member.base) {
                        return errorExpr(member.line, member.column);
                    }
                    std::unique_ptr<IrExpr> base = lowerExpr(*member.base);
                    IrType type = IrType::Error;
                    IrType valueType = base->type;
                    if (member.arrow && isPointer(valueType) && valueType.element) {
                        valueType = *valueType.element; // p->x 等价 (*p).x
                    }
                    if (valueType.kind == IrType::Kind::Struct) {
                        auto it = m_structs.find(valueType.tag);
                        if (it != m_structs.end() && it->second.complete) {
                            for (const auto& field : it->second.fields) {
                                if (field.name == member.member) {
                                    type = field.type;
                                    break;
                                }
                            }
                        }
                    }
                    return std::make_unique<IrMemberExpr>(std::move(base),
                                                          member.member,
                                                          member.arrow,
                                                          std::move(type),
                                                          member.line,
                                                          member.column);
                }

                case ASTNodeType::INIT_LIST_EXPR: {
                    // 独立出现的初始化列表无类型语境；let/global 侧会改写节点类型
                    const auto& init = static_cast<const InitListExpr&>(expr);
                    auto node = std::make_unique<IrInitListExpr>(IrType::Error,
                                                                 init.line,
                                                                 init.column);
                    for (const auto& value : init.values) {
                        if (value) {
                            node->values.push_back(lowerExpr(*value));
                        }
                    }
                    return node;
                }

                default:
                    return errorExpr(expr.line, expr.column);
                }
            }

            // 声明初始化器：{ e1, e2 } → IrInitListExpr（类型改写为声明类型）
            std::unique_ptr<IrExpr> lowerInitializer(const Expr& initializer,
                                                     const IrType& declared) {
                if (initializer.type == ASTNodeType::INIT_LIST_EXPR) {
                    std::unique_ptr<IrExpr> node = lowerExpr(initializer);
                    node->type = declared;
                    return node;
                }
                return lowerExpr(initializer);
            }

            // ---- 语句降级 ----
            std::unique_ptr<IrLetStmt> lowerVarDecl(const StmtVarDeclaration& decl) {
                IrType declared = resolveVarDeclType(decl);
                std::unique_ptr<IrExpr> init;
                if (decl.initializer) {
                    init = lowerInitializer(*decl.initializer, declared);
                }
                declareVariable(decl.name, declared);
                if (m_locals != nullptr) {
                    IrLocal local;
                    local.name = decl.name;
                    local.type = declared;
                    m_locals->push_back(std::move(local));
                }
                return std::make_unique<IrLetStmt>(decl.name,
                                                   std::move(declared),
                                                   std::move(init),
                                                   decl.line,
                                                   decl.column);
            }

            std::unique_ptr<IrBlockStmt> lowerCompound(const CompoundStmt& stmt) {
                auto block = std::make_unique<IrBlockStmt>(stmt.line, stmt.column);
                pushScope();
                for (const auto& inner : stmt.statements) {
                    if (inner) {
                        block->statements.push_back(lowerStmt(*inner));
                    }
                }
                popScope();
                return block;
            }

            // 分支/循环体的单语句形态：包临时作用域降级（与语义层策略一致），但不在
            // IR 中物化包装块——IR 结构与源码一致，作用域隔离只是降级期关注点
            std::unique_ptr<IrStmt> lowerNestedStmt(const Stmt& stmt) {
                if (stmt.type == ASTNodeType::COMPOUND_STMT) {
                    return lowerCompound(static_cast<const CompoundStmt&>(stmt));
                }
                pushScope();
                std::unique_ptr<IrStmt> lowered = lowerStmt(stmt);
                popScope();
                return lowered;
            }

            std::unique_ptr<IrStmt> lowerStmt(const Stmt& stmt) {
                switch (stmt.type) {
                case ASTNodeType::COMPOUND_STMT:
                    return lowerCompound(static_cast<const CompoundStmt&>(stmt));

                case ASTNodeType::VAR_DECLARATION:
                    return lowerVarDecl(static_cast<const StmtVarDeclaration&>(stmt));

                case ASTNodeType::IF_STMT: {
                    const auto& ifStmt = static_cast<const IfStmt&>(stmt);
                    std::unique_ptr<IrExpr> condition;
                    if (ifStmt.condition) {
                        condition = lowerExpr(*ifStmt.condition);
                    } else {
                        condition = errorExpr(ifStmt.line, ifStmt.column);
                    }
                    std::unique_ptr<IrStmt> thenBranch;
                    std::unique_ptr<IrStmt> elseBranch;
                    if (ifStmt.thenBranch) {
                        thenBranch = lowerNestedStmt(*ifStmt.thenBranch);
                    }
                    if (ifStmt.elseBranch) {
                        elseBranch = lowerNestedStmt(*ifStmt.elseBranch);
                    }
                    return std::make_unique<IrIfStmt>(std::move(condition),
                                                      std::move(thenBranch),
                                                      std::move(elseBranch),
                                                      ifStmt.line,
                                                      ifStmt.column);
                }

                case ASTNodeType::WHILE_STMT: {
                    const auto& whileStmt = static_cast<const WhileStmt&>(stmt);
                    std::unique_ptr<IrExpr> condition;
                    if (whileStmt.condition) {
                        condition = lowerExpr(*whileStmt.condition);
                    } else {
                        condition = errorExpr(whileStmt.line, whileStmt.column);
                    }
                    std::unique_ptr<IrStmt> body;
                    if (whileStmt.body) {
                        body = lowerNestedStmt(*whileStmt.body);
                    }
                    return std::make_unique<IrWhileStmt>(std::move(condition),
                                                         std::move(body),
                                                         whileStmt.line,
                                                         whileStmt.column);
                }

                case ASTNodeType::FOR_STMT: {
                    const auto& forStmt = static_cast<const ForStmt&>(stmt);
                    auto node = std::make_unique<IrForStmt>(nullptr,
                                                            nullptr,
                                                            nullptr,
                                                            nullptr,
                                                            forStmt.line,
                                                            forStmt.column);
                    pushScope(); // for 整体独立作用域（init 声明不外泄）
                    if (forStmt.init) {
                        node->init = lowerStmt(*forStmt.init);
                    }
                    if (forStmt.condition) {
                        node->condition = lowerExpr(*forStmt.condition);
                    }
                    if (forStmt.increment) {
                        node->step = lowerExpr(*forStmt.increment);
                    }
                    if (forStmt.body) {
                        node->body = lowerNestedStmt(*forStmt.body);
                    }
                    popScope();
                    return node;
                }

                case ASTNodeType::RETURN_STMT: {
                    const auto& returnStmt = static_cast<const ReturnStmt&>(stmt);
                    std::unique_ptr<IrExpr> value;
                    if (returnStmt.value) {
                        value = lowerExpr(*returnStmt.value);
                    }
                    return std::make_unique<IrReturnStmt>(std::move(value),
                                                          returnStmt.line,
                                                          returnStmt.column);
                }

                case ASTNodeType::BREAK_STMT:
                    return std::make_unique<IrBreakStmt>(stmt.line, stmt.column);
                case ASTNodeType::CONTINUE_STMT:
                    return std::make_unique<IrContinueStmt>(stmt.line, stmt.column);

                case ASTNodeType::EXPR_STMT: {
                    const auto& exprStmt = static_cast<const ExprStmt&>(stmt);
                    if (!exprStmt.expression) {
                        return std::make_unique<IrEvalStmt>(
                          errorExpr(exprStmt.line, exprStmt.column),
                          exprStmt.line,
                          exprStmt.column);
                    }
                    // 语句级赋值降为 Store（三地址形态）；表达式语境的赋值保留为节点
                    if (exprStmt.expression->type == ASTNodeType::ASSIGN_EXPR) {
                        const auto& assign =
                          static_cast<const AssignExpr&>(*exprStmt.expression);
                        if (assign.target && assign.value) {
                            return std::make_unique<IrStoreStmt>(
                              lowerExpr(*assign.target),
                              lowerExpr(*assign.value),
                              exprStmt.line,
                              exprStmt.column);
                        }
                    }
                    return std::make_unique<IrEvalStmt>(lowerExpr(*exprStmt.expression),
                                                        exprStmt.line,
                                                        exprStmt.column);
                }

                default:
                    // 不可达（语句节点已穷举）；防御性降级为 eval <error>
                    return std::make_unique<IrEvalStmt>(errorExpr(stmt.line, stmt.column),
                                                        stmt.line,
                                                        stmt.column);
                }
            }

            // ---- 函数/全局 ----
            std::unique_ptr<IrFunction> lowerFunction(const FuncDeclaration& decl) {
                auto func = std::make_unique<IrFunction>();
                func->name = decl.name;
                func->returnType = resolveDeclType(decl.returnType,
                                                   decl.returnIsStruct,
                                                   decl.returnPointerDepth,
                                                   false,
                                                   0);

                std::vector<IrLocal> locals;
                m_locals = &locals;

                // 函数体最外层块与参数共用一个作用域（与语义层一致）
                const CompoundStmt& body = static_cast<const CompoundStmt&>(*decl.body);
                func->body = std::make_unique<IrBlockStmt>(body.line, body.column);
                pushScope();
                for (const auto& param : decl.parameters) {
                    IrType type = resolveVarDeclType(*param);
                    declareVariable(param->name, type);
                    IrParam entry;
                    entry.name = param->name;
                    entry.type = std::move(type);
                    func->params.push_back(std::move(entry));
                }
                for (const auto& inner : body.statements) {
                    if (inner) {
                        func->body->statements.push_back(lowerStmt(*inner));
                    }
                }
                popScope();

                func->locals = std::move(locals);
                m_locals = nullptr;
                return func;
            }

            IrGlobal lowerGlobal(const VarDeclaration& decl) {
                IrGlobal global;
                global.name = decl.name;
                global.type = resolveVarDeclType(decl);
                if (decl.initializer) {
                    global.init = lowerInitializer(*decl.initializer, global.type);
                }
                declareVariable(decl.name, global.type);
                return global;
            }
        };

        // ---------------------------------------------------------------------------
        // dump
        // ---------------------------------------------------------------------------

        std::string escapeChar(char value) {
            switch (value) {
            case '\\':
                return "\\\\";
            case '\'':
                return "\\'";
            case '\n':
                return "\\n";
            case '\t':
                return "\\t";
            case '\r':
                return "\\r";
            case '\0':
                return "\\0";
            default:
                return std::string(1, value);
            }
        }

        std::string dumpExpr(const IrExpr& expr) {
            switch (expr.kind) {
            case IrExpr::Kind::IntConst:
                return "(const int "
                       + std::to_string(static_cast<const IrIntConst&>(expr).value) + ")";
            case IrExpr::Kind::CharConst:
                return "(const char '"
                       + escapeChar(static_cast<const IrCharConst&>(expr).value) + "')";
            case IrExpr::Kind::StringConst:
                return "(string " + expr.type.toString() + " \""
                       + static_cast<const IrStringConst&>(expr).value + "\")";
            case IrExpr::Kind::NullConst:
                return "(null)";
            case IrExpr::Kind::Var:
                return "(var " + expr.type.toString() + " "
                       + static_cast<const IrVarRef&>(expr).name + ")";
            case IrExpr::Kind::Unary: {
                const auto& unary = static_cast<const IrUnaryExpr&>(expr);
                return "(unary int " + unary.op + " " + dumpExpr(*unary.operand) + ")";
            }
            case IrExpr::Kind::Binary: {
                const auto& binary = static_cast<const IrBinaryExpr&>(expr);
                return "(" + binaryMnemonic(binary.op) + " " + expr.type.toString() + " "
                       + dumpExpr(*binary.left) + " " + dumpExpr(*binary.right) + ")";
            }
            case IrExpr::Kind::Logical: {
                const auto& logic = static_cast<const IrLogicalExpr&>(expr);
                const std::string head = logic.op == "&&" ? "logand" : "logor";
                return "(" + head + " int " + dumpExpr(*logic.left) + " "
                       + dumpExpr(*logic.right) + ")";
            }
            case IrExpr::Kind::Assign: {
                const auto& assign = static_cast<const IrAssignExpr&>(expr);
                return "(assign " + expr.type.toString() + " " + dumpExpr(*assign.target)
                       + " " + dumpExpr(*assign.value) + ")";
            }
            case IrExpr::Kind::Index: {
                const auto& index = static_cast<const IrIndexExpr&>(expr);
                return "(index " + expr.type.toString() + " " + dumpExpr(*index.base)
                       + " " + dumpExpr(*index.index) + ")";
            }
            case IrExpr::Kind::Member: {
                const auto& member = static_cast<const IrMemberExpr&>(expr);
                const std::string access = member.arrow ? "->" : ".";
                return "(member " + expr.type.toString() + " " + dumpExpr(*member.base)
                       + " " + access + member.member + ")";
            }
            case IrExpr::Kind::AddrOf: {
                const auto& addrOf = static_cast<const IrAddrOfExpr&>(expr);
                return "(addrof " + expr.type.toString() + " " + dumpExpr(*addrOf.operand)
                       + ")";
            }
            case IrExpr::Kind::Deref: {
                const auto& deref = static_cast<const IrDerefExpr&>(expr);
                return "(deref " + expr.type.toString() + " " + dumpExpr(*deref.operand)
                       + ")";
            }
            case IrExpr::Kind::Call: {
                const auto& call = static_cast<const IrCallExpr&>(expr);
                std::string out = "(call " + expr.type.toString() + " " + call.callee;
                for (const auto& argument : call.arguments) {
                    out += " " + dumpExpr(*argument);
                }
                return out + ")";
            }
            case IrExpr::Kind::InitList: {
                const auto& init = static_cast<const IrInitListExpr&>(expr);
                std::string out = "(initlist " + expr.type.toString();
                for (const auto& value : init.values) {
                    out += " " + dumpExpr(*value);
                }
                return out + ")";
            }
            }
            return "(<unknown-expr> " + expr.type.toString() + ")";
        }

        // 语句单行形态（for 头部内的 init 复用：不带缩进，不带换行）
        std::string dumpStmtInline(const IrStmt& stmt) {
            switch (stmt.kind) {
            case IrStmt::Kind::Let: {
                const auto& let = static_cast<const IrLetStmt&>(stmt);
                std::string out = "let " + let.type.toString() + " " + let.name;
                if (let.init) {
                    out += " = " + dumpExpr(*let.init);
                }
                return out;
            }
            case IrStmt::Kind::Store: {
                const auto& store = static_cast<const IrStoreStmt&>(stmt);
                return "store " + dumpExpr(*store.target) + " = "
                       + dumpExpr(*store.value);
            }
            case IrStmt::Kind::Eval:
                return "eval "
                       + dumpExpr(*static_cast<const IrEvalStmt&>(stmt).expression);
            default:
                return "<unknown-stmt>";
            }
        }

        void dumpStmt(std::ostream& out, int indent, const IrStmt& stmt);

        // 分支/循环体：块体直接以自身大括号呈现（if/while/for 已带 `{`），独立语句
        // 缩进一层；嵌套的独立 Block 语句仍按 `{ }` 输出
        void dumpBranchBody(std::ostream& out, int indent, const IrStmt& body) {
            if (body.kind == IrStmt::Kind::Block) {
                const auto& block = static_cast<const IrBlockStmt&>(body);
                for (const auto& inner : block.statements) {
                    dumpStmt(out, indent + 1, *inner);
                }
                return;
            }
            dumpStmt(out, indent + 1, body);
        }

        // if/else-if 链：else 分支又是 If 时内联展开（可读性优先）。
        // dumpIfChain 打印链头（"if cond {"），dumpIfTail 打印 then 体、else 链与收尾
        void dumpIfTail(std::ostream& out, int indent, const IrIfStmt& ifStmt) {
            const std::string pad(static_cast<size_t>(indent) * 2, ' ');
            if (ifStmt.thenBranch) {
                dumpBranchBody(out, indent, *ifStmt.thenBranch);
            }
            if (!ifStmt.elseBranch) {
                out << pad << "}\n";
                return;
            }
            if (ifStmt.elseBranch->kind == IrStmt::Kind::If) {
                const auto& elseIf = static_cast<const IrIfStmt&>(*ifStmt.elseBranch);
                out << pad << "} else if " << dumpExpr(*elseIf.condition) << " {\n";
                dumpIfTail(out, indent, elseIf);
                return;
            }
            out << pad << "} else {\n";
            dumpBranchBody(out, indent, *ifStmt.elseBranch);
            out << pad << "}\n";
        }

        void dumpIfChain(std::ostream& out, int indent, const IrIfStmt& ifStmt) {
            const std::string pad(static_cast<size_t>(indent) * 2, ' ');
            out << pad << "if " << dumpExpr(*ifStmt.condition) << " {\n";
            dumpIfTail(out, indent, ifStmt);
        }

        void dumpStmt(std::ostream& out, int indent, const IrStmt& stmt) {
            const std::string pad(static_cast<size_t>(indent) * 2, ' ');
            switch (stmt.kind) {
            case IrStmt::Kind::Let:
            case IrStmt::Kind::Store:
            case IrStmt::Kind::Eval:
                out << pad << dumpStmtInline(stmt) << "\n";
                break;
            case IrStmt::Kind::If:
                dumpIfChain(out, indent, static_cast<const IrIfStmt&>(stmt));
                break;
            case IrStmt::Kind::While: {
                const auto& whileStmt = static_cast<const IrWhileStmt&>(stmt);
                out << pad << "while " << dumpExpr(*whileStmt.condition) << " {\n";
                if (whileStmt.body) {
                    dumpBranchBody(out, indent, *whileStmt.body);
                }
                out << pad << "}\n";
                break;
            }
            case IrStmt::Kind::For: {
                const auto& forStmt = static_cast<const IrForStmt&>(stmt);
                out << pad << "for ("
                    << (forStmt.init ? dumpStmtInline(*forStmt.init) : std::string());
                out << "; "
                    << (forStmt.condition ? dumpExpr(*forStmt.condition) : std::string());
                out << "; " << (forStmt.step ? dumpExpr(*forStmt.step) : std::string());
                out << ") {\n";
                if (forStmt.body) {
                    dumpBranchBody(out, indent, *forStmt.body);
                }
                out << pad << "}\n";
                break;
            }
            case IrStmt::Kind::Return: {
                const auto& returnStmt = static_cast<const IrReturnStmt&>(stmt);
                if (returnStmt.value) {
                    out << pad << "return " << dumpExpr(*returnStmt.value) << "\n";
                } else {
                    out << pad << "return\n";
                }
                break;
            }
            case IrStmt::Kind::Break:
                out << pad << "break\n";
                break;
            case IrStmt::Kind::Continue:
                out << pad << "continue\n";
                break;
            case IrStmt::Kind::Block: {
                const auto& block = static_cast<const IrBlockStmt&>(stmt);
                out << pad << "{\n";
                for (const auto& inner : block.statements) {
                    dumpStmt(out, indent + 1, *inner);
                }
                out << pad << "}\n";
                break;
            }
            }
        }

    } // namespace

    std::string Module::dump() const {
        std::ostringstream out;
        out << "module\n";
        for (const auto& def : structs) {
            if (!def.complete) {
                out << "  struct " << def.tag << ";\n";
                continue;
            }
            out << "  struct " << def.tag << " {\n";
            for (const auto& field : def.fields) {
                out << "    field " << field.type.toString() << " " << field.name << "\n";
            }
            out << "  }\n";
        }
        for (const auto& alias : typedefs) {
            out << "  typedef " << alias.alias << " = " << alias.type.toString() << "\n";
        }
        for (const auto& global : globals) {
            out << "  global " << global.type.toString() << " " << global.name;
            if (global.init) {
                out << " = " << dumpExpr(*global.init);
            }
            out << "\n";
        }
        for (const auto& func : functions) {
            out << "  func " << func->returnType.toString() << " " << func->name << "(";
            for (size_t i = 0; i < func->params.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << func->params[i].type.toString() << " " << func->params[i].name;
            }
            out << ") {\n";
            if (!func->locals.empty()) {
                out << "    locals:";
                for (size_t i = 0; i < func->locals.size(); ++i) {
                    out << (i > 0 ? "," : "") << " " << func->locals[i].type.toString()
                        << " " << func->locals[i].name;
                }
                out << "\n";
            }
            if (func->body) {
                for (const auto& stmt : func->body->statements) {
                    dumpStmt(out, 2, *stmt);
                }
            }
            out << "  }\n";
        }
        for (const auto& ext : externs) {
            out << "  extern " << ext.returnType.toString() << " " << ext.name << "(";
            for (size_t i = 0; i < ext.params.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << ext.params[i].type.toString() << " " << ext.params[i].name;
            }
            if (ext.isVariadic) {
                if (!ext.params.empty()) {
                    out << ", ";
                }
                out << "...";
            }
            out << ");\n";
        }
        return out.str();
    }

    ca::Result<Module, std::string> lower(const Program& program) {
        Lowering lowering;
        return lowering.run(program);
    }

} // namespace ir
