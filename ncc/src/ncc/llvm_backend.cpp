#include "ncc/llvm_backend.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

// IR → LLVM 文本 IR 发射器（设计决策见 llvm_backend.hpp 文件头注释）。
//
// 发射顺序：文件头注释 → struct 类型定义（含布局注释）→ 字符串字面量池 →
// 全局变量 → extern declare → 函数定义。LLVM 文本格式下 declare/define 与
// global 的相对顺序不影响可见性（@ 符号全模块可见），此顺序对齐 C 后端的
// 可读布局（类型/数据/外部声明/函数体）。
//
// 函数体内的值模型：标量表达式的值域是 i32（char 读值即 sext 提升，与 C
// 整型提升一致）；左值表达式（Var/Index/Member/Deref）由 emitAddr 产出地址
// （ptr），load/store 在使用点发生；struct 表达式的值是聚合值
// （load %struct.X）。所有参数与局部变量 alloca + store，alloca 统一提升到
// 入口块，SSA 化交给 LLVM opt（mem2reg），后端不做数据流分析。
namespace llvm_backend {

    namespace {

        // -------------------------------------------------------------------
        // 字符串字面量的转义处理
        // -------------------------------------------------------------------
        // IR 字符串值保留源码引号内原文（转义序列原样，与 AST 约定一致）。
        // C 后端原样回填（C 转义语法兼容）；LLVM c"..." 字面量只认 \XX
        // 十六进制转义，须先解码 C 风格转义再按 LLVM 规则重编码。
        int hexDigit(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        int octalDigit(char c) { return (c >= '0' && c <= '7') ? c - '0' : -1; }

        // 解码源码 C 风格转义为原始字节；未识别的转义取字符本身（C 行为）
        std::string decodeEscapes(const std::string& raw) {
            std::string bytes;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] != '\\' || i + 1 >= raw.size()) {
                    bytes += raw[i];
                    continue;
                }
                const char next = raw[++i];
                switch (next) {
                case 'n':
                    bytes += '\n';
                    break;
                case 't':
                    bytes += '\t';
                    break;
                case 'r':
                    bytes += '\r';
                    break;
                case '0':
                    bytes += '\0';
                    break;
                case 'a':
                    bytes += '\a';
                    break;
                case 'b':
                    bytes += '\b';
                    break;
                case 'f':
                    bytes += '\f';
                    break;
                case 'v':
                    bytes += '\v';
                    break;
                case '\\':
                    bytes += '\\';
                    break;
                case '"':
                    bytes += '"';
                    break;
                case '\'':
                    bytes += '\'';
                    break;
                case 'x': {
                    int value = 0;
                    int digits = 0;
                    while (digits < 2 && i + 1 < raw.size()
                           && hexDigit(raw[i + 1]) >= 0) {
                        value = value * 16 + hexDigit(raw[++i]);
                        ++digits;
                    }
                    if (digits == 0) {
                        bytes += next; // 无十六进制位：'x' 字面量
                    } else {
                        bytes += static_cast<char>(value);
                    }
                    break;
                }
                default: {
                    if (octalDigit(next) >= 0) {
                        int value = octalDigit(next);
                        int digits = 1;
                        while (digits < 3 && i + 1 < raw.size()
                               && octalDigit(raw[i + 1]) >= 0) {
                            value = value * 8 + octalDigit(raw[++i]);
                            ++digits;
                        }
                        bytes += static_cast<char>(value);
                    } else {
                        bytes += next; // 未识别转义：取字符本身
                    }
                    break;
                }
                }
            }
            return bytes;
        }

        // 原始字节 → LLVM c"..." 内容：可打印 ASCII（除 " 与 \）原样，
        // 其余按 \XX 十六进制转义
        std::string encodeLlvmBytes(const std::string& bytes) {
            std::string out;
            char buffer[8];
            for (const char rawByte : bytes) {
                const unsigned char byte = static_cast<unsigned char>(rawByte);
                if (byte >= 0x20 && byte < 0x7F && byte != '"' && byte != '\\') {
                    out += static_cast<char>(byte);
                } else {
                    std::snprintf(buffer, sizeof(buffer), "\\%02X", byte);
                    out += buffer;
                }
            }
            return out;
        }

        // -------------------------------------------------------------------
        // LLVM 自然布局（仅用于 struct 布局注释与指针减法缩放；
        // x86_64 datalayout：i32 4B/4B、i8 1B/1B、ptr 8B/8B）
        // -------------------------------------------------------------------
        class NaturalLayout {
        public:
            NaturalLayout(const std::map<std::string, int64_t>& sizes,
                          const std::map<std::string, int64_t>& aligns)
              : m_sizes(sizes), m_aligns(aligns) {}

            int64_t sizeOf(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Int:
                    return 4;
                case ir::IrType::Kind::Char:
                    return 1;
                case ir::IrType::Kind::Pointer:
                case ir::IrType::Kind::Null:
                    return 8;
                case ir::IrType::Kind::Array:
                    return static_cast<int64_t>(type.length) * sizeOf(*type.element);
                case ir::IrType::Kind::Struct: {
                    auto it = m_sizes.find(type.tag);
                    return it != m_sizes.end() ? it->second : 0;
                }
                default:
                    return 0;
                }
            }

            int64_t alignOf(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Char:
                    return 1;
                case ir::IrType::Kind::Int:
                    return 4;
                case ir::IrType::Kind::Pointer:
                case ir::IrType::Kind::Null:
                    return 8;
                case ir::IrType::Kind::Array:
                    return type.element ? alignOf(*type.element) : 4;
                case ir::IrType::Kind::Struct: {
                    auto it = m_aligns.find(type.tag);
                    return it != m_aligns.end() ? it->second : 8;
                }
                default:
                    return 1;
                }
            }

        private:
            const std::map<std::string, int64_t>& m_sizes;
            const std::map<std::string, int64_t>& m_aligns;
        };

        // struct 尺寸/对齐预计算（声明序单遍；自引用只能是指针成员，跨
        // struct 成员引用要求先行声明完整类型，语义层保证）
        void computeStructLayouts(const std::vector<ir::IrStructDef>& defs,
                                  std::map<std::string, int64_t>& sizes,
                                  std::map<std::string, int64_t>& aligns) {
            NaturalLayout natural(sizes, aligns);
            for (const auto& def : defs) {
                if (!def.complete) {
                    continue;
                }
                int64_t offset = 0;
                int64_t maxAlign = 1;
                for (const auto& field : def.fields) {
                    const int64_t a = natural.alignOf(field.type);
                    maxAlign = a > maxAlign ? a : maxAlign;
                    offset = (offset + a - 1) / a * a + natural.sizeOf(field.type);
                }
                sizes[def.tag] = (offset + maxAlign - 1) / maxAlign * maxAlign;
                aligns[def.tag] = maxAlign;
            }
        }

        // NanoC 布局（PR #44）：全成员 4 字节、无填充
        int64_t nanoSizeOf(const ir::IrType& type,
                           const std::map<std::string, int64_t>& structSizes) {
            switch (type.kind) {
            case ir::IrType::Kind::Int:
            case ir::IrType::Kind::Char:
            case ir::IrType::Kind::Pointer:
                return 4;
            case ir::IrType::Kind::Array:
                return static_cast<int64_t>(type.length)
                       * nanoSizeOf(*type.element, structSizes);
            case ir::IrType::Kind::Struct: {
                auto it = structSizes.find(type.tag);
                return it != structSizes.end() ? it->second : 0;
            }
            default:
                return 0;
            }
        }

        std::map<std::string, int64_t>
        nanoStructSizes(const std::vector<ir::IrStructDef>& defs) {
            std::map<std::string, int64_t> sizes;
            for (const auto& def : defs) {
                if (!def.complete) {
                    continue;
                }
                int64_t total = 0;
                for (const auto& field : def.fields) {
                    total += nanoSizeOf(field.type, sizes);
                }
                sizes[def.tag] = total;
            }
            return sizes;
        }

        // -------------------------------------------------------------------
        // 表达式求值结果（类型 + SSA 引用/立即数文本）
        // -------------------------------------------------------------------
        struct Val {
            std::string type; // "i32" / "i8" / "i1" / "ptr" / "%struct.X" / "void"
            std::string ref;  // "%t3" / "42" / "null" / "@g"
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
                m_out = &out;
                emitPrelude();
                emitStructTypes();
                emitStringPool();
                emitGlobals();
                emitExternDecls();
                emitFunctions();
                return out.str();
            }

        private:
            const ir::Module& m_module;
            std::ostringstream* m_out = nullptr;

            // ---- 模块级事实 ----
            std::map<std::string, std::string> m_stringNames; // 原文 → 池名
            std::vector<std::pair<std::string, std::string>> m_stringOrder;
            std::set<std::string> m_moduleNames; // 用户占用的 @ 名（防池名冲突）
            std::map<std::string, std::map<std::string, std::size_t>>
              m_structFields; // tag → 成员名 → 序号
            std::map<std::string, int64_t> m_structSizes;
            std::map<std::string, int64_t> m_structAligns;
            std::map<std::string, int64_t> m_nanoSizes;
            int m_stringCounter = 0;
            std::vector<std::string> m_unresolved; // 未解析外部（首次出现序）

            // 非常量全局初始化：对齐 VM 语义在 main 入口前注入
            struct DeferredInit {
                std::string global;
                const ir::IrExpr* init;
            };
            std::vector<DeferredInit> m_deferredInits;

            // ---- 函数级状态 ----
            const ir::IrFunction* m_func = nullptr;
            std::set<std::string> m_usedNames; // % 值名占用（含保留标号 entry）
            std::vector<std::map<std::string, std::string>> m_scopes;
            std::vector<std::string> m_slots; // Let 声明序 → alloca 槽名
            std::size_t m_nextSlot = 0;
            int m_tempCounter = 0;
            int m_blockCounter = 0;
            std::string m_pendingLabel; // 已分配未落盘的当前块标号
            std::string m_currentLabel; // 当前块标号（phi 前驱记录用）
            bool m_terminated = false;
            std::vector<std::pair<std::string, std::string>>
              m_loopStack; // {continue 目标, break 目标}

            // ---- 基础工具 ----
            void write(const std::string& text) { (*m_out) << text; }

            void writeLine(const std::string& text) { (*m_out) << text << "\n"; }

            std::string temp() {
                return "%" + freshName("t" + std::to_string(m_tempCounter++));
            }

            std::string freshName(const std::string& base) {
                std::string name = base;
                int suffix = 1;
                while (m_usedNames.count(name) > 0) {
                    name = base + "." + std::to_string(suffix++);
                }
                m_usedNames.insert(name);
                return name;
            }

            std::string freshBlock(const std::string& base) {
                return base + "." + std::to_string(m_blockCounter++);
            }

            // 当前块已终结时续写指令需要新块（不可达续延块，LLVM 允许）
            void emitInstr(const std::string& text) {
                if (m_terminated) {
                    placeLabel(freshBlock("cont"));
                }
                flushLabel();
                write("    " + text + "\n");
            }

            void emitTerminator(const std::string& text) {
                emitInstr(text);
                m_terminated = true;
            }

            void placeLabel(const std::string& label) {
                m_pendingLabel = label;
                m_currentLabel = label;
                m_terminated = false;
            }

            void flushLabel() {
                if (!m_pendingLabel.empty()) {
                    write(m_pendingLabel + ":\n");
                    m_pendingLabel.clear();
                }
            }

            void pushScope() { m_scopes.emplace_back(); }

            void popScope() { m_scopes.pop_back(); }

            void declareLocal(const std::string& name, const std::string& slot) {
                m_scopes.back()[name] = slot;
            }

            // 名字解析：作用域栈 → 全局 @（语义层保证可见性，查不到属降级输入）
            std::string resolveVar(const std::string& name) const {
                for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
                    auto found = it->find(name);
                    if (found != it->end()) {
                        return found->second;
                    }
                }
                return "@" + name;
            }

            // ---- LLVM 类型拼写 ----
            std::string llvmType(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Int:
                    return "i32";
                case ir::IrType::Kind::Char:
                    return "i8";
                case ir::IrType::Kind::Void:
                    return "void";
                case ir::IrType::Kind::Null:
                case ir::IrType::Kind::Pointer:
                    return "ptr";
                case ir::IrType::Kind::Struct:
                    return "%struct." + type.tag;
                case ir::IrType::Kind::Array:
                    return "[" + std::to_string(type.length) + " x "
                           + (type.element ? llvmType(*type.element) : std::string("i32"))
                           + "]";
                default:
                    // Error 等防御性兜底（有效输入不会走到）
                    return "i32";
                }
            }

            bool isComparison(const std::string& op) const {
                return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">"
                       || op == ">=";
            }

            const ir::IrStructDef* findStruct(const std::string& tag) const {
                for (const auto& def : m_module.structs) {
                    if (def.tag == tag) {
                        return &def;
                    }
                }
                return nullptr;
            }

            const ir::IrFunction* findFunction(const std::string& name) const {
                for (const auto& func : m_module.functions) {
                    if (func->name == name) {
                        return func.get();
                    }
                }
                return nullptr;
            }

            const ir::IrExternDecl* findExtern(const std::string& name) const {
                for (const auto& ext : m_module.externs) {
                    if (ext.name == name) {
                        return &ext;
                    }
                }
                return nullptr;
            }

            std::size_t fieldIndex(const ir::IrType& structType,
                                   const std::string& member) const {
                auto it = m_structFields.find(structType.tag);
                if (it == m_structFields.end()) {
                    return 0;
                }
                auto field = it->second.find(member);
                return field != it->second.end() ? field->second : 0;
            }

            // LLVM 自然尺寸（指针减法缩放用；与 NaturalLayout 口径一致）
            int64_t naturalSize(const ir::IrType& type) const {
                NaturalLayout natural(m_structSizes, m_structAligns);
                return natural.sizeOf(type);
            }

            // 数组 → 首元素指针（与 lower 的 decayed 一致）
            ir::IrType decayedType(const ir::IrType& type) const {
                if (type.kind == ir::IrType::Kind::Array && type.element) {
                    return ir::IrType::pointerTo(*type.element);
                }
                return type;
            }

            // -------------------------------------------------------------
            // 预遍历：字符串池、@ 名占用表、struct 字段索引、struct 布局
            // -------------------------------------------------------------
            void collectModuleFacts() {
                for (const auto& global : m_module.globals) {
                    m_moduleNames.insert(global.name);
                }
                for (const auto& func : m_module.functions) {
                    m_moduleNames.insert(func->name);
                }
                for (const auto& ext : m_module.externs) {
                    m_moduleNames.insert(ext.name);
                }
                for (const auto& def : m_module.structs) {
                    if (!def.complete) {
                        continue;
                    }
                    std::map<std::string, std::size_t> fields;
                    for (std::size_t i = 0; i < def.fields.size(); ++i) {
                        fields[def.fields[i].name] = i;
                    }
                    m_structFields[def.tag] = std::move(fields);
                }
                computeStructLayouts(m_module.structs, m_structSizes, m_structAligns);
                m_nanoSizes = nanoStructSizes(m_module.structs);

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
                    if (findFunction(call.callee) == nullptr
                        && findExtern(call.callee) == nullptr) {
                        recordUnresolved(call.callee);
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

            void recordUnresolved(const std::string& name) {
                for (const auto& existing : m_unresolved) {
                    if (existing == name) {
                        return;
                    }
                }
                m_unresolved.push_back(name);
            }

            void internString(const std::string& value) {
                if (m_stringNames.count(value) > 0) {
                    return;
                }
                // 池名避开用户占用的 @ 名（用户全局恰名为 NcStrK 的防御）
                while (m_moduleNames.count("NcStr" + std::to_string(m_stringCounter))
                       > 0) {
                    ++m_stringCounter;
                }
                const std::string name = "NcStr" + std::to_string(m_stringCounter++);
                m_stringNames[value] = name;
                m_stringOrder.emplace_back(name, value);
            }

            // -------------------------------------------------------------
            // 文件级发射
            // -------------------------------------------------------------
            void emitPrelude() {
                writeLine("; Generated by ncc LLVM backend (NanoC PRD R6). "
                          "Do not edit.");
                writeLine("; Type mapping: int -> i32, char -> i8 (signed, C "
                          "char semantics),");
                writeLine("; T* -> ptr (opaque pointers, LLVM 15+), struct -> "
                          "%struct.Tag.");
                writeLine("; No target triple: llc applies its host default.");
            }

            void emitStructTypes() {
                NaturalLayout natural(m_structSizes, m_structAligns);
                for (const auto& def : m_module.structs) {
                    writeLine("");
                    if (!def.complete) {
                        writeLine("; struct: struct " + def.tag + " (forward declared)");
                        writeLine("%struct." + def.tag + " = type opaque");
                        continue;
                    }
                    writeLine("; struct: struct " + def.tag
                              + " (NanoC layout: 4-byte aligned, no padding)");
                    std::string body;
                    for (std::size_t i = 0; i < def.fields.size(); ++i) {
                        if (i > 0) {
                            body += ", ";
                        }
                        body += llvmType(def.fields[i].type);
                    }
                    writeLine("%struct." + def.tag + " = type { " + body + " }");

                    // 布局注释：自然布局与 NanoC 规则一致时钉死偏移，否则留
                    // 说明（对齐 C 后端 _Static_assert/note 的决策口径）
                    bool matches = true;
                    int64_t nanoOffset = 0;
                    int64_t naturalOffset = 0;
                    std::string offsetList;
                    for (const auto& field : def.fields) {
                        const int64_t a = natural.alignOf(field.type);
                        naturalOffset = (naturalOffset + a - 1) / a * a;
                        if (nanoOffset != naturalOffset) {
                            matches = false;
                            break;
                        }
                        if (!offsetList.empty()) {
                            offsetList += ", ";
                        }
                        offsetList += std::to_string(nanoOffset);
                        nanoOffset += nanoSizeOf(field.type, m_nanoSizes);
                        naturalOffset += natural.sizeOf(field.type);
                    }
                    if (matches && m_nanoSizes.count(def.tag) > 0
                        && m_nanoSizes.at(def.tag)
                             == natural.sizeOf(ir::IrType::structOf(def.tag))) {
                        writeLine("; layout: member offsets match the NanoC"
                                  " rule ("
                                  + offsetList + "); sizeof = "
                                  + std::to_string(m_nanoSizes.at(def.tag)));
                    } else {
                        writeLine("; layout note: natural LLVM layout diverges"
                                  " from the NanoC 4-byte");
                        writeLine("; layout (char = 1 byte, pointer = 8 bytes);"
                                  " this module is");
                        writeLine("; self-consistent under the natural LLVM"
                                  " layout.");
                    }
                }
            }

            void emitStringPool() {
                for (const auto& entry : m_stringOrder) {
                    const std::string bytes =
                      decodeEscapes(entry.second) + std::string(1, '\0');
                    writeLine("@" + entry.first + " = private unnamed_addr constant ["
                              + std::to_string(bytes.size()) + " x i8] c\""
                              + encodeLlvmBytes(bytes) + "\"");
                }
            }

            // -------------------------------------------------------------
            // 常量初始化器（全局）
            // -------------------------------------------------------------
            bool isConstInit(const ir::IrExpr& expr) const {
                switch (expr.kind) {
                case ir::IrExpr::Kind::IntConst:
                case ir::IrExpr::Kind::CharConst:
                case ir::IrExpr::Kind::NullConst:
                case ir::IrExpr::Kind::StringConst:
                    return true;
                case ir::IrExpr::Kind::Unary: {
                    const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
                    return unary.op == "-" && unary.operand
                           && isConstInit(*unary.operand);
                }
                case ir::IrExpr::Kind::InitList: {
                    const auto& init = static_cast<const ir::IrInitListExpr&>(expr);
                    for (const auto& value : init.values) {
                        if (!value || !isConstInit(*value)) {
                            return false;
                        }
                    }
                    return true;
                }
                default:
                    return false;
                }
            }

            // 标量/指针常量操作数文本（调用方保证 isConstInit）
            std::string constOperand(const ir::IrExpr& expr) const {
                switch (expr.kind) {
                case ir::IrExpr::Kind::IntConst:
                    return std::to_string(static_cast<const ir::IrIntConst&>(expr).value);
                case ir::IrExpr::Kind::CharConst:
                    return std::to_string(
                      static_cast<int>(static_cast<const ir::IrCharConst&>(expr).value));
                case ir::IrExpr::Kind::NullConst:
                    return "null";
                case ir::IrExpr::Kind::StringConst:
                    return "@"
                           + m_stringNames.at(
                             static_cast<const ir::IrStringConst&>(expr).value);
                case ir::IrExpr::Kind::Unary: {
                    const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
                    return "-" + constOperand(*unary.operand);
                }
                default:
                    return "zeroinitializer";
                }
            }

            // 零值操作数（LLVM 聚合初始化器必须完整，缺省成员显式补零）
            std::string zeroOperand(const ir::IrType& type) const {
                switch (type.kind) {
                case ir::IrType::Kind::Struct:
                case ir::IrType::Kind::Array:
                    return llvmType(type) + " zeroinitializer";
                case ir::IrType::Kind::Pointer:
                case ir::IrType::Kind::Null:
                    return "null";
                default:
                    return "0";
                }
            }

            // struct 常量初始化器 { T1 v1, T2 v2, ... }（缺省成员补零）
            std::string constAggr(const ir::IrInitListExpr& init,
                                  const ir::IrStructDef& def) const {
                std::string body;
                for (std::size_t i = 0; i < def.fields.size(); ++i) {
                    if (i > 0) {
                        body += ", ";
                    }
                    const ir::IrType& fieldType = def.fields[i].type;
                    if (i < init.values.size() && init.values[i]) {
                        if (init.values[i]->kind == ir::IrExpr::Kind::InitList
                            && fieldType.kind == ir::IrType::Kind::Struct) {
                            const ir::IrStructDef* nested = findStruct(fieldType.tag);
                            const auto& nestedInit =
                              static_cast<const ir::IrInitListExpr&>(*init.values[i]);
                            body += nested ? llvmType(fieldType) + " "
                                               + constAggr(nestedInit, *nested)
                                           : zeroOperand(fieldType);
                        } else {
                            body +=
                              llvmType(fieldType) + " " + constOperand(*init.values[i]);
                        }
                    } else {
                        body += zeroOperand(fieldType);
                    }
                }
                return "{ " + body + " }";
            }

            // 全局是否可用常量初始化器（struct 逐成员要求全部常量）
            bool globalConstInit(const ir::IrGlobal& global) const {
                if (!global.init) {
                    return false;
                }
                if (global.type.kind == ir::IrType::Kind::Struct) {
                    if (global.init->kind != ir::IrExpr::Kind::InitList) {
                        return false; // struct 拷贝初始化走 main 入口注入
                    }
                    return isConstInit(*global.init);
                }
                return isConstInit(*global.init);
            }

            void emitGlobals() {
                for (const auto& global : m_module.globals) {
                    writeLine("");
                    writeLine("; global: " + global.type.toString() + " " + global.name);
                    const std::string ty = llvmType(global.type);
                    if (global.type.kind == ir::IrType::Kind::Array) {
                        // 数组整体初始化被语义拒绝；防御性零初始化
                        writeLine("@" + global.name + " = global " + ty
                                  + " zeroinitializer");
                        continue;
                    }
                    if (globalConstInit(global)) {
                        std::string init;
                        if (global.type.kind == ir::IrType::Kind::Struct) {
                            const ir::IrStructDef* def = findStruct(global.type.tag);
                            const auto& list =
                              static_cast<const ir::IrInitListExpr&>(*global.init);
                            init = def ? constAggr(list, *def) : "zeroinitializer";
                        } else {
                            init = constOperand(*global.init);
                        }
                        writeLine("@" + global.name + " = global " + ty + " " + init);
                        continue;
                    }
                    // 无初始化 / 非常量初始化：零值落地，非常量部分对齐 VM
                    // 语义在 main 入口前注入
                    writeLine("@" + global.name + " = global " + ty + " "
                              + zeroOperand(global.type));
                    if (global.init) {
                        m_deferredInits.push_back(
                          DeferredInit{ global.name, global.init.get() });
                    }
                }
            }

            void emitExternDecls() {
                if (m_module.externs.empty() && m_unresolved.empty()) {
                    return;
                }
                writeLine("");
                if (!m_module.externs.empty()) {
                    writeLine("; extern declarations: host-provided C functions");
                    for (const auto& ext : m_module.externs) {
                        std::string params;
                        for (const auto& param : ext.params) {
                            if (!params.empty()) {
                                params += ", ";
                            }
                            params += llvmType(param.type);
                        }
                        if (ext.isVariadic) {
                            if (!params.empty()) {
                                params += ", ";
                            }
                            params += "...";
                        }
                        writeLine("declare " + llvmType(ext.returnType) + " @" + ext.name
                                  + "(" + params + ")");
                    }
                }
                if (!m_unresolved.empty()) {
                    // 未解析外部（#37 兼容路径，跳过语义门禁的降级输入）：
                    // llc 不接受调用点隐式声明，按 varargs 发射 declare 承接
                    // 任意调用形状；返回类型兜底 i32（同 C 后端口径）
                    writeLine("; unresolved externals (calls to functions not"
                              " defined in this");
                    writeLine("; module and without an extern declaration)");
                    for (const auto& name : m_unresolved) {
                        writeLine("declare i32 @" + name + "(...)");
                    }
                }
            }

            // -------------------------------------------------------------
            // 函数发射
            // -------------------------------------------------------------
            void emitFunctions() {
                for (const auto& func : m_module.functions) {
                    emitFunction(*func);
                }
            }

            void emitFunction(const ir::IrFunction& func) {
                // ---- 函数级状态复位 ----
                m_func = &func;
                m_usedNames.clear();
                m_usedNames.insert("entry"); // 入口块标号保留
                m_scopes.clear();
                m_slots.clear();
                m_nextSlot = 0;
                m_tempCounter = 0;
                m_blockCounter = 0;
                m_pendingLabel.clear();
                m_currentLabel.clear();
                m_terminated = false;
                m_loopStack.clear();

                // 槽位预分配：参数值名/参数槽/局部槽（与 Let 声明序对齐）
                std::vector<std::string> paramValues;
                std::vector<std::string> paramSlots;
                for (const auto& param : func.params) {
                    paramValues.push_back("%" + freshName(param.name));
                    paramSlots.push_back("%" + freshName(param.name + ".addr"));
                }
                for (const auto& local : func.locals) {
                    m_slots.push_back(freshName(local.name));
                }

                // 溯源注释保留 NanoC 源类型拼写（同 C 后端 `// func:` 口径）
                std::string sourceParams;
                for (std::size_t i = 0; i < func.params.size(); ++i) {
                    if (i > 0) {
                        sourceParams += ", ";
                    }
                    sourceParams +=
                      func.params[i].type.toString() + " " + func.params[i].name;
                }
                writeLine("");
                writeLine("; func: " + func.returnType.toString() + " " + func.name + "("
                          + sourceParams + ")");

                std::string params;
                for (std::size_t i = 0; i < func.params.size(); ++i) {
                    if (i > 0) {
                        params += ", ";
                    }
                    params += llvmType(func.params[i].type) + " " + paramValues[i];
                }
                writeLine("define " + llvmType(func.returnType) + " @" + func.name + "("
                          + params + ") {");

                placeLabel("entry");
                for (std::size_t i = 0; i < func.params.size(); ++i) {
                    emitInstr(paramSlots[i] + " = alloca "
                              + llvmType(func.params[i].type));
                    emitInstr("store " + llvmType(func.params[i].type) + " "
                              + paramValues[i] + ", ptr " + paramSlots[i]);
                }
                for (std::size_t i = 0; i < func.locals.size(); ++i) {
                    emitInstr("%" + m_slots[i] + " = alloca "
                              + llvmType(func.locals[i].type));
                }

                // 非常量全局初始化：对齐 VM 语义（先跑全局初始化再进 main）
                if (func.name == "main") {
                    for (const auto& deferred : m_deferredInits) {
                        emitDeferredInit(deferred);
                    }
                }

                pushScope();
                for (std::size_t i = 0; i < func.params.size(); ++i) {
                    declareLocal(func.params[i].name, paramSlots[i]);
                }
                if (func.body) {
                    for (const auto& stmt : func.body->statements) {
                        emitStmt(*stmt);
                    }
                }
                popScope();

                // 尾部兜底返回（块已终止则不补）
                if (!m_terminated) {
                    emitDefaultReturn(func.returnType);
                }
                writeLine("}");
                m_func = nullptr;
            }

            void emitDefaultReturn(const ir::IrType& returnType) {
                switch (returnType.kind) {
                case ir::IrType::Kind::Void:
                    emitTerminator("ret void");
                    break;
                case ir::IrType::Kind::Char:
                    emitTerminator("ret i8 0");
                    break;
                case ir::IrType::Kind::Pointer:
                case ir::IrType::Kind::Null:
                    emitTerminator("ret ptr null");
                    break;
                case ir::IrType::Kind::Struct:
                    emitTerminator("ret " + llvmType(returnType) + " zeroinitializer");
                    break;
                default:
                    emitTerminator("ret i32 0");
                    break;
                }
            }

            void emitDeferredInit(const DeferredInit& deferred) {
                const ir::IrExpr& init = *deferred.init;
                if (init.kind == ir::IrExpr::Kind::InitList) {
                    const auto& list = static_cast<const ir::IrInitListExpr&>(init);
                    const ir::IrStructDef* def =
                      list.type.kind == ir::IrType::Kind::Struct
                        ? findStruct(list.type.tag)
                        : nullptr;
                    if (def != nullptr) {
                        // 已列成员发射存储；未列成员已被全局零初始化覆盖
                        for (std::size_t i = 0;
                             i < list.values.size() && i < def->fields.size();
                             ++i) {
                            if (!list.values[i]) {
                                continue;
                            }
                            const Val field =
                              emitFieldAddr(ir::IrType::structOf(def->tag),
                                            Val{ "ptr", "@" + deferred.global },
                                            static_cast<unsigned>(i));
                            storeTo(field,
                                    def->fields[i].type,
                                    emitValue(*list.values[i]));
                        }
                        return;
                    }
                }
                storeTo(Val{ "ptr", "@" + deferred.global }, init.type, emitValue(init));
            }

            // -------------------------------------------------------------
            // 语句发射
            // -------------------------------------------------------------
            // 分支/循环体：块体自带作用域；单语句体包临时作用域（与 lower
            // 的降级期临时作用域对应）
            void emitBranchBody(const ir::IrStmt& body) {
                if (body.kind == ir::IrStmt::Kind::Block) {
                    emitStmt(body);
                    return;
                }
                pushScope();
                emitStmt(body);
                popScope();
            }

            void emitStmt(const ir::IrStmt& stmt) {
                switch (stmt.kind) {
                case ir::IrStmt::Kind::Let:
                    emitLet(static_cast<const ir::IrLetStmt&>(stmt));
                    break;
                case ir::IrStmt::Kind::Store: {
                    const auto& store = static_cast<const ir::IrStoreStmt&>(stmt);
                    const Val addr = emitAddr(*store.target);
                    storeTo(addr, store.target->type, emitValue(*store.value));
                    break;
                }
                case ir::IrStmt::Kind::Eval:
                    // 值丢弃；求值副作用（含短路块分裂）保留
                    emitValue(*static_cast<const ir::IrEvalStmt&>(stmt).expression);
                    break;
                case ir::IrStmt::Kind::If:
                    emitIf(static_cast<const ir::IrIfStmt&>(stmt));
                    break;
                case ir::IrStmt::Kind::While:
                    emitWhile(static_cast<const ir::IrWhileStmt&>(stmt));
                    break;
                case ir::IrStmt::Kind::For:
                    emitFor(static_cast<const ir::IrForStmt&>(stmt));
                    break;
                case ir::IrStmt::Kind::Return: {
                    const auto& returnStmt = static_cast<const ir::IrReturnStmt&>(stmt);
                    if (returnStmt.value) {
                        emitReturnValue(*m_func, emitValue(*returnStmt.value));
                    } else {
                        emitTerminator("ret void");
                    }
                    break;
                }
                case ir::IrStmt::Kind::Break:
                    if (!m_loopStack.empty()) {
                        emitTerminator("br label %" + m_loopStack.back().second);
                    }
                    break;
                case ir::IrStmt::Kind::Continue:
                    if (!m_loopStack.empty()) {
                        emitTerminator("br label %" + m_loopStack.back().first);
                    }
                    break;
                case ir::IrStmt::Kind::Block: {
                    const auto& block = static_cast<const ir::IrBlockStmt&>(stmt);
                    pushScope();
                    for (const auto& inner : block.statements) {
                        emitStmt(*inner);
                    }
                    popScope();
                    break;
                }
                }
            }

            void emitLet(const ir::IrLetStmt& let) {
                // 槽位已在入口块预分配（alloca 提升避免循环体反复吃栈）
                const std::string slot = "%" + m_slots[m_nextSlot++];
                declareLocal(let.name, slot);

                if (!let.init) {
                    return;
                }
                if (let.type.kind == ir::IrType::Kind::Struct
                    && let.init->kind == ir::IrExpr::Kind::InitList) {
                    emitStructInitList(static_cast<const ir::IrInitListExpr&>(*let.init),
                                       let.type,
                                       Val{ "ptr", slot });
                    return;
                }
                if (let.type.kind == ir::IrType::Kind::Array) {
                    return; // 数组整体初始化被语义拒绝；防御性跳过
                }
                storeTo(Val{ "ptr", slot }, let.type, emitValue(*let.init));
            }

            // struct 逐成员初始化：已列成员按值发射，缺省成员补零（C 语义）
            void emitStructInitList(const ir::IrInitListExpr& init,
                                    const ir::IrType& structType,
                                    const Val& addr) {
                const ir::IrStructDef* def = findStruct(structType.tag);
                if (def == nullptr) {
                    return;
                }
                for (std::size_t i = 0; i < def->fields.size(); ++i) {
                    const Val field =
                      emitFieldAddr(structType, addr, static_cast<unsigned>(i));
                    if (i < init.values.size() && init.values[i]) {
                        storeTo(field, def->fields[i].type, emitValue(*init.values[i]));
                    } else {
                        storeZero(field, def->fields[i].type);
                    }
                }
            }

            void storeZero(const Val& addr, const ir::IrType& targetType) {
                switch (targetType.kind) {
                case ir::IrType::Kind::Struct:
                case ir::IrType::Kind::Array:
                    emitInstr("store " + llvmType(targetType) + " zeroinitializer, ptr "
                              + addr.ref);
                    break;
                case ir::IrType::Kind::Pointer:
                case ir::IrType::Kind::Null:
                    emitInstr("store ptr null, ptr " + addr.ref);
                    break;
                default:
                    emitInstr("store " + llvmType(targetType) + " 0, ptr " + addr.ref);
                    break;
                }
            }

            void emitIf(const ir::IrIfStmt& ifStmt) {
                const std::string thenLabel = freshBlock("if.then");
                const std::string elseLabel =
                  ifStmt.elseBranch ? freshBlock("if.else") : std::string();
                const std::string endLabel = freshBlock("if.end");
                const std::string falseLabel = elseLabel.empty() ? endLabel : elseLabel;

                emitCondBranch(*ifStmt.condition, thenLabel, falseLabel);
                placeLabel(thenLabel);
                if (ifStmt.thenBranch) {
                    emitBranchBody(*ifStmt.thenBranch);
                }
                if (!m_terminated) {
                    emitTerminator("br label %" + endLabel);
                }
                if (ifStmt.elseBranch) {
                    placeLabel(elseLabel);
                    emitBranchBody(*ifStmt.elseBranch);
                    if (!m_terminated) {
                        emitTerminator("br label %" + endLabel);
                    }
                }
                placeLabel(endLabel);
            }

            void emitWhile(const ir::IrWhileStmt& whileStmt) {
                const std::string condLabel = freshBlock("while.cond");
                const std::string bodyLabel = freshBlock("while.body");
                const std::string endLabel = freshBlock("while.end");

                emitTerminator("br label %" + condLabel);
                placeLabel(condLabel);
                if (whileStmt.condition) {
                    emitCondBranch(*whileStmt.condition, bodyLabel, endLabel);
                } else {
                    emitTerminator("br label %" + bodyLabel);
                }
                placeLabel(bodyLabel);
                m_loopStack.emplace_back(condLabel, endLabel);
                if (whileStmt.body) {
                    emitBranchBody(*whileStmt.body);
                }
                m_loopStack.pop_back();
                if (!m_terminated) {
                    emitTerminator("br label %" + condLabel);
                }
                placeLabel(endLabel);
            }

            void emitFor(const ir::IrForStmt& forStmt) {
                pushScope(); // for 整体独立作用域（init 声明不外泄）
                if (forStmt.init) {
                    emitStmt(*forStmt.init);
                }
                const std::string condLabel = freshBlock("for.cond");
                const std::string bodyLabel = freshBlock("for.body");
                const std::string stepLabel = freshBlock("for.step");
                const std::string endLabel = freshBlock("for.end");

                emitTerminator("br label %" + condLabel);
                placeLabel(condLabel);
                if (forStmt.condition) {
                    emitCondBranch(*forStmt.condition, bodyLabel, endLabel);
                } else {
                    emitTerminator("br label %" + bodyLabel);
                }
                placeLabel(bodyLabel);
                m_loopStack.emplace_back(stepLabel, endLabel); // continue → step
                if (forStmt.body) {
                    emitBranchBody(*forStmt.body);
                }
                m_loopStack.pop_back();
                if (!m_terminated) {
                    emitTerminator("br label %" + (forStmt.step ? stepLabel : condLabel));
                }
                if (forStmt.step) {
                    placeLabel(stepLabel);
                    emitValue(*forStmt.step); // 副作用保留，值丢弃
                    emitTerminator("br label %" + condLabel);
                }
                placeLabel(endLabel);
                popScope();
            }

            void emitReturnValue(const ir::IrFunction& func, const Val& value) {
                const std::string ret = llvmType(func.returnType);
                if (func.returnType.kind == ir::IrType::Kind::Void
                    || value.type == "void") {
                    emitTerminator("ret void");
                    return;
                }
                if (value.type == ret) {
                    emitTerminator("ret " + ret + " " + value.ref);
                    return;
                }
                // 值域统一 i32（char 提升）：按返回类型收窄/扩展
                const Val converted = convertForStore(func.returnType, value);
                emitTerminator("ret " + ret + " " + converted.ref);
            }

            // -------------------------------------------------------------
            // 条件发射
            // -------------------------------------------------------------
            void emitCondBranch(const ir::IrExpr& cond,
                                const std::string& trueLabel,
                                const std::string& falseLabel) {
                const std::string flag = condI1(cond);
                emitTerminator("br i1 " + flag + ", label %" + trueLabel + ", label %"
                               + falseLabel);
            }

            // 条件语境 i1：短路块分裂、! 反转、其余经值域统一
            std::string condI1(const ir::IrExpr& expr) {
                switch (expr.kind) {
                case ir::IrExpr::Kind::Logical:
                    return emitLogicalI1(static_cast<const ir::IrLogicalExpr&>(expr));
                case ir::IrExpr::Kind::Unary: {
                    const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
                    if (unary.op == "!" && unary.operand) {
                        const std::string inner = condI1(*unary.operand);
                        const std::string t = temp();
                        emitInstr(t + " = xor i1 " + inner + ", true");
                        return t;
                    }
                    break;
                }
                default:
                    break;
                }
                return boolI1Of(expr);
            }

            // 表达式 → i1：比较直出，其余值域统一后 icmp ne 0
            std::string boolI1Of(const ir::IrExpr& expr) {
                if (expr.kind == ir::IrExpr::Kind::Binary) {
                    const auto& binary = static_cast<const ir::IrBinaryExpr&>(expr);
                    if (isComparison(binary.op)) {
                        return emitIcmp(binary);
                    }
                }
                return toBoolI1(emitValue(expr));
            }

            // 值 → i1（i1 直通；ptr 判非空；标量判非零）
            std::string toBoolI1(const Val& value) {
                if (value.type == "i1") {
                    return value.ref;
                }
                const std::string t = temp();
                if (value.type == "ptr") {
                    emitInstr(t + " = icmp ne ptr " + value.ref + ", null");
                } else {
                    const Val v = asI32(value);
                    emitInstr(t + " = icmp ne i32 " + v.ref + ", 0");
                }
                return t;
            }

            // 比较指令（标量按有符号谓词；指针相等直比、序比较经 ptrtoint
            // 取无符号口径——C 对指针序未定义）
            std::string emitIcmp(const ir::IrBinaryExpr& binary) {
                const Val left = emitValue(*binary.left);
                const Val right = emitValue(*binary.right);
                std::string predicate;
                if (binary.op == "==") {
                    predicate = "eq";
                } else if (binary.op == "!=") {
                    predicate = "ne";
                } else if (binary.op == "<") {
                    predicate = "slt";
                } else if (binary.op == "<=") {
                    predicate = "sle";
                } else if (binary.op == ">") {
                    predicate = "sgt";
                } else {
                    predicate = "sge";
                }
                const std::string t = temp();
                if (left.type == "ptr" || right.type == "ptr") {
                    if (predicate == "eq" || predicate == "ne") {
                        emitInstr(t + " = icmp " + predicate + " ptr " + left.ref + ", "
                                  + right.ref);
                        return t;
                    }
                    const std::string li = temp();
                    emitInstr(li + " = ptrtoint ptr " + left.ref + " to i64");
                    const std::string ri = temp();
                    emitInstr(ri + " = ptrtoint ptr " + right.ref + " to i64");
                    const std::string u =
                      "u" + predicate.substr(1); // slt → ult 等无符号序
                    emitInstr(t + " = icmp " + u + " i64 " + li + ", " + ri);
                    return t;
                }
                const Val l = asI32(left);
                const Val r = asI32(right);
                emitInstr(t + " = icmp " + predicate + " i32 " + l.ref + ", " + r.ref);
                return t;
            }

            // 短路块分裂（&&/||）：phi 归并布尔结果
            std::string emitLogicalI1(const ir::IrLogicalExpr& logic) {
                const bool isAnd = logic.op == "&&";
                const std::string leftFlag = boolI1Of(*logic.left);
                const std::string preLabel = m_currentLabel;
                const std::string rhsLabel = freshBlock(isAnd ? "land.rhs" : "lor.rhs");
                const std::string endLabel = freshBlock(isAnd ? "land.end" : "lor.end");
                if (isAnd) {
                    emitTerminator("br i1 " + leftFlag + ", label %" + rhsLabel
                                   + ", label %" + endLabel);
                } else {
                    emitTerminator("br i1 " + leftFlag + ", label %" + endLabel
                                   + ", label %" + rhsLabel);
                }
                placeLabel(rhsLabel);
                const std::string rightFlag = boolI1Of(*logic.right);
                const std::string rhsBlock = m_currentLabel;
                emitTerminator("br label %" + endLabel);
                placeLabel(endLabel);
                const std::string t = temp();
                emitInstr(t + " = phi i1 [ " + (isAnd ? "false" : "true") + ", %"
                          + preLabel + " ], [ " + rightFlag + ", %" + rhsBlock + " ]");
                return t;
            }

            // -------------------------------------------------------------
            // 值/地址发射
            // -------------------------------------------------------------
            // i32 值域统一（char sext、i1 zext；其余类型原样，降级输入由
            // 语义层保证）
            Val asI32(const Val& value) {
                if (value.type == "i32") {
                    return value;
                }
                if (value.type == "i8") {
                    const std::string t = temp();
                    emitInstr(t + " = sext i8 " + value.ref + " to i32");
                    return Val{ "i32", t };
                }
                if (value.type == "i1") {
                    const std::string t = temp();
                    emitInstr(t + " = zext i1 " + value.ref + " to i32");
                    return Val{ "i32", t };
                }
                return Val{ "i32", value.ref };
            }

            // 存储前类型收敛（值域 i32 → lvalue 声明类型；立即数直接改写
            // 文本，不生成 trunc 指令）
            Val convertForStore(const ir::IrType& targetType, const Val& value) {
                const std::string ty = llvmType(targetType);
                if (value.type == ty || value.type.empty()) {
                    return value;
                }
                if (ty == "i8") {
                    if (value.type == "i32" && isIntLiteral(value.ref)) {
                        return Val{ "i8", value.ref };
                    }
                    const Val widened = asI32(value);
                    const std::string t = temp();
                    emitInstr(t + " = trunc i32 " + widened.ref + " to i8");
                    return Val{ "i8", t };
                }
                if (ty == "i32") {
                    return asI32(value);
                }
                return value; // ptr / 聚合：不转换
            }

            // 立即数文本（可选负号 + 全数字）
            static bool isIntLiteral(const std::string& text) {
                std::size_t start = !text.empty() && text[0] == '-' ? 1 : 0;
                if (start >= text.size()) {
                    return false;
                }
                for (std::size_t i = start; i < text.size(); ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
                        return false;
                    }
                }
                return true;
            }

            void
            storeTo(const Val& addr, const ir::IrType& targetType, const Val& value) {
                if (value.type == "void") {
                    return;
                }
                const Val converted = convertForStore(targetType, value);
                emitInstr("store " + converted.type + " " + converted.ref + ", ptr "
                          + addr.ref);
            }

            Val loadFrom(const Val& addr, const ir::IrType& type) {
                const std::string ty = llvmType(type);
                if (ty == "void") {
                    return Val{ "i32", "0" };
                }
                const std::string t = temp();
                emitInstr(t + " = load " + ty + ", ptr " + addr.ref);
                if (ty == "i8") {
                    const std::string s = temp();
                    emitInstr(s + " = sext i8 " + t + " to i32");
                    return Val{ "i32", s }; // char 读值即提升（C 整型提升）
                }
                return Val{ ty, t };
            }

            // struct 字段地址：gep 按成员序号（布局见 %struct 定义注释）
            Val
            emitFieldAddr(const ir::IrType& structType, const Val& base, unsigned index) {
                const std::string t = temp();
                emitInstr(t + " = getelementptr inbounds " + llvmType(structType)
                          + ", ptr " + base.ref + ", i32 0, i32 "
                          + std::to_string(index));
                return Val{ "ptr", t };
            }

            Val emitValue(const ir::IrExpr& expr) {
                switch (expr.kind) {
                case ir::IrExpr::Kind::IntConst:
                    return Val{ "i32",
                                std::to_string(
                                  static_cast<const ir::IrIntConst&>(expr).value) };
                case ir::IrExpr::Kind::CharConst:
                    // char 常量按有符号提升到 i32（C 整型提升）
                    return Val{ "i32",
                                std::to_string(static_cast<int>(
                                  static_cast<const ir::IrCharConst&>(expr).value)) };
                case ir::IrExpr::Kind::NullConst:
                    return Val{ "ptr", "null" };
                case ir::IrExpr::Kind::StringConst: {
                    const auto& literal = static_cast<const ir::IrStringConst&>(expr);
                    const std::string bytes = decodeEscapes(literal.value);
                    const std::string t = temp();
                    emitInstr(t + " = getelementptr inbounds ["
                              + std::to_string(bytes.size() + 1) + " x i8], ptr @"
                              + m_stringNames.at(literal.value) + ", i32 0, i32 0");
                    return Val{ "ptr", t };
                }
                case ir::IrExpr::Kind::Var: {
                    const std::string slot =
                      resolveVar(static_cast<const ir::IrVarRef&>(expr).name);
                    return emitVarValue(expr.type, slot);
                }
                case ir::IrExpr::Kind::Unary: {
                    const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
                    if (unary.op == "-" && unary.operand) {
                        const Val operand = asI32(emitValue(*unary.operand));
                        const std::string t = temp();
                        emitInstr(t + " = sub i32 0, " + operand.ref);
                        return Val{ "i32", t };
                    }
                    if (unary.op == "!" && unary.operand) {
                        const std::string flag = condI1(*unary.operand);
                        const std::string x = temp();
                        emitInstr(x + " = xor i1 " + flag + ", true");
                        const std::string t = temp();
                        emitInstr(t + " = zext i1 " + x + " to i32");
                        return Val{ "i32", t };
                    }
                    return Val{ "i32", "0" };
                }
                case ir::IrExpr::Kind::Binary:
                    return emitBinary(static_cast<const ir::IrBinaryExpr&>(expr));
                case ir::IrExpr::Kind::Logical: {
                    const std::string flag =
                      emitLogicalI1(static_cast<const ir::IrLogicalExpr&>(expr));
                    const std::string t = temp();
                    emitInstr(t + " = zext i1 " + flag + " to i32");
                    return Val{ "i32", t };
                }
                case ir::IrExpr::Kind::Assign:
                    return emitAssign(static_cast<const ir::IrAssignExpr&>(expr));
                case ir::IrExpr::Kind::Index:
                case ir::IrExpr::Kind::Member:
                case ir::IrExpr::Kind::Deref: {
                    const Val addr = emitAddr(expr);
                    return loadFrom(addr, expr.type);
                }
                case ir::IrExpr::Kind::AddrOf:
                    // &e 的值就是 e 的地址（操作数的左值地址）
                    return emitAddr(*static_cast<const ir::IrAddrOfExpr&>(expr).operand);
                case ir::IrExpr::Kind::Call:
                    return emitCall(static_cast<const ir::IrCallExpr&>(expr));
                case ir::IrExpr::Kind::InitList:
                    // 独立初始化列表不出现在值语境（语义层保证）；防御性零值
                    return Val{ "i32", "0" };
                }
                return Val{ "i32", "0" };
            }

            // 变量读值：数组退化（gep 首元素）、struct/指针聚合 load、
            // char 读值提升
            Val emitVarValue(const ir::IrType& type, const std::string& slot) {
                switch (type.kind) {
                case ir::IrType::Kind::Array: {
                    const std::string t = temp();
                    emitInstr(t + " = getelementptr inbounds " + llvmType(type) + ", ptr "
                              + slot + ", i32 0, i32 0");
                    return Val{ "ptr", t };
                }
                default:
                    return loadFrom(Val{ "ptr", slot }, type);
                }
            }

            Val emitBinary(const ir::IrBinaryExpr& binary) {
                // 比较产出 i1 → 值语境 zext i32
                if (isComparison(binary.op)) {
                    const std::string flag = emitIcmp(binary);
                    const std::string t = temp();
                    emitInstr(t + " = zext i1 " + flag + " to i32");
                    return Val{ "i32", t };
                }

                const ir::IrType leftDecayed = decayedType(binary.left->type);
                const ir::IrType rightDecayed = decayedType(binary.right->type);
                const bool leftPtr = leftDecayed.kind == ir::IrType::Kind::Pointer;
                const bool rightPtr = rightDecayed.kind == ir::IrType::Kind::Pointer;

                // 指针 ± 整数 → 按元素尺寸 gep；p - q → ptrtoint/sdiv（C 语义）
                if (leftPtr && !rightPtr) {
                    const Val base = emitValue(*binary.left);
                    const Val offset = asI32(emitValue(*binary.right));
                    const std::string elem = leftDecayed.element
                                               ? llvmType(*leftDecayed.element)
                                               : std::string("i32");
                    const std::string t = temp();
                    if (binary.op == "-") {
                        const std::string n = temp();
                        emitInstr(n + " = sub i32 0, " + offset.ref);
                        emitInstr(t + " = getelementptr " + elem + ", ptr " + base.ref
                                  + ", i32 " + n);
                    } else {
                        emitInstr(t + " = getelementptr " + elem + ", ptr " + base.ref
                                  + ", i32 " + offset.ref);
                    }
                    return Val{ "ptr", t };
                }
                if (binary.op == "+" && !leftPtr && rightPtr) {
                    const Val offset = asI32(emitValue(*binary.left));
                    const Val base = emitValue(*binary.right);
                    const std::string elem = rightDecayed.element
                                               ? llvmType(*rightDecayed.element)
                                               : std::string("i32");
                    const std::string t = temp();
                    emitInstr(t + " = getelementptr " + elem + ", ptr " + base.ref
                              + ", i32 " + offset.ref);
                    return Val{ "ptr", t };
                }
                if (binary.op == "-" && leftPtr && rightPtr) {
                    const Val left = emitValue(*binary.left);
                    const Val right = emitValue(*binary.right);
                    const std::string li = temp();
                    emitInstr(li + " = ptrtoint ptr " + left.ref + " to i64");
                    const std::string ri = temp();
                    emitInstr(ri + " = ptrtoint ptr " + right.ref + " to i64");
                    const std::string diff = temp();
                    emitInstr(diff + " = sub i64 " + li + ", " + ri);
                    const int64_t elemSize =
                      leftDecayed.element ? naturalSize(*leftDecayed.element) : 1;
                    const std::string scaled = temp();
                    emitInstr(scaled + " = sdiv i64 " + diff + ", "
                              + std::to_string(elemSize));
                    const std::string t = temp();
                    emitInstr(t + " = trunc i64 " + scaled + " to i32");
                    return Val{ "i32", t };
                }

                // 标量算术：值域 i32、有符号（C 语义）
                const Val left = asI32(emitValue(*binary.left));
                const Val right = asI32(emitValue(*binary.right));
                std::string opcode;
                if (binary.op == "+") {
                    opcode = "add";
                } else if (binary.op == "-") {
                    opcode = "sub";
                } else if (binary.op == "*") {
                    opcode = "mul";
                } else if (binary.op == "/") {
                    opcode = "sdiv";
                } else {
                    opcode = "srem";
                }
                const std::string t = temp();
                emitInstr(t + " = " + opcode + " i32 " + left.ref + ", " + right.ref);
                return Val{ "i32", t };
            }

            Val emitAssign(const ir::IrAssignExpr& assign) {
                const Val addr = emitAddr(*assign.target);
                const Val value = emitValue(*assign.value);
                storeTo(addr, assign.target->type, value);
                // 赋值表达式的值 = 存入后的左值内容（char 收窄后需重读）
                if (assign.target->type.kind == ir::IrType::Kind::Char
                    || value.type == "void") {
                    return loadFrom(addr, assign.target->type);
                }
                if (assign.target->type.kind == ir::IrType::Kind::Int) {
                    return asI32(value);
                }
                return value;
            }

            // 左值地址：Var/Index/Member/Deref → ptr（gep/load 组合）
            Val emitAddr(const ir::IrExpr& expr) {
                switch (expr.kind) {
                case ir::IrExpr::Kind::Var:
                    return Val{ "ptr",
                                resolveVar(static_cast<const ir::IrVarRef&>(expr).name) };
                case ir::IrExpr::Kind::Index: {
                    const auto& index = static_cast<const ir::IrIndexExpr&>(expr);
                    if (index.base->type.kind == ir::IrType::Kind::Array) {
                        // 数组下标：数组地址两段 gep（[N x T] → 元素）
                        const Val arrayAddr = emitAddr(*index.base);
                        const Val subscript = asI32(emitValue(*index.index));
                        const std::string t = temp();
                        emitInstr(t + " = getelementptr inbounds "
                                  + llvmType(index.base->type) + ", ptr " + arrayAddr.ref
                                  + ", i32 0, i32 " + subscript.ref);
                        return Val{ "ptr", t };
                    }
                    // 指针下标：与 *p 同规则（元素类型取下标表达式类型）
                    const Val base = emitValue(*index.base);
                    const Val subscript = asI32(emitValue(*index.index));
                    const std::string elem = expr.type.kind != ir::IrType::Kind::Error
                                               ? llvmType(expr.type)
                                               : std::string("i32");
                    const std::string t = temp();
                    emitInstr(t + " = getelementptr " + elem + ", ptr " + base.ref
                              + ", i32 " + subscript.ref);
                    return Val{ "ptr", t };
                }
                case ir::IrExpr::Kind::Member: {
                    const auto& member = static_cast<const ir::IrMemberExpr&>(expr);
                    ir::IrType structType = member.base->type;
                    Val base;
                    if (member.arrow) {
                        base = emitValue(*member.base); // p->x 等价 (*p).x
                        const ir::IrType decayed = decayedType(structType);
                        structType = decayed.element ? *decayed.element : structType;
                    } else {
                        switch (member.base->kind) {
                        case ir::IrExpr::Kind::Var:
                        case ir::IrExpr::Kind::Index:
                        case ir::IrExpr::Kind::Member:
                        case ir::IrExpr::Kind::Deref:
                            base = emitAddr(*member.base);
                            break;
                        default: {
                            // 右值 struct 取成员（f().x）：临时落栈后 gep
                            const Val value = emitValue(*member.base);
                            const std::string slot = temp();
                            emitInstr(slot + " = alloca " + value.type);
                            emitInstr("store " + value.type + " " + value.ref + ", ptr "
                                      + slot);
                            base = Val{ "ptr", slot };
                            break;
                        }
                        }
                    }
                    if (structType.kind != ir::IrType::Kind::Struct) {
                        return base; // 降级输入（语义层已报）：原地址返回
                    }
                    return emitFieldAddr(
                      structType,
                      base,
                      static_cast<unsigned>(fieldIndex(structType, member.member)));
                }
                case ir::IrExpr::Kind::Deref: {
                    const Val pointer =
                      emitValue(*static_cast<const ir::IrDerefExpr&>(expr).operand);
                    return Val{ "ptr", pointer.ref };
                }
                default: {
                    // 降级输入：临时槽位兜底
                    const std::string slot = temp();
                    emitInstr(slot + " = alloca i32");
                    return Val{ "ptr", slot };
                }
                }
            }

            // -------------------------------------------------------------
            // 调用发射
            // -------------------------------------------------------------
            Val emitCall(const ir::IrCallExpr& call) {
                const ir::IrFunction* fn = findFunction(call.callee);
                const ir::IrExternDecl* ext = fn ? nullptr : findExtern(call.callee);

                std::string args;
                for (std::size_t i = 0; i < call.arguments.size(); ++i) {
                    if (i > 0) {
                        args += ", ";
                    }
                    const Val value = emitValue(*call.arguments[i]);
                    if (fn && i < fn->params.size()) {
                        const Val converted = convertForStore(fn->params[i].type, value);
                        args += converted.type + " " + converted.ref;
                    } else if (ext && i < ext->params.size()) {
                        const Val converted = convertForStore(ext->params[i].type, value);
                        args += converted.type + " " + converted.ref;
                    } else {
                        // varargs：保持 i32 提升（C 缺省提升）/原类型
                        args += value.type + " " + value.ref;
                    }
                }

                // 返回类型：定义签名 > extern 声明 > 调用点类型（Error 兜底 i32）
                std::string returnType;
                if (fn) {
                    returnType = llvmType(fn->returnType);
                } else if (ext) {
                    returnType = llvmType(ext->returnType);
                } else {
                    returnType = llvmType(call.type);
                }

                if (returnType == "void") {
                    emitInstr("call void @" + call.callee + "(" + args + ")");
                    return Val{ "void", "" };
                }
                const std::string t = temp();
                emitInstr(t + " = call " + returnType + " @" + call.callee + "(" + args
                          + ")");
                if (returnType == "i8") {
                    const std::string s = temp();
                    emitInstr(s + " = sext i8 " + t + " to i32");
                    return Val{ "i32", s };
                }
                return Val{ returnType, t };
            }
        };

        // -------------------------------------------------------------------
        // 工具链探测（进程内缓存；顺序见 llvm_backend.hpp）
        // -------------------------------------------------------------------
        // std::system 走 cmd /c：命令行首字符为引号且引号多于两个时，cmd
        // 按其规则 2 剥掉首尾引号、破坏内层引号结构。整体再包一层引号是
        // 既定 workaround；POSIX 无此行为，不包。
        std::string wrapForSystem(const std::string& command) {
#if defined(_WIN32)
            return "\"" + command + "\"";
#else
            return command;
#endif
        }

        bool runProbe(const std::string& command) {
            const std::string logPath = "ncc_llvm_probe.log";
            const std::string full =
              wrapForSystem(command + " --version > \"" + logPath + "\" 2>&1");
            const int rc = std::system(full.c_str());
            std::remove(logPath.c_str());
            return rc == 0;
        }

        bool fileExists(const std::string& path) {
            std::error_code ec;
            return std::filesystem::exists(path, ec);
        }

        std::string joinPath(const std::string& dir, const std::string& name) {
            std::string result = dir;
            while (!result.empty() && (result.back() == '/' || result.back() == '\\')) {
                result.pop_back();
            }
            return result + "/" + name;
        }

        // 目录（bin 或根）→ llc 可执行命令；找不到或不可执行返回空
        std::string llcInDir(const std::string& dir) {
            for (const std::string& candidate : { dir, joinPath(dir, "bin") }) {
                for (const std::string& exe : { "llc.exe", "llc" }) {
                    const std::string path = joinPath(candidate, exe);
                    if (fileExists(path) && runProbe("\"" + path + "\"")) {
                        return path;
                    }
                }
            }
            return std::string();
        }

        // 目录（bin 或根）下的可执行文件路径；找不到返回空
        std::string exeInDir(const std::string& dir, const std::string& name) {
            for (const std::string& candidate : { joinPath(dir, "bin"), dir }) {
                for (const std::string& exe : { name + ".exe", name }) {
                    const std::string path = joinPath(candidate, exe);
                    if (fileExists(path)) {
                        return path;
                    }
                }
            }
            return std::string();
        }

        Toolchain probeToolchain() {
            Toolchain tc;

            // 1. NANOC_LLVM_DIR（指向 bin 或根目录均可）
            if (const char* env = std::getenv("NANOC_LLVM_DIR")) {
                const std::string dir = env;
                tc.llc = llcInDir(dir);
                if (!tc.llc.empty()) {
                    tc.source = "NANOC_LLVM_DIR=" + dir;
                }
            }

            // 2. PATH 上的 llc
            if (tc.llc.empty() && runProbe("llc")) {
                tc.llc = "llc";
                tc.source = "PATH";
            }

            // 3. 本机参考 SDK 路径（新版优先；均探测便于诊断）
            for (const auto& candidate :
                 { std::make_pair(std::string("D:/sdk/llvm-23.1.2"),
                                  std::string("sdk:llvm-23.1.2")),
                   std::make_pair(
                     std::string("D:/sdk/clang+llvm-18.1.8-x86_64-pc-windows-msvc"),
                     std::string("sdk:clang+llvm-18.1.8")) }) {
                if (tc.llc.empty()) {
                    const std::string llc = llcInDir(candidate.first);
                    if (!llc.empty()) {
                        tc.llc = llc;
                        tc.source = candidate.second;
                    }
                }
            }

            if (tc.llc.empty()) {
                return tc; // 无 llc：无需再探测链接器
            }

            // 链接器：llc 同目录 lld-link（自动发现 MSVC 安装）> PATH
            // lld-link > clang 驱动（同目录 > PATH）
            const std::size_t slash = tc.llc.find_last_of("/\\");
            const std::string llcDir =
              slash == std::string::npos ? std::string() : tc.llc.substr(0, slash);
            const std::vector<std::string> lldCandidates = {
                llcDir.empty() ? std::string() : exeInDir(llcDir, "lld-link"),
                std::string("lld-link")
            };
            for (const std::string& candidate : lldCandidates) {
                if (candidate.empty()) {
                    continue;
                }
                if (candidate == "lld-link" || runProbe("\"" + candidate + "\"")) {
                    tc.linker = candidate;
                    break;
                }
            }
            if (tc.linker.empty()) {
                const std::vector<std::string> clangCandidates = {
                    llcDir.empty() ? std::string() : exeInDir(llcDir, "clang"),
                    std::string("clang")
                };
                for (const std::string& candidate : clangCandidates) {
                    if (candidate.empty()) {
                        continue;
                    }
                    if (candidate == "clang" || runProbe("\"" + candidate + "\"")) {
                        tc.linker = candidate;
                        break;
                    }
                }
            }
            return tc;
        }

        bool runTool(const std::string& command, const std::string& logPath) {
            const std::string full =
              wrapForSystem(command + " > \"" + logPath + "\" 2>&1");
            const int rc = std::system(full.c_str());
            return rc == 0;
        }

        std::string readLog(const std::string& logPath) {
            std::ifstream log(logPath, std::ios::binary);
            std::ostringstream buffer;
            buffer << log.rdbuf();
            return buffer.str();
        }

        bool ensureLlc(std::string& diagnostics) {
            if (toolchain().llc.empty()) {
                diagnostics =
                  "LLVM toolchain not found (need llc). Set NANOC_LLVM_DIR to "
                  "an LLVM bin directory (e.g. C:/Program Files/LLVM/bin) or "
                  "add llc to PATH. Install LLVM 15+ from "
                  "https://github.com/llvm/llvm-project/releases"
                  " (see doc/llvm-setup.md).";
                return false;
            }
            return true;
        }

        bool ensureLinker(std::string& diagnostics) {
            if (toolchain().linker.empty()) {
                diagnostics = "linker not found for exe emission (tried lld-link and"
                              " clang driver). Install LLVM 15+ (lld-link ships with it)"
                              " or set NANOC_LLVM_DIR to an LLVM bin directory"
                              " (see doc/llvm-setup.md).";
                return false;
            }
            return true;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // 公开接口
    // ---------------------------------------------------------------------------

    const Toolchain& toolchain() {
        static const Toolchain cached = probeToolchain();
        return cached;
    }

    bool compileToObj(const std::string& llText,
                      const std::string& objPath,
                      std::string& diagnostics) {
        if (!ensureLlc(diagnostics)) {
            return false;
        }
        const Toolchain& tc = toolchain();
        const std::string llPath = objPath + ".ll";
        const std::string logPath = objPath + ".log";

        {
            std::ofstream out(llPath, std::ios::binary);
            out << llText;
        }
        const std::string command =
          "\"" + tc.llc + "\" -filetype=obj \"" + llPath + "\" -o \"" + objPath + "\"";
        if (!runTool(command, logPath)) {
            diagnostics = "llc command: " + command + "\nllc log:\n" + readLog(logPath)
                          + "\n--- generated LLVM IR ---\n" + llText;
            std::remove(llPath.c_str());
            std::remove(logPath.c_str());
            return false;
        }
        std::remove(llPath.c_str());
        std::remove(logPath.c_str());
        return true;
    }

    bool compileToExe(const std::string& llText,
                      const std::string& exePath,
                      std::string& diagnostics) {
        if (!ensureLlc(diagnostics) || !ensureLinker(diagnostics)) {
            return false;
        }
        const Toolchain& tc = toolchain();
        const std::string objPath = exePath + ".obj";
        if (!compileToObj(llText, objPath, diagnostics)) {
            return false;
        }
        // lld-link 自动发现 MSVC 安装；llc 产物不带 /DEFAULTLIB 指示，静态
        // CRT 需显式给出（MSVC link.exe 的隐式行为）。clang 驱动回退时链接
        // 编排由 clang 自理。
        const bool isLld = tc.linker.find("lld-link") != std::string::npos;
        const std::string command =
          "\"" + tc.linker + "\" \"" + objPath + "\" "
          + (isLld ? "/OUT:\"" + exePath + "\" /defaultlib:libcmt"
                   : "-o \"" + exePath + "\"");
        const std::string logPath = exePath + ".link.log";
        const bool ok = runTool(command, logPath);
        if (!ok) {
            diagnostics = "link command: " + command + "\nlink log:\n" + readLog(logPath);
        }
        std::remove(objPath.c_str());
        std::remove(logPath.c_str());
        return ok;
    }

    std::string emit(const ir::Module& module) {
        Emitter emitter(module);
        return emitter.run();
    }

} // namespace llvm_backend
