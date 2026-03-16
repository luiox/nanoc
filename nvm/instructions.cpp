// ============================================================================
// NanoC NCI v2.1 指令实现
// 设计原则：极简主义，无系统调用，所有功能通过 C 函数实现
// ============================================================================

#include <algorithm>
#include <cctype>
#include <instructions.hpp>
#include <string_helper.hpp>

// ============================================================================
// 全局映射表
// ============================================================================

std::map<std::string, NRegister> g_textToRegisterMap = {
    { "R0", NRegister::R0 }, { "R1", NRegister::R1 }, { "R2", NRegister::R2 },
    { "R3", NRegister::R3 }, { "R4", NRegister::R4 }, { "R5", NRegister::R5 },
    { "R6", NRegister::R6 }, { "R7", NRegister::R7 }
};

// 辅助函数：从文本提取寄存器
static NRegister
parseRegister(const std::string & str)
{
    std::string reg = str;
    trim(reg);
    std::transform(reg.begin(), reg.end(), reg.begin(), ::toupper);
    auto it = g_textToRegisterMap.find(reg);
    if (it != g_textToRegisterMap.end()) {
        return it->second;
    }
    return NRegister::R0;
}

// 辅助函数：解析 32 位立即数
static bool
parseInt32(const std::string & str, int32_t & out)
{
    std::string s = str;
    trim(s);
    if (s.empty())
        return false;

    try {
        if (s.substr(0, 2) == "0x" || s.substr(0, 2) == "0X") {
            out = std::stoi(s, nullptr, 16);
        }
        else {
            out = std::stoi(s, nullptr, 10);
        }
        return true;
    }
    catch (...) {
        return false;
    }
}

// 辅助函数：解析 16 位立即数
static bool
parseInt16(const std::string & str, int16_t & out)
{
    int32_t val;
    if (!parseInt32(str, val))
        return false;
    out = static_cast<int16_t>(val);
    return true;
}

// 辅助函数：解析 8 位立即数
static bool
parseInt8(const std::string & str, int8_t & out)
{
    int32_t val;
    if (!parseInt32(str, val))
        return false;
    out = static_cast<int8_t>(val);
    return true;
}

// 辅助函数：写入 32 位值（小端序）
static void
writeInt32(std::vector<uint8_t> & code, int32_t val)
{
    code.push_back(static_cast<uint8_t>((val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

// 辅助函数：写入 16 位值（小端序）
static void
writeInt16(std::vector<uint8_t> & code, int16_t val)
{
    code.push_back(static_cast<uint8_t>((val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

// ============================================================================
// 内存访问指令
// ============================================================================

// ----------------------------------------------------------------------------
// LMM - Load Memory to Register
// ----------------------------------------------------------------------------
NInstructionsLMM::NInstructionsLMM(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::LMM;
}

std::vector<uint8_t>
NInstructionsLMM::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsLMM::generateInstructionName()
{
    return "lmm";
}

NInstructionsInterface *
NInstructionsLMM::parserInstructionText(std::string & text)
{
    // 格式：lmm R0, 100
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    // 提取寄存器（跳过 "lmm"）
    size_t regStart = regStr.find_first_not_of("lLmM");
    if (regStart == std::string::npos)
        return nullptr;
    regStr = regStr.substr(regStart);
    trim(regStr);

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsLMM(reg, val);
}

// ----------------------------------------------------------------------------
// ST - Store to direct address
// ----------------------------------------------------------------------------
NInstructionsST::NInstructionsST(NRegister reg, int32_t addr)
  : m_reg(reg)
  , m_addr(addr)
{
    m_opcode = NOpcode::ST;
}

std::vector<uint8_t>
NInstructionsST::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsST::generateInstructionName()
{
    return "st";
}

NInstructionsInterface *
NInstructionsST::parserInstructionText(std::string & text)
{
    // 格式：st R0, 0x1000
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string addrStr = text.substr(pos + 1);
    trim(regStr);
    trim(addrStr);

    size_t regStart = regStr.find_first_not_of("sStT");
    if (regStart == std::string::npos)
        return nullptr;
    regStr = regStr.substr(regStart);
    trim(regStr);

    NRegister reg = parseRegister(regStr);
    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsST(reg, addr);
}

// ----------------------------------------------------------------------------
// LEA - Load Effective Address
// ----------------------------------------------------------------------------
NInstructionsLEA::NInstructionsLEA(NRegister reg, int32_t addr)
  : m_reg(reg)
  , m_addr(addr)
{
    m_opcode = NOpcode::LEA;
}

std::vector<uint8_t>
NInstructionsLEA::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsLEA::generateInstructionName()
{
    return "lea";
}

NInstructionsInterface *
NInstructionsLEA::parserInstructionText(std::string & text)
{
    // 格式：lea R0, label
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string addrStr = text.substr(pos + 1);
    trim(regStr);
    trim(addrStr);

    size_t regStart = regStr.find_first_not_of("lLeEaA");
    if (regStart == std::string::npos)
        return nullptr;
    regStr = regStr.substr(regStart);
    trim(regStr);

    NRegister reg = parseRegister(regStr);
    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsLEA(reg, addr);
}

// ----------------------------------------------------------------------------
// LOAD - Load from memory (间接寻址)
// ----------------------------------------------------------------------------
NInstructionsLOAD::NInstructionsLOAD(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::LOAD;
}

std::vector<uint8_t>
NInstructionsLOAD::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsLOAD::generateInstructionName()
{
    return "load";
}

NInstructionsInterface *
NInstructionsLOAD::parserInstructionText(std::string & text)
{
    // 格式：load R1, [R0]
    size_t lbracket = text.find('[');
    size_t rbracket = text.find(']');
    if (lbracket == std::string::npos || rbracket == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, lbracket);
    std::string srcStr = text.substr(lbracket + 1, rbracket - lbracket - 1);
    trim(destStr);
    trim(srcStr);

    // 跳过 "load"
    size_t destStart = destStr.find_first_not_of("lL oO aA dD");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsLOAD(dest, src);
}

// ----------------------------------------------------------------------------
// STORE - Store to memory (间接寻址)
// ----------------------------------------------------------------------------
NInstructionsSTORE::NInstructionsSTORE(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::STORE;
}

std::vector<uint8_t>
NInstructionsSTORE::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsSTORE::generateInstructionName()
{
    return "store";
}

NInstructionsInterface *
NInstructionsSTORE::parserInstructionText(std::string & text)
{
    // 格式：store [R0], R1
    size_t lbracket = text.find('[');
    size_t rbracket = text.find(']');
    if (lbracket == std::string::npos || rbracket == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(lbracket + 1, rbracket - lbracket - 1);
    std::string srcStr = text.substr(rbracket + 1);
    trim(destStr);
    trim(srcStr);

    // 跳过 "store"
    size_t srcStart = srcStr.find_first_not_of("sStT oO rR eE");
    if (srcStart != std::string::npos) {
        srcStr = srcStr.substr(srcStart);
        trim(srcStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsSTORE(dest, src);
}

// ============================================================================
// 算术运算指令
// ============================================================================

// ----------------------------------------------------------------------------
// ADD - Add registers
// ----------------------------------------------------------------------------
NInstructionsADD::NInstructionsADD(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::ADD;
}

std::vector<uint8_t>
NInstructionsADD::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsADD::generateInstructionName()
{
    return "add";
}

NInstructionsInterface *
NInstructionsADD::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("aA dD");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsADD(dest, src);
}

// ----------------------------------------------------------------------------
// ADDI - Add immediate
// ----------------------------------------------------------------------------
NInstructionsADDI::NInstructionsADDI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::ADDI;
}

std::vector<uint8_t>
NInstructionsADDI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsADDI::generateInstructionName()
{
    return "addi";
}

NInstructionsInterface *
NInstructionsADDI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("aA dD dDiI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsADDI(reg, val);
}

// ----------------------------------------------------------------------------
// SUB - Subtract registers
// ----------------------------------------------------------------------------
NInstructionsSUB::NInstructionsSUB(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::SUB;
}

std::vector<uint8_t>
NInstructionsSUB::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsSUB::generateInstructionName()
{
    return "sub";
}

NInstructionsInterface *
NInstructionsSUB::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("sS uU bB");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsSUB(dest, src);
}

// ----------------------------------------------------------------------------
// SUBI - Subtract immediate
// ----------------------------------------------------------------------------
NInstructionsSUBI::NInstructionsSUBI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::SUBI;
}

std::vector<uint8_t>
NInstructionsSUBI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsSUBI::generateInstructionName()
{
    return "subi";
}

NInstructionsInterface *
NInstructionsSUBI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("sS uU bB iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsSUBI(reg, val);
}

// ----------------------------------------------------------------------------
// MUL - Multiply registers
// ----------------------------------------------------------------------------
NInstructionsMUL::NInstructionsMUL(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::MUL;
}

std::vector<uint8_t>
NInstructionsMUL::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsMUL::generateInstructionName()
{
    return "mul";
}

NInstructionsInterface *
NInstructionsMUL::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("mM uU lL");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsMUL(dest, src);
}

// ----------------------------------------------------------------------------
// MULI - Multiply immediate
// ----------------------------------------------------------------------------
NInstructionsMULI::NInstructionsMULI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::MULI;
}

std::vector<uint8_t>
NInstructionsMULI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsMULI::generateInstructionName()
{
    return "muli";
}

NInstructionsInterface *
NInstructionsMULI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("mM uU lL iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsMULI(reg, val);
}

// ============================================================================
// 栈操作指令
// ============================================================================

// ----------------------------------------------------------------------------
// PUSH - Push register to stack
// ----------------------------------------------------------------------------
NInstructionsPUSH::NInstructionsPUSH(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::PUSH;
}

std::vector<uint8_t>
NInstructionsPUSH::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsPUSH::generateInstructionName()
{
    return "push";
}

NInstructionsInterface *
NInstructionsPUSH::parserInstructionText(std::string & text)
{
    std::string regStr = text;
    size_t start = regStr.find_first_not_of("pP uU sS hH");
    if (start != std::string::npos) {
        regStr = regStr.substr(start);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    return new NInstructionsPUSH(reg);
}

// ----------------------------------------------------------------------------
// PUSHI - Push immediate to stack
// ----------------------------------------------------------------------------
NInstructionsPUSHI::NInstructionsPUSHI(int32_t val)
  : m_val(val)
{
    m_opcode = NOpcode::PUSHI;
}

std::vector<uint8_t>
NInstructionsPUSHI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsPUSHI::generateInstructionName()
{
    return "pushi";
}

NInstructionsInterface *
NInstructionsPUSHI::parserInstructionText(std::string & text)
{
    std::string valStr = text;
    size_t start = valStr.find_first_not_of("pP uU sS hH iI");
    if (start != std::string::npos) {
        valStr = valStr.substr(start);
        trim(valStr);
    }

    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsPUSHI(val);
}

// ----------------------------------------------------------------------------
// POP - Pop from stack to register
// ----------------------------------------------------------------------------
NInstructionsPOP::NInstructionsPOP(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::POP;
}

std::vector<uint8_t>
NInstructionsPOP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsPOP::generateInstructionName()
{
    return "pop";
}

NInstructionsInterface *
NInstructionsPOP::parserInstructionText(std::string & text)
{
    std::string regStr = text;
    size_t start = regStr.find_first_not_of("pP oO pP");
    if (start != std::string::npos) {
        regStr = regStr.substr(start);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    return new NInstructionsPOP(reg);
}

// ----------------------------------------------------------------------------
// ENTER - Enter stack frame
// ----------------------------------------------------------------------------
NInstructionsENTER::NInstructionsENTER(int16_t size)
  : m_size(size)
{
    m_opcode = NOpcode::ENTER;
}

std::vector<uint8_t>
NInstructionsENTER::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt16(code, m_size);
    return code;
}

std::string
NInstructionsENTER::generateInstructionName()
{
    return "enter";
}

NInstructionsInterface *
NInstructionsENTER::parserInstructionText(std::string & text)
{
    std::string sizeStr = text;
    size_t start = sizeStr.find_first_not_of("eE nN tT eE rR");
    if (start != std::string::npos) {
        sizeStr = sizeStr.substr(start);
        trim(sizeStr);
    }

    int16_t size;
    if (!parseInt16(sizeStr, size))
        return nullptr;

    return new NInstructionsENTER(size);
}

// ----------------------------------------------------------------------------
// LEAVE - Leave stack frame
// ----------------------------------------------------------------------------
NInstructionsLEAVE::NInstructionsLEAVE() { m_opcode = NOpcode::LEAVE; }

std::vector<uint8_t>
NInstructionsLEAVE::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    return code;
}

std::string
NInstructionsLEAVE::generateInstructionName()
{
    return "leave";
}

NInstructionsInterface *
NInstructionsLEAVE::parserInstructionText(std::string & text)
{
    return new NInstructionsLEAVE();
}

// ============================================================================
// 控制流指令
// ============================================================================

// ----------------------------------------------------------------------------
// JMP - Unconditional jump
// ----------------------------------------------------------------------------
NInstructionsJMP::NInstructionsJMP(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JMP;
}

std::vector<uint8_t>
NInstructionsJMP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsJMP::generateInstructionName()
{
    return "jmp";
}

NInstructionsInterface *
NInstructionsJMP::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("jJ mM pP");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsJMP(addr);
}

// ----------------------------------------------------------------------------
// JZ - Jump if zero
// ----------------------------------------------------------------------------
NInstructionsJZ::NInstructionsJZ(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JZ;
}

std::vector<uint8_t>
NInstructionsJZ::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsJZ::generateInstructionName()
{
    return "jz";
}

NInstructionsInterface *
NInstructionsJZ::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("jJ zZ");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsJZ(addr);
}

// ----------------------------------------------------------------------------
// JNZ - Jump if not zero
// ----------------------------------------------------------------------------
NInstructionsJNZ::NInstructionsJNZ(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JNZ;
}

std::vector<uint8_t>
NInstructionsJNZ::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsJNZ::generateInstructionName()
{
    return "jnz";
}

NInstructionsInterface *
NInstructionsJNZ::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("jJ nN zZ");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsJNZ(addr);
}

// ----------------------------------------------------------------------------
// JN - Jump if negative
// ----------------------------------------------------------------------------
NInstructionsJN::NInstructionsJN(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JN;
}

std::vector<uint8_t>
NInstructionsJN::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsJN::generateInstructionName()
{
    return "jn";
}

NInstructionsInterface *
NInstructionsJN::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("jJ nN");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsJN(addr);
}

// ----------------------------------------------------------------------------
// JP - Jump if positive
// ----------------------------------------------------------------------------
NInstructionsJP::NInstructionsJP(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JP;
}

std::vector<uint8_t>
NInstructionsJP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsJP::generateInstructionName()
{
    return "jp";
}

NInstructionsInterface *
NInstructionsJP::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("jJ pP");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsJP(addr);
}

// ============================================================================
// 函数调用指令
// ============================================================================

// ----------------------------------------------------------------------------
// CALL - Call internal function
// ----------------------------------------------------------------------------
NInstructionsCALL::NInstructionsCALL(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::CALL;
}

std::vector<uint8_t>
NInstructionsCALL::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsCALL::generateInstructionName()
{
    return "call";
}

NInstructionsInterface *
NInstructionsCALL::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("cC aA lL lL");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsCALL(addr);
}

// ----------------------------------------------------------------------------
// CALLX - Call external function
// ----------------------------------------------------------------------------
NInstructionsCALLX::NInstructionsCALLX(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::CALLX;
}

std::vector<uint8_t>
NInstructionsCALLX::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsCALLX::generateInstructionName()
{
    return "callx";
}

NInstructionsInterface *
NInstructionsCALLX::parserInstructionText(std::string & text)
{
    std::string addrStr = text;
    size_t start = addrStr.find_first_not_of("cC aA lL lL xX");
    if (start != std::string::npos) {
        addrStr = addrStr.substr(start);
        trim(addrStr);
    }

    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsCALLX(addr);
}

// ----------------------------------------------------------------------------
// RET - Return from function
// ----------------------------------------------------------------------------
NInstructionsRET::NInstructionsRET() { m_opcode = NOpcode::RET; }

std::vector<uint8_t>
NInstructionsRET::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    return code;
}

std::string
NInstructionsRET::generateInstructionName()
{
    return "ret";
}

NInstructionsInterface *
NInstructionsRET::parserInstructionText(std::string & text)
{
    return new NInstructionsRET();
}

// ============================================================================
// 寄存器操作指令
// ============================================================================

// ----------------------------------------------------------------------------
// MOV - Move between registers
// ----------------------------------------------------------------------------
NInstructionsMOV::NInstructionsMOV(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::MOV;
}

std::vector<uint8_t>
NInstructionsMOV::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsMOV::generateInstructionName()
{
    return "mov";
}

NInstructionsInterface *
NInstructionsMOV::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("mM oO vV");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsMOV(dest, src);
}

// ----------------------------------------------------------------------------
// CLR - Clear register
// ----------------------------------------------------------------------------
NInstructionsCLR::NInstructionsCLR(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::CLR;
}

std::vector<uint8_t>
NInstructionsCLR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsCLR::generateInstructionName()
{
    return "clr";
}

NInstructionsInterface *
NInstructionsCLR::parserInstructionText(std::string & text)
{
    std::string regStr = text;
    size_t start = regStr.find_first_not_of("cC lL rR");
    if (start != std::string::npos) {
        regStr = regStr.substr(start);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    return new NInstructionsCLR(reg);
}

// ----------------------------------------------------------------------------
// NOP - No operation
// ----------------------------------------------------------------------------
NInstructionsNOP::NInstructionsNOP() { m_opcode = NOpcode::NOP; }

std::vector<uint8_t>
NInstructionsNOP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    return code;
}

std::string
NInstructionsNOP::generateInstructionName()
{
    return "nop";
}

NInstructionsInterface *
NInstructionsNOP::parserInstructionText(std::string & text)
{
    return new NInstructionsNOP();
}

// ============================================================================
// 算术运算指令（续）
// ============================================================================

// ----------------------------------------------------------------------------
// DIV - Divide registers
// ----------------------------------------------------------------------------
NInstructionsDIV::NInstructionsDIV(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::DIV;
}

std::vector<uint8_t>
NInstructionsDIV::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsDIV::generateInstructionName()
{
    return "div";
}

NInstructionsInterface *
NInstructionsDIV::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("dD iI vV");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsDIV(dest, src);
}

// ----------------------------------------------------------------------------
// DIVI - Divide immediate
// ----------------------------------------------------------------------------
NInstructionsDIVI::NInstructionsDIVI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::DIVI;
}

std::vector<uint8_t>
NInstructionsDIVI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsDIVI::generateInstructionName()
{
    return "divi";
}

NInstructionsInterface *
NInstructionsDIVI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("dD iI vV iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsDIVI(reg, val);
}

// ----------------------------------------------------------------------------
// MOD - Modulo registers
// ----------------------------------------------------------------------------
NInstructionsMOD::NInstructionsMOD(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::MOD;
}

std::vector<uint8_t>
NInstructionsMOD::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsMOD::generateInstructionName()
{
    return "mod";
}

NInstructionsInterface *
NInstructionsMOD::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("mM oO dD");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsMOD(dest, src);
}

// ----------------------------------------------------------------------------
// MODI - Modulo immediate
// ----------------------------------------------------------------------------
NInstructionsMODI::NInstructionsMODI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::MODI;
}

std::vector<uint8_t>
NInstructionsMODI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsMODI::generateInstructionName()
{
    return "modi";
}

NInstructionsInterface *
NInstructionsMODI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("mM oO dD iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsMODI(reg, val);
}

// ----------------------------------------------------------------------------
// NOT - Bitwise NOT
// ----------------------------------------------------------------------------
NInstructionsNOT::NInstructionsNOT(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::NOT;
}

std::vector<uint8_t>
NInstructionsNOT::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsNOT::generateInstructionName()
{
    return "not";
}

NInstructionsInterface *
NInstructionsNOT::parserInstructionText(std::string & text)
{
    std::string regStr = text;
    size_t start = regStr.find_first_not_of("nN oO tT");
    if (start != std::string::npos) {
        regStr = regStr.substr(start);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    return new NInstructionsNOT(reg);
}

// ----------------------------------------------------------------------------
// NEG - Arithmetic NEG
// ----------------------------------------------------------------------------
NInstructionsNEG::NInstructionsNEG(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::NEG;
}

std::vector<uint8_t>
NInstructionsNEG::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsNEG::generateInstructionName()
{
    return "neg";
}

NInstructionsInterface *
NInstructionsNEG::parserInstructionText(std::string & text)
{
    std::string regStr = text;
    size_t start = regStr.find_first_not_of("nN eE gG");
    if (start != std::string::npos) {
        regStr = regStr.substr(start);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    return new NInstructionsNEG(reg);
}

// ============================================================================
// 逻辑运算指令
// ============================================================================

// ----------------------------------------------------------------------------
// AND - Bitwise AND registers
// ----------------------------------------------------------------------------
NInstructionsAND::NInstructionsAND(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::AND;
}

std::vector<uint8_t>
NInstructionsAND::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsAND::generateInstructionName()
{
    return "and";
}

NInstructionsInterface *
NInstructionsAND::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("aA nN dD");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsAND(dest, src);
}

// ----------------------------------------------------------------------------
// ANDI - Bitwise AND immediate
// ----------------------------------------------------------------------------
NInstructionsANDI::NInstructionsANDI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::ANDI;
}

std::vector<uint8_t>
NInstructionsANDI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsANDI::generateInstructionName()
{
    return "andi";
}

NInstructionsInterface *
NInstructionsANDI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("aA nN dD iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsANDI(reg, val);
}

// ----------------------------------------------------------------------------
// OR - Bitwise OR registers
// ----------------------------------------------------------------------------
NInstructionsOR::NInstructionsOR(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::OR;
}

std::vector<uint8_t>
NInstructionsOR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsOR::generateInstructionName()
{
    return "or";
}

NInstructionsInterface *
NInstructionsOR::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("oO rR");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsOR(dest, src);
}

// ----------------------------------------------------------------------------
// ORI - Bitwise OR immediate
// ----------------------------------------------------------------------------
NInstructionsORI::NInstructionsORI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::ORI;
}

std::vector<uint8_t>
NInstructionsORI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsORI::generateInstructionName()
{
    return "ori";
}

NInstructionsInterface *
NInstructionsORI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("oO rR iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsORI(reg, val);
}

// ----------------------------------------------------------------------------
// XOR - Bitwise XOR registers
// ----------------------------------------------------------------------------
NInstructionsXOR::NInstructionsXOR(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::XOR;
}

std::vector<uint8_t>
NInstructionsXOR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsXOR::generateInstructionName()
{
    return "xor";
}

NInstructionsInterface *
NInstructionsXOR::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("xX oO rR");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsXOR(dest, src);
}

// ----------------------------------------------------------------------------
// XORI - Bitwise XOR immediate
// ----------------------------------------------------------------------------
NInstructionsXORI::NInstructionsXORI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::XORI;
}

std::vector<uint8_t>
NInstructionsXORI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsXORI::generateInstructionName()
{
    return "xori";
}

NInstructionsInterface *
NInstructionsXORI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("xX oO rR iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsXORI(reg, val);
}

// ----------------------------------------------------------------------------
// SHL - Shift Left registers
// ----------------------------------------------------------------------------
NInstructionsSHL::NInstructionsSHL(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::SHL;
}

std::vector<uint8_t>
NInstructionsSHL::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsSHL::generateInstructionName()
{
    return "shl";
}

NInstructionsInterface *
NInstructionsSHL::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("sS hH lL");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsSHL(dest, src);
}

// ----------------------------------------------------------------------------
// SHLI - Shift Left immediate
// ----------------------------------------------------------------------------
NInstructionsSHLI::NInstructionsSHLI(NRegister reg, int8_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::SHLI;
}

std::vector<uint8_t>
NInstructionsSHLI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>(m_val & 0xFF));
    return code;
}

std::string
NInstructionsSHLI::generateInstructionName()
{
    return "shli";
}

NInstructionsInterface *
NInstructionsSHLI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("sS hH lL iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int8_t val;
    if (!parseInt8(valStr, val))
        return nullptr;

    return new NInstructionsSHLI(reg, val);
}

// ----------------------------------------------------------------------------
// SHR - Shift Right registers
// ----------------------------------------------------------------------------
NInstructionsSHR::NInstructionsSHR(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::SHR;
}

std::vector<uint8_t>
NInstructionsSHR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsSHR::generateInstructionName()
{
    return "shr";
}

NInstructionsInterface *
NInstructionsSHR::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("sS hH rR");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsSHR(dest, src);
}

// ----------------------------------------------------------------------------
// SHRI - Shift Right immediate
// ----------------------------------------------------------------------------
NInstructionsSHRI::NInstructionsSHRI(NRegister reg, int8_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::SHRI;
}

std::vector<uint8_t>
NInstructionsSHRI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>(m_val & 0xFF));
    return code;
}

std::string
NInstructionsSHRI::generateInstructionName()
{
    return "shri";
}

NInstructionsInterface *
NInstructionsSHRI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("sS hH rR iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int8_t val;
    if (!parseInt8(valStr, val))
        return nullptr;

    return new NInstructionsSHRI(reg, val);
}

// ============================================================================
// 比较指令
// ============================================================================

// ----------------------------------------------------------------------------
// CMP - Compare registers
// ----------------------------------------------------------------------------
NInstructionsCMP::NInstructionsCMP(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::CMP;
}

std::vector<uint8_t>
NInstructionsCMP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsCMP::generateInstructionName()
{
    return "cmp";
}

NInstructionsInterface *
NInstructionsCMP::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("cC mM pP");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsCMP(dest, src);
}

// ----------------------------------------------------------------------------
// CMPI - Compare immediate
// ----------------------------------------------------------------------------
NInstructionsCMPI::NInstructionsCMPI(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::CMPI;
}

std::vector<uint8_t>
NInstructionsCMPI::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_val);
    return code;
}

std::string
NInstructionsCMPI::generateInstructionName()
{
    return "cmpi";
}

NInstructionsInterface *
NInstructionsCMPI::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string valStr = text.substr(pos + 1);
    trim(regStr);
    trim(valStr);

    size_t regStart = regStr.find_first_not_of("cC mM pP iI");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t val;
    if (!parseInt32(valStr, val))
        return nullptr;

    return new NInstructionsCMPI(reg, val);
}

// ----------------------------------------------------------------------------
// TEST - Bit test
// ----------------------------------------------------------------------------
NInstructionsTEST::NInstructionsTEST(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::TEST;
}

std::vector<uint8_t>
NInstructionsTEST::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_dest));
    code.push_back(static_cast<uint8_t>(m_src));
    return code;
}

std::string
NInstructionsTEST::generateInstructionName()
{
    return "test";
}

NInstructionsInterface *
NInstructionsTEST::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string destStr = text.substr(0, pos);
    std::string srcStr = text.substr(pos + 1);
    trim(destStr);
    trim(srcStr);

    size_t destStart = destStr.find_first_not_of("tT eE sS tT");
    if (destStart != std::string::npos) {
        destStr = destStr.substr(destStart);
        trim(destStr);
    }

    NRegister dest = parseRegister(destStr);
    NRegister src = parseRegister(srcStr);

    return new NInstructionsTEST(dest, src);
}

// ============================================================================
// 内存访问指令（续）
// ============================================================================

// ----------------------------------------------------------------------------
// LOADA - Load from absolute address
// ----------------------------------------------------------------------------
NInstructionsLOADA::NInstructionsLOADA(NRegister reg, int32_t addr)
  : m_reg(reg)
  , m_addr(addr)
{
    m_opcode = NOpcode::LOADA;
}

std::vector<uint8_t>
NInstructionsLOADA::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsLOADA::generateInstructionName()
{
    return "loada";
}

NInstructionsInterface *
NInstructionsLOADA::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string addrStr = text.substr(pos + 1);
    trim(regStr);
    trim(addrStr);

    size_t regStart = regStr.find_first_not_of("lL oO aA dD aA");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsLOADA(reg, addr);
}

// ----------------------------------------------------------------------------
// STOREA - Store to absolute address
// ----------------------------------------------------------------------------
NInstructionsSTOREA::NInstructionsSTOREA(NRegister reg, int32_t addr)
  : m_reg(reg)
  , m_addr(addr)
{
    m_opcode = NOpcode::STOREA;
}

std::vector<uint8_t>
NInstructionsSTOREA::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(m_opcode));
    code.push_back(static_cast<uint8_t>(m_reg));
    writeInt32(code, m_addr);
    return code;
}

std::string
NInstructionsSTOREA::generateInstructionName()
{
    return "storea";
}

NInstructionsInterface *
NInstructionsSTOREA::parserInstructionText(std::string & text)
{
    size_t pos = text.find(',');
    if (pos == std::string::npos)
        return nullptr;

    std::string regStr = text.substr(0, pos);
    std::string addrStr = text.substr(pos + 1);
    trim(regStr);
    trim(addrStr);

    size_t regStart = regStr.find_first_not_of("sS tT oO rR eE aA");
    if (regStart != std::string::npos) {
        regStr = regStr.substr(regStart);
        trim(regStr);
    }

    NRegister reg = parseRegister(regStr);
    int32_t addr;
    if (!parseInt32(addrStr, addr))
        return nullptr;

    return new NInstructionsSTOREA(reg, addr);
}

// ============================================================================
// 基类方法实现
// ============================================================================

NInstructionsInterface::NInstructionsInterface()
  : m_opcode(NOpcode::NOP)
{
}

NInstructionsInterface::~NInstructionsInterface() = default;

NOpcode
NInstructionsInterface::getOpcode() const
{
    return m_opcode;
}

void
NInstructionsInterface::setOpcode(NOpcode op)
{
    m_opcode = op;
}

NInstructionsInterface *
NInstructionsInterface::parserInstructionText(std::string & text)
{
    // 通用解析器，根据 opcode 映射调用具体解析器
    trim(text);
    if (text.empty())
        return nullptr;

    // 提取 opcode
    std::string opcode = text;
    size_t end = opcode.find_first_of(" \t,");
    if (end != std::string::npos) {
        opcode = opcode.substr(0, end);
    }

    auto it = g_opcodeToGeneratorMap.find(opcode);
    if (it == g_opcodeToGeneratorMap.end()) {
        return nullptr;
    }

    return it->second(text);
}
}
