#include "nvm/core.hpp"
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 小端 32 位读取（memcpy 实现避免对齐问题）
static int32_t
readI32(const int8_t * data, int64_t offset)
{
    int32_t v;
    memcpy(&v, data + offset, sizeof(v));
    return v;
}

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
    // m_sp/m_bp 是 m_registers[4]/[5] 的引用别名，须先清零寄存器，
    // 再通过引用写入 SP 初始值（栈从高地址向低地址生长）
    for (int i = 0; i < 8; i++)
        m_registers[i] = 0;
    m_sp = stackSize;
}

NVirtualMachine::~NVirtualMachine() { free(m_stack); }

void
NVirtualMachine::load(std::string filename)
{
    FILE * pf = fopen(filename.c_str(), "rb");
    if (!pf) {
        printf("Error: Cannot open %s\n", filename.c_str());
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
        loadV21(data, size);
        free(data);
    }
    else {
        // 旧裸格式 fallback：整文件当代码
        m_codeSize = size;
        m_code = data;
        m_pc = 0;
        data = NULL;
    }
    if (data)
        free(data);
}

// 严格 v2.1 加载：header(32B) | code | data | import table | export table
void
NVirtualMachine::loadV21(const int8_t * data, int64_t fileSize)
{
    if (memcmp(data, "NanoC\0\0\0", 8) != 0)
        throw std::runtime_error("NCI v2.1: bad magic (expect \"NanoC\\0\\0\\0\")");
    int32_t headerSize = readI32(data, 8);
    if (headerSize != 32)
        throw std::runtime_error("NCI v2.1: unsupported headerSize "
                                 + std::to_string(headerSize) + " (expect 32)");
    int32_t codeSize = readI32(data, 12);
    int32_t dataSize = readI32(data, 16);
    int32_t importCount = readI32(data, 20);
    int32_t exportCount = readI32(data, 24);
    int32_t entryPoint = readI32(data, 28);
    if (codeSize < 0 || dataSize < 0 || importCount < 0 || exportCount < 0)
        throw std::runtime_error("NCI v2.1: negative segment/table size in header");
    if ((int64_t)32 + codeSize + dataSize > fileSize)
        throw std::runtime_error("NCI v2.1: code/data size exceeds file size");
    if (entryPoint < 0 || entryPoint > codeSize)
        throw std::runtime_error("NCI v2.1: entryPoint out of code segment");

    // 导入表：int32 nameLen + name + NUL + pad 到 4 字节对齐（以 entry 起始为基准）+ addr
    // + flags
    int64_t off = 32 + (int64_t)codeSize + dataSize;
    std::vector<NImportSymbol> imports;
    for (int32_t i = 0; i < importCount; i++) {
        if (off + 4 > fileSize)
            throw std::runtime_error("NCI v2.1: truncated import table");
        int32_t nameLen = readI32(data, off);
        if (nameLen < 0 || off + 4 + (int64_t)nameLen + 1 > fileSize)
            throw std::runtime_error("NCI v2.1: bad import symbol name");
        NImportSymbol sym;
        sym.name.assign((const char *)&data[off + 4], nameLen);
        int64_t fixed = ((int64_t)4 + nameLen + 1 + 3) & ~(int64_t)3;
        if (off + fixed + 8 > fileSize)
            throw std::runtime_error("NCI v2.1: truncated import entry");
        sym.addr = readI32(data, off + fixed);
        sym.flags = readI32(data, off + fixed + 4);
        imports.push_back(sym);
        off += fixed + 8;
    }

    // 导出表：同构，addr=代码段地址，flags 恒 0
    std::vector<NExportSymbol> exports;
    for (int32_t i = 0; i < exportCount; i++) {
        if (off + 4 > fileSize)
            throw std::runtime_error("NCI v2.1: truncated export table");
        int32_t nameLen = readI32(data, off);
        if (nameLen < 0 || off + 4 + (int64_t)nameLen + 1 > fileSize)
            throw std::runtime_error("NCI v2.1: bad export symbol name");
        NExportSymbol sym;
        sym.name.assign((const char *)&data[off + 4], nameLen);
        int64_t fixed = ((int64_t)4 + nameLen + 1 + 3) & ~(int64_t)3;
        if (off + fixed + 8 > fileSize)
            throw std::runtime_error("NCI v2.1: truncated export entry");
        sym.addr = readI32(data, off + fixed);
        sym.flags = readI32(data, off + fixed + 4);
        exports.push_back(sym);
        off += fixed + 8;
    }

    // 全部校验通过后再提交，避免异常路径污染 VM 状态。
    // 数据段加载点 = m_stack[codeSize .. codeSize+dataSize)，数据标号统一编址
    if ((int64_t)codeSize + dataSize > m_stackSize)
        throw std::runtime_error("NCI v2.1: data segment does not fit into memory");
    int8_t * newCode = (int8_t *)malloc(codeSize > 0 ? codeSize : 1);
    memcpy(newCode, data + 32, codeSize);
    free(m_code);
    m_code = newCode;
    m_codeSize = codeSize;
    m_dataSize = dataSize;
    memcpy(m_stack + codeSize, data + 32 + codeSize, dataSize);
    m_imports = std::move(imports);
    m_exports = std::move(exports);
    m_pc = entryPoint;
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
    m_sp -= 4;
    *(int32_t *)&m_stack[m_sp] = (int32_t)m_codeSize;

    while (m_pc < m_codeSize) {
        uint8_t op = m_code[m_pc];
        if (h[op])
            (this->*h[op])();
        else {
            printf("Unknown op 0x%02X at %d\n", op, m_pc);
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
    for (int i = s; i < e; i += 4)
        printf("[%04X]=%d\n", i, *(int32_t *)&m_stack[i]);
}

int32_t
NVirtualMachine::getRegister(int32_t i)
{
    return i < 8 ? m_registers[i] : 0;
}
void
NVirtualMachine::setRegister(int32_t i, int32_t v)
{
    if (i < 8)
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
