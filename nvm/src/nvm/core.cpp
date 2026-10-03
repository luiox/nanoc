#include "nvm/core.hpp"
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
    // 解析一张符号表 entry：int32 nameLen + name + NUL + pad4（entry 起始基准）
    // + addr + flags（规范 §2.1）。导入/导出表同构，tableName 仅用于错误消息
    // （"import"/"export"）；截断或非法即抛 std::runtime_error
    void
    readSymbolEntry(const int8_t * data,
                    int64_t fileSize,
                    int64_t & off,
                    const char * tableName,
                    std::string & name,
                    int32_t & addr,
                    int32_t & flags)
    {
        if (off + 4 > fileSize)
            throw std::runtime_error(std::string("NCI v2.1: truncated ") + tableName
                                     + " table");
        int32_t nameLen = readI32(data, off);
        if (nameLen < 0 || off + 4 + (int64_t)nameLen + 1 > fileSize)
            throw std::runtime_error(std::string("NCI v2.1: bad ") + tableName
                                     + " symbol name");
        name.assign((const char *)&data[off + 4], nameLen);
        int64_t fixed = ((int64_t)4 + nameLen + 1 + 3) & ~(int64_t)3;
        if (off + fixed + 8 > fileSize)
            throw std::runtime_error(std::string("NCI v2.1: truncated ") + tableName
                                     + " entry");
        addr = readI32(data, off + fixed);
        flags = readI32(data, off + fixed + 4);
        off += fixed + 8;
    }
} // namespace

NVirtualMachine::NVirtualMachine(int32_t stackSize)
  : m_sp(m_registers[4])
  , m_bp(m_registers[5])
{
    m_stack = (int8_t *)malloc(stackSize);
    m_stackSize = stackSize;
    m_codeSize = 0;
    m_dataSize = 0;
    m_ax = m_bp = m_flags = m_pc = 0;
    m_code = NULL;
    m_nextHostAddr = HOST_ADDRESS_BASE;
    // m_sp/m_bp 是 m_registers[4]/[5] 的引用别名，须先清零寄存器，
    // 再通过引用写入 SP 初始值（栈从高地址向低地址生长）
    for (int i = 0; i < REGISTER_COUNT; i++)
        m_registers[i] = 0;
    m_sp = stackSize;
}

NVirtualMachine::~NVirtualMachine() { free(m_stack); }

void
NVirtualMachine::load(std::string filename)
{
    FILE * pf = fopen(filename.c_str(), "rb");
    if (!pf) {
        // 进程级致命 I/O 错误：不经异常通道，报 stderr 后退出（CLI 错误流约定）
        fprintf(stderr, "Error: Cannot open %s\n", filename.c_str());
        exit(1);
    }
    fseek(pf, 0, SEEK_END);
    int64_t size = ftell(pf);
    int8_t * data = (int8_t *)malloc(size);
    fseek(pf, 0, SEEK_SET);
    fread(data, 1, size, pf);
    fclose(pf);
    if (size >= 32 && memcmp(data, "NanoC", 5) == 0) {
        // 严格 v2.1 路径：校验失败抛异常（不污染 VM 状态）
        try {
            loadV21(data, size);
        }
        catch (...) {
            free(data);
            throw;
        }
        free(data);
        data = NULL;
    }
    else {
        // 旧裸格式 fallback：整文件当代码
        m_codeSize = size;
        m_code = data;
        m_pc = 0;
        data = NULL;
    }
}

// 严格 v2.1 加载：header(32B) | code | data | import table | export table
void
NVirtualMachine::loadV21(const int8_t * data, int64_t fileSize)
{
    if (memcmp(data, NCI_V21_MAGIC, sizeof(NCI_V21_MAGIC)) != 0)
        throw std::runtime_error("NCI v2.1: bad magic (expect \"NanoC\\0\\0\\0\")");
    int32_t headerSize = readI32(data, 8);
    if (headerSize != NCI_V21_HEADER_SIZE)
        throw std::runtime_error("NCI v2.1: unsupported headerSize "
                                 + std::to_string(headerSize) + " (expect 32)");
    int32_t codeSize = readI32(data, 12);
    int32_t dataSize = readI32(data, 16);
    int32_t importCount = readI32(data, 20);
    int32_t exportCount = readI32(data, 24);
    int32_t entryPoint = readI32(data, 28);
    if (codeSize < 0 || dataSize < 0 || importCount < 0 || exportCount < 0)
        throw std::runtime_error("NCI v2.1: negative segment/table size in header");
    if ((int64_t)NCI_V21_HEADER_SIZE + codeSize + dataSize > fileSize)
        throw std::runtime_error("NCI v2.1: code/data size exceeds file size");
    if (entryPoint < 0 || entryPoint > codeSize)
        throw std::runtime_error("NCI v2.1: entryPoint out of code segment");

    // 导入表：int32 nameLen + name + NUL + pad 到 4 字节对齐（以 entry 起始为基准）+ addr
    // + flags
    int64_t off = NCI_V21_HEADER_SIZE + (int64_t)codeSize + dataSize;
    std::vector<NImportSymbol> imports;
    for (int32_t i = 0; i < importCount; i++) {
        NImportSymbol sym;
        readSymbolEntry(data, fileSize, off, "import", sym.name, sym.addr, sym.flags);
        imports.push_back(sym);
    }

    // 导出表：同构，addr=代码段地址，flags 恒 0
    std::vector<NExportSymbol> exports;
    for (int32_t i = 0; i < exportCount; i++) {
        NExportSymbol sym;
        readSymbolEntry(data, fileSize, off, "export", sym.name, sym.addr, sym.flags);
        exports.push_back(sym);
    }

    // 全部校验通过后再提交，避免异常路径污染 VM 状态。
    // 数据段加载点 = m_stack[codeSize .. codeSize+dataSize)，数据标号统一编址
    if ((int64_t)codeSize + dataSize > m_stackSize)
        throw std::runtime_error("NCI v2.1: data segment does not fit into memory");
    int8_t * newCode = (int8_t *)malloc(codeSize > 0 ? codeSize : 1);
    memcpy(newCode, data + NCI_V21_HEADER_SIZE, codeSize);
    free(m_code);
    m_code = newCode;
    m_codeSize = codeSize;
    m_dataSize = dataSize;
    memcpy(m_stack + codeSize, data + NCI_V21_HEADER_SIZE + codeSize, dataSize);
    m_imports = std::move(imports);
    m_exports = std::move(exports);
    m_pc = entryPoint;
}

// ==== 宿主库函数注册与动态链接 ====

void
NVirtualMachine::registerHostFunction(int32_t addr, NHostFunction fn)
{
    if (addr <= 0 || !fn) {
        printf("Error: registerHostFunction: invalid host address 0x%08X\n",
               (uint32_t)addr);
        return;
    }
    m_hostFunctions[addr] = fn;
}

void
NVirtualMachine::registerHostFunction(const std::string & name, NHostFunction fn)
{
    if (name.empty() || !fn) {
        printf("Error: registerHostFunction: invalid symbol name\n");
        return;
    }
    m_hostFunctionsByName[name] = fn;
}

int32_t
NVirtualMachine::internHostSymbol(const std::string & name, NHostFunction fn)
{
    auto it = m_hostAddrByName.find(name);
    if (it != m_hostAddrByName.end())
        return it->second; // 同一符号复用已分配地址
    int32_t addr = m_nextHostAddr++;
    m_hostFunctions[addr] = fn;
    m_hostAddrByName[name] = addr;
    return addr;
}

int32_t
NVirtualMachine::bindHostSymbol(const std::string & name, NHostFunction fn, int32_t addr)
{
    if (addr == 0)
        return internHostSymbol(name, fn);
    auto it = m_hostAddrByName.find(name);
    if (it != m_hostAddrByName.end())
        return it->second; // 同一符号复用已登记地址
    m_hostFunctions[addr] = fn;
    m_hostAddrByName[name] = addr;
    return addr;
}

// ==== 宿主库函数签名包装器（loadHostLibrary 专用）====
//
// GetProcAddress/dlsym 拿到的是真 C 函数指针，而 VM 宿主函数签名为
// int32_t(*)(int32_t* regs, int8_t* mem, int32_t memSize)，两者 ABI 不同，
// 不能直接 cast 调用。按"已知签名白名单"适配：每个白名单函数一个包装器，
// 从寄存器/统一内存取参、做边界保护后经正确原型调用真函数。整型参数走 R0 起
// （fastcall 传参）；约定位仅是调用方元数据，不影响包装器取参。
// 白名单按需扩展；库中存在但不在白名单的符号 → 明确报错（避免错误 ABI 调用）。
namespace
{
    // VM 统一内存安全取 C 字符串：addr 合法且 [addr, memSize) 内存在 NUL 终止
    // 时返回长度并置 out；越界或未终止返回 -1（防止真 C 函数越界读）
    int32_t
    vmCString(const int8_t * mem, int32_t memSize, int32_t addr, const char *& out)
    {
        if (addr < 0 || addr >= memSize)
            return -1;
        int64_t limit = (int64_t)memSize - addr;
        for (int64_t n = 0; n < limit; ++n) {
            if (mem[addr + n] == '\0') {
                out = reinterpret_cast<const char *>(mem + addr);
                return static_cast<int32_t>(n);
            }
        }
        return -1;
    }

    // 每个白名单函数的真函数指针槽（loadHostLibrary 解析命中后填充）
    void * g_real_puts = nullptr;
    void * g_real_putchar = nullptr;
    void * g_real_abs = nullptr;
    void * g_real_atoi = nullptr;
    void * g_real_strlen = nullptr;
    void * g_real_exit = nullptr;
#ifdef _WIN32
    void * g_real_GetTickCount = nullptr;
#endif

    // int puts(const char*)：输出统一内存中的 C 字符串（含换行由调用方自带）
    int32_t
    wrap_puts(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef int (*RealFn)(const char *);
        const char * s = nullptr;
        if (!g_real_puts || vmCString(mem, memSize, regs[0], s) < 0) {
            fprintf(stderr,
                    "Error: host-lib puts: R0=%d 不是统一内存中的有效 C 字符串\n",
                    regs[0]);
            return -1;
        }
        return reinterpret_cast<RealFn>(g_real_puts)(s);
    }

    // int putchar(int)
    int32_t
    wrap_putchar(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef int (*RealFn)(int);
        (void)mem;
        (void)memSize;
        if (!g_real_putchar)
            return -1;
        return reinterpret_cast<RealFn>(g_real_putchar)(regs[0]);
    }

    // int abs(int)
    int32_t
    wrap_abs(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef int (*RealFn)(int);
        (void)mem;
        (void)memSize;
        if (!g_real_abs) {
            fprintf(stderr, "Error: host-lib abs: 真函数指针未解析\n");
            return 0;
        }
        return reinterpret_cast<RealFn>(g_real_abs)(regs[0]);
    }

    // int atoi(const char*)
    int32_t
    wrap_atoi(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef int (*RealFn)(const char *);
        const char * s = nullptr;
        if (!g_real_atoi || vmCString(mem, memSize, regs[0], s) < 0) {
            fprintf(stderr,
                    "Error: host-lib atoi: R0=%d 不是统一内存中的有效 C 字符串\n",
                    regs[0]);
            return 0;
        }
        return reinterpret_cast<RealFn>(g_real_atoi)(s);
    }

    // size_t strlen(const char*)：返回值截断为 int32（VM 整型即 32 位）
    int32_t
    wrap_strlen(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef size_t (*RealFn)(const char *);
        const char * s = nullptr;
        if (!g_real_strlen || vmCString(mem, memSize, regs[0], s) < 0) {
            fprintf(stderr,
                    "Error: host-lib strlen: R0=%d 不是统一内存中的有效 C 字符串\n",
                    regs[0]);
            return 0;
        }
        return static_cast<int32_t>(reinterpret_cast<RealFn>(g_real_strlen)(s));
    }

    // void exit(int)：以调用方给出的码终止 nvm 进程（规范 §5.1 示例语义）
    int32_t
    wrap_exit(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef void (*RealFn)(int);
        (void)mem;
        (void)memSize;
        if (g_real_exit)
            reinterpret_cast<RealFn>(g_real_exit)(regs[0]);
        return 0;
    }

#ifdef _WIN32
    // DWORD GetTickCount(void)：无参，返回值按 int32 回写 R0
    int32_t
    wrap_GetTickCount(int32_t * regs, int8_t * mem, int32_t memSize)
    {
        typedef unsigned long (*RealFn)(void);
        (void)regs;
        (void)mem;
        (void)memSize;
        if (!g_real_GetTickCount)
            return 0;
        return static_cast<int32_t>(reinterpret_cast<RealFn>(g_real_GetTickCount)());
    }
#endif

    struct HostLibBinding {
        const char * name;
        NHostFunction wrapper;
        void ** slot; // 真函数指针槽
    };

    // 一期白名单：msvcrt/libc 常用整型签名函数；GetTickCount 供旧格式动态链接
    // 路径（addr=0 + kernel32.dll）回归使用
    HostLibBinding HOST_LIB_BINDINGS[] = {
        { "puts", wrap_puts, &g_real_puts },
        { "putchar", wrap_putchar, &g_real_putchar },
        { "abs", wrap_abs, &g_real_abs },
        { "atoi", wrap_atoi, &g_real_atoi },
        { "strlen", wrap_strlen, &g_real_strlen },
        { "exit", wrap_exit, &g_real_exit },
#ifdef _WIN32
        { "GetTickCount", wrap_GetTickCount, &g_real_GetTickCount },
#endif
    };

    const HostLibBinding *
    findHostLibBinding(const std::string & name)
    {
        for (const auto & b : HOST_LIB_BINDINGS)
            if (name == b.name)
                return &b;
        return nullptr;
    }
} // namespace

bool
NVirtualMachine::resolveImportsByName()
{
    bool ok = true;
    for (auto & sym : m_imports) {
        if (!isDynamicImport(sym))
            continue;
        if (m_hostAddrByName.count(sym.name))
            continue; // 已被此前的解析路径登记
        if (sym.addr != 0 && m_hostFunctions.count(sym.addr))
            continue;
        auto it = m_hostFunctionsByName.find(sym.name);
        if (it == m_hostFunctionsByName.end()) {
            printf("Error: resolveImportsByName: unresolved import symbol '%s'\n",
                   sym.name.c_str());
            ok = false;
            continue;
        }
        sym.addr = bindHostSymbol(sym.name, it->second, sym.addr);
    }
    return ok;
}

bool
NVirtualMachine::loadHostLibrary(const std::string & path)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(path.c_str());
    if (!lib) {
        printf("Error: loadHostLibrary: cannot load '%s' (GetLastError=%lu)\n",
               path.c_str(),
               GetLastError());
        return false;
    }
#else
    void * lib = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        printf("Error: loadHostLibrary: cannot load '%s' (%s)\n",
               path.c_str(),
               dlerror());
        return false;
    }
#endif
    bool ok = true;
    for (auto & sym : m_imports) {
        if (!isDynamicImport(sym))
            continue; // 静态绑定不解析
        if (m_hostAddrByName.count(sym.name))
            continue; // 已被此前的解析路径登记
        if (sym.addr != 0 && m_hostFunctions.count(sym.addr))
            continue;
#ifdef _WIN32
        FARPROC proc = GetProcAddress(lib, sym.name.c_str());
#else
        void * proc = dlsym(lib, sym.name.c_str());
#endif
        if (!proc) {
            printf("Error: loadHostLibrary: symbol '%s' not found in '%s'\n",
                   sym.name.c_str(),
                   path.c_str());
            ok = false;
            continue;
        }
        // 真 C 函数指针不能直接当 VM 宿主函数调用（ABI 不同）：查白名单取包装器
        const HostLibBinding * binding = findHostLibBinding(sym.name);
        if (!binding) {
            printf("Error: loadHostLibrary: symbol '%s' found in '%s' but has no "
                   "known signature wrapper (host-lib whitelist)\n",
                   sym.name.c_str(),
                   path.c_str());
            ok = false;
            continue;
        }
        void * addr = nullptr;
        memcpy(&addr, &proc, sizeof(addr)); // 函数指针 → 对象指针（避免直接 cast）
        *binding->slot = addr;
        sym.addr = bindHostSymbol(sym.name, binding->wrapper, sym.addr);
    }
    return ok;
}

void
NVirtualMachine::start()
{
    if (!m_code || !m_stack)
        return;
    typedef void (NVirtualMachine::*H)();
    H h[256] = {};
    h[0x00] = &NVirtualMachine::executeLMM;
    h[0x01] = &NVirtualMachine::executeST;
    h[0x02] = &NVirtualMachine::executeLEA;
    h[0x03] = &NVirtualMachine::executeLOAD;
    h[0x04] = &NVirtualMachine::executeSTORE;
    h[0x05] = &NVirtualMachine::executeLOADA;
    h[0x06] = &NVirtualMachine::executeSTOREA;
    h[0x10] = &NVirtualMachine::executeADD;
    h[0x11] = &NVirtualMachine::executeADDI;
    h[0x12] = &NVirtualMachine::executeSUB;
    h[0x13] = &NVirtualMachine::executeSUBI;
    h[0x14] = &NVirtualMachine::executeMUL;
    h[0x15] = &NVirtualMachine::executeMULI;
    h[0x16] = &NVirtualMachine::executeDIV;
    h[0x17] = &NVirtualMachine::executeDIVI;
    h[0x18] = &NVirtualMachine::executeMOD;
    h[0x19] = &NVirtualMachine::executeMODI;
    h[0x1A] = &NVirtualMachine::executeNOT;
    h[0x1B] = &NVirtualMachine::executeNEG;
    h[0x20] = &NVirtualMachine::executeAND;
    h[0x21] = &NVirtualMachine::executeANDI;
    h[0x22] = &NVirtualMachine::executeOR;
    h[0x23] = &NVirtualMachine::executeORI;
    h[0x24] = &NVirtualMachine::executeXOR;
    h[0x25] = &NVirtualMachine::executeXORI;
    h[0x26] = &NVirtualMachine::executeSHL;
    h[0x27] = &NVirtualMachine::executeSHLI;
    h[0x28] = &NVirtualMachine::executeSHR;
    h[0x29] = &NVirtualMachine::executeSHRI;
    h[0x30] = &NVirtualMachine::executeCMP;
    h[0x31] = &NVirtualMachine::executeCMPI;
    h[0x32] = &NVirtualMachine::executeTEST;
    h[0x40] = &NVirtualMachine::executePUSH;
    h[0x41] = &NVirtualMachine::executePUSHI;
    h[0x42] = &NVirtualMachine::executePOP;
    h[0x43] = &NVirtualMachine::executeENTER;
    h[0x44] = &NVirtualMachine::executeLEAVE;
    h[0x50] = &NVirtualMachine::executeJMP;
    h[0x51] = &NVirtualMachine::executeJZ;
    h[0x52] = &NVirtualMachine::executeJNZ;
    h[0x53] = &NVirtualMachine::executeJN;
    h[0x54] = &NVirtualMachine::executeJP;
    h[0x60] = &NVirtualMachine::executeCALL;
    h[0x61] = &NVirtualMachine::executeCALLX;
    h[0x62] = &NVirtualMachine::executeRET;
    h[0x70] = &NVirtualMachine::executeMOV;
    h[0x71] = &NVirtualMachine::executeCLR;
    h[0x7F] = &NVirtualMachine::executeNOP;
    // 栈底压入哨兵返回地址：main 顶层的 leave/ret 落到代码段末尾，循环自然结束
    m_sp -= STACK_SLOT_SIZE;
    memWrite32(m_stack, m_sp, (int32_t)m_codeSize);

    while (m_pc < m_codeSize) {
        uint8_t op = m_code[m_pc];
        if (h[op])
            (this->*h[op])();
        else {
            // VM 运行时诊断走 stdout（与 CALLX 未解析等运行时报错一致，
            // tests 以 CaptureStdout 钉死该约定）；格式对齐 "Error: " 前缀
            printf("Error: unknown opcode 0x%02X at pc %d, execution stopped\n",
                   op,
                   m_pc);
            break;
        }
    }
}

void
NVirtualMachine::print_info()
{
    printf("PC=%d SP=%d BP=%d AX=%d F=%d\n", m_pc, m_sp, m_bp, m_ax, m_flags);
}

void
NVirtualMachine::print_stack(int32_t s, int32_t e)
{
    for (int i = s; i < e; i += STACK_SLOT_SIZE)
        printf("[%04X]=%d\n", i, memRead32(m_stack, i));
}

int32_t
NVirtualMachine::getRegister(int32_t i)
{
    return i < REGISTER_COUNT ? m_registers[i] : 0;
}
void
NVirtualMachine::setRegister(int32_t i, int32_t v)
{
    if (i < REGISTER_COUNT)
        m_registers[i] = v;
}
int32_t
NVirtualMachine::getPC()
{
    return m_pc;
}
void
NVirtualMachine::setPC(int32_t v)
{
    m_pc = v;
}
int32_t
NVirtualMachine::getSP()
{
    return m_sp;
}
void
NVirtualMachine::setSP(int32_t v)
{
    m_sp = v;
}
int32_t
NVirtualMachine::getBP()
{
    return m_bp;
}
void
NVirtualMachine::setBP(int32_t v)
{
    m_bp = v;
}
int32_t
NVirtualMachine::getAX()
{
    return m_ax;
}
void
NVirtualMachine::setAX(int32_t v)
{
    m_ax = v;
}
int32_t
NVirtualMachine::getFlags()
{
    return m_flags;
}
void
NVirtualMachine::setFlags(int32_t v)
{
    m_flags = v;
}
int32_t
NVirtualMachine::getStackSize()
{
    return m_stackSize;
}
int64_t
NVirtualMachine::getCodeSize()
{
    return m_codeSize;
}
int32_t
NVirtualMachine::getDataSize()
{
    return m_dataSize;
}
const std::vector<NImportSymbol> &
NVirtualMachine::getImports()
{
    return m_imports;
}
const std::vector<NExportSymbol> &
NVirtualMachine::getExports()
{
    return m_exports;
}
int8_t *
NVirtualMachine::getStack()
{
    return m_stack;
}
int8_t *
NVirtualMachine::getCode()
{
    return m_code;
}
