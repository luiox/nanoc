#include "ncc/c_backend.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

// IR → C 发射器（设计决策见 c_backend.hpp 文件头注释）。
//
// 发射顺序：文件头注释 → include → struct 前向声明 → struct 定义（带布局
// 断言）→ typedef → 字符串字面量池 → extern 原型 → 函数原型 → 全局变量 →
// 函数定义。前置原型使函数相互递归与任意声明序可用（NanoC 语义：函数签名
// 先统一登记；全局"先声明后可见"——C 侧全局统一前置，可见性是超集）。
//
// 表达式括号策略：复合运算（Binary/Logical/Assign/Unary）作子操作数时加
// 括号；后缀操作（成员/下标）的基与解引用/取址操作数按需要加括号；其余
// 场景保持裸形态。输出可读且求值次序确定。
namespace c_backend {

    namespace {

        // -------------------------------------------------------------------
        // C 侧尺寸/对齐模型（仅用于布局断言的偏移计算，x64 目标；见头文件）
        // -------------------------------------------------------------------
        struct LayoutModel {
            explicit LayoutModel(const std::vector<ir::IrStructDef>& defs) {
                for (const auto& def : defs) {
                    m_sizes[def.tag] = computeSize(def);
                    m_aligns[def.tag] = computeAlign(def);
                }
            }

            int64_t sizeOf(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Int:
                    return 4;
                case ir::IrType::Kind::Char:
                    return 1;
                case ir::IrType::Kind::Pointer:
                    return 8;
                case ir::IrType::Kind::Array:
                    return static_cast<int64_t>(type.length) * sizeOf(*type.element);
                case ir::IrType::Kind::Struct:
                    return structSize(type.tag);
                default:
                    return 0;
                }
            }

            int64_t alignOf(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Char:
                    return 1;
                case ir::IrType::Kind::Pointer:
                    return 8;
                case ir::IrType::Kind::Int:
                    return 4;
                case ir::IrType::Kind::Array:
                    return type.element ? alignOf(*type.element) : 4;
                case ir::IrType::Kind::Struct:
                    return structAlign(type.tag);
                default:
                    return 1;
                }
            }

        private:
            std::map<std::string, int64_t> m_sizes;
            std::map<std::string, int64_t> m_aligns;

            // C 布局：偏移对齐到成员对齐，总尺寸补齐到最大对齐（本类型域内
            // 天然等价于"无填充到 4 的倍数"，分歧只来自 char/指针尺寸差）
            int64_t computeSize(const ir::IrStructDef& def) const {
                int64_t offset = 0;
                int64_t maxAlign = 1;
                for (const auto& field : def.fields) {
                    const int64_t a = alignOf(field.type);
                    maxAlign = a > maxAlign ? a : maxAlign;
                    offset = (offset + a - 1) / a * a + sizeOf(field.type);
                }
                return (offset + maxAlign - 1) / maxAlign * maxAlign;
            }

            int64_t computeAlign(const ir::IrStructDef& def) const {
                int64_t maxAlign = 1;
                for (const auto& field : def.fields) {
                    const int64_t a = alignOf(field.type);
                    maxAlign = a > maxAlign ? a : maxAlign;
                }
                return maxAlign;
            }

            int64_t structSize(const std::string& tag) const {
                auto it = m_sizes.find(tag);
                return it != m_sizes.end() ? it->second : 0;
            }

            int64_t structAlign(const std::string& tag) const {
                auto it = m_aligns.find(tag);
                return it != m_aligns.end() ? it->second : 1;
            }
        };

        // NanoC 布局（PR #44）：全成员 4 字节对齐、无填充，总尺寸 4 的倍数
        struct NanoLayout {
            static int64_t sizeOf(const ir::IrType& type,
                                  const std::map<std::string, int64_t>& structSizes) {
                switch (type.kind) {
                case ir::IrType::Kind::Int:
                case ir::IrType::Kind::Char:
                case ir::IrType::Kind::Pointer:
                    return 4;
                case ir::IrType::Kind::Array:
                    return static_cast<int64_t>(type.length)
                           * sizeOf(*type.element, structSizes);
                case ir::IrType::Kind::Struct: {
                    auto it = structSizes.find(type.tag);
                    return it != structSizes.end() ? it->second : 0;
                }
                default:
                    return 0;
                }
            }

            static std::map<std::string, int64_t>
            computeStructSizes(const std::vector<ir::IrStructDef>& defs) {
                // struct 成员尺寸只依赖成员类型，声明序单遍即可（自引用只能是
                // 指针成员，指针固定 4 字节）
                std::map<std::string, int64_t> sizes;
                for (const auto& def : defs) {
                    int64_t total = 0;
                    for (const auto& field : def.fields) {
                        total += sizeOf(field.type, sizes);
                    }
                    sizes[def.tag] = total;
                }
                return sizes;
            }
        };

        // -------------------------------------------------------------------
        // 发射器
        // -------------------------------------------------------------------
        class Emitter {
        public:
            explicit Emitter(const ir::Module& module) : m_module(module) {}

            std::string run() {
                collectModuleFacts();

                std::ostringstream out;
                emitPrelude(out);
                emitStructs(out);
                emitTypedefs(out);
                emitStringPool(out);
                emitExternPrototypes(out);
                emitFunctionPrototypes(out);
                emitGlobals(out);
                emitFunctionBodies(out);
                return out.str();
            }

        private:
            const ir::Module& m_module;
            std::map<std::string, std::string> m_stringNames; // 字面量原文 → 池名
            std::vector<std::pair<std::string, std::string>> m_stringOrder; // 池名 → 原文
            std::set<std::string> m_definedFunctions;
            std::vector<std::string> m_externNames; // 未解析外部函数（首次出现序）
            int m_stringCounter = 0;

            // ---- 语义模型 ----
            const ir::IrStructDef* findStruct(const std::string& tag) const {
                for (const auto& def : m_module.structs) {
                    if (def.tag == tag) {
                        return &def;
                    }
                }
                return nullptr;
            }

            bool isDefinedFunction(const std::string& name) const {
                return m_definedFunctions.find(name) != m_definedFunctions.end();
            }

            // ---- C 类型拼写 ----
            // 标量/指针/struct 的类型名（声明符位置之外使用）
            std::string cTypeName(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Int:
                    return "int32_t";
                case ir::IrType::Kind::Char:
                    return "char";
                case ir::IrType::Kind::Void:
                    return "void";
                case ir::IrType::Kind::Struct:
                    return "struct " + type.tag;
                case ir::IrType::Kind::Pointer:
                    return (type.element ? cTypeName(*type.element)
                                         : std::string("int32_t"))
                           + "*";
                default:
                    // Error 等防御性兜底（有效输入不会走到）
                    return "int32_t";
                }
            }

            // 声明符：T name、T name[N]（数组长度在声明符尾部拼接）
            std::string cDeclarator(const ir::IrType& type,
                                    const std::string& name) const {
                if (type.kind == ir::IrType::Kind::Array) {
                    const std::string elem =
                      type.element ? cTypeName(*type.element) : std::string("int32_t");
                    const std::string bound =
                      type.length > 0 ? std::to_string(type.length) : std::string("0");
                    return elem + " " + name + "[" + bound + "]";
                }
                return cTypeName(type) + " " + name;
            }

            // -----------------------------------------------------------------
            // 预遍历：字符串字面量池、外部函数收集
            // -----------------------------------------------------------------
            void collectModuleFacts() {
                for (const auto& func : m_module.functions) {
                    m_definedFunctions.insert(func->name);
                }
                for (const auto& global : m_module.globals) {
                    if (global.init) {
                        walkExpr(*global.init);
                    }
                }
                for (const auto& func : m_module.functions) {
                    if (func->body) {
                        for (const auto& stmt : func->body->statements) {
                            walkStmt(*stmt);
                        }
                    }
                }
            }

            void walkStmt(const ir::IrStmt& stmt) {
                switch (stmt.kind) {
                case ir::IrStmt::Kind::Let: {
                    const auto& let = static_cast<const ir::IrLetStmt&>(stmt);
                    if (let.init) {
                        walkExpr(*let.init);
                    }
                    break;
                }
                case ir::IrStmt::Kind::Store: {
                    const auto& store = static_cast<const ir::IrStoreStmt&>(stmt);
                    walkExpr(*store.target);
                    walkExpr(*store.value);
                    break;
                }
                case ir::IrStmt::Kind::Eval:
                    walkExpr(*static_cast<const ir::IrEvalStmt&>(stmt).expression);
                    break;
                case ir::IrStmt::Kind::If: {
                    const auto& ifStmt = static_cast<const ir::IrIfStmt&>(stmt);
                    walkExpr(*ifStmt.condition);
                    if (ifStmt.thenBranch) {
                        walkStmt(*ifStmt.thenBranch);
                    }
                    if (ifStmt.elseBranch) {
                        walkStmt(*ifStmt.elseBranch);
                    }
                    break;
                }
                case ir::IrStmt::Kind::While: {
                    const auto& whileStmt = static_cast<const ir::IrWhileStmt&>(stmt);
                    walkExpr(*whileStmt.condition);
                    if (whileStmt.body) {
                        walkStmt(*whileStmt.body);
                    }
                    break;
                }
                case ir::IrStmt::Kind::For: {
                    const auto& forStmt = static_cast<const ir::IrForStmt&>(stmt);
                    if (forStmt.init) {
                        walkStmt(*forStmt.init);
                    }
                    if (forStmt.condition) {
                        walkExpr(*forStmt.condition);
                    }
                    if (forStmt.step) {
                        walkExpr(*forStmt.step);
                    }
                    if (forStmt.body) {
                        walkStmt(*forStmt.body);
                    }
                    break;
                }
                case ir::IrStmt::Kind::Return: {
                    const auto& returnStmt = static_cast<const ir::IrReturnStmt&>(stmt);
                    if (returnStmt.value) {
                        walkExpr(*returnStmt.value);
                    }
                    break;
                }
                case ir::IrStmt::Kind::Block: {
                    const auto& block = static_cast<const ir::IrBlockStmt&>(stmt);
                    for (const auto& inner : block.statements) {
                        walkStmt(*inner);
                    }
                    break;
                }
                case ir::IrStmt::Kind::Break:
                case ir::IrStmt::Kind::Continue:
                    break;
                case ir::IrStmt::Kind::Yield:
                    // R12 coro：状态机变换保证后端不见 yield 点（PRD R12）
                    throw std::runtime_error(
                      "internal error: yield statement reached the C backend");
                }
            }

            void walkExpr(const ir::IrExpr& expr) {
                switch (expr.kind) {
                case ir::IrExpr::Kind::StringConst:
                    internString(static_cast<const ir::IrStringConst&>(expr).value);
                    break;
                case ir::IrExpr::Kind::Unary:
                    walkExpr(*static_cast<const ir::IrUnaryExpr&>(expr).operand);
                    break;
                case ir::IrExpr::Kind::Binary: {
                    const auto& binary = static_cast<const ir::IrBinaryExpr&>(expr);
                    walkExpr(*binary.left);
                    walkExpr(*binary.right);
                    break;
                }
                case ir::IrExpr::Kind::Logical: {
                    const auto& logic = static_cast<const ir::IrLogicalExpr&>(expr);
                    walkExpr(*logic.left);
                    walkExpr(*logic.right);
                    break;
                }
                case ir::IrExpr::Kind::Assign: {
                    const auto& assign = static_cast<const ir::IrAssignExpr&>(expr);
                    walkExpr(*assign.target);
                    walkExpr(*assign.value);
                    break;
                }
                case ir::IrExpr::Kind::Index: {
                    const auto& index = static_cast<const ir::IrIndexExpr&>(expr);
                    walkExpr(*index.base);
                    walkExpr(*index.index);
                    break;
                }
                case ir::IrExpr::Kind::Member:
                    walkExpr(*static_cast<const ir::IrMemberExpr&>(expr).base);
                    break;
                case ir::IrExpr::Kind::AddrOf:
                    walkExpr(*static_cast<const ir::IrAddrOfExpr&>(expr).operand);
                    break;
                case ir::IrExpr::Kind::Deref:
                    walkExpr(*static_cast<const ir::IrDerefExpr&>(expr).operand);
                    break;
                case ir::IrExpr::Kind::Call: {
                    const auto& call = static_cast<const ir::IrCallExpr&>(expr);
                    if (!isDefinedFunction(call.callee)) {
                        recordExtern(call.callee);
                    }
                    for (const auto& argument : call.arguments) {
                        walkExpr(*argument);
                    }
                    break;
                }
                case ir::IrExpr::Kind::InitList: {
                    const auto& init = static_cast<const ir::IrInitListExpr&>(expr);
                    for (const auto& value : init.values) {
                        walkExpr(*value);
                    }
                    break;
                }
                case ir::IrExpr::Kind::IntConst:
                case ir::IrExpr::Kind::CharConst:
                case ir::IrExpr::Kind::NullConst:
                case ir::IrExpr::Kind::Var:
                    break;
                }
            }

            void internString(const std::string& value) {
                if (m_stringNames.find(value) != m_stringNames.end()) {
                    return;
                }
                const std::string name = "NcStr" + std::to_string(m_stringCounter++);
                m_stringNames[value] = name;
                m_stringOrder.emplace_back(name, value);
            }

            void recordExtern(const std::string& name) {
                for (const auto& existing : m_externNames) {
                    if (existing == name) {
                        return;
                    }
                }
                m_externNames.push_back(name);
            }

            // -----------------------------------------------------------------
            // 字面量转义
            // -----------------------------------------------------------------
            // 字符常量：IrCharConst.value 是解码后的单字符（见 lexer），按 C
            // 规则重转义；不可打印字符用 3 位八制（避免 \x 后吞十六进制位）
            std::string escapeCharLiteral(char value) const {
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
                    break;
                }
                const unsigned char byte = static_cast<unsigned char>(value);
                if (byte < 0x20 || byte >= 0x7F) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\%03o", byte);
                    return buffer;
                }
                return std::string(1, value);
            }

            // -----------------------------------------------------------------
            // 文件级发射
            // -----------------------------------------------------------------
            void emitPrelude(std::ostringstream& out) const {
                out << "// Generated by ncc C backend (NanoC PRD R4). Do not edit.\n";
                out << "// Traceability: each `// struct:` / `// global:` / `// func:`\n";
                out << "// comment echoes the source ir::Module entity (NanoC type\n";
                out << "// spelling, same as `ir::Module::dump()`).\n";
                out << "#include <stdint.h>\n";
                out << "#include <stddef.h>\n";
            }

            void emitStructs(std::ostringstream& out) const {
                if (m_module.structs.empty()) {
                    return;
                }
                out << "\n";
                // 前向标签声明：任意声明序下的指针成员引用可用
                for (const auto& def : m_module.structs) {
                    out << "struct " << def.tag << ";\n";
                }
                const LayoutModel cModel(m_module.structs);
                const std::map<std::string, int64_t> nanoSizes =
                  NanoLayout::computeStructSizes(m_module.structs);
                for (const auto& def : m_module.structs) {
                    if (!def.complete) {
                        continue; // 前向声明已发射
                    }
                    out << "\n";
                    out << "// struct: struct " << def.tag
                        << " (NanoC layout: 4-byte aligned, no padding)\n";
                    out << "struct " << def.tag << " {\n";
                    for (const auto& field : def.fields) {
                        out << "    " << cDeclarator(field.type, field.name) << ";\n";
                    }
                    out << "};\n";

                    // 布局断言：C 侧偏移与 NanoC 规则一致时逐成员钉死
                    bool layoutMatches = true;
                    int64_t nanoOffset = 0;
                    int64_t cOffset = 0;
                    for (const auto& field : def.fields) {
                        const int64_t cAlign = cModel.alignOf(field.type);
                        cOffset = (cOffset + cAlign - 1) / cAlign * cAlign;
                        if (nanoOffset != cOffset) {
                            layoutMatches = false;
                            break;
                        }
                        nanoOffset += NanoLayout::sizeOf(field.type, nanoSizes);
                        cOffset += cModel.sizeOf(field.type);
                    }
                    if (layoutMatches && nanoSizes.count(def.tag) > 0
                        && nanoSizes.at(def.tag)
                             == cModel.sizeOf(ir::IrType::structOf(def.tag))) {
                        int64_t offset = 0;
                        for (const auto& field : def.fields) {
                            out << "_Static_assert(offsetof(struct " << def.tag << ", "
                                << field.name << ") == " << offset
                                << ", \"NanoC struct layout\");\n";
                            offset += NanoLayout::sizeOf(field.type, nanoSizes);
                        }
                        out << "_Static_assert(sizeof(struct " << def.tag
                            << ") == " << nanoSizes.at(def.tag)
                            << ", \"NanoC struct layout\");\n";
                    } else {
                        out << "// layout note: NanoC byte layout is not reproducible\n";
                        out
                          << "// under the C type mapping (char/pointer size differ);\n";
                        out << "// generated C is self-consistent for this struct.\n";
                    }
                }
            }

            void emitTypedefs(std::ostringstream& out) const {
                if (m_module.typedefs.empty()) {
                    return;
                }
                out << "\n";
                for (const auto& alias : m_module.typedefs) {
                    out << "// typedef: " << alias.alias << " = " << alias.type.toString()
                        << "\n";
                    if (alias.type.kind == ir::IrType::Kind::Array) {
                        // 数组 typedef：T alias[N]
                        const std::string elem = alias.type.element
                                                   ? cTypeName(*alias.type.element)
                                                   : std::string("int32_t");
                        out << "typedef " << elem << " " << alias.alias << "["
                            << (alias.type.length > 0 ? alias.type.length : 0) << "];\n";
                    } else {
                        out << "typedef " << cTypeName(alias.type) << " " << alias.alias
                            << ";\n";
                    }
                }
            }

            void emitStringPool(std::ostringstream& out) const {
                if (m_stringOrder.empty()) {
                    return;
                }
                out << "\n";
                out << "// string literal pool (static const char[])\n";
                for (const auto& entry : m_stringOrder) {
                    // value 保留源码引号内原文（转义序列原样，与 AST 约定一致），
                    // C 转义语法兼容，原样回填引号内即可
                    out << "static const char " << entry.first << "[] = \""
                        << entry.second << "\";\n";
                }
            }

            // extern 声明的 C 签名（PRD R3）：带参数类型与名字的完整原型；
            // varargs 尾部发射 ...；空参数表统一 (void)
            std::string cExternSignature(const ir::IrExternDecl& ext) const {
                std::string params;
                if (ext.params.empty() && !ext.isVariadic) {
                    params = "void";
                } else {
                    for (size_t i = 0; i < ext.params.size(); ++i) {
                        if (i > 0) {
                            params += ", ";
                        }
                        params += cDeclarator(ext.params[i].type, ext.params[i].name);
                    }
                    if (ext.isVariadic) {
                        if (!ext.params.empty()) {
                            params += ", ";
                        }
                        params += "...";
                    }
                }
                return "extern " + cTypeName(ext.returnType) + " " + ext.name + "("
                       + params + ")";
            }

            void emitExternPrototypes(std::ostringstream& out) const {
                // 声明的 extern（PRD R3）：按声明发射带签名 C 原型
                // 未解析外部（#37 兼容路径）：调用点 callee 无定义且无 extern
                // 声明（跳过语义门禁的降级输入），沿用无参原型
                std::vector<std::string> unresolved;
                for (const auto& name : m_externNames) {
                    bool declared = false;
                    for (const auto& ext : m_module.externs) {
                        if (ext.name == name) {
                            declared = true;
                            break;
                        }
                    }
                    if (!declared) {
                        unresolved.push_back(name);
                    }
                }
                if (m_module.externs.empty() && unresolved.empty()) {
                    return;
                }
                out << "\n";
                if (!m_module.externs.empty()) {
                    out << "// extern declarations: host-provided C functions\n";
                    for (const auto& ext : m_module.externs) {
                        out << cExternSignature(ext) << ";\n";
                    }
                }
                if (!unresolved.empty()) {
                    out << "// unresolved externals (calls to functions not defined "
                           "in this\n";
                    out << "// module and without an extern declaration)\n";
                    for (const auto& name : unresolved) {
                        out << "extern int32_t " << name << "();\n";
                    }
                }
            }

            void emitFunctionPrototypes(std::ostringstream& out) const {
                if (m_module.functions.empty()) {
                    return;
                }
                out << "\n";
                for (const auto& func : m_module.functions) {
                    out << cSignature(*func) << ";\n";
                }
            }

            void emitGlobals(std::ostringstream& out) const {
                if (m_module.globals.empty()) {
                    return;
                }
                out << "\n";
                for (const auto& global : m_module.globals) {
                    out << "// global: " << global.type.toString() << " " << global.name
                        << "\n";
                    out << cDeclarator(global.type, global.name);
                    if (global.init) {
                        out << " = " << emitExpr(*global.init);
                    }
                    out << ";\n";
                }
            }

            void emitFunctionBodies(std::ostringstream& out) const {
                for (const auto& func : m_module.functions) {
                    out << "\n";
                    out << "// func: " << func->returnType.toString() << " " << func->name
                        << "(";
                    for (size_t i = 0; i < func->params.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << func->params[i].type.toString() << " "
                            << func->params[i].name;
                    }
                    out << ")\n";
                    out << cSignature(*func) << " {\n";
                    if (func->body) {
                        for (const auto& stmt : func->body->statements) {
                            emitStmt(out, 1, *stmt);
                        }
                    }
                    out << "}\n";
                }
            }

            std::string cSignature(const ir::IrFunction& func) const {
                // 空参数表统一 (void)（C 的 () 表示无原型，(void) 才是确定签名）
                std::string params;
                if (func.params.empty()) {
                    params = "void";
                } else {
                    for (size_t i = 0; i < func.params.size(); ++i) {
                        if (i > 0) {
                            params += ", ";
                        }
                        params += cDeclarator(func.params[i].type, func.params[i].name);
                    }
                }
                if (func.returnType.kind == ir::IrType::Kind::Array) {
                    // 数组不可作返回类型（语义层已拒）；防御性兜底为首元素指针
                    const std::string elem = func.returnType.element
                                               ? cTypeName(*func.returnType.element)
                                               : std::string("int32_t");
                    return elem + "* " + func.name + "(" + params + ")";
                }
                return cTypeName(func.returnType) + " " + func.name + "(" + params + ")";
            }

            // -----------------------------------------------------------------
            // 语句发射
            // -----------------------------------------------------------------
            void
            emitStmt(std::ostringstream& out, int indent, const ir::IrStmt& stmt) const {
                const std::string pad(static_cast<size_t>(indent) * 4, ' ');
                switch (stmt.kind) {
                case ir::IrStmt::Kind::Let: {
                    const auto& let = static_cast<const ir::IrLetStmt&>(stmt);
                    out << pad << cDeclarator(let.type, let.name);
                    if (let.init) {
                        out << " = " << emitExpr(*let.init);
                    }
                    out << ";\n";
                    break;
                }
                case ir::IrStmt::Kind::Store: {
                    const auto& store = static_cast<const ir::IrStoreStmt&>(stmt);
                    out << pad << emitExpr(*store.target) << " = "
                        << emitExpr(*store.value) << ";\n";
                    break;
                }
                case ir::IrStmt::Kind::Eval:
                    out << pad
                        << emitExpr(*static_cast<const ir::IrEvalStmt&>(stmt).expression)
                        << ";\n";
                    break;
                case ir::IrStmt::Kind::If:
                    emitIf(out, indent, static_cast<const ir::IrIfStmt&>(stmt));
                    break;
                case ir::IrStmt::Kind::While: {
                    const auto& whileStmt = static_cast<const ir::IrWhileStmt&>(stmt);
                    out << pad << "while (" << emitExpr(*whileStmt.condition) << ") {\n";
                    if (whileStmt.body) {
                        emitBranchBody(out, indent, *whileStmt.body);
                    }
                    out << pad << "}\n";
                    break;
                }
                case ir::IrStmt::Kind::For: {
                    const auto& forStmt = static_cast<const ir::IrForStmt&>(stmt);
                    out << pad << "for ("
                        << (forStmt.init ? emitForInit(*forStmt.init) : std::string())
                        << "; "
                        << (forStmt.condition ? emitExpr(*forStmt.condition)
                                              : std::string())
                        << "; "
                        << (forStmt.step ? emitExpr(*forStmt.step) : std::string())
                        << ") {\n";
                    if (forStmt.body) {
                        emitBranchBody(out, indent, *forStmt.body);
                    }
                    out << pad << "}\n";
                    break;
                }
                case ir::IrStmt::Kind::Return: {
                    const auto& returnStmt = static_cast<const ir::IrReturnStmt&>(stmt);
                    if (returnStmt.value) {
                        out << pad << "return " << emitExpr(*returnStmt.value) << ";\n";
                    } else {
                        out << pad << "return;\n";
                    }
                    break;
                }
                case ir::IrStmt::Kind::Break:
                    out << pad << "break;\n";
                    break;
                case ir::IrStmt::Kind::Continue:
                    out << pad << "continue;\n";
                    break;
                case ir::IrStmt::Kind::Block: {
                    const auto& block = static_cast<const ir::IrBlockStmt&>(stmt);
                    out << pad << "{\n";
                    for (const auto& inner : block.statements) {
                        emitStmt(out, indent + 1, *inner);
                    }
                    out << pad << "}\n";
                    break;
                }
                case ir::IrStmt::Kind::Yield:
                    // R12 coro：状态机变换保证后端不见 yield 点（PRD R12）
                    throw std::runtime_error(
                      "internal error: yield statement reached the C backend");
                }
            }

            // 分支/循环体：外层大括号由 if/while/for 自身提供；块体的语句内联
            // 进本层，单语句体缩进一层（与 ir dump 的分支呈现同构）
            void emitBranchBody(std::ostringstream& out,
                                int indent,
                                const ir::IrStmt& body) const {
                if (body.kind == ir::IrStmt::Kind::Block) {
                    const auto& block = static_cast<const ir::IrBlockStmt&>(body);
                    for (const auto& inner : block.statements) {
                        emitStmt(out, indent + 1, *inner);
                    }
                    return;
                }
                emitStmt(out, indent + 1, body);
            }

            // if/else-if 链：链头在此发射（"if (cond) {"），链尾（then 体、
            // else 链与收尾大括号）交给 emitIfTail（与 ir dump 的同构拆分）
            void emitIf(std::ostringstream& out,
                        int indent,
                        const ir::IrIfStmt& ifStmt) const {
                const std::string pad(static_cast<size_t>(indent) * 4, ' ');
                out << pad << "if (" << emitExpr(*ifStmt.condition) << ") {\n";
                emitIfTail(out, indent, ifStmt);
            }

            void emitIfTail(std::ostringstream& out,
                            int indent,
                            const ir::IrIfStmt& ifStmt) const {
                const std::string pad(static_cast<size_t>(indent) * 4, ' ');
                if (ifStmt.thenBranch) {
                    emitBranchBody(out, indent, *ifStmt.thenBranch);
                }
                if (!ifStmt.elseBranch) {
                    out << pad << "}\n";
                    return;
                }
                if (ifStmt.elseBranch->kind == ir::IrStmt::Kind::If) {
                    const auto& elseIf =
                      static_cast<const ir::IrIfStmt&>(*ifStmt.elseBranch);
                    out << pad << "} else if (" << emitExpr(*elseIf.condition) << ") {\n";
                    emitIfTail(out, indent, elseIf);
                    return;
                }
                out << pad << "} else {\n";
                emitBranchBody(out, indent, *ifStmt.elseBranch);
                out << pad << "}\n";
            }

            // for 头部内的 init：声明/赋值/表达式语句去分号形态
            std::string emitForInit(const ir::IrStmt& stmt) const {
                switch (stmt.kind) {
                case ir::IrStmt::Kind::Let: {
                    const auto& let = static_cast<const ir::IrLetStmt&>(stmt);
                    std::string out = cDeclarator(let.type, let.name);
                    if (let.init) {
                        out += " = " + emitExpr(*let.init);
                    }
                    return out;
                }
                case ir::IrStmt::Kind::Store: {
                    const auto& store = static_cast<const ir::IrStoreStmt&>(stmt);
                    return emitExpr(*store.target) + " = " + emitExpr(*store.value);
                }
                case ir::IrStmt::Kind::Eval:
                    return emitExpr(*static_cast<const ir::IrEvalStmt&>(stmt).expression);
                default:
                    return "";
                }
            }

            // -----------------------------------------------------------------
            // 表达式发射
            // -----------------------------------------------------------------
            // 复合运算：作为子操作数时需要括号（求值次序确定、可读性可接受）
            static bool isComplex(const ir::IrExpr& expr) {
                switch (expr.kind) {
                case ir::IrExpr::Kind::Binary:
                case ir::IrExpr::Kind::Logical:
                case ir::IrExpr::Kind::Assign:
                case ir::IrExpr::Kind::Unary:
                    return true;
                default:
                    return false;
                }
            }

            // 后缀操作（成员/下标）的基位置：复合、解引用、取址需要括号
            // （(*p).x、(&n)->value；否则会绑定成 *(p.x)、&(n->value)）
            static bool needsParensAsBase(const ir::IrExpr& expr) {
                if (isComplex(expr)) {
                    return true;
                }
                return expr.kind == ir::IrExpr::Kind::Deref
                       || expr.kind == ir::IrExpr::Kind::AddrOf;
            }

            std::string emitOperand(const ir::IrExpr& expr) const {
                const std::string text = emitExpr(expr);
                return isComplex(expr) ? "(" + text + ")" : text;
            }

            std::string emitExpr(const ir::IrExpr& expr) const {
                switch (expr.kind) {
                case ir::IrExpr::Kind::IntConst: {
                    const int value = static_cast<const ir::IrIntConst&>(expr).value;
                    return value < 0 ? "(" + std::to_string(value) + ")"
                                     : std::to_string(value);
                }
                case ir::IrExpr::Kind::CharConst:
                    return "'"
                           + escapeCharLiteral(
                             static_cast<const ir::IrCharConst&>(expr).value)
                           + "'";
                case ir::IrExpr::Kind::StringConst:
                    return m_stringNames.at(
                      static_cast<const ir::IrStringConst&>(expr).value);
                case ir::IrExpr::Kind::NullConst:
                    return "NULL";
                case ir::IrExpr::Kind::Var:
                    return static_cast<const ir::IrVarRef&>(expr).name;
                case ir::IrExpr::Kind::Unary: {
                    const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
                    return unary.op + emitOperand(*unary.operand);
                }
                case ir::IrExpr::Kind::Binary: {
                    const auto& binary = static_cast<const ir::IrBinaryExpr&>(expr);
                    return emitOperand(*binary.left) + " " + binary.op + " "
                           + emitOperand(*binary.right);
                }
                case ir::IrExpr::Kind::Logical: {
                    const auto& logic = static_cast<const ir::IrLogicalExpr&>(expr);
                    // && / || 与 C 本征短路语义一致，直接映射
                    return emitOperand(*logic.left) + " " + logic.op + " "
                           + emitOperand(*logic.right);
                }
                case ir::IrExpr::Kind::Assign: {
                    const auto& assign = static_cast<const ir::IrAssignExpr&>(expr);
                    return emitExpr(*assign.target) + " = " + emitOperand(*assign.value);
                }
                case ir::IrExpr::Kind::Index: {
                    const auto& index = static_cast<const ir::IrIndexExpr&>(expr);
                    const std::string base = emitExpr(*index.base);
                    return (needsParensAsBase(*index.base) ? "(" + base + ")" : base)
                           + "[" + emitExpr(*index.index) + "]";
                }
                case ir::IrExpr::Kind::Member: {
                    const auto& member = static_cast<const ir::IrMemberExpr&>(expr);
                    const std::string base = emitExpr(*member.base);
                    return (needsParensAsBase(*member.base) ? "(" + base + ")" : base)
                           + (member.arrow ? "->" : ".") + member.member;
                }
                case ir::IrExpr::Kind::AddrOf:
                    return "&"
                           + emitOperand(
                             *static_cast<const ir::IrAddrOfExpr&>(expr).operand);
                case ir::IrExpr::Kind::Deref:
                    return "*"
                           + emitOperand(
                             *static_cast<const ir::IrDerefExpr&>(expr).operand);
                case ir::IrExpr::Kind::Call: {
                    const auto& call = static_cast<const ir::IrCallExpr&>(expr);
                    std::string out = call.callee + "(";
                    for (size_t i = 0; i < call.arguments.size(); ++i) {
                        if (i > 0) {
                            out += ", ";
                        }
                        out += emitExpr(*call.arguments[i]);
                    }
                    return out + ")";
                }
                case ir::IrExpr::Kind::InitList: {
                    const auto& init = static_cast<const ir::IrInitListExpr&>(expr);
                    std::string out = "{ ";
                    for (size_t i = 0; i < init.values.size(); ++i) {
                        if (i > 0) {
                            out += ", ";
                        }
                        out += emitExpr(*init.values[i]);
                    }
                    return out + " }";
                }
                }
                return "0 /* <error> */";
            }
        };

    } // namespace

    std::string emit(const ir::Module& module) {
        Emitter emitter(module);
        return emitter.run();
    }

} // namespace c_backend
