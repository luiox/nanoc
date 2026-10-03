#include "ncc/codegen.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {

    // 规范类型名后缀工具：基型（int/char/void/struct Tag）+ 指针星号 + 数组方括号
    bool endsWith(const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size()
               && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string stripSuffix(const std::string& s, const std::string& suffix) {
        if (endsWith(s, suffix)) {
            return s.substr(0, s.size() - suffix.size());
        }
        return s;
    }

    bool isPointerTypeName(const std::string& t) { return endsWith(t, "*"); }

    bool isArrayTypeName(const std::string& t) { return endsWith(t, "[]"); }

    std::string pointerPointee(const std::string& t) { return stripSuffix(t, "*"); }

    std::string arrayElement(const std::string& t) { return stripSuffix(t, "[]"); }

    // 数组名在值上下文退化为指针
    std::string decayedTypeName(const std::string& t) {
        if (isArrayTypeName(t)) {
            return arrayElement(t) + "*";
        }
        return t;
    }

} // namespace

CodeGenerator::CodeGenerator() { m_sink = &m_code; }

LinkageTable buildLinkageTable(const Program& program) {
    LinkageTable table;
    if (program.type != ASTNodeType::PROGRAM) {
        return table;
    }
    // 位置对齐契约：ir::lower 的第二遍同序遍历 program.declarations，把函数
    // 与全局变量依序导出到 Module::functions / Module::globals，因此这里的
    // 收集顺序（先按类别过滤）与 IR 两个向量按下标一一对应。
    // extern 声明（PRD R3）不进入 Module::functions（无函数体），这里同步跳过
    for (const auto& decl : program.declarations) {
        if (decl->type == ASTNodeType::FUNC_DECLARATION) {
            const auto& func = static_cast<const FuncDeclaration&>(*decl);
            if (func.isExtern) {
                continue;
            }
            LinkageEntry entry;
            entry.name = func.name;
            entry.file = func.sourceFile;
            entry.isExported = func.isExported;
            table.functions.push_back(std::move(entry));
        } else if (decl->type == ASTNodeType::VAR_DECLARATION) {
            const auto& var = static_cast<const VarDeclaration&>(*decl);
            LinkageEntry entry;
            entry.name = var.name;
            entry.file = var.sourceFile;
            entry.isExported = var.isExported;
            table.globals.push_back(std::move(entry));
        }
    }
    return table;
}

std::string CodeGenerator::generate(const ir::Module& module,
                                    const LinkageTable* linkage) {
    m_code.clear();
    m_data.clear();
    m_labelCounter = 0;
    m_functionTable.clear();
    m_globalSymbols.clear();
    m_localSymbols.clear();
    m_globalOrder.clear();
    m_globalInits.clear();
    m_externs.clear();
    m_externSet.clear();
    m_variadicExterns.clear();
    m_externReturnTypes.clear();
    m_stringLiterals.clear();
    m_breakLabels.clear();
    m_continueLabels.clear();
    m_nextSlot = 0;
    m_currentFile.clear();
    m_modulePrefixes.clear();
    m_usedPrefixes.clear();
    m_structs.clear();
    m_structReturnTag.clear();
    m_sretSaveSlot = 0;
    m_tempCursor = 0;
    m_tempLimit = 0;
    m_sink = &m_code;

    // linkage 缺省 = 单文件模式（全部条目 file 空、未导出）；显式给出时必须
    // 与 IR 模块对齐（buildLinkageTable 的位置对齐契约，见 LinkageEntry 注释）
    LinkageTable fallback;
    if (linkage == nullptr) {
        fallback.functions.resize(module.functions.size());
        fallback.globals.resize(module.globals.size());
        linkage = &fallback;
    }
    if (linkage->functions.size() != module.functions.size()
        || linkage->globals.size() != module.globals.size()) {
        throw std::runtime_error(
          "internal error: linkage table is not aligned with the IR module");
    }

    // 第一遍之一：struct 布局（声明序，与 semantic 同规则）
    for (const auto& def : module.structs) {
        registerStructLayout(def);
    }

    // 第一遍之二：登记函数符号（允许前向引用）与全局变量
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const ir::IrFunction& func = *module.functions[i];
        const LinkageEntry& link = linkage->functions[i];
        FunctionEntry entry;
        entry.file = link.file;
        entry.isExported = link.isExported;
        entry.label = functionLabel(func.name, link);
        entry.returnType = typeName(func.returnType);
        m_functionTable[func.name].push_back(std::move(entry));
    }
    // extern 声明（PRD R3）：登记 varargs 标记（cdecl 调用序列判定）与返回类型
    for (const auto& ext : module.externs) {
        if (ext.isVariadic) {
            m_variadicExterns.insert(ext.name);
        }
        m_externReturnTypes[ext.name] = typeName(ext.returnType);
    }
    for (size_t i = 0; i < module.globals.size(); ++i) {
        registerGlobal(module.globals[i], linkage->globals[i]);
    }

    // 第二遍：逐函数生成
    bool hasMain = false;
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const ir::IrFunction& func = *module.functions[i];
        if (func.name == "main") {
            hasMain = true;
        }
        generateFunction(func, linkage->functions[i]);
    }

    // 缺省 main：空函数，由 VM 栈底哨兵终止
    if (!hasMain) {
        emit("");
        emit("; Default main function");
        emitLabel("main");
        emit("    enter 0");
        emitGlobalInits();
        emit("    leave");
        emit("    ret");
    }

    // 数据段：全局变量/字符串字面量标号地址 = codeSize + 段内偏移（nas 统一编址回填）
    if (!m_globalOrder.empty() || !m_stringLiterals.empty()) {
        m_sink = &m_data;
        emit("");
        emit("; Data segment");
        // 全局变量：标量/指针 1 个字；数组按元素数；struct 按布局字数
        for (const auto& name : m_globalOrder) {
            const Symbol& symbol = m_globalSymbols.at(name);
            emitLabel(symbol.label);
            const std::string elementType =
              symbol.isArray ? arrayElement(symbol.type) : symbol.type;
            const int wordsPerElement = typeSizeWords(elementType);
            const int count = symbol.isArray ? std::max(symbol.arraySize, 1) : 1;
            const int words = wordsPerElement * count;
            for (int i = 0; i < words; ++i) {
                emit("    dd 0");
            }
        }
        // 字符串字面量：按字节打包 + 显式 NUL 终止（宿主 strlen 等按字节读）
        for (const auto& entry : m_stringLiterals) {
            emitLabel(entry.second);
            emit("    db \"" + entry.first + "\", 0");
        }
        m_sink = &m_code;
    }

    // 拼装：头注释 + extern 指令 + 函数体 + 数据段。
    // extern 指令无地址 = 动态导入（nas 分配伪宿主地址 + flags bit2，加载期
    // 经 --host-lib 按名解析）。varargs 符号按规范 §4.2 以 cdecl 导出：
    // `.calling_convention` 文件级顺序生效，声明后恢复 fastcall
    std::string output;
    output += "; NanoC Generated Assembly (NCI v2.1)\n";
    output += "; Generated by ncc compiler\n\n";
    for (const auto& name : m_externs) {
        if (m_variadicExterns.count(name) > 0) {
            output += ".calling_convention cdecl\n";
            output += "extern " + name + "\n";
            output += ".calling_convention fastcall\n";
        } else {
            output += "extern " + name + "\n";
        }
    }
    if (!m_externs.empty()) {
        output += "\n";
    }
    output += m_code;
    output += m_data;
    return output;
}

// 兼容入口：AST → ir::lower → IR 发射（回归测试与 compile_examples 的既有
// 调用形态；发射逻辑只此一份，全部经 IR）
std::string CodeGenerator::generate(Program& program) {
    auto lowered = ir::lower(program);
    if (lowered.is_err()) {
        throw std::runtime_error("IR lowering failed: " + lowered.unwrap_err());
    }
    LinkageTable linkage = buildLinkageTable(program);
    ir::Module module = std::move(lowered).unwrap(); // Module 只移动（含 unique_ptr）
    return generate(module, &linkage);
}

void CodeGenerator::emit(const std::string& code) { *m_sink += code + "\n"; }

void CodeGenerator::emitLabel(const std::string& label) { *m_sink += label + ":\n"; }

std::string CodeGenerator::newLabel() { return "L" + std::to_string(m_labelCounter++); }

const CodeGenerator::Symbol* CodeGenerator::findSymbol(const std::string& name) const {
    auto local = m_localSymbols.find(name);
    if (local != m_localSymbols.end()) {
        return &local->second;
    }
    // 全局变量解析与语义可见性规则一致：当前文件定义（私有键）优先，
    // 其次展示名键（导出符号；单文件模式全部存展示名键）
    if (!m_currentFile.empty()) {
        auto sameFile = m_globalSymbols.find(globalKey(name, m_currentFile, false));
        if (sameFile != m_globalSymbols.end()) {
            return &sameFile->second;
        }
    }
    auto global = m_globalSymbols.find(name);
    if (global != m_globalSymbols.end()) {
        return &global->second;
    }
    return nullptr;
}

// ---- 顶层符号标号与跨文件解析（PRD R2a）----

std::string CodeGenerator::globalKey(const std::string& name,
                                     const std::string& file,
                                     bool isExported) {
    // 导出符号全单元唯一、单文件模式无跨文件冲突 → 展示名；未导出顶层变量
    // 以文件限定（\x01 不会出现在源码标识符中）
    if (file.empty() || isExported) {
        return name;
    }
    return file + '\x01' + name;
}

std::string CodeGenerator::modulePrefix(const std::string& file) {
    if (file.empty()) {
        return "";
    }
    auto memo = m_modulePrefixes.find(file);
    if (memo != m_modulePrefixes.end()) {
        return memo->second;
    }
    // stem：去目录与扩展名；非字母/数字/下划线字符压成下划线
    const size_t sep = file.find_last_of("/\\");
    const size_t start = (sep == std::string::npos) ? 0 : sep + 1;
    size_t end = file.size();
    const size_t dot = file.find_last_of('.');
    if (dot != std::string::npos && dot > start) {
        end = dot;
    }
    std::string stem = file.substr(start, end - start);
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }
    if (stem.empty()) {
        stem = "m";
    }
    // 同 stem 的不同文件追加 _2/_3...（不同目录同名文件共存）
    std::string candidate = stem;
    int suffix = 2;
    while (m_usedPrefixes.count(candidate) > 0) {
        candidate = stem + "_" + std::to_string(suffix++);
    }
    m_usedPrefixes.insert(candidate);
    m_modulePrefixes[file] = candidate;
    return candidate;
}

std::string CodeGenerator::functionLabel(const std::string& name,
                                         const LinkageEntry& link) {
    if (name == "main") {
        return "main"; // VM 入口标号恒不 mangle
    }
    if (link.isExported || link.file.empty()) {
        return name;
    }
    return ".f_" + modulePrefix(link.file) + "_" + name;
}

const CodeGenerator::FunctionEntry*
CodeGenerator::resolveFunction(const std::string& name) const {
    const auto it = m_functionTable.find(name);
    if (it == m_functionTable.end() || it->second.empty()) {
        return nullptr;
    }
    const std::vector<FunctionEntry>& candidates = it->second;
    if (candidates.size() == 1) {
        return &candidates.front();
    }
    // 跨文件同名候选（双方皆私有才会共存，语义已保证无导出歧义）：
    // 当前文件定义优先
    for (const FunctionEntry& entry : candidates) {
        if (entry.file == m_currentFile) {
            return &entry;
        }
    }
    return &candidates.front(); // 防御：语义已拒绝的歧义形态
}

// extern 声明的返回类型名（PRD R3）；未声明外部返回 "int"（既有 #37 兼容路径）
std::string CodeGenerator::externReturnTypeName(const std::string& name) const {
    const auto it = m_externReturnTypes.find(name);
    return it != m_externReturnTypes.end() ? it->second : std::string("int");
}

// ---- IR 类型 → 规范类型名 ----

// IrType → 规范名（与迁移前的 canonicalType 产物一致：数组不带长度——长度
// 仅参与布局计算，不进入类型名判定；typedef 已由 lower 展开为 IrType）
std::string CodeGenerator::typeName(const ir::IrType& type) {
    using Kind = ir::IrType::Kind;
    switch (type.kind) {
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
        return "struct " + type.tag;
    case Kind::Pointer:
        return type.element ? typeName(*type.element) + "*" : std::string("<error>*");
    case Kind::Array:
        return type.element ? typeName(*type.element) + "[]" : std::string("<error>[]");
    }
    return "<error>";
}

// 声明占用槽位字数：struct 值按布局；数组按元素数 × 元素字数；标量/指针 1
int CodeGenerator::declWords(const ir::IrType& type) const {
    using Kind = ir::IrType::Kind;
    if (type.kind == Kind::Array) {
        const int elementWords =
          type.element ? typeSizeWords(typeName(*type.element)) : 1;
        return std::max(type.length, 1) * elementWords;
    }
    return std::max(typeSizeWords(typeName(type)), 1);
}

// ---- struct 布局（typedef/struct 透明展开已由 IR 完成）----

// "struct T"（值形态）→ 布局表条目；其余返回 nullptr
const CodeGenerator::StructLayout*
CodeGenerator::structLayoutOf(const std::string& type) const {
    if (type.rfind("struct ", 0) != 0 || isPointerTypeName(type)
        || isArrayTypeName(type)) {
        return nullptr;
    }
    auto it = m_structs.find(type.substr(7));
    return it != m_structs.end() ? &it->second : nullptr;
}

const CodeGenerator::FieldLayout*
CodeGenerator::findField(const StructLayout& layout, const std::string& name) const {
    for (const auto& field : layout.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

// IrStructDef → 布局表（与 semantic 同一规则：按声明顺序累加、4 字节对齐无填充）
void CodeGenerator::registerStructLayout(const ir::IrStructDef& def) {
    if (!def.complete) {
        if (m_structs.find(def.tag) == m_structs.end()) {
            m_structs[def.tag] = StructLayout{};
        }
        return;
    }
    auto existing = m_structs.find(def.tag);
    if (existing != m_structs.end() && existing->second.complete) {
        return; // 重复定义：语义分析已报错
    }

    StructLayout layout;
    int offsetWords = 0;
    for (const auto& field : def.fields) {
        const std::string fieldType = typeName(field.type);
        int sizeWords = declWords(field.type);
        FieldLayout entry;
        entry.name = field.name;
        entry.offsetWords = offsetWords;
        entry.sizeWords = std::max(sizeWords, 1);
        entry.type = fieldType;
        offsetWords += entry.sizeWords;
        layout.fields.push_back(std::move(entry));
    }
    layout.sizeWords = offsetWords;
    layout.complete = !layout.fields.empty();
    m_structs[def.tag] = std::move(layout);
}

// 类型字数：标量/指针 1，struct 值按布局；数组/未知类型由调用方另行处理
int CodeGenerator::typeSizeWords(const std::string& type) const {
    if (const StructLayout* layout = structLayoutOf(type)) {
        return std::max(layout->sizeWords, 1);
    }
    if (isPointerTypeName(type) || isArrayTypeName(type)) {
        return 1;
    }
    if (type == "int" || type == "char" || type == "void") {
        return 1;
    }
    return 1; // 未知类型占位（语义分析已报错）
}

// ---- 全局变量 ----

void CodeGenerator::registerGlobal(const ir::IrGlobal& global, const LinkageEntry& link) {
    // 标号与存储键（PRD R2a）：导出/单文件 → ".g_" + 名字；未导出跨文件
    // 私有符号 → ".f_<stem>_<name>"，存储键带文件限定
    const std::string key = globalKey(global.name, link.file, link.isExported);
    if (m_globalSymbols.find(key) == m_globalSymbols.end()) {
        m_globalOrder.push_back(key);
    }
    Symbol symbol;
    symbol.kind = SymKind::Global;
    symbol.label = (link.isExported || link.file.empty())
                     ? ".g_" + global.name
                     : ".f_" + modulePrefix(link.file) + "_" + global.name;
    symbol.type = typeName(global.type);
    symbol.isArray = global.type.kind == ir::IrType::Kind::Array;
    symbol.arraySize = symbol.isArray ? global.type.length : 0;
    m_globalSymbols[key] = symbol;

    if (!global.init) {
        return;
    }
    if (global.init->kind == ir::IrExpr::Kind::InitList) {
        // struct 逐成员初始化器：每个标量/指针成员一条全局初始化
        const auto& initList = static_cast<const ir::IrInitListExpr&>(*global.init);
        const StructLayout* layout = structLayoutOf(symbol.type);
        if (layout == nullptr) {
            throw std::runtime_error("array initializers are not supported");
        }
        for (size_t i = 0; i < initList.values.size() && i < layout->fields.size(); ++i) {
            if (initList.values[i]->kind == ir::IrExpr::Kind::InitList) {
                throw std::runtime_error("nested initializers are not supported");
            }
            GlobalInit init;
            init.label = symbol.label;
            init.offsetWords = layout->fields[i].offsetWords;
            init.expr = initList.values[i].get();
            m_globalInits.push_back(init);
        }
        return;
    }
    // 数组整体初始化被语义拒绝；此处防御式跳过
    if (!symbol.isArray) {
        GlobalInit init;
        init.label = symbol.label;
        init.offsetWords = 0;
        init.expr = global.init.get();
        m_globalInits.push_back(init);
    }
}

void CodeGenerator::emitGlobalInits() {
    for (const auto& init : m_globalInits) {
        evalExpr(*init.expr); // 求值结果压栈
        emit("    pop R0");
        emit("    lea R6, " + init.label);
        if (init.offsetWords > 0) {
            emit("    addi R6, " + std::to_string(4 * init.offsetWords));
        }
        emit("    store [R6], R0");
    }
}

// ---- 变量读写 ----

void CodeGenerator::emitLoadVar(const Symbol& sym) {
    switch (sym.kind) {
    case SymKind::Local:
        emit("    mov R6, R5");
        emit("    subi R6, " + std::to_string(4 * sym.slot));
        emit("    load R0, [R6]");
        break;
    case SymKind::StackArg:
        emit("    mov R6, R5");
        emit("    addi R6, " + std::to_string(4 * (sym.argIndex - 3)));
        emit("    load R0, [R6]");
        break;
    case SymKind::Global:
        emit("    lea R6, " + sym.label);
        emit("    load R0, [R6]");
        break;
    }
}

void CodeGenerator::emitStoreVar(const Symbol& sym) {
    switch (sym.kind) {
    case SymKind::Local:
        emit("    mov R6, R5");
        emit("    subi R6, " + std::to_string(4 * sym.slot));
        break;
    case SymKind::StackArg:
        emit("    mov R6, R5");
        emit("    addi R6, " + std::to_string(4 * (sym.argIndex - 3)));
        break;
    case SymKind::Global:
        emit("    lea R6, " + sym.label);
        break;
    }
    emit("    store [R6], R0");
}

// struct 对象地址 → R0：形参槽位存副本地址（load 即得），局部/全局取槽地址
void CodeGenerator::emitStructAddressOfSymbol(const Symbol& sym) {
    if (sym.isStructParam) {
        emitLoadVar(sym);
        return;
    }
    emitAddressOfSymbol(sym);
}

// ---- 条件分支 ----

void CodeGenerator::emitTestBranch(const std::string& target, bool branchOnTrue) {
    // TEST R, R：按位 AND 置 flags（0 → Z，负 → N，正 → P）
    emit("    test R0, R0");
    emit(std::string("    ") + (branchOnTrue ? "jnz " : "jz ") + target);
}

void CodeGenerator::emitCompareBranch(const std::string& op,
                                      const std::string& target,
                                      bool branchOnTrue) {
    // flags 由 CMP 置位：Z = 相等、N = 左 < 右、P = 左 > 右
    // 无 JNN/JNP，Z|N / Z|P 类条件用两次跳转复合
    emit("    cmp R0, R1");
    auto jump = [&](const std::string& mnemonic) {
        emit("    " + mnemonic + " " + target);
    };
    if (op == "==") {
        branchOnTrue ? jump("jz") : jump("jnz");
    } else if (op == "!=") {
        branchOnTrue ? jump("jnz") : jump("jz");
    } else if (op == "<") {
        branchOnTrue ? jump("jn") : (jump("jz"), jump("jp"));
    } else if (op == "<=") {
        branchOnTrue ? (jump("jz"), jump("jn")) : jump("jp");
    } else if (op == ">") {
        branchOnTrue ? jump("jp") : (jump("jz"), jump("jn"));
    } else if (op == ">=") {
        branchOnTrue ? (jump("jz"), jump("jp")) : jump("jn");
    } else {
        throw std::runtime_error("Unknown comparison operator: " + op);
    }
}

void CodeGenerator::emitCompareValue(const std::string& op) {
    // 比较作为表达式值时物化为 0/1：条件成立 → 1，否则 → 0
    const std::string trueLabel = newLabel();
    const std::string endLabel = newLabel();
    emit("    cmp R0, R1");
    if (op == "==") {
        emit("    jz " + trueLabel);
    } else if (op == "!=") {
        emit("    jnz " + trueLabel);
    } else if (op == "<") {
        emit("    jn " + trueLabel);
    } else if (op == "<=") {
        emit("    jz " + trueLabel);
        emit("    jn " + trueLabel);
    } else if (op == ">") {
        emit("    jp " + trueLabel);
    } else if (op == ">=") {
        emit("    jz " + trueLabel);
        emit("    jp " + trueLabel);
    } else {
        throw std::runtime_error("Unknown comparison operator: " + op);
    }
    emit("    lmm R2, 0");
    emit("    jmp " + endLabel);
    emitLabel(trueLabel);
    emit("    lmm R2, 1");
    emitLabel(endLabel);
    emit("    push R2");
}

void CodeGenerator::emitBranchExpr(const ir::IrExpr& expr,
                                   const std::string& target,
                                   bool branchOnTrue) {
    using Kind = ir::IrExpr::Kind;
    if (expr.kind == Kind::Logical) {
        const auto& logic = static_cast<const ir::IrLogicalExpr&>(expr);
        if (logic.op == "&&") {
            // 左假即假；左右皆真才为真
            if (branchOnTrue) {
                const std::string falseLabel = newLabel();
                emitBranchExpr(*logic.left, falseLabel, false);
                emitBranchExpr(*logic.right, target, true);
                emitLabel(falseLabel);
            } else {
                emitBranchExpr(*logic.left, target, false);
                emitBranchExpr(*logic.right, target, false);
            }
            return;
        }
        if (logic.op == "||") {
            // 左真即真；左右皆假才为假
            if (branchOnTrue) {
                emitBranchExpr(*logic.left, target, true);
                emitBranchExpr(*logic.right, target, true);
            } else {
                const std::string trueLabel = newLabel();
                emitBranchExpr(*logic.left, trueLabel, true);
                emitBranchExpr(*logic.right, target, false);
                emitLabel(trueLabel);
            }
            return;
        }
    }
    if (expr.kind == Kind::Binary) {
        const auto& binary = static_cast<const ir::IrBinaryExpr&>(expr);
        if (binary.op == "==" || binary.op == "!=" || binary.op == "<"
            || binary.op == "<=" || binary.op == ">" || binary.op == ">=") {
            evalExpr(*binary.left);
            evalExpr(*binary.right);
            emit("    pop R1");
            emit("    pop R0");
            emitCompareBranch(binary.op, target, branchOnTrue);
            return;
        }
    }
    if (expr.kind == Kind::Unary) {
        const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
        if (unary.op == "!") {
            emitBranchExpr(*unary.operand, target, !branchOnTrue);
            return;
        }
    }
    // 其余表达式：求值后按真值跳转
    evalExpr(expr);
    emit("    pop R0");
    emitTestBranch(target, branchOnTrue);
}

// ---- 函数帧统计 ----

int CodeGenerator::countLocalSlots(const ir::IrStmt* stmt) const {
    using Kind = ir::IrStmt::Kind;
    if (stmt == nullptr) {
        return 0;
    }
    switch (stmt->kind) {
    case Kind::Block: {
        const auto& block = static_cast<const ir::IrBlockStmt&>(*stmt);
        int count = 0;
        for (const auto& child : block.statements) {
            count += countLocalSlots(child.get());
        }
        return count;
    }
    case Kind::Let: // struct 值按布局字数占槽；数组按元素数 × 元素字数
        return declWords(static_cast<const ir::IrLetStmt&>(*stmt).type);
    case Kind::If: {
        const auto& ifStmt = static_cast<const ir::IrIfStmt&>(*stmt);
        return countLocalSlots(ifStmt.thenBranch.get())
               + countLocalSlots(ifStmt.elseBranch.get());
    }
    case Kind::While:
        return countLocalSlots(static_cast<const ir::IrWhileStmt&>(*stmt).body.get());
    case Kind::For: {
        const auto& forStmt = static_cast<const ir::IrForStmt&>(*stmt);
        return countLocalSlots(forStmt.init.get()) + countLocalSlots(forStmt.body.get());
    }
    default:
        return 0;
    }
}

// ---- struct 临时空间统计 ----

// 表达式子树需要的 struct 临时词数：struct 返回调用的接收槽 +
// struct 值实参的副本槽（类型直接取自 IR 节点注记）
int CodeGenerator::structTempWords(const ir::IrExpr* expr) {
    using Kind = ir::IrExpr::Kind;
    if (expr == nullptr) {
        return 0;
    }
    switch (expr->kind) {
    case Kind::Call: {
        const auto& call = static_cast<const ir::IrCallExpr&>(*expr);
        int words = 0;
        const FunctionEntry* target = resolveFunction(call.callee);
        // 返回类型与发射处同源：已定义函数取签名，extern 声明取声明类型，
        // 未声明外部兜底 int（struct 返回 extern 的接收槽计数据此一致）
        const std::string returnType =
          target != nullptr ? target->returnType : externReturnTypeName(call.callee);
        if (const StructLayout* layout = structLayoutOf(returnType)) {
            words += std::max(layout->sizeWords, 1);
        }
        for (const auto& argument : call.arguments) {
            words += structTempWords(argument.get());
            if (const StructLayout* argumentLayout =
                  structLayoutOf(typeName(argument->type))) {
                words += std::max(argumentLayout->sizeWords, 1);
            }
        }
        return words;
    }
    case Kind::Binary: {
        const auto& binary = static_cast<const ir::IrBinaryExpr&>(*expr);
        return structTempWords(binary.left.get()) + structTempWords(binary.right.get());
    }
    case Kind::Logical: {
        const auto& logic = static_cast<const ir::IrLogicalExpr&>(*expr);
        return structTempWords(logic.left.get()) + structTempWords(logic.right.get());
    }
    case Kind::Unary:
        return structTempWords(static_cast<const ir::IrUnaryExpr&>(*expr).operand.get());
    case Kind::AddrOf:
        return structTempWords(static_cast<const ir::IrAddrOfExpr&>(*expr).operand.get());
    case Kind::Deref:
        return structTempWords(static_cast<const ir::IrDerefExpr&>(*expr).operand.get());
    case Kind::Assign: {
        const auto& assign = static_cast<const ir::IrAssignExpr&>(*expr);
        return structTempWords(assign.target.get()) + structTempWords(assign.value.get());
    }
    case Kind::Index: {
        const auto& index = static_cast<const ir::IrIndexExpr&>(*expr);
        return structTempWords(index.base.get()) + structTempWords(index.index.get());
    }
    case Kind::Member:
        return structTempWords(static_cast<const ir::IrMemberExpr&>(*expr).base.get());
    case Kind::InitList: {
        int words = 0;
        for (const auto& value : static_cast<const ir::IrInitListExpr&>(*expr).values) {
            words += structTempWords(value.get());
        }
        return words;
    }
    default:
        return 0;
    }
}

// 语句树：累计 struct 临时词数（IR 节点自带类型，无需预登记符号）
int CodeGenerator::countStructTemps(const ir::IrStmt* stmt) {
    using Kind = ir::IrStmt::Kind;
    if (stmt == nullptr) {
        return 0;
    }
    switch (stmt->kind) {
    case Kind::Block: {
        int words = 0;
        for (const auto& child : static_cast<const ir::IrBlockStmt&>(*stmt).statements) {
            words += countStructTemps(child.get());
        }
        return words;
    }
    case Kind::Let:
        return structTempWords(static_cast<const ir::IrLetStmt&>(*stmt).init.get());
    case Kind::Store: {
        const auto& store = static_cast<const ir::IrStoreStmt&>(*stmt);
        return structTempWords(store.target.get()) + structTempWords(store.value.get());
    }
    case Kind::Eval:
        return structTempWords(
          static_cast<const ir::IrEvalStmt&>(*stmt).expression.get());
    case Kind::If: {
        const auto& ifStmt = static_cast<const ir::IrIfStmt&>(*stmt);
        return structTempWords(ifStmt.condition.get())
               + countStructTemps(ifStmt.thenBranch.get())
               + countStructTemps(ifStmt.elseBranch.get());
    }
    case Kind::While: {
        const auto& whileStmt = static_cast<const ir::IrWhileStmt&>(*stmt);
        return structTempWords(whileStmt.condition.get())
               + countStructTemps(whileStmt.body.get());
    }
    case Kind::For: {
        const auto& forStmt = static_cast<const ir::IrForStmt&>(*stmt);
        return countStructTemps(forStmt.init.get())
               + structTempWords(forStmt.condition.get())
               + structTempWords(forStmt.step.get())
               + countStructTemps(forStmt.body.get());
    }
    case Kind::Return:
        return structTempWords(static_cast<const ir::IrReturnStmt&>(*stmt).value.get());
    default:
        return 0;
    }
}

// ---- struct 临时槽与拷贝 ----

int CodeGenerator::allocStructTemp(int sizeWords) {
    if (m_tempCursor + sizeWords > m_tempLimit) {
        throw std::runtime_error("internal error: struct temporary slots exhausted");
    }
    // 块占 [cursor+1 .. cursor+sizeWords] 号槽；返回最高槽号作为基址
    // （与栈布局一致：地址 BP-4*槽号，槽号越大地址越低，拷贝向高地址延伸）
    m_tempCursor += sizeWords;
    return m_tempCursor;
}

// R1=源地址、R2=目的地址 → 逐字拷贝 sizeWords 字
void CodeGenerator::emitCopyWords(int sizeWords) {
    for (int i = 0; i < sizeWords; ++i) {
        emit("    mov R6, R1");
        if (i > 0) {
            emit("    addi R6, " + std::to_string(4 * i));
        }
        emit("    load R0, [R6]");
        emit("    mov R6, R2");
        if (i > 0) {
            emit("    addi R6, " + std::to_string(4 * i));
        }
        emit("    store [R6], R0");
    }
}

// 栈顶=源地址、R0=目的地址 → 逐字拷贝（语句级赋值/初始化：无表达式结果）
void CodeGenerator::emitPopCopy(int sizeWords) {
    emit("    pop R1");     // 源地址（struct 值 = 地址）
    emit("    mov R2, R0"); // 目的地址
    emitCopyWords(sizeWords);
}

// 栈顶=源地址、R0=目的地址 → 逐字拷贝，push 目的地址（struct 赋值表达式的值）
void CodeGenerator::emitPopCopyPush(int sizeWords) {
    emitPopCopy(sizeWords);
    emit("    mov R0, R2");
    emit("    push R0");
}

// 栈顶=源 struct 地址 → 拷贝到新临时槽，push 临时槽地址（struct 实参按值传递）
void CodeGenerator::emitStructArgCopy(const std::string& structType) {
    const StructLayout* layout = structLayoutOf(structType);
    const int sizeWords = layout != nullptr ? std::max(layout->sizeWords, 1) : 1;
    const int slot = allocStructTemp(sizeWords);
    emit("    mov R0, R5");
    emit("    subi R0, " + std::to_string(4 * slot)); // 目的地址
    emitPopCopyPush(sizeWords);
}

// ---- 模块：函数 ----

void CodeGenerator::generateFunction(const ir::IrFunction& func,
                                     const LinkageEntry& link) {
    using Kind = ir::IrType::Kind;
    m_localSymbols.clear();
    m_nextSlot = 0;
    m_currentFile = link.file;
    const bool isMain = (func.name == "main");

    // 标号：main/导出函数用原名，私有函数 mangle（与第一遍登记一致）
    const std::string label = functionLabel(func.name, link);

    // 返回类型直接取自 IR（lower 已解析；旧实现经符号表自查，结果一致）
    const std::string returnType = typeName(func.returnType);
    const StructLayout* returnLayout = structLayoutOf(returnType);
    const bool returnsStruct = returnLayout != nullptr;
    m_structReturnTag = returnsStruct ? returnType : std::string();

    emit("");
    emit("; Function: " + func.name);
    emitLabel(label);

    // fastcall：前 4 个参数占帧槽位（入口溢出保存），第 5 个起在调用者栈上。
    // struct 形参按地址传递：槽位存调用者副本地址（type 记为 struct T*）；
    // 数组形参按指针槽处理（实参已退化，与迁移前行为一致）
    auto paramType = [this](const ir::IrParam& param) {
        if (param.type.kind == Kind::Array) {
            return param.type.element ? typeName(*param.type.element)
                                      : std::string("<error>");
        }
        return typeName(param.type);
    };
    const int regParams = static_cast<int>(std::min<size_t>(func.params.size(), 4));
    for (int i = 0; i < regParams; ++i) {
        Symbol symbol;
        symbol.kind = SymKind::Local;
        symbol.slot = i + 1;
        symbol.type = paramType(func.params[static_cast<size_t>(i)]);
        if (structLayoutOf(symbol.type) != nullptr) {
            symbol.type += "*";
            symbol.isStructParam = true;
        }
        m_localSymbols[func.params[static_cast<size_t>(i)].name] = symbol;
    }
    for (size_t i = 4; i < func.params.size(); ++i) {
        Symbol symbol;
        symbol.kind = SymKind::StackArg;
        symbol.argIndex = static_cast<int>(i) + 1;
        symbol.type = paramType(func.params[i]);
        if (structLayoutOf(symbol.type) != nullptr) {
            symbol.type += "*";
            symbol.isStructParam = true;
        }
        m_localSymbols[func.params[i].name] = symbol;
    }
    m_nextSlot = regParams;

    // 帧布局：形参槽 + 局部槽 + struct 临时区（+ struct 返回的 R7 保存槽）
    const int localSlots = countLocalSlots(func.body.get());
    const int tempWords = countStructTemps(func.body.get());
    const int sretWords = returnsStruct ? 1 : 0;
    m_tempLimit = m_nextSlot + localSlots + tempWords + sretWords;
    m_tempCursor = m_nextSlot + localSlots;
    if (returnsStruct) {
        m_sretSaveSlot = allocStructTemp(1); // 1 字槽固定存调用者的 R7
    }

    const int enterSize = 4 * m_tempLimit;
    emit("    enter " + std::to_string(enterSize));

    // 溢出寄存器参数到帧槽位
    for (int i = 0; i < regParams; ++i) {
        emit("    mov R6, R5");
        emit("    subi R6, " + std::to_string(4 * (i + 1)));
        emit("    store [R6], R" + std::to_string(i));
    }

    // struct 返回：保存调用者传入的 R7（接收槽地址），防嵌套 struct 调用覆盖
    if (returnsStruct) {
        emit("    mov R6, R5");
        emit("    subi R6, " + std::to_string(4 * m_sretSaveSlot));
        emit("    store [R6], R7");
    }

    // main：先执行全局变量初始化（此时寄存器参数已溢出，可自由使用 R0/R6）
    if (isMain) {
        emitGlobalInits();
    }

    if (func.body) {
        generateStmt(*func.body);
    }

    emit("    leave");
    emit("    ret");
    m_localSymbols.clear();
    m_structReturnTag.clear();
}

// ---- 语句 ----

void CodeGenerator::generateStmt(const ir::IrStmt& stmt) {
    using Kind = ir::IrStmt::Kind;
    switch (stmt.kind) {
    case Kind::Block: {
        const auto& block = static_cast<const ir::IrBlockStmt&>(stmt);
        for (const auto& inner : block.statements) {
            generateStmt(*inner);
        }
        break;
    }
    case Kind::Let: {
        const auto& let = static_cast<const ir::IrLetStmt&>(stmt);
        Symbol symbol;
        symbol.kind = SymKind::Local;
        symbol.slot = ++m_nextSlot;
        symbol.type = typeName(let.type);
        symbol.isArray = let.type.kind == ir::IrType::Kind::Array;
        symbol.arraySize = symbol.isArray ? let.type.length : 0;
        m_localSymbols[let.name] = symbol;

        // 槽位占用：struct 按布局字数；数组按元素数 × 元素字数
        m_nextSlot += std::max(declWords(let.type), 1) - 1;

        if (!let.init) {
            break;
        }
        if (let.init->kind == ir::IrExpr::Kind::InitList) {
            // struct 逐成员初始化：成员表达式求值后存入 基址+成员偏移
            const auto& initList = static_cast<const ir::IrInitListExpr&>(*let.init);
            const StructLayout* layout = structLayoutOf(symbol.type);
            if (layout == nullptr) {
                throw std::runtime_error("array initializers are not supported");
            }
            for (size_t i = 0; i < initList.values.size() && i < layout->fields.size();
                 ++i) {
                if (initList.values[i]->kind == ir::IrExpr::Kind::InitList) {
                    throw std::runtime_error("nested initializers are not supported");
                }
                evalExpr(*initList.values[i]);
                emitStructAddressOfSymbol(symbol); // 基址 → R0
                const FieldLayout& field = layout->fields[i];
                if (field.offsetWords > 0) {
                    emit("    addi R0, " + std::to_string(4 * field.offsetWords));
                }
                emit("    mov R6, R0");
                emit("    pop R0");
                emit("    store [R6], R0");
            }
        } else if (structLayoutOf(symbol.type) != nullptr) {
            // struct 整体初始化：源地址压栈 → 拷贝到变量槽（语句语境：无结果回填）
            evalExpr(*let.init);
            emitStructAddressOfSymbol(symbol); // 目的地址 → R0
            emitPopCopy(declWords(let.type));
        } else {
            evalExpr(*let.init);
            emit("    pop R0");
            emitStoreVar(symbol);
        }
        break;
    }
    case Kind::Store:
        emitStoreStmt(static_cast<const ir::IrStoreStmt&>(stmt));
        break;
    case Kind::Eval: {
        const auto& eval = static_cast<const ir::IrEvalStmt&>(stmt);
        if (eval.expression) {
            evalExpr(*eval.expression);
            emit("    pop R0"); // 表达式语句的结果被丢弃
        }
        break;
    }
    case Kind::If: {
        const auto& ifStmt = static_cast<const ir::IrIfStmt&>(stmt);
        const std::string endLabel = newLabel();
        if (ifStmt.elseBranch) {
            const std::string elseLabel = newLabel();
            emitBranchExpr(*ifStmt.condition, elseLabel, false);
            if (ifStmt.thenBranch) {
                generateStmt(*ifStmt.thenBranch);
            }
            emit("    jmp " + endLabel);
            emitLabel(elseLabel);
            generateStmt(*ifStmt.elseBranch);
        } else {
            emitBranchExpr(*ifStmt.condition, endLabel, false);
            if (ifStmt.thenBranch) {
                generateStmt(*ifStmt.thenBranch);
            }
        }
        emitLabel(endLabel);
        break;
    }
    case Kind::While: {
        const auto& whileStmt = static_cast<const ir::IrWhileStmt&>(stmt);
        const std::string loopLabel = newLabel();
        const std::string endLabel = newLabel();

        m_breakLabels.push_back(endLabel);
        m_continueLabels.push_back(loopLabel);

        emitLabel(loopLabel);
        emitBranchExpr(*whileStmt.condition, endLabel, false);
        if (whileStmt.body) {
            generateStmt(*whileStmt.body);
        }
        emit("    jmp " + loopLabel);
        emitLabel(endLabel);

        m_breakLabels.pop_back();
        m_continueLabels.pop_back();
        break;
    }
    case Kind::For: {
        const auto& forStmt = static_cast<const ir::IrForStmt&>(stmt);
        const std::string loopLabel = newLabel();
        const std::string continueLabel = newLabel();
        const std::string endLabel = newLabel();

        m_breakLabels.push_back(endLabel);
        m_continueLabels.push_back(continueLabel);

        if (forStmt.init) {
            generateStmt(*forStmt.init);
        }
        emitLabel(loopLabel);
        if (forStmt.condition) {
            emitBranchExpr(*forStmt.condition, endLabel, false);
        }
        if (forStmt.body) {
            generateStmt(*forStmt.body);
        }
        emitLabel(continueLabel);
        if (forStmt.step) {
            evalExpr(*forStmt.step);
        }
        emit("    jmp " + loopLabel);
        emitLabel(endLabel);

        m_breakLabels.pop_back();
        m_continueLabels.pop_back();
        break;
    }
    case Kind::Return: {
        const auto& returnStmt = static_cast<const ir::IrReturnStmt&>(stmt);
        if (returnStmt.value) {
            evalExpr(*returnStmt.value);
            emit("    pop R0"); // 返回值统一在 R0
            if (!m_structReturnTag.empty()) {
                // sret：把返回的 struct（源地址在 R0）逐字拷到调用者接收槽 [保存的 R7]
                emit("    mov R1, R0"); // 源地址
                emit("    mov R6, R5");
                emit("    subi R6, " + std::to_string(4 * m_sretSaveSlot));
                emit("    load R2, [R6]"); // 目的地址 = 调用者接收槽
                emitCopyWords(typeSizeWords(m_structReturnTag));
                emit("    mov R0, R2"); // 约定 R0 = sret 接收槽地址
            }
        }
        emit("    leave");
        emit("    ret");
        break;
    }
    case Kind::Break:
        if (m_breakLabels.empty()) {
            throw std::runtime_error("break statement not within loop");
        }
        emit("    jmp " + m_breakLabels.back());
        break;
    case Kind::Continue:
        if (m_continueLabels.empty()) {
            throw std::runtime_error("continue statement not within loop");
        }
        emit("    jmp " + m_continueLabels.back());
        break;
    }
}

// 语句级赋值（三地址形态）：值入栈后按目标形态存回。
// 与表达式语境的 emitAssignExpr 相比不回填赋值结果（省去死 push/pop 对）。
void CodeGenerator::emitStoreStmt(const ir::IrStoreStmt& stmt) {
    using Kind = ir::IrExpr::Kind;
    evalExpr(*stmt.value); // 值/源 struct 地址压栈

    switch (stmt.target->kind) {
    case Kind::Var: {
        const auto& var = static_cast<const ir::IrVarRef&>(*stmt.target);
        const Symbol* symbol = findSymbol(var.name);
        if (symbol == nullptr) {
            throw std::runtime_error("Undefined variable: " + var.name);
        }
        if (isArrayTypeName(symbol->type)) {
            // 数组整体赋值已被语义拒绝；防御式报错
            throw std::runtime_error("Cannot assign to array: " + var.name);
        }
        if (structLayoutOf(symbol->type) != nullptr) {
            // struct 整体赋值：逐字拷贝（栈顶=源地址）
            emitStructAddressOfSymbol(*symbol); // 目的地址 → R0
            emitPopCopy(typeSizeWords(symbol->type));
            return;
        }
        emit("    pop R0");
        emitStoreVar(*symbol);
        break;
    }
    case Kind::Member: {
        const FieldLayout* field = nullptr;
        emitMemberAddress(static_cast<const ir::IrMemberExpr&>(*stmt.target), &field);
        if (field != nullptr && structLayoutOf(field->type) != nullptr) {
            // struct 成员整体赋值：逐字拷贝
            emitPopCopy(field->sizeWords);
            return;
        }
        if (field != nullptr && isArrayTypeName(field->type)) {
            throw std::runtime_error("Cannot assign to array member: " + field->name);
        }
        emit("    mov R6, R0");
        emit("    pop R0");
        emit("    store [R6], R0");
        break;
    }
    case Kind::Index:
    case Kind::Deref: {
        // a[i] = v / *p = v：目标地址 → R0，弹出值存入；
        // struct 元素/解引用整体赋值 → 逐字拷贝
        emitAddressOf(*stmt.target);
        const std::string targetType = typeName(stmt.target->type);
        if (const StructLayout* layout = structLayoutOf(targetType)) {
            emitPopCopy(std::max(layout->sizeWords, 1));
            return;
        }
        emit("    mov R6, R0");
        emit("    pop R0");
        emit("    store [R6], R0");
        break;
    }
    default:
        throw std::runtime_error("Invalid assignment target");
    }
}

// ---- 表达式（求值结果压栈）----

void CodeGenerator::evalExpr(const ir::IrExpr& expr) {
    using Kind = ir::IrExpr::Kind;
    switch (expr.kind) {
    case Kind::IntConst:
        emit("    lmm R0, "
             + std::to_string(static_cast<const ir::IrIntConst&>(expr).value));
        emit("    push R0");
        break;
    case Kind::CharConst:
        emit("    lmm R0, "
             + std::to_string(
               static_cast<int>(static_cast<const ir::IrCharConst&>(expr).value)));
        emit("    push R0");
        break;
    case Kind::StringConst: {
        // 字符串字面量：数据段标号地址即 char* 值
        const std::string label =
          internString(static_cast<const ir::IrStringConst&>(expr).value);
        emit("    lea R0, " + label);
        emit("    push R0");
        break;
    }
    case Kind::NullConst:
        emit("    lmm R0, 0");
        emit("    push R0");
        break;
    case Kind::Var: {
        const auto& var = static_cast<const ir::IrVarRef&>(expr);
        const Symbol* symbol = findSymbol(var.name);
        if (symbol == nullptr) {
            throw std::runtime_error("Undefined variable: " + var.name);
        }
        if (isArrayTypeName(symbol->type)) {
            // 数组名退化为首元素地址（传参/赋给指针/比较/条件）
            emitAddressOfSymbol(*symbol);
        } else if (structLayoutOf(symbol->type) != nullptr) {
            // struct 值：地址即值（形参槽位存地址 → load；局部/全局 → 槽地址）
            emitStructAddressOfSymbol(*symbol);
        } else {
            emitLoadVar(*symbol);
        }
        emit("    push R0");
        break;
    }
    case Kind::Unary: {
        const auto& unary = static_cast<const ir::IrUnaryExpr&>(expr);
        if (unary.op == "&") {
            // 取址：目标地址 → R0（仅左值，语义已校验）
            emitAddressOf(*unary.operand);
            emit("    push R0");
            break;
        }

        // 剩余形态：一元 - 与 !（* 解引用由 lower 独立为 IrDerefExpr 节点）
        evalExpr(*unary.operand);
        emit("    pop R0");

        if (unary.op == "-") {
            emit("    neg R0");
            emit("    push R0");
        } else if (unary.op == "!") {
            // 逻辑非：test 置 flags 后物化 0/1
            const std::string nonzeroLabel = newLabel();
            const std::string endLabel = newLabel();
            emit("    test R0, R0");
            emit("    jnz " + nonzeroLabel);
            emit("    lmm R2, 1");
            emit("    jmp " + endLabel);
            emitLabel(nonzeroLabel);
            emit("    lmm R2, 0");
            emitLabel(endLabel);
            emit("    push R2");
        } else {
            throw std::runtime_error("Unknown unary operator: " + unary.op);
        }
        break;
    }
    case Kind::Binary: {
        const auto& binary = static_cast<const ir::IrBinaryExpr&>(expr);
        if (binary.op == "==" || binary.op == "!=" || binary.op == "<"
            || binary.op == "<=" || binary.op == ">" || binary.op == ">=") {
            evalExpr(*binary.left);
            evalExpr(*binary.right);
            emit("    pop R1"); // 右操作数
            emit("    pop R0"); // 左操作数
            emitCompareValue(binary.op);
            break;
        }

        // 算术运算：左操作数先压栈，右操作数在栈顶
        evalExpr(*binary.left);
        evalExpr(*binary.right);
        emit("    pop R1"); // 右操作数
        emit("    pop R0"); // 左操作数

        if (binary.op == "+") {
            // 指针 ± 整数按指向类型大小缩放（标量/指针 4 字节，struct 按布局）
            const std::string lt = decayedTypeName(typeName(binary.left->type));
            const std::string rt = decayedTypeName(typeName(binary.right->type));
            if (isPointerTypeName(lt)) {
                emit("    lmm R2, "
                     + std::to_string(4 * typeSizeWords(pointerPointee(lt))));
                emit("    mul R1, R2");
                emit("    add R0, R1");
            } else if (isPointerTypeName(rt)) {
                emit("    lmm R2, "
                     + std::to_string(4 * typeSizeWords(pointerPointee(rt))));
                emit("    mul R0, R2");
                emit("    add R0, R1");
            } else {
                emit("    add R0, R1");
            }
        } else if (binary.op == "-") {
            const std::string lt = decayedTypeName(typeName(binary.left->type));
            if (isPointerTypeName(lt)) {
                emit("    lmm R2, "
                     + std::to_string(4 * typeSizeWords(pointerPointee(lt))));
                emit("    mul R1, R2");
            }
            emit("    sub R0, R1");
        } else if (binary.op == "*") {
            emit("    mul R0, R1");
        } else if (binary.op == "/") {
            emit("    div R0, R1");
        } else if (binary.op == "%") {
            emit("    mod R0, R1");
        } else {
            throw std::runtime_error("Unknown binary operator: " + binary.op);
        }
        emit("    push R0");
        break;
    }
    case Kind::Logical: {
        const auto& logic = static_cast<const ir::IrLogicalExpr&>(expr);
        if (logic.op == "&&") {
            const std::string falseLabel = newLabel();
            const std::string endLabel = newLabel();
            emitBranchExpr(expr, falseLabel, false);
            emit("    lmm R0, 1");
            emit("    push R0");
            emit("    jmp " + endLabel);
            emitLabel(falseLabel);
            emit("    lmm R0, 0");
            emit("    push R0");
            emitLabel(endLabel);
        } else if (logic.op == "||") {
            const std::string trueLabel = newLabel();
            const std::string endLabel = newLabel();
            emitBranchExpr(expr, trueLabel, true);
            emit("    lmm R0, 0");
            emit("    push R0");
            emit("    jmp " + endLabel);
            emitLabel(trueLabel);
            emit("    lmm R0, 1");
            emit("    push R0");
            emitLabel(endLabel);
        } else {
            throw std::runtime_error("Unknown logical operator: " + logic.op);
        }
        break;
    }
    case Kind::Assign:
        emitAssignExpr(static_cast<const ir::IrAssignExpr&>(expr));
        break;
    case Kind::Index: {
        // a[i] / p[i]：元素地址 → LOAD；struct 元素地址即 struct 值，不 LOAD
        const auto& index = static_cast<const ir::IrIndexExpr&>(expr);
        emitElementAddress(index);
        const std::string baseType = decayedTypeName(typeName(index.base->type));
        const std::string element =
          isArrayTypeName(baseType) ? arrayElement(baseType) : pointerPointee(baseType);
        if (structLayoutOf(element) == nullptr) {
            emit("    load R0, [R0]");
        }
        emit("    push R0");
        break;
    }
    case Kind::Member: {
        const auto& member = static_cast<const ir::IrMemberExpr&>(expr);
        const FieldLayout* field = nullptr;
        emitMemberAddress(member, &field);
        // 成员为标量/指针 → LOAD 取值；struct/array 成员地址即值（数组名退化）
        const bool loadNeeded =
          field == nullptr
          || (structLayoutOf(field->type) == nullptr && !isArrayTypeName(field->type));
        if (loadNeeded) {
            emit("    load R0, [R0]");
        }
        emit("    push R0");
        break;
    }
    case Kind::AddrOf:
        // 取址在 Unary "&" 已不出现（lower 独立成节点）：目标地址 → R0
        emitAddressOf(*static_cast<const ir::IrAddrOfExpr&>(expr).operand);
        emit("    push R0");
        break;
    case Kind::Deref: {
        // 解引用：操作数求值得地址；struct 指针解引用地址即 struct 值，不 LOAD
        const auto& deref = static_cast<const ir::IrDerefExpr&>(expr);
        evalExpr(*deref.operand);
        emit("    pop R0");
        const std::string operandType = decayedTypeName(typeName(deref.operand->type));
        if (isPointerTypeName(operandType)
            && structLayoutOf(pointerPointee(operandType)) == nullptr) {
            emit("    load R0, [R0]");
        }
        emit("    push R0");
        break;
    }
    case Kind::Call: {
        // 被调解析：当前文件私有函数优先，其次导出函数；未命中 = 宿主外部符号
        // （extern 声明或未声明外部——后者为兼容保留的 #37 路径）
        const auto& call = static_cast<const ir::IrCallExpr&>(expr);
        const FunctionEntry* target = resolveFunction(call.callee);
        const bool external = target == nullptr;
        if (external && m_externSet.insert(call.callee).second) {
            m_externs.push_back(call.callee);
        }
        // varargs extern（PRD R3）：cdecl——实参全部压栈、调用者清栈（规范 §4.2）
        const bool variadicExternal =
          external && m_variadicExterns.count(call.callee) > 0;

        // 返回类型：已定义函数取签名；extern 声明取声明类型；未声明外部兜底 int
        std::string returnType;
        if (target != nullptr) {
            returnType = target->returnType;
        } else {
            returnType = externReturnTypeName(call.callee);
        }
        const StructLayout* returnLayout = structLayoutOf(returnType);

        // 参数从右向左求值：a1 最后求值留在栈顶，a5..aN 依序压在栈上；
        // struct 值实参逐字拷贝到调用者临时槽，副本地址作为实参（按值语义）
        for (int i = static_cast<int>(call.arguments.size()) - 1; i >= 0; --i) {
            const ir::IrExpr& argument = *call.arguments[static_cast<size_t>(i)];
            evalExpr(argument);
            const std::string argumentType = typeName(argument.type);
            if (structLayoutOf(argumentType) != nullptr) {
                emitStructArgCopy(argumentType);
            }
        }

        if (!variadicExternal) {
            // fastcall：前 4 个参数弹入 R0-R3；无参调用不得动栈（0 参调用 pop 会
            // 破坏栈底哨兵）。cdecl（varargs）实参留在栈上，被调方自取
            if (!call.arguments.empty()) {
                emit("    pop R0");
                if (call.arguments.size() > 1) {
                    emit("    pop R1");
                }
                if (call.arguments.size() > 2) {
                    emit("    pop R2");
                }
                if (call.arguments.size() > 3) {
                    emit("    pop R3");
                }
            }
        }

        // struct 返回（sret）：接收槽地址经 R7 传入
        int sretSlot = 0;
        if (returnLayout != nullptr) {
            sretSlot = allocStructTemp(std::max(returnLayout->sizeWords, 1));
            emit("    mov R7, R5");
            emit("    subi R7, " + std::to_string(4 * sretSlot));
        }

        if (external) {
            emit("    callx " + call.callee);
        } else {
            emit("    call " + target->label);
        }

        // 调用者清栈：cdecl 清全部实参；fastcall 第 5 个参数起清栈
        if (variadicExternal) {
            if (!call.arguments.empty()) {
                emit("    addi R4, " + std::to_string(4 * call.arguments.size()));
            }
        } else if (call.arguments.size() > 4) {
            emit("    addi R4, " + std::to_string(4 * (call.arguments.size() - 4)));
        }

        // 返回值压栈：struct → 接收槽地址；标量/指针 → R0
        if (returnLayout != nullptr) {
            emit("    mov R0, R5");
            emit("    subi R0, " + std::to_string(4 * sretSlot));
        }
        emit("    push R0");
        break;
    }
    case Kind::InitList:
        // 初始化器列表只允许作为 struct 声明的初始化器（声明处展开），
        // 出现在一般表达式位置属于内部错误
        throw std::runtime_error("initializer list is not a first-class expression");
    }
}

// 赋值表达式（表达式语境）：求值并存回，赋值结果（存入值/目的地址）压栈
void CodeGenerator::emitAssignExpr(const ir::IrAssignExpr& node) {
    using Kind = ir::IrExpr::Kind;
    evalExpr(*node.value); // 值/源 struct 地址压栈

    switch (node.target->kind) {
    case Kind::Var: {
        const auto& var = static_cast<const ir::IrVarRef&>(*node.target);
        const Symbol* symbol = findSymbol(var.name);
        if (symbol == nullptr) {
            throw std::runtime_error("Undefined variable: " + var.name);
        }
        if (isArrayTypeName(symbol->type)) {
            // 数组整体赋值已被语义拒绝；防御式报错
            throw std::runtime_error("Cannot assign to array: " + var.name);
        }
        if (structLayoutOf(symbol->type) != nullptr) {
            // struct 整体赋值：逐字拷贝（栈顶=源地址）
            emitStructAddressOfSymbol(*symbol); // 目的地址 → R0
            emitPopCopyPush(typeSizeWords(symbol->type));
            return; // 已 push 赋值结果（目的地址）
        }
        emit("    pop R0");
        emitStoreVar(*symbol);
        break;
    }
    case Kind::Member: {
        const FieldLayout* field = nullptr;
        emitMemberAddress(static_cast<const ir::IrMemberExpr&>(*node.target), &field);
        if (field != nullptr && structLayoutOf(field->type) != nullptr) {
            // struct 成员整体赋值：逐字拷贝
            emitPopCopyPush(field->sizeWords);
            return;
        }
        if (field != nullptr && isArrayTypeName(field->type)) {
            throw std::runtime_error("Cannot assign to array member: " + field->name);
        }
        emit("    mov R6, R0");
        emit("    pop R0");
        emit("    store [R6], R0");
        break;
    }
    case Kind::Index:
    case Kind::Deref: {
        // a[i] = v / *p = v：目标地址 → R0，弹出值存入；
        // struct 元素/解引用整体赋值 → 逐字拷贝
        emitAddressOf(*node.target);
        const std::string targetType = typeName(node.target->type);
        if (const StructLayout* layout = structLayoutOf(targetType)) {
            emitPopCopyPush(std::max(layout->sizeWords, 1));
            return;
        }
        emit("    mov R6, R0");
        emit("    pop R0");
        emit("    store [R6], R0");
        break;
    }
    default:
        throw std::runtime_error("Invalid assignment target");
    }

    // 赋值表达式的值
    emit("    push R0");
}

// ---- 地址计算 ----

// 变量地址 → R0（数组名即首元素地址）
// 栈数组占据 slot..slot+size-1 号槽（地址 BP-4·slot 为块内最高地址），
// 首元素固定放在最低地址槽：base = BP - 4*(slot+size-1)，元素向高地址延伸，
// 与指针运算 a[k] == *(a+k) 的语义保持一致
void CodeGenerator::emitAddressOfSymbol(const Symbol& sym) {
    switch (sym.kind) {
    case SymKind::Local: {
        const int elementWords = sym.isArray ? typeSizeWords(arrayElement(sym.type)) : 1;
        const int blockWords =
          sym.isArray ? std::max(sym.arraySize, 1) * elementWords
                      : typeSizeWords(sym.type); // struct 值占多槽；标量/指针 1 槽
        const int slotOffset = sym.slot + std::max(blockWords, 1) - 1;
        emit("    mov R0, R5");
        emit("    subi R0, " + std::to_string(4 * slotOffset));
        break;
    }
    case SymKind::StackArg:
        emit("    mov R0, R5");
        emit("    addi R0, " + std::to_string(4 * (sym.argIndex - 3)));
        break;
    case SymKind::Global:
        emit("    lea R0, " + sym.label);
        break;
    }
}

// 左值表达式地址 → R0：x / a[i] / *p / p.x
void CodeGenerator::emitAddressOf(const ir::IrExpr& expr) {
    using Kind = ir::IrExpr::Kind;
    switch (expr.kind) {
    case Kind::Var: {
        const auto& var = static_cast<const ir::IrVarRef&>(expr);
        const Symbol* symbol = findSymbol(var.name);
        if (symbol == nullptr) {
            throw std::runtime_error("Undefined variable: " + var.name);
        }
        if (symbol->isStructParam) {
            // struct 形参：槽位存副本地址（load 即对象地址）
            emitLoadVar(*symbol);
        } else {
            emitAddressOfSymbol(*symbol);
        }
        break;
    }
    case Kind::Index:
        emitElementAddress(static_cast<const ir::IrIndexExpr&>(expr));
        break;
    case Kind::Member:
        emitMemberAddress(static_cast<const ir::IrMemberExpr&>(expr), nullptr);
        break;
    case Kind::Deref:
        // *p：操作数求值即地址（数组名退化同样成立，*a == a[0]）
        evalExpr(*static_cast<const ir::IrDerefExpr&>(expr).operand);
        emit("    pop R0");
        break;
    default:
        throw std::runtime_error("cannot take address of this expression");
    }
}

// struct 对象地址 → R0：标识符（局部/全局/形参）、成员、下标、解引用、
// struct 返回调用的临时槽
void CodeGenerator::emitStructValueAddress(const ir::IrExpr& expr) {
    using Kind = ir::IrExpr::Kind;
    switch (expr.kind) {
    case Kind::Var: {
        const auto& var = static_cast<const ir::IrVarRef&>(expr);
        const Symbol* symbol = findSymbol(var.name);
        if (symbol == nullptr) {
            throw std::runtime_error("Undefined variable: " + var.name);
        }
        emitStructAddressOfSymbol(*symbol);
        break;
    }
    case Kind::Member:
        emitMemberAddress(static_cast<const ir::IrMemberExpr&>(expr), nullptr);
        break;
    case Kind::Index:
        emitElementAddress(static_cast<const ir::IrIndexExpr&>(expr));
        break;
    case Kind::Deref:
        evalExpr(*static_cast<const ir::IrDerefExpr&>(expr).operand);
        emit("    pop R0");
        break;
    case Kind::Call:
        evalExpr(expr); // struct 返回：求值结果 = 接收槽地址
        emit("    pop R0");
        break;
    default:
        throw std::runtime_error("cannot obtain address of struct value");
    }
}

// 成员地址 → R0：基址（dot：struct 值地址；arrow：指针值）+ 偏移。
// struct 形参经 dot 访问走值路径：emitStructAddressOfSymbol 对形参 load 即地址
void CodeGenerator::emitMemberAddress(const ir::IrMemberExpr& node,
                                      const FieldLayout** outField) {
    const std::string baseType = typeName(node.base->type);
    const bool throughPointer = isPointerTypeName(baseType);
    const std::string valueType = throughPointer ? pointerPointee(baseType) : baseType;
    const StructLayout* layout = structLayoutOf(valueType);
    if (layout == nullptr) {
        throw std::runtime_error("member access on non-struct type: " + baseType);
    }
    const FieldLayout* field = findField(*layout, node.member);
    if (field == nullptr) {
        throw std::runtime_error("no member named '" + node.member + "' in " + valueType);
    }
    if (outField != nullptr) {
        *outField = field;
    }

    if (throughPointer) {
        evalExpr(*node.base);
        emit("    pop R0"); // 指针值即 struct 地址
    } else {
        emitStructValueAddress(*node.base);
    }
    if (field->offsetWords > 0) {
        emit("    addi R0, " + std::to_string(4 * field->offsetWords));
    }
}

// a[i] / p[i] 元素地址 → R0
// 先求下标压栈暂存，再取基址（下标求值会使用 R0 作暂存，不能先算基址）；
// 元素地址 = 基址 + 元素字数×4×i（数组首元素与指针运算同一语义：a[k] == *(a+k)）
void CodeGenerator::emitElementAddress(const ir::IrIndexExpr& node) {
    using Kind = ir::IrExpr::Kind;
    // 下标 → 栈
    evalExpr(*node.index);

    // 基址 → R0：数组名取首元素地址；其余表达式求值得指针
    if (node.base->kind == Kind::Var) {
        const auto& var = static_cast<const ir::IrVarRef&>(*node.base);
        const Symbol* symbol = findSymbol(var.name);
        if (symbol != nullptr && isArrayTypeName(symbol->type)) {
            emitAddressOfSymbol(*symbol);
        } else {
            evalExpr(*node.base);
            emit("    pop R0");
        }
    } else {
        evalExpr(*node.base);
        emit("    pop R0");
    }

    // 变址：R1 = index × 元素字节数（标量/指针元素 4 字节，struct 元素按布局）
    const std::string baseType = decayedTypeName(typeName(node.base->type));
    const std::string element =
      isArrayTypeName(baseType) ? arrayElement(baseType) : pointerPointee(baseType);
    emit("    pop R1");
    emit("    lmm R2, " + std::to_string(4 * typeSizeWords(element)));
    emit("    mul R1, R2");
    emit("    add R0, R1");
}

// 字符串字面量去重入数据段
std::string CodeGenerator::internString(const std::string& content) {
    auto it = m_stringLiterals.find(content);
    if (it != m_stringLiterals.end()) {
        return it->second;
    }
    const std::string label = ".str" + std::to_string(m_stringLiterals.size());
    m_stringLiterals[content] = label;
    return label;
}
