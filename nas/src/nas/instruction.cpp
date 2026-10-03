#include "nas/instruction.hpp"
#include <algorithm>
#include <cctype>
#include <map>

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

// 追加 opcode 字节（各指令类 emit 的公共前缀）
void
Instruction::emitOpcode()
{
    bytes.push_back(static_cast<uint8_t>(opcode));
}

// 追加 4 字节立即数；操作数为标号（pendingLabel 非空）时写 0 占位并记录回填偏移
void
Instruction::emitImm32(int32_t v)
{
    if (!pendingLabel.empty()) {
        patchOffset = static_cast<int32_t>(bytes.size());
        write32(bytes, 0);
    }
    else {
        write32(bytes, v);
    }
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
        emitOpcode();
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
        emitOpcode();
        bytes.push_back(reg);
        emitImm32(addr);
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
        emitOpcode();
        bytes.push_back(reg);
        emitImm32(addr);
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
        bytes.push_back(reg);
        emitImm32(addr);
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
        emitOpcode();
        bytes.push_back(reg);
        emitImm32(addr);
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

// ============================================================================
// Compare Instructions
// ============================================================================

class CMP : public Instruction
{
    uint8_t dest, src;

public:
    CMP(uint8_t d, uint8_t s)
      : Instruction(NOpcode::CMP)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        emitOpcode();
        bytes.push_back(dest);
        bytes.push_back(src);
    }
};

class CMPI : public Instruction
{
    uint8_t reg;
    int32_t val;

public:
    CMPI(uint8_t r, int32_t v)
      : Instruction(NOpcode::CMPI)
      , reg(r)
      , val(v)
    {
    }
    void
    emit() override
    {
        emitOpcode();
        bytes.push_back(reg);
        write32(bytes, val);
    }
};

class TEST : public Instruction
{
    uint8_t dest, src;

public:
    TEST(uint8_t d, uint8_t s)
      : Instruction(NOpcode::TEST)
      , dest(d)
      , src(s)
    {
    }
    void
    emit() override
    {
        emitOpcode();
        bytes.push_back(dest);
        bytes.push_back(src);
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
        emitImm32(addr);
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
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
        emitOpcode();
    }
};

// ============================================================================
// Source-Line Helpers (v2.1 装配驱动使用)
// ============================================================================

// 严格整数判定：全串必须是合法十进制/十六进制（区别于 parseInt 的宽容回 0）
static bool
isStrictInt(const std::string & s)
{
    std::string t = s;
    trim(t);
    if (t.empty())
        return false;
    size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (t.size() > i + 2 && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) {
        for (size_t j = i + 2; j < t.size(); ++j)
            if (!std::isxdigit(static_cast<unsigned char>(t[j])))
                return false;
        return true;
    }
    if (i >= t.size())
        return false;
    for (size_t j = i; j < t.size(); ++j)
        if (!std::isdigit(static_cast<unsigned char>(t[j])))
            return false;
    return true;
}

// 标识符：[A-Za-z_.][A-Za-z0-9_.]*（允许点前缀的数据标号）
static bool
isIdent(const std::string & s)
{
    if (s.empty())
        return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_' && s[0] != '.')
        return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.')
            return false;
    return true;
}

// 地址类操作数：非纯数字则记为待回填标号（callx 符号从导入表解析）
static void
markLabel(Instruction * ins, const std::string & arg, bool fromImports = false)
{
    if (!isStrictInt(arg)) {
        ins->pendingLabel = arg;
        ins->patchFromImports = fromImports;
    }
}

// 剥离注释（; 或 # 起始），字符串字面量内部不视为注释
static std::string
stripComment(const std::string & line)
{
    bool inStr = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inStr) {
            if (c == '\\') {
                ++i;
                continue;
            }
            if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"')
            inStr = true;
        else if (c == ';' || c == '#')
            return line.substr(0, i);
    }
    return line;
}

// db 字符串字面量 → 字节序列（支持 \n \t \r \0 \\ \" 转义）
static bool
parseStringBytes(const std::string & s, std::vector<uint8_t> & out, std::string & err)
{
    for (size_t i = 1; i < s.size(); ++i) { // s[0] 为起始引号
        char c = s[i];
        if (c == '\\') {
            if (i + 1 >= s.size()) {
                err = "字符串转义符后缺少字符";
                return false;
            }
            char e = s[++i];
            switch (e) {
            case 'n':
                out.push_back(0x0A);
                break;
            case 't':
                out.push_back(0x09);
                break;
            case 'r':
                out.push_back(0x0D);
                break;
            case '0':
                out.push_back(0x00);
                break;
            case '\\':
                out.push_back(0x5C);
                break;
            case '"':
                out.push_back(0x22);
                break;
            default:
                err = std::string("未知转义序列 '\\") + e + "'";
                return false;
            }
            continue;
        }
        if (c == '"')
            return true;
        out.push_back(static_cast<uint8_t>(c));
    }
    err = "字符串缺少闭合引号";
    return false;
}

// 按逗号切分数据项（字符串字面量内的逗号与引号不参与切分）
static std::vector<std::string>
splitDataItems(const std::string & args, std::string & err)
{
    std::vector<std::string> items;
    std::string cur;
    bool inStr = false;
    for (size_t i = 0; i < args.size(); ++i) {
        char c = args[i];
        if (inStr) {
            cur.push_back(c);
            if (c == '\\' && i + 1 < args.size()) {
                cur.push_back(args[++i]);
                continue;
            }
            if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"') {
            inStr = true;
            cur.push_back(c);
            continue;
        }
        if (c == ',') {
            items.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (inStr) {
        err = "字符串缺少闭合引号";
        return items;
    }
    items.push_back(cur);
    for (auto & it : items)
        trim(it);
    return items;
}

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
    if (op == "st") {
        auto ins = std::make_unique<ST>(parseRegister(arg1), parseInt(arg2));
        markLabel(ins.get(), arg2);
        return ins;
    }
    if (op == "lea") {
        auto ins = std::make_unique<LEA>(parseRegister(arg1), parseInt(arg2));
        markLabel(ins.get(), arg2);
        return ins;
    }
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
    if (op == "loada") {
        auto ins = std::make_unique<LOADA>(parseRegister(arg1), parseInt(arg2));
        markLabel(ins.get(), arg2);
        return ins;
    }
    if (op == "storea") {
        auto ins = std::make_unique<STOREA>(parseRegister(arg1), parseInt(arg2));
        markLabel(ins.get(), arg2);
        return ins;
    }
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
    if (op == "cmp")
        return std::make_unique<CMP>(parseRegister(arg1), parseRegister(arg2));
    if (op == "cmpi")
        return std::make_unique<CMPI>(parseRegister(arg1), parseInt(arg2));
    if (op == "test")
        return std::make_unique<TEST>(parseRegister(arg1), parseRegister(arg2));
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
    if (op == "jmp") {
        auto ins = std::make_unique<JMP>(parseInt(arg1));
        markLabel(ins.get(), arg1);
        return ins;
    }
    if (op == "jz") {
        auto ins = std::make_unique<JZ>(parseInt(arg1));
        markLabel(ins.get(), arg1);
        return ins;
    }
    if (op == "jnz") {
        auto ins = std::make_unique<JNZ>(parseInt(arg1));
        markLabel(ins.get(), arg1);
        return ins;
    }
    if (op == "jn") {
        auto ins = std::make_unique<JN>(parseInt(arg1));
        markLabel(ins.get(), arg1);
        return ins;
    }
    if (op == "jp") {
        auto ins = std::make_unique<JP>(parseInt(arg1));
        markLabel(ins.get(), arg1);
        return ins;
    }
    if (op == "call") {
        auto ins = std::make_unique<CALL>(parseInt(arg1));
        markLabel(ins.get(), arg1);
        return ins;
    }
    if (op == "callx") {
        auto ins = std::make_unique<CALLX>(parseInt(arg1));
        markLabel(ins.get(), arg1, true);
        return ins;
    }
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

// ============================================================================
// Whole-Source Assembly：两遍扫描生成 NCI v2.1 完整目标文件
//
// 文件布局（统一编址：代码地址 [0, codeSize)，数据标号地址 = codeSize + 段内偏移）：
//   header(32B) | code(codeSize) | data(dataSize) | import table | export table
//
// 导入表 entry（exportCount 同构，flags 恒 0）：
//   int32 nameLen | uint8 name[nameLen] | uint8 0(NUL)
//   | pad 至 4 字节对齐（以 entry 起始为基准）
//   | int32 addr（宿主地址：静态绑定 = 显式地址；动态导入 = 伪宿主地址）
//   | int32 flags（bit0-1 = convention，bit2 = 动态导入，规范 §2.1）
// ============================================================================

namespace
{

    struct LabelDef {
        bool isData;
        int32_t offset; // 段内偏移
    };

    struct LabelBind {
        std::string name;
        int line;
    };

    struct ImportRec {
        std::string name;
        int32_t address;
        uint8_t convention;
        bool dynamic; // 无地址 extern：伪宿主地址 + flags bit2（加载期按名解析）
        int line;
    };

    struct ExportRec {
        std::string name;
        int line;
    };

    struct DataRef {
        int32_t offset; // 数据段内偏移（dd 的 4 字节地址常量位置）
        std::string label;
        int line;
    };

    // 追加 n 个 0 填充字节
    void
    appendPad(std::vector<uint8_t> & c, size_t n)
    {
        c.insert(c.end(), n, 0);
    }

    // 写入 nameLen + name + NUL + pad 的符号名部分（4 字节对齐，以 entry 起始为基准）
    void
    appendNameField(std::vector<uint8_t> & c, const std::string & name)
    {
        int32_t nameLen = static_cast<int32_t>(name.size());
        write32(c, nameLen);
        c.insert(c.end(), name.begin(), name.end());
        c.push_back(0);
        // 名字部分结束于 4+nameLen+1，pad 至 4 的倍数
        size_t pad = (4 - ((4 + nameLen + 1) % 4)) % 4;
        appendPad(c, pad);
    }

    // 按名查找导入记录；未命中返回 nullptr
    const ImportRec *
    findImport(const std::vector<ImportRec> & imports, const std::string & name)
    {
        for (const auto & im : imports)
            if (im.name == name)
                return &im;
        return nullptr;
    }

} // namespace

// 回填 4 字节小端立即数到 bytes[offset..offset+4)（调用方须保证偏移合法）
static void
patchI32(std::vector<uint8_t> & bytes, int32_t offset, int32_t value)
{
    for (int k = 0; k < 4; ++k)
        bytes[offset + k] = static_cast<uint8_t>((value >> (8 * k)) & 0xFF);
}

AssemblyResult
Assembler::assemble(const std::string & source)
{
    AssemblyResult result;
    result.ok = false;

    // ---- 按行拆分（容忍 CRLF）----
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : source) {
            if (c == '\n') {
                lines.push_back(cur);
                cur.clear();
            }
            else if (c != '\r') {
                cur.push_back(c);
            }
        }
        lines.push_back(cur);
    }

    // ---- 第一遍：收集标号 / 符号表 / 线性编码 ----
    std::map<std::string, LabelDef> labels;
    std::vector<LabelBind> pendingBinds; // 等待绑定到下一段内容的标号
    std::vector<ImportRec> imports;
    std::vector<ExportRec> exports;
    std::vector<DataRef> dataRefs;
    std::vector<std::unique_ptr<Instruction>> instructions;
    std::vector<uint8_t> dataBytes;
    int32_t codePc = 0;
    uint8_t curConvention = static_cast<uint8_t>(NCallingConvention::FASTCALL);
    int32_t nextDynAddr = DYNAMIC_HOST_BASE; // 下一个无地址 extern 的伪宿主地址

    auto fail = [&result](int line, const std::string & msg) {
        result.errorLine = line;
        result.errorMessage = msg;
    };

    // 悬空标号绑定到下一段内容的起始地址（指令 → 代码段；db/dw/dd → 数据段）
    auto bindPending = [&](bool isData, int32_t offset) -> bool {
        for (const auto & b : pendingBinds) {
            if (labels.count(b.name)) {
                fail(b.line, "重复标号 '" + b.name + "'");
                return false;
            }
            labels[b.name] = LabelDef{ isData, offset };
        }
        pendingBinds.clear();
        return true;
    };

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        int lineNo = static_cast<int>(idx) + 1;
        std::string l = stripComment(lines[idx]);
        trim(l);
        if (l.empty())
            continue;

        // 标号行：不占地址空间，绑定延迟到下一段内容出现时
        if (l.back() == ':') {
            std::string name = l.substr(0, l.size() - 1);
            trim(name);
            if (name.empty()) {
                fail(lineNo, "空标号名");
                return result;
            }
            if (!isIdent(name)) {
                fail(lineNo, "非法标号名 '" + name + "'");
                return result;
            }
            pendingBinds.push_back(LabelBind{ name, lineNo });
            continue;
        }

        size_t sp = l.find_first_of(" \t");
        std::string op = (sp == std::string::npos) ? l : l.substr(0, sp);
        std::string rest = (sp == std::string::npos) ? "" : l.substr(sp + 1);
        trim(rest);
        std::string opLow = op;
        std::transform(opLow.begin(), opLow.end(), opLow.begin(), ::tolower);

        // extern 名 [地址]：不占代码地址空间。缺省地址 = 动态导入：分配确定性伪宿主
        // 地址（DYNAMIC_HOST_BASE 起按声明序 +4）写导入表 addr 与 callx 站点 imm
        // （同值，站点↔符号一对一），flags 置 bit2，加载期经宿主库按符号名解析；
        // 显式地址 = 静态宿主绑定（无动态位，加载期不解析）
        if (opLow == "extern") {
            size_t sp2 = rest.find_first_of(" \t");
            std::string name = (sp2 == std::string::npos) ? rest : rest.substr(0, sp2);
            std::string addrStr = (sp2 == std::string::npos) ? "" : rest.substr(sp2 + 1);
            trim(addrStr);
            if (!isIdent(name)) {
                fail(lineNo, "extern 缺少合法符号名: '" + name + "'");
                return result;
            }
            int32_t addr = 0;
            if (!addrStr.empty()) {
                if (!isStrictInt(addrStr)) {
                    fail(lineNo, "extern 地址非法: '" + addrStr + "'");
                    return result;
                }
                addr = parseInt(addrStr);
            }
            // 同名重复声明：地址/约定以后者为准，仅保留一条导入记录
            ImportRec * existing = nullptr;
            for (auto & im : imports)
                if (im.name == name) {
                    existing = &im;
                    break;
                }
            if (existing) {
                existing->convention = curConvention;
                if (addrStr.empty()) {
                    if (!existing->dynamic) {
                        existing->address = nextDynAddr;
                        existing->dynamic = true;
                        nextDynAddr += DYNAMIC_HOST_STEP;
                    }
                }
                else {
                    existing->address = addr;
                    existing->dynamic = false;
                }
            }
            else if (addrStr.empty()) {
                imports.push_back(
                  ImportRec{ name, nextDynAddr, curConvention, true, lineNo });
                nextDynAddr += DYNAMIC_HOST_STEP;
            }
            else {
                imports.push_back(ImportRec{ name, addr, curConvention, false, lineNo });
            }
            continue;
        }

        // export 名：地址在第二遍解析为对应标号的地址
        if (opLow == "export") {
            if (rest.find_first_of(" \t") != std::string::npos) {
                fail(lineNo, "export 只接受一个符号名");
                return result;
            }
            if (!isIdent(rest)) {
                fail(lineNo, "export 缺少合法符号名: '" + rest + "'");
                return result;
            }
            for (const auto & ex : exports)
                if (ex.name == rest) {
                    fail(lineNo, "重复导出 '" + rest + "'");
                    return result;
                }
            exports.push_back(ExportRec{ rest, lineNo });
            continue;
        }

        // .calling_convention fastcall|cdecl：作用于其后声明的 extern（顺序生效）
        if (opLow == ".calling_convention") {
            if (rest == "fastcall")
                curConvention = static_cast<uint8_t>(NCallingConvention::FASTCALL);
            else if (rest == "cdecl")
                curConvention = static_cast<uint8_t>(NCallingConvention::CDECL);
            else {
                fail(lineNo, "未知调用约定 '" + rest + "'（支持 fastcall|cdecl）");
                return result;
            }
            continue;
        }

        // db/dw/dd 数据定义：切换到数据段
        if (opLow == "db" || opLow == "dw" || opLow == "dd") {
            if (!bindPending(true, static_cast<int32_t>(dataBytes.size())))
                return result;
            bool isDb = (opLow == "db");
            bool isDw = (opLow == "dw");
            std::string err;
            std::vector<std::string> items = splitDataItems(rest, err);
            if (!err.empty()) {
                fail(lineNo, err);
                return result;
            }
            if (items.size() == 1 && items[0].empty()) {
                fail(lineNo, opLow + " 缺少数据项");
                return result;
            }
            for (const auto & item : items) {
                if (item.empty()) {
                    fail(lineNo, "空的数据项");
                    return result;
                }
                if (item[0] == '"') {
                    if (!isDb) {
                        fail(lineNo, "dw/dd 不支持字符串字面量");
                        return result;
                    }
                    if (!parseStringBytes(item, dataBytes, err)) {
                        fail(lineNo, err);
                        return result;
                    }
                    continue;
                }
                if (isStrictInt(item)) {
                    int32_t v = parseInt(item);
                    if (isDb)
                        dataBytes.push_back(static_cast<uint8_t>(v));
                    else if (isDw) {
                        dataBytes.push_back(static_cast<uint8_t>(v & 0xFF));
                        dataBytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                    }
                    else
                        write32(dataBytes, v);
                    continue;
                }
                if (isIdent(item)) {
                    if (isDb || isDw) {
                        fail(lineNo, "db/dw 不支持标号引用（请改用 dd）");
                        return result;
                    }
                    dataRefs.push_back(
                      DataRef{ static_cast<int32_t>(dataBytes.size()), item, lineNo });
                    write32(dataBytes, 0);
                    continue;
                }
                fail(lineNo, "非法数据项 '" + item + "'");
                return result;
            }
            continue;
        }

        // 指令：切回代码段
        {
            auto instr = Assembler::parseLine(l);
            if (!instr) {
                fail(lineNo, "未知指令或语法错误: '" + l + "'");
                return result;
            }
            instr->sourceLine = lineNo;
            if (!bindPending(false, codePc))
                return result;
            instr->emit();
            codePc += static_cast<int32_t>(instr->bytes.size());
            instructions.push_back(std::move(instr));
        }
    }

    // 文件末尾仍悬空的标号 → 无内容可指向
    if (!pendingBinds.empty()) {
        fail(pendingBinds.front().line,
             "标号 '" + pendingBinds.front().name + "' 之后没有内容");
        return result;
    }

    int32_t codeSize = codePc;
    int32_t dataSize = static_cast<int32_t>(dataBytes.size());

    // 标号地址解析：代码标号 = 段内偏移；数据标号 = codeSize + 段内偏移（统一编址）
    auto labelAddress = [&](const std::string & name, bool & found) -> int32_t {
        auto it = labels.find(name);
        if (it == labels.end()) {
            found = false;
            return 0;
        }
        found = true;
        return it->second.isData ? codeSize + it->second.offset : it->second.offset;
    };

    // 导出符号解析
    std::vector<int32_t> exportAddrs;
    exportAddrs.reserve(exports.size());
    for (const auto & ex : exports) {
        bool found = false;
        int32_t a = labelAddress(ex.name, found);
        if (!found) {
            fail(ex.line, "export 符号 '" + ex.name + "' 未定义为标号");
            return result;
        }
        exportAddrs.push_back(a);
    }

    // 入口点：main 标号优先，其次第一个导出符号，否则 0
    int32_t entryPoint = 0;
    {
        bool found = false;
        int32_t a = labelAddress("main", found);
        if (found)
            entryPoint = a;
        else if (!exportAddrs.empty())
            entryPoint = exportAddrs.front();
    }

    // ---- 第二遍：标号 / 导入地址回填 ----
    for (auto & ins : instructions) {
        if (ins->pendingLabel.empty())
            continue;
        int32_t value = 0;
        if (ins->patchFromImports) {
            const ImportRec * im = findImport(imports, ins->pendingLabel);
            if (!im) {
                fail(ins->sourceLine,
                     "callx 引用了未声明的外部符号 '" + ins->pendingLabel
                       + "'（需先 extern）");
                return result;
            }
            value = im->address;
        }
        else {
            bool found = false;
            value = labelAddress(ins->pendingLabel, found);
            if (!found) {
                // 数据地址类指令回落导入表：允许 lea/loada/storea/st 引用 extern
                // 符号（跨模块数据引用，imm = 导入地址，由链接器改写）。
                // callx 仍仅走导入表，jmp/call 仍仅走本模块标号
                const ImportRec * ext = nullptr;
                if (ins->opcode == NOpcode::ST || ins->opcode == NOpcode::LEA
                    || ins->opcode == NOpcode::LOADA || ins->opcode == NOpcode::STOREA) {
                    ext = findImport(imports, ins->pendingLabel);
                }
                if (!ext) {
                    fail(ins->sourceLine, "未定义的标号 '" + ins->pendingLabel + "'");
                    return result;
                }
                value = ext->address;
            }
        }
        if (ins->patchOffset < 0
            || ins->patchOffset + 4 > static_cast<int32_t>(ins->bytes.size())) {
            fail(ins->sourceLine, "内部错误：回填偏移越界");
            return result;
        }
        patchI32(ins->bytes, ins->patchOffset, value);
    }

    // dd 标号引用回填（可引用代码/数据标号做地址常量）
    for (const auto & ref : dataRefs) {
        bool found = false;
        int32_t value = labelAddress(ref.label, found);
        if (!found) {
            fail(ref.line, "dd 引用了未定义的标号 '" + ref.label + "'");
            return result;
        }
        patchI32(dataBytes, ref.offset, value);
    }

    // ---- 生成文件镜像 ----
    std::vector<uint8_t> & img = result.image;
    img.insert(img.end(), NCI_MAGIC, NCI_MAGIC + sizeof(NCI_MAGIC));
    write32(img, NCI_HEADER_SIZE); // headerSize
    write32(img, codeSize);
    write32(img, dataSize);
    write32(img, static_cast<int32_t>(imports.size()));
    write32(img, static_cast<int32_t>(exports.size()));
    write32(img, entryPoint);

    for (const auto & ins : instructions)
        img.insert(img.end(), ins->bytes.begin(), ins->bytes.end());
    img.insert(img.end(), dataBytes.begin(), dataBytes.end());

    // 导入表：nameLen|name|NUL|pad|addr|flags（bit0-1 约定，bit2 动态导入）
    for (const auto & im : imports) {
        appendNameField(img, im.name);
        write32(img, im.address);
        write32(img,
                static_cast<int32_t>(im.convention & 0x03)
                  | (im.dynamic ? IMPORT_FLAG_DYNAMIC : 0));
    }
    // 导出表：与导入表同构，addr = 标号统一编址地址，flags 恒 0
    for (size_t i = 0; i < exports.size(); ++i) {
        appendNameField(img, exports[i].name);
        write32(img, exportAddrs[i]);
        write32(img, 0);
    }

    result.ok = true;
    return result;
}
