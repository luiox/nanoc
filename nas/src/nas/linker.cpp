#include "nas/linker.hpp"
#include "nas/instruction.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

// Linker 实现：链接算法、决策与已知限制的权威记录见 linker.hpp 类注释。
// 本文件按阶段组织：解析各模块（parseModule，只做结构校验）→ 基址前缀和
// → 导出表合并与导入内部解析判定 → 未解析导入合并 → 重定位扫描（线性
// 解码 + 地址分类平移 + CALLX→CALL 直调改写）→ 段拼接与序列化。

namespace
{
    // ==== 小端 32 位读写 ====

    void
    putI32(std::vector<uint8_t> & v, int32_t x)
    {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    }

    int32_t
    getI32(const std::vector<uint8_t> & v, size_t off)
    {
        int32_t x;
        memcpy(&x, v.data() + off, sizeof(x));
        return x;
    }

    // NCI v2.1 头字段偏移（规范 §2；头尺寸/魔数常量在 instruction.hpp）
    constexpr size_t HDR_OFF_HEADER_SIZE = 8;
    constexpr size_t HDR_OFF_CODE_SIZE = 12;
    constexpr size_t HDR_OFF_DATA_SIZE = 16;
    constexpr size_t HDR_OFF_IMPORT_COUNT = 20;
    constexpr size_t HDR_OFF_EXPORT_COUNT = 24;
    // 符号表 entry 尾部定长字段：addr@+0、flags@+4（共 8 字节）
    constexpr size_t SYM_TRAILER_SIZE = 8;
    // 导入表 flags 合法位：bit0-1 调用约定 + bit2 动态导入（规范 §2.1）
    constexpr int32_t IMPORT_FLAGS_LEGAL_MASK = 0x7;

    // ==== 符号表中间表示 ====

    struct LinkImport {
        std::string name;
        int32_t addr;  // 宿主地址：静态绑定 / 伪地址（动态导入）/ 0（旧格式动态）
        int32_t flags; // bit0-1 = 调用约定，bit2 = 动态导入
    };

    struct LinkExport {
        std::string name;
        int32_t addr; // 代码/数据统一编址地址（模块内）
    };

    // 单个输入目标文件（重定位前的中间表示）
    struct ObjModule {
        std::vector<uint8_t> code;
        std::vector<uint8_t> data;
        std::vector<LinkImport> imports;
        std::vector<LinkExport> exports;
        int32_t codeSize = 0;
        int32_t dataSize = 0;
        int32_t codeBase = 0;       // Σ 前面模块 codeSize
        int32_t dataBase = 0;       // Σ 前面模块 (codeSize+dataSize)，统一编址数据基址
        std::vector<bool> resolved; // imports 逐项：true = 命中导出，内部解析
    };

    // 规范 §3.1 指令长度表（opcode 取值用权威枚举 NOpcode 钉死，防手写 hex 漂移）；
    // -1 = 未知操作码
    int
    instrLength(uint8_t op)
    {
        switch (op) {
        case uint8_t(NOpcode::LMM):    // LMM R, IMM32
        case uint8_t(NOpcode::ST):     // ST R, ADDR32
        case uint8_t(NOpcode::LEA):    // LEA R, ADDR32
        case uint8_t(NOpcode::LOADA):  // LOADA R, IMM32
        case uint8_t(NOpcode::STOREA): // STOREA R, IMM32
        case uint8_t(NOpcode::ADDI):   // ADDI
        case uint8_t(NOpcode::SUBI):   // SUBI
        case uint8_t(NOpcode::MULI):   // MULI
        case uint8_t(NOpcode::DIVI):   // DIVI
        case uint8_t(NOpcode::MODI):   // MODI
        case uint8_t(NOpcode::ANDI):   // ANDI
        case uint8_t(NOpcode::ORI):    // ORI
        case uint8_t(NOpcode::XORI):   // XORI
        case uint8_t(NOpcode::CMPI):   // CMPI
            return 6;
        case uint8_t(NOpcode::LOAD):  // LOAD R1, R2
        case uint8_t(NOpcode::STORE): // STORE R1, R2
        case uint8_t(NOpcode::ADD):   // ADD
        case uint8_t(NOpcode::SUB):   // SUB
        case uint8_t(NOpcode::MUL):   // MUL
        case uint8_t(NOpcode::DIV):   // DIV
        case uint8_t(NOpcode::MOD):   // MOD
        case uint8_t(NOpcode::AND):   // AND
        case uint8_t(NOpcode::OR):    // OR
        case uint8_t(NOpcode::XOR):   // XOR
        case uint8_t(NOpcode::SHL):   // SHL
        case uint8_t(NOpcode::SHLI):  // SHLI R, IMM8
        case uint8_t(NOpcode::SHR):   // SHR
        case uint8_t(NOpcode::SHRI):  // SHRI R, IMM8
        case uint8_t(NOpcode::CMP):   // CMP
        case uint8_t(NOpcode::TEST):  // TEST
        case uint8_t(NOpcode::ENTER): // ENTER IMM16
        case uint8_t(NOpcode::MOV):   // MOV
            return 3;
        case uint8_t(NOpcode::NOT):  // NOT
        case uint8_t(NOpcode::NEG):  // NEG
        case uint8_t(NOpcode::PUSH): // PUSH
        case uint8_t(NOpcode::POP):  // POP
        case uint8_t(NOpcode::CLR):  // CLR
            return 2;
        case uint8_t(NOpcode::PUSHI): // PUSHI IMM32
        case uint8_t(NOpcode::JMP):   // JMP ADDR32
        case uint8_t(NOpcode::JZ):    // JZ
        case uint8_t(NOpcode::JNZ):   // JNZ
        case uint8_t(NOpcode::JN):    // JN
        case uint8_t(NOpcode::JP):    // JP
        case uint8_t(NOpcode::CALL):  // CALL ADDR32
        case uint8_t(NOpcode::CALLX): // CALLX IMM32
            return 5;
        case uint8_t(NOpcode::LEAVE): // LEAVE
        case uint8_t(NOpcode::RET):   // RET
        case uint8_t(NOpcode::NOP):   // NOP
            return 1;
        default:
            return -1;
        }
    }

    // 地址类操作数分类：数据地址类（可含导入引用）/ 代码地址类 / CALLX（导入双语义）
    enum class AddrKind {
        NONE,
        DATA,  // ST/LEA/LOADA/STOREA：imm 在 op+reg 之后（偏移 2）
        CODE,  // JMP/JZ/JNZ/JN/JP/CALL：imm 在 opcode 之后（偏移 1）
        CALLX, // CALLX：imm 在 opcode 之后（偏移 1）
    };

    AddrKind
    addrKind(uint8_t op)
    {
        switch (op) {
        case uint8_t(NOpcode::ST):
        case uint8_t(NOpcode::LEA):
        case uint8_t(NOpcode::LOADA):
        case uint8_t(NOpcode::STOREA):
            return AddrKind::DATA;
        case uint8_t(NOpcode::JMP):
        case uint8_t(NOpcode::JZ):
        case uint8_t(NOpcode::JNZ):
        case uint8_t(NOpcode::JN):
        case uint8_t(NOpcode::JP):
        case uint8_t(NOpcode::CALL):
            return AddrKind::CODE;
        case uint8_t(NOpcode::CALLX):
            return AddrKind::CALLX;
        default:
            return AddrKind::NONE;
        }
    }

    // 表项解析：int32 nameLen + name + NUL + pad(4 对齐，entry 起始基准) + addr + flags
    bool
    readTableEntry(const std::vector<uint8_t> & img,
                   size_t & off,
                   std::string & name,
                   int32_t & addr,
                   int32_t & flags,
                   std::string & err)
    {
        if (off + 4 > img.size()) {
            err = "符号表截断（nameLen 不完整）";
            return false;
        }
        int32_t nameLen = getI32(img, off);
        if (nameLen < 0 || off + 4 + static_cast<size_t>(nameLen) + 1 > img.size()) {
            err = "符号表截断（符号名不完整）";
            return false;
        }
        name.assign(reinterpret_cast<const char *>(img.data() + off + 4),
                    static_cast<size_t>(nameLen));
        size_t fixed = 4 + static_cast<size_t>(nameLen) + 1;
        fixed = (fixed + 3) & ~static_cast<size_t>(3);
        if (off + fixed + SYM_TRAILER_SIZE > img.size()) {
            err = "符号表截断（addr/flags 不完整）";
            return false;
        }
        addr = getI32(img, off + fixed);
        flags = getI32(img, off + fixed + 4);
        off += fixed + SYM_TRAILER_SIZE;
        return true;
    }

    // 解析单个 v2.1 镜像（结构校验；代码解码在重定位遍中做）
    bool
    parseModule(const std::vector<uint8_t> & img, ObjModule & m, std::string & err)
    {
        if (img.size() < NCI_HEADER_SIZE
            || memcmp(img.data(), NCI_MAGIC, sizeof(NCI_MAGIC)) != 0) {
            err = "坏魔数（期望 \"NanoC\\0\\0\\0\"）";
            return false;
        }
        if (getI32(img, HDR_OFF_HEADER_SIZE) != NCI_HEADER_SIZE) {
            err = "不支持的 headerSize（期望 32）";
            return false;
        }
        m.codeSize = getI32(img, HDR_OFF_CODE_SIZE);
        m.dataSize = getI32(img, HDR_OFF_DATA_SIZE);
        int32_t importCount = getI32(img, HDR_OFF_IMPORT_COUNT);
        int32_t exportCount = getI32(img, HDR_OFF_EXPORT_COUNT);
        if (m.codeSize < 0 || m.dataSize < 0 || importCount < 0 || exportCount < 0) {
            err = "头部段/表大小为负";
            return false;
        }
        if (NCI_HEADER_SIZE + static_cast<int64_t>(m.codeSize) + m.dataSize
            > static_cast<int64_t>(img.size())) {
            err = "code/data 大小超出文件";
            return false;
        }
        m.code.assign(img.begin() + NCI_HEADER_SIZE,
                      img.begin() + NCI_HEADER_SIZE + m.codeSize);
        m.data.assign(img.begin() + NCI_HEADER_SIZE + m.codeSize,
                      img.begin() + NCI_HEADER_SIZE + m.codeSize + m.dataSize);

        size_t off = NCI_HEADER_SIZE + static_cast<size_t>(m.codeSize) + m.dataSize;
        for (int32_t i = 0; i < importCount; ++i) {
            LinkImport im;
            if (!readTableEntry(img, off, im.name, im.addr, im.flags, err))
                return false;
            if (im.flags & ~IMPORT_FLAGS_LEGAL_MASK) {
                err = "导入符号 '" + im.name + "' flags 非法（bit0-2 之外必须为 0）";
                return false;
            }
            m.imports.push_back(std::move(im));
        }
        for (int32_t i = 0; i < exportCount; ++i) {
            LinkExport ex;
            int32_t flags;
            if (!readTableEntry(img, off, ex.name, ex.addr, flags, err))
                return false;
            if (flags != 0) {
                err = "导出符号 '" + ex.name + "' flags 非法（恒 0）";
                return false;
            }
            m.exports.push_back(std::move(ex));
        }
        return true;
    }

    // 追加 n 个 0
    void
    appendPad(std::vector<uint8_t> & v, size_t n)
    {
        v.insert(v.end(), n, 0);
    }

    // 符号表项序列化：nameLen + name + NUL + pad(4 对齐，entry 起始基准) + addr + flags
    void
    appendTableEntry(std::vector<uint8_t> & v,
                     const std::string & name,
                     int32_t addr,
                     int32_t flags)
    {
        const size_t entryStart = v.size();
        putI32(v, static_cast<int32_t>(name.size()));
        v.insert(v.end(), name.begin(), name.end());
        v.push_back(0);
        while ((v.size() - entryStart) % 4 != 0)
            v.push_back(0);
        putI32(v, addr);
        putI32(v, flags);
    }

} // namespace

LinkResult
Linker::linkFiles(const std::vector<std::string> & inputPaths)
{
    std::vector<std::vector<uint8_t>> images;
    for (const auto & path : inputPaths) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            LinkResult r;
            r.errorMessage = "无法打开目标文件 " + path;
            return r;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)),
                                   std::istreambuf_iterator<char>());
        images.push_back(std::move(bytes));
    }
    return linkImages(images);
}

// 链接主流程：解析各模块 → 基址前缀和 → 合并导出表 → 导入内部解析判定 →
// 合并未解析导入 → 重定位 code 段（线性解码 + 地址分类平移，即"重定位扫描"）
// → 拼接段 → 重算 entryPoint → 序列化。链接语义与已知限制详见 linker.hpp 类注释
LinkResult
Linker::linkImages(const std::vector<std::vector<uint8_t>> & images)
{
    LinkResult r;
    if (images.empty()) {
        r.errorMessage = "没有输入目标文件";
        return r;
    }

    // ---- 解析各模块 ----
    std::vector<ObjModule> mods(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        if (!parseModule(images[i], mods[i], r.errorMessage)) {
            r.errorMessage = "模块 " + std::to_string(i) + ": " + r.errorMessage;
            return r;
        }
    }

    // ---- 基址前缀和（统一编址：数据基址 = Σ前面 (codeSize+dataSize)）----
    int32_t codeBase = 0;
    int32_t dataBase = 0;
    for (auto & m : mods) {
        m.codeBase = codeBase;
        m.dataBase = dataBase;
        codeBase += m.codeSize;
        dataBase += m.codeSize + m.dataSize;
    }
    // 输出镜像代码段总长（CALLX→CALL 直调改写的合法性判据：目标须是代码地址）
    const int32_t totalCodeSize = codeBase;

    // ---- 合并导出表：地址平移 + 重名报错 ----
    std::vector<LinkExport> mergedExports;
    std::map<std::string, int32_t> exportAddr; // name → 平移后地址
    for (size_t i = 0; i < mods.size(); ++i) {
        const ObjModule & m = mods[i];
        for (const auto & ex : m.exports) {
            int32_t a = ex.addr;
            if (a < 0 || a >= m.codeSize + m.dataSize) {
                r.errorMessage =
                  "模块 " + std::to_string(i) + ": 导出符号 '" + ex.name + "' 地址越界";
                return r;
            }
            a += (a < m.codeSize) ? m.codeBase : m.dataBase;
            if (exportAddr.count(ex.name)) {
                r.errorMessage = "重复导出符号 '" + ex.name + "'";
                return r;
            }
            exportAddr[ex.name] = a;
            mergedExports.push_back({ ex.name, a });
        }
    }

    // ---- 导入内部解析判定：addr=0（旧格式动态）或 flags bit2（伪地址动态导入）
    // 且名字命中导出 → resolved（内部导出优先于加载期宿主解析）。
    // 伪地址在模块内按声明序唯一 → callx/地址站点按 imm 值一对一映射到符号；
    // 同值多导入中存在可解析者 → 无法按值消歧 → 报错（不静默错链）
    for (size_t i = 0; i < mods.size(); ++i) {
        ObjModule & m = mods[i];
        std::map<int32_t, int> countByAddr;
        for (const auto & im : m.imports)
            ++countByAddr[im.addr];
        m.resolved.assign(m.imports.size(), false);
        for (size_t k = 0; k < m.imports.size(); ++k) {
            const LinkImport & im = m.imports[k];
            bool dynamic = (im.flags & IMPORT_FLAG_DYNAMIC) != 0;
            if ((im.addr != 0 && !dynamic) || !exportAddr.count(im.name))
                continue;
            if (countByAddr[im.addr] > 1) {
                r.errorMessage = "模块 " + std::to_string(i) + ": 导入符号 '" + im.name
                                 + "' 与其他导入共享地址 0x" + std::to_string(im.addr)
                                 + "，地址站点无法消歧（请为 extern 指定显式地址）";
                return r;
            }
            m.resolved[k] = true;
        }
    }

    // ---- 合并未解析导入：按名去重，flags 冲突报错 ----
    // （提前到重定位之前：未内部解析的动态导入站点需按输出表首现伪地址改写）
    std::vector<LinkImport> mergedImports;
    std::map<std::string, size_t> importIndex;
    for (size_t i = 0; i < mods.size(); ++i) {
        const ObjModule & m = mods[i];
        for (size_t k = 0; k < m.imports.size(); ++k) {
            if (m.resolved[k])
                continue; // 已内部解析，从输出导入表移除
            const LinkImport & im = m.imports[k];
            auto it = importIndex.find(im.name);
            if (it != importIndex.end()) {
                if (mergedImports[it->second].flags != im.flags) {
                    r.errorMessage = "模块 " + std::to_string(i) + ": 导入符号 '"
                                     + im.name
                                     + "' 导入表 flags 冲突（调用约定/动态标记不一致）";
                    return r;
                }
                continue; // 重名导入去重
            }
            importIndex[im.name] = mergedImports.size();
            mergedImports.push_back(im);
        }
    }

    // ---- 重定位 code 段：线性解码 + 地址分类平移 ----
    for (size_t i = 0; i < mods.size(); ++i) {
        ObjModule & m = mods[i];
        int32_t pc = 0;
        while (pc < m.codeSize) {
            uint8_t op = m.code[pc];
            int len = instrLength(op);
            if (len < 0) {
                char hex[8];
                snprintf(hex, sizeof(hex), "%02X", op);
                r.errorMessage = "模块 " + std::to_string(i) + ": 未知操作码 0x" + hex
                                 + "（pc=" + std::to_string(pc) + "）";
                return r;
            }
            if (pc + len > m.codeSize) {
                r.errorMessage = "模块 " + std::to_string(i) + ": 指令越过代码段末尾（pc="
                                 + std::to_string(pc) + "）";
                return r;
            }
            AddrKind kind = addrKind(op);
            if (kind != AddrKind::NONE) {
                size_t immOff = (kind == AddrKind::DATA) ? static_cast<size_t>(pc) + 2
                                                         : static_cast<size_t>(pc) + 1;
                int32_t v = getI32(m.code, immOff);
                int32_t nv = v;
                bool patched = false;
                if (kind == AddrKind::CALLX || kind == AddrKind::DATA) {
                    // 导入解析（按值唯一命中）：
                    //   已内部解析（addr=0 旧格式或 flags bit2 动态导入命中导出）
                    //     → 改写为平移后内部目标地址；CALLX 站点同时把 opcode
                    //     改写为 CALL 直调（#54 根修）：CALL/CALLX 操作数编码
                    //     同为 5 字节 IMM32，直调后 CALLX 只保留给宿主/动态
                    //     导入站点，消除"addr=0 既可能是内部地址又可能是未解析
                    //     动态导入"的编码歧义（落在地址 0 的导出函数由此可被
                    //     正确调用）；目标为数据地址时保留 CALLX（调数据段在
                    //     VM 侧走宿主查表报错，不应变成执行数据）；
                    //   未内部解析的动态导入 → 改写为该符号在输出导入表中的首现
                    //     伪地址（跨模块声明序差异归一，单模块为恒等变换）
                    int hits = 0;
                    size_t hit = 0;
                    for (size_t k = 0; k < m.imports.size(); ++k)
                        if (m.imports[k].addr == v) {
                            hit = k;
                            ++hits;
                        }
                    if (hits == 1) {
                        if (m.resolved[hit]) {
                            nv = exportAddr[m.imports[hit].name];
                            patched = true;
                            if (kind == AddrKind::CALLX && nv < totalCodeSize)
                                m.code[pc] = static_cast<uint8_t>(NOpcode::CALL);
                        }
                        else if (m.imports[hit].flags & IMPORT_FLAG_DYNAMIC) {
                            nv = mergedImports[importIndex.at(m.imports[hit].name)].addr;
                            patched = true;
                        }
                    }
                }
                if (!patched && kind != AddrKind::CALLX) {
                    // 范围判断：代码地址 / 数据地址 / 宿主地址（不动）
                    if (v >= 0 && v < m.codeSize)
                        nv = v + m.codeBase;
                    else if (v >= m.codeSize && v < m.codeSize + m.dataSize)
                        nv = v + m.dataBase;
                }
                if (nv != v) {
                    for (int b = 0; b < 4; ++b)
                        m.code[immOff + static_cast<size_t>(b)] =
                          static_cast<uint8_t>((nv >> (8 * b)) & 0xFF);
                }
            }
            pc += len;
        }
    }

    // ---- 拼接 code/data 段 ----
    std::vector<uint8_t> outCode;
    std::vector<uint8_t> outData;
    for (const auto & m : mods) {
        outCode.insert(outCode.end(), m.code.begin(), m.code.end());
        outData.insert(outData.end(), m.data.begin(), m.data.end());
    }

    // ---- entryPoint：main 导出优先，其次第一个导出符号，否则 0 ----
    int32_t entryPoint = 0;
    if (exportAddr.count("main"))
        entryPoint = exportAddr["main"];
    else if (!mergedExports.empty())
        entryPoint = mergedExports.front().addr;

    // ---- 序列化：header(32B) | code | data | import table | export table ----
    std::vector<uint8_t> & img = r.image;
    img.insert(img.end(), NCI_MAGIC, NCI_MAGIC + sizeof(NCI_MAGIC));
    putI32(img, NCI_HEADER_SIZE); // headerSize
    putI32(img, static_cast<int32_t>(outCode.size()));
    putI32(img, static_cast<int32_t>(outData.size()));
    putI32(img, static_cast<int32_t>(mergedImports.size()));
    putI32(img, static_cast<int32_t>(mergedExports.size()));
    putI32(img, entryPoint);
    img.insert(img.end(), outCode.begin(), outCode.end());
    img.insert(img.end(), outData.begin(), outData.end());
    for (const auto & im : mergedImports)
        appendTableEntry(img, im.name, im.addr, im.flags);
    for (const auto & ex : mergedExports)
        appendTableEntry(img, ex.name, ex.addr, 0);

    r.ok = true;
    return r;
}
