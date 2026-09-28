#include "nas/instruction.hpp"
#include <algorithm>
#include <cctype>

// Trim whitespace
static void
trim(std::string & s)
{
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
}

uint8_t
Assembler::parseRegister(const std::string & s)
{
    std::string r = s;
    trim(r);
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    if (r.size() < 2 || r[0] != 'R')
        return 0;
    return r[1] - '0';
}

int32_t
Assembler::parseInt(const std::string & s)
{
    std::string str = s;
    trim(str);
    if (str.empty())
        return 0;
    try {
        return (str.size() > 2 && str.substr(0, 2) == "0x") ? std::stoi(str, nullptr, 16)
                                                            : std::stoi(str, nullptr, 10);
    }
    catch (...) {
        return 0;
    }
}

// Emit helpers
static void
write32(std::vector<uint8_t> & c, int32_t v)
{
    c.push_back(v & 0xFF);
    c.push_back((v >> 8) & 0xFF);
    c.push_back((v >> 16) & 0xFF);
    c.push_back((v >> 24) & 0xFF);
}

static void
write16(std::vector<uint8_t> & c, int16_t v)
{
    c.push_back(v & 0xFF);
    c.push_back((v >> 8) & 0xFF);
}

// ============================================================================
// Memory Instructions
// ============================================================================

class LMM : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    LMM(uint8_t r, int32_t v)
      : Instruction(NOpcode::LMM)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class ST : public Instruction
{
    uint8_t reg;
    int32_t addr;

public:
    ST(uint8_t r, int32_t a)
      : Instruction(NOpcode::ST)
      , reg(r)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, addr);
    }
};

class LEA : public Instruction
{
    uint8_t reg;
    int32_t addr;

public:
    LEA(uint8_t r, int32_t a)
      : Instruction(NOpcode::LEA)
      , reg(r)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, addr);
    }
};

class LOAD : public Instruction
{
    uint8_t dest, src;

public:
    LOAD(uint8_t d, uint8_t s)
      : Instruction(NOpcode::LOAD)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class STORE : public Instruction
{
    uint8_t dest, src;

public:
    STORE(uint8_t d, uint8_t s)
      : Instruction(NOpcode::STORE)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class LOADA : public Instruction
{
    uint8_t reg;
    int32_t addr;

public:
    LOADA(uint8_t r, int32_t a)
      : Instruction(NOpcode::LOADA)
      , reg(r)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, addr);
    }
};

class STOREA : public Instruction
{
    uint8_t reg;
    int32_t addr;

public:
    STOREA(uint8_t r, int32_t a)
      : Instruction(NOpcode::STOREA)
      , reg(r)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, addr);
    }
};

// ============================================================================
// Arithmetic Instructions
// ============================================================================

class ADD : public Instruction
{
    uint8_t dest, src;

public:
    ADD(uint8_t d, uint8_t s)
      : Instruction(NOpcode::ADD)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class ADDI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    ADDI(uint8_t r, int32_t v)
      : Instruction(NOpcode::ADDI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class SUB : public Instruction
{
    uint8_t dest, src;

public:
    SUB(uint8_t d, uint8_t s)
      : Instruction(NOpcode::SUB)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class SUBI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    SUBI(uint8_t r, int32_t v)
      : Instruction(NOpcode::SUBI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class MUL : public Instruction
{
    uint8_t dest, src;

public:
    MUL(uint8_t d, uint8_t s)
      : Instruction(NOpcode::MUL)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class MULI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    MULI(uint8_t r, int32_t v)
      : Instruction(NOpcode::MULI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class DIV : public Instruction
{
    uint8_t dest, src;

public:
    DIV(uint8_t d, uint8_t s)
      : Instruction(NOpcode::DIV)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class DIVI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    DIVI(uint8_t r, int32_t v)
      : Instruction(NOpcode::DIVI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class MOD : public Instruction
{
    uint8_t dest, src;

public:
    MOD(uint8_t d, uint8_t s)
      : Instruction(NOpcode::MOD)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class MODI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    MODI(uint8_t r, int32_t v)
      : Instruction(NOpcode::MODI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class NOT : public Instruction
{
    uint8_t reg;

public:
    NOT(uint8_t r)
      : Instruction(NOpcode::NOT)
      , reg(r)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
    }
};

class NEG : public Instruction
{
    uint8_t reg;

public:
    NEG(uint8_t r)
      : Instruction(NOpcode::NEG)
      , reg(r)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
    }
};

// ============================================================================
// Logic Instructions (immediate forms)
// ============================================================================

class ANDI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    ANDI(uint8_t r, int32_t v)
      : Instruction(NOpcode::ANDI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class ORI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    ORI(uint8_t r, int32_t v)
      : Instruction(NOpcode::ORI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class XORI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    XORI(uint8_t r, int32_t v)
      : Instruction(NOpcode::XORI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class SHLI : public Instruction
{
    uint8_t reg;
    uint8_t shamt;

public:
    SHLI(uint8_t r, uint8_t s)
      : Instruction(NOpcode::SHLI)
      , reg(r)
      , shamt(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        bytes.push_back(shamt);
    }
};

class SHRI : public Instruction
{
    uint8_t reg;
    uint8_t shamt;

public:
    SHRI(uint8_t r, uint8_t s)
      : Instruction(NOpcode::SHRI)
      , reg(r)
      , shamt(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
        bytes.push_back(shamt);
    }
};

// ============================================================================
// Stack Instructions
// ============================================================================

class PUSH : public Instruction
{
    uint8_t reg;

public:
    PUSH(uint8_t r)
      : Instruction(NOpcode::PUSH)
      , reg(r)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
    }
};

class PUSHI : public Instruction
{
    int32_t val;

public:
    PUSHI(int32_t v)
      : Instruction(NOpcode::PUSHI)
      , val(v)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, val);
    }
};

class POP : public Instruction
{
    uint8_t reg;

public:
    POP(uint8_t r)
      : Instruction(NOpcode::POP)
      , reg(r)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
    }
};

class ENTER : public Instruction
{
    int16_t size;

public:
    ENTER(int16_t s)
      : Instruction(NOpcode::ENTER)
      , size(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write16(bytes, size);
    }
};

class LEAVE : public Instruction
{
public:
    LEAVE()
      : Instruction(NOpcode::LEAVE)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
    }
};

// ============================================================================
// Control Flow Instructions
// ============================================================================

class JMP : public Instruction
{
    int32_t addr;

public:
    JMP(int32_t a)
      : Instruction(NOpcode::JMP)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

class JZ : public Instruction
{
    int32_t addr;

public:
    JZ(int32_t a)
      : Instruction(NOpcode::JZ)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

class JNZ : public Instruction
{
    int32_t addr;

public:
    JNZ(int32_t a)
      : Instruction(NOpcode::JNZ)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

class JN : public Instruction
{
    int32_t addr;

public:
    JN(int32_t a)
      : Instruction(NOpcode::JN)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

class JP : public Instruction
{
    int32_t addr;

public:
    JP(int32_t a)
      : Instruction(NOpcode::JP)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

// ============================================================================
// Call Instructions
// ============================================================================

class CALL : public Instruction
{
    int32_t addr;

public:
    CALL(int32_t a)
      : Instruction(NOpcode::CALL)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

class CALLX : public Instruction
{
    int32_t addr;

public:
    CALLX(int32_t a)
      : Instruction(NOpcode::CALLX)
      , addr(a)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        write32(bytes, addr);
    }
};

class RET : public Instruction
{
public:
    RET()
      : Instruction(NOpcode::RET)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
    }
};

// ============================================================================
// Misc Instructions
// ============================================================================

class MOV : public Instruction
{
    uint8_t dest, src;

public:
    MOV(uint8_t d, uint8_t s)
      : Instruction(NOpcode::MOV)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class CLR : public Instruction
{
    uint8_t reg;

public:
    CLR(uint8_t r)
      : Instruction(NOpcode::CLR)
      , reg(r)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
        bytes.push_back(reg);
    }
};

class NOP : public Instruction
{
public:
    NOP()
      : Instruction(NOpcode::NOP)
    {
    }
    void
    emit() override
    {
        bytes.push_back(static_cast<uint8_t>(opcode));
    }
};

// ============================================================================
// Parser
// ============================================================================

std::unique_ptr<Instruction>
Assembler::parseLine(const std::string & line)
{
    std::string l = line;
    trim(l);

    // Skip empty lines and comments
    if (l.empty() || l[0] == ';' || l[0] == '#')
        return nullptr;

    // Find opcode
    size_t space = l.find_first_of(" \t");
    std::string op = (space != std::string::npos) ? l.substr(0, space) : l;
    std::string args = (space != std::string::npos) ? l.substr(space + 1) : "";
    trim(args);

    // Parse operands
    std::transform(op.begin(), op.end(), op.begin(), ::tolower);

    // Split args by comma
    std::string arg1, arg2;
    size_t comma = args.find(',');
    if (comma != std::string::npos) {
        arg1 = args.substr(0, comma);
        arg2 = args.substr(comma + 1);
        trim(arg1);
        trim(arg2);
    }
    else {
        arg1 = args;
    }

    // Match instructions
    if (op == "lmm")
        return std::make_unique<LMM>(parseRegister(arg1), parseInt(arg2));
    if (op == "st")
        return std::make_unique<ST>(parseRegister(arg1), parseInt(arg2));
    if (op == "lea")
        return std::make_unique<LEA>(parseRegister(arg1), parseInt(arg2));
    if (op == "load") {
        // Handle [Rx] syntax
        size_t lb = arg2.find('['), rb = arg2.find(']');
        if (lb != std::string::npos && rb != std::string::npos) {
            std::string reg = arg2.substr(lb + 1, rb - lb - 1);
            return std::make_unique<LOAD>(parseRegister(arg1), parseRegister(reg));
        }
    }
    if (op == "store") {
        size_t lb = arg1.find('['), rb = arg1.find(']');
        if (lb != std::string::npos && rb != std::string::npos) {
            std::string reg = arg1.substr(lb + 1, rb - lb - 1);
            return std::make_unique<STORE>(parseRegister(reg), parseRegister(arg2));
        }
    }
    if (op == "loada")
        return std::make_unique<LOADA>(parseRegister(arg1), parseInt(arg2));
    if (op == "storea")
        return std::make_unique<STOREA>(parseRegister(arg1), parseInt(arg2));
    if (op == "add")
        return std::make_unique<ADD>(parseRegister(arg1), parseRegister(arg2));
    if (op == "addi")
        return std::make_unique<ADDI>(parseRegister(arg1), parseInt(arg2));
    if (op == "sub")
        return std::make_unique<SUB>(parseRegister(arg1), parseRegister(arg2));
    if (op == "subi")
        return std::make_unique<SUBI>(parseRegister(arg1), parseInt(arg2));
    if (op == "mul")
        return std::make_unique<MUL>(parseRegister(arg1), parseRegister(arg2));
    if (op == "muli")
        return std::make_unique<MULI>(parseRegister(arg1), parseInt(arg2));
    if (op == "div")
        return std::make_unique<DIV>(parseRegister(arg1), parseRegister(arg2));
    if (op == "divi")
        return std::make_unique<DIVI>(parseRegister(arg1), parseInt(arg2));
    if (op == "mod")
        return std::make_unique<MOD>(parseRegister(arg1), parseRegister(arg2));
    if (op == "modi")
        return std::make_unique<MODI>(parseRegister(arg1), parseInt(arg2));
    if (op == "not")
        return std::make_unique<NOT>(parseRegister(arg1));
    if (op == "neg")
        return std::make_unique<NEG>(parseRegister(arg1));
    if (op == "andi")
        return std::make_unique<ANDI>(parseRegister(arg1), parseInt(arg2));
    if (op == "ori")
        return std::make_unique<ORI>(parseRegister(arg1), parseInt(arg2));
    if (op == "xori")
        return std::make_unique<XORI>(parseRegister(arg1), parseInt(arg2));
    if (op == "shli")
        return std::make_unique<SHLI>(parseRegister(arg1), parseInt(arg2));
    if (op == "shri")
        return std::make_unique<SHRI>(parseRegister(arg1), parseInt(arg2));
    if (op == "push")
        return std::make_unique<PUSH>(parseRegister(arg1));
    if (op == "pushi")
        return std::make_unique<PUSHI>(parseInt(arg1));
    if (op == "pop")
        return std::make_unique<POP>(parseRegister(arg1));
    if (op == "enter")
        return std::make_unique<ENTER>(parseInt(arg1));
    if (op == "leave")
        return std::make_unique<LEAVE>();
    if (op == "jmp")
        return std::make_unique<JMP>(parseInt(arg1));
    if (op == "jz")
        return std::make_unique<JZ>(parseInt(arg1));
    if (op == "jnz")
        return std::make_unique<JNZ>(parseInt(arg1));
    if (op == "jn")
        return std::make_unique<JN>(parseInt(arg1));
    if (op == "jp")
        return std::make_unique<JP>(parseInt(arg1));
    if (op == "call")
        return std::make_unique<CALL>(parseInt(arg1));
    if (op == "callx")
        return std::make_unique<CALLX>(parseInt(arg1));
    if (op == "ret")
        return std::make_unique<RET>();
    if (op == "mov")
        return std::make_unique<MOV>(parseRegister(arg1), parseRegister(arg2));
    if (op == "clr")
        return std::make_unique<CLR>(parseRegister(arg1));
    if (op == "nop")
        return std::make_unique<NOP>();

    return nullptr;
}
