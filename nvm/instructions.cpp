// NanoC NCI v2.1 Instructions Implementation
#include <algorithm>
#include <cctype>
#include <instructions.hpp>
#include <string_helper.hpp>

// Global register map
std::map<std::string, NRegister> g_textToRegisterMap = {
    { "R0", NRegister::R0 }, { "R1", NRegister::R1 }, { "R2", NRegister::R2 },
    { "R3", NRegister::R3 }, { "R4", NRegister::R4 }, { "R5", NRegister::R5 },
    { "R6", NRegister::R6 }, { "R7", NRegister::R7 }
};

std::map<std::string, NInstructionsInterface * (*)(std::string &)> g_opcodeToGeneratorMap;

// Helper: parse register
static NRegister
parseReg(const std::string & s)
{
    std::string r = s;
    trim(r);
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    auto it = g_textToRegisterMap.find(r);
    return (it != g_textToRegisterMap.end()) ? it->second : NRegister::R0;
}

// Helper: parse int32
static bool
parseInt(const std::string & s, int32_t & out)
{
    std::string str = s;
    trim(str);
    if (str.empty())
        return false;
    try {
        out = (str.size() > 2 && str.substr(0, 2) == "0x") ? std::stoi(str, nullptr, 16)
                                                           : std::stoi(str, nullptr, 10);
        return true;
    }
    catch (...) {
        return false;
    }
}

// Helper: write int32 (little-endian)
static void
write32(std::vector<uint8_t> & c, int32_t v)
{
    c.push_back(v & 0xFF);
    c.push_back((v >> 8) & 0xFF);
    c.push_back((v >> 16) & 0xFF);
    c.push_back((v >> 24) & 0xFF);
}

// Helper: write int16 (little-endian)
static void
write16(std::vector<uint8_t> & c, int16_t v)
{
    c.push_back(v & 0xFF);
    c.push_back((v >> 8) & 0xFF);
}

// LMM: lmm R0, 100
NInstructionsLMM::NInstructionsLMM(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
    m_opcode = NOpcode::LMM;
}
std::vector<uint8_t>
NInstructionsLMM::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    write32(c, m_val);
    return c;
}
std::string
NInstructionsLMM::generateInstructionName()
{
    return "lmm";
}
NInstructionsInterface *
NInstructionsLMM::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string rs = text.substr(3, p - 3); // skip "lmm"
    std::string vs = text.substr(p + 1);
    trim(rs);
    trim(vs);
    int32_t v;
    if (!parseInt(vs, v))
        return nullptr;
    return new NInstructionsLMM(parseReg(rs), v);
}

// ST: st R0, 0x1000
NInstructionsST::NInstructionsST(NRegister reg, int32_t addr)
  : m_reg(reg)
  , m_addr(addr)
{
    m_opcode = NOpcode::ST;
}
std::vector<uint8_t>
NInstructionsST::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsST::generateInstructionName()
{
    return "st";
}
NInstructionsInterface *
NInstructionsST::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string rs = text.substr(2, p - 2);
    std::string as = text.substr(p + 1);
    trim(rs);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsST(parseReg(rs), a);
}

// LEA: lea R0, addr
NInstructionsLEA::NInstructionsLEA(NRegister reg, int32_t addr)
  : m_reg(reg)
  , m_addr(addr)
{
    m_opcode = NOpcode::LEA;
}
std::vector<uint8_t>
NInstructionsLEA::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsLEA::generateInstructionName()
{
    return "lea";
}
NInstructionsInterface *
NInstructionsLEA::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string rs = text.substr(3, p - 3);
    std::string as = text.substr(p + 1);
    trim(rs);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsLEA(parseReg(rs), a);
}

// LOAD: load R1, [R0]
NInstructionsLOAD::NInstructionsLOAD(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::LOAD;
}
std::vector<uint8_t>
NInstructionsLOAD::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_dest));
    c.push_back(static_cast<uint8_t>(m_src));
    return c;
}
std::string
NInstructionsLOAD::generateInstructionName()
{
    return "load";
}
NInstructionsInterface *
NInstructionsLOAD::parserInstructionText(std::string & text)
{
    size_t lb = text.find('['), rb = text.find(']');
    if (lb == std::string::npos || rb == std::string::npos)
        return nullptr;
    std::string ds = text.substr(4, lb - 4);
    std::string ss = text.substr(lb + 1, rb - lb - 1);
    trim(ds);
    trim(ss);
    return new NInstructionsLOAD(parseReg(ds), parseReg(ss));
}

// STORE: store [R0], R1
NInstructionsSTORE::NInstructionsSTORE(NRegister dest, NRegister src)
  : dest(dest)
  , src(src)
{
    m_opcode = NOpcode::STORE;
}
std::vector<uint8_t>
NInstructionsSTORE::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(dest));
    c.push_back(static_cast<uint8_t>(src));
    return c;
}
std::string
NInstructionsSTORE::generateInstructionName()
{
    return "store";
}
NInstructionsInterface *
NInstructionsSTORE::parserInstructionText(std::string & text)
{
    size_t lb = text.find('['), rb = text.find(']');
    if (lb == std::string::npos || rb == std::string::npos)
        return nullptr;
    std::string ds = text.substr(lb + 1, rb - lb - 1);
    std::string ss = text.substr(rb + 1);
    trim(ds);
    trim(ss);
    return new NInstructionsSTORE(parseReg(ds), parseReg(ss));
}

// ADD: add R0, R1
NInstructionsADD::NInstructionsADD(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::ADD;
}
std::vector<uint8_t>
NInstructionsADD::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_dest));
    c.push_back(static_cast<uint8_t>(m_src));
    return c;
}
std::string
NInstructionsADD::generateInstructionName()
{
    return "add";
}
NInstructionsInterface *
NInstructionsADD::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string ds = text.substr(3, p - 3);
    std::string ss = text.substr(p + 1);
    trim(ds);
    trim(ss);
    return new NInstructionsADD(parseReg(ds), parseReg(ss));
}

// SUB: sub R0, R1
NInstructionsSUB::NInstructionsSUB(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::SUB;
}
std::vector<uint8_t>
NInstructionsSUB::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_dest));
    c.push_back(static_cast<uint8_t>(m_src));
    return c;
}
std::string
NInstructionsSUB::generateInstructionName()
{
    return "sub";
}
NInstructionsInterface *
NInstructionsSUB::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string ds = text.substr(3, p - 3);
    std::string ss = text.substr(p + 1);
    trim(ds);
    trim(ss);
    return new NInstructionsSUB(parseReg(ds), parseReg(ss));
}

// MUL: mul R0, R1
NInstructionsMUL::NInstructionsMUL(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::MUL;
}
std::vector<uint8_t>
NInstructionsMUL::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_dest));
    c.push_back(static_cast<uint8_t>(m_src));
    return c;
}
std::string
NInstructionsMUL::generateInstructionName()
{
    return "mul";
}
NInstructionsInterface *
NInstructionsMUL::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string ds = text.substr(3, p - 3);
    std::string ss = text.substr(p + 1);
    trim(ds);
    trim(ss);
    return new NInstructionsMUL(parseReg(ds), parseReg(ss));
}

// DIV: div R0, R1
NInstructionsDIV::NInstructionsDIV(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::DIV;
}
std::vector<uint8_t>
NInstructionsDIV::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_dest));
    c.push_back(static_cast<uint8_t>(m_src));
    return c;
}
std::string
NInstructionsDIV::generateInstructionName()
{
    return "div";
}
NInstructionsInterface *
NInstructionsDIV::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string ds = text.substr(3, p - 3);
    std::string ss = text.substr(p + 1);
    trim(ds);
    trim(ss);
    return new NInstructionsDIV(parseReg(ds), parseReg(ss));
}

// NOT: not R0
NInstructionsNOT::NInstructionsNOT(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::NOT;
}
std::vector<uint8_t>
NInstructionsNOT::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    return c;
}
std::string
NInstructionsNOT::generateInstructionName()
{
    return "not";
}
NInstructionsInterface *
NInstructionsNOT::parserInstructionText(std::string & text)
{
    std::string rs = text.substr(3);
    trim(rs);
    return new NInstructionsNOT(parseReg(rs));
}

// PUSH: push R0
NInstructionsPUSH::NInstructionsPUSH(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::PUSH;
}
std::vector<uint8_t>
NInstructionsPUSH::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    return c;
}
std::string
NInstructionsPUSH::generateInstructionName()
{
    return "push";
}
NInstructionsInterface *
NInstructionsPUSH::parserInstructionText(std::string & text)
{
    std::string rs = text.substr(4);
    trim(rs);
    return new NInstructionsPUSH(parseReg(rs));
}

// POP: pop R0
NInstructionsPOP::NInstructionsPOP(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::POP;
}
std::vector<uint8_t>
NInstructionsPOP::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    return c;
}
std::string
NInstructionsPOP::generateInstructionName()
{
    return "pop";
}
NInstructionsInterface *
NInstructionsPOP::parserInstructionText(std::string & text)
{
    std::string rs = text.substr(3);
    trim(rs);
    return new NInstructionsPOP(parseReg(rs));
}

// ENTER: enter 32
NInstructionsENTER::NInstructionsENTER(int16_t size)
  : m_size(size)
{
    m_opcode = NOpcode::ENTER;
}
std::vector<uint8_t>
NInstructionsENTER::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    write16(c, m_size);
    return c;
}
std::string
NInstructionsENTER::generateInstructionName()
{
    return "enter";
}
NInstructionsInterface *
NInstructionsENTER::parserInstructionText(std::string & text)
{
    std::string ss = text.substr(5);
    trim(ss);
    int32_t v;
    if (!parseInt(ss, v))
        return nullptr;
    return new NInstructionsENTER(static_cast<int16_t>(v));
}

// LEAVE: leave
NInstructionsLEAVE::NInstructionsLEAVE() { m_opcode = NOpcode::LEAVE; }
std::vector<uint8_t>
NInstructionsLEAVE::generateInstructionCode()
{
    return { static_cast<uint8_t>(m_opcode) };
}
std::string
NInstructionsLEAVE::generateInstructionName()
{
    return "leave";
}
NInstructionsInterface *
NInstructionsLEAVE::parserInstructionText(std::string &)
{
    return new NInstructionsLEAVE();
}

// JMP: jmp addr
NInstructionsJMP::NInstructionsJMP(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JMP;
}
std::vector<uint8_t>
NInstructionsJMP::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsJMP::generateInstructionName()
{
    return "jmp";
}
NInstructionsInterface *
NInstructionsJMP::parserInstructionText(std::string & text)
{
    std::string as = text.substr(3);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsJMP(a);
}

// JZ: jz addr
NInstructionsJZ::NInstructionsJZ(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JZ;
}
std::vector<uint8_t>
NInstructionsJZ::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsJZ::generateInstructionName()
{
    return "jz";
}
NInstructionsInterface *
NInstructionsJZ::parserInstructionText(std::string & text)
{
    std::string as = text.substr(2);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsJZ(a);
}

// JNZ: jnz addr
NInstructionsJNZ::NInstructionsJNZ(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::JNZ;
}
std::vector<uint8_t>
NInstructionsJNZ::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsJNZ::generateInstructionName()
{
    return "jnz";
}
NInstructionsInterface *
NInstructionsJNZ::parserInstructionText(std::string & text)
{
    std::string as = text.substr(3);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsJNZ(a);
}

// CALL: call addr
NInstructionsCALL::NInstructionsCALL(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::CALL;
}
std::vector<uint8_t>
NInstructionsCALL::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsCALL::generateInstructionName()
{
    return "call";
}
NInstructionsInterface *
NInstructionsCALL::parserInstructionText(std::string & text)
{
    std::string as = text.substr(4);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsCALL(a);
}

// CALLX: callx addr
NInstructionsCALLX::NInstructionsCALLX(int32_t addr)
  : m_addr(addr)
{
    m_opcode = NOpcode::CALLX;
}
std::vector<uint8_t>
NInstructionsCALLX::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    write32(c, m_addr);
    return c;
}
std::string
NInstructionsCALLX::generateInstructionName()
{
    return "callx";
}
NInstructionsInterface *
NInstructionsCALLX::parserInstructionText(std::string & text)
{
    std::string as = text.substr(5);
    trim(as);
    int32_t a;
    if (!parseInt(as, a))
        return nullptr;
    return new NInstructionsCALLX(a);
}

// RET: ret
NInstructionsRET::NInstructionsRET() { m_opcode = NOpcode::RET; }
std::vector<uint8_t>
NInstructionsRET::generateInstructionCode()
{
    return { static_cast<uint8_t>(m_opcode) };
}
std::string
NInstructionsRET::generateInstructionName()
{
    return "ret";
}
NInstructionsInterface *
NInstructionsRET::parserInstructionText(std::string &)
{
    return new NInstructionsRET();
}

// MOV: mov R0, R1
NInstructionsMOV::NInstructionsMOV(NRegister dest, NRegister src)
  : m_dest(dest)
  , m_src(src)
{
    m_opcode = NOpcode::MOV;
}
std::vector<uint8_t>
NInstructionsMOV::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_dest));
    c.push_back(static_cast<uint8_t>(m_src));
    return c;
}
std::string
NInstructionsMOV::generateInstructionName()
{
    return "mov";
}
NInstructionsInterface *
NInstructionsMOV::parserInstructionText(std::string & text)
{
    size_t p = text.find(',');
    if (p == std::string::npos)
        return nullptr;
    std::string ds = text.substr(3, p - 3);
    std::string ss = text.substr(p + 1);
    trim(ds);
    trim(ss);
    return new NInstructionsMOV(parseReg(ds), parseReg(ss));
}

// CLR: clr R0
NInstructionsCLR::NInstructionsCLR(NRegister reg)
  : m_reg(reg)
{
    m_opcode = NOpcode::CLR;
}
std::vector<uint8_t>
NInstructionsCLR::generateInstructionCode()
{
    std::vector<uint8_t> c;
    c.push_back(static_cast<uint8_t>(m_opcode));
    c.push_back(static_cast<uint8_t>(m_reg));
    return c;
}
std::string
NInstructionsCLR::generateInstructionName()
{
    return "clr";
}
NInstructionsInterface *
NInstructionsCLR::parserInstructionText(std::string & text)
{
    std::string rs = text.substr(3);
    trim(rs);
    return new NInstructionsCLR(parseReg(rs));
}

// NOP: nop
NInstructionsNOP::NInstructionsNOP() { m_opcode = NOpcode::NOP; }
std::vector<uint8_t>
NInstructionsNOP::generateInstructionCode()
{
    return { static_cast<uint8_t>(m_opcode) };
}
std::string
NInstructionsNOP::generateInstructionName()
{
    return "nop";
}
NInstructionsInterface *
NInstructionsNOP::parserInstructionText(std::string &)
{
    return new NInstructionsNOP();
}

NInstructionsInterface *
NInstructionsInterface::parserInstructionText(std::string & text)
{
    trim(text);
    if (text.empty())
        return nullptr;

    std::string opcode = text;
    size_t end = opcode.find_first_of(" \t,");
    if (end != std::string::npos)
        opcode = opcode.substr(0, end);
    std::transform(opcode.begin(), opcode.end(), opcode.begin(), ::tolower);

    auto it = g_opcodeToGeneratorMap.find(opcode);
    return (it != g_opcodeToGeneratorMap.end()) ? it->second(text) : nullptr;
}
