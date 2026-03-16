#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

#include <cstdint>
#include <map>
#include <string>
#include <string_helper.hpp>
#include <vector>

// ============================================================================
// NanoC NCI v2.1 指令集定义
// 设计原则：极简主义，无系统调用，所有功能通过 C 函数实现
// ============================================================================

// ----------------------------------------------------------------------------
// 寄存器定义（8 个通用寄存器）
// ----------------------------------------------------------------------------
enum class NRegister {
    R0 = 0, // AX - 返回值、参数 1
    R1,     // BX - 参数 2
    R2,     // CX - 参数 3
    R3,     // DX - 参数 4
    R4,     // SP - 栈指针（专用）
    R5,     // BP - 基址指针（专用）
    R6,     // 通用
    R7,     // 通用
};

// ----------------------------------------------------------------------------
// 操作码定义（NCI v2.1）
// 编码规则：按功能分组，每组 16 个操作码空间
// ----------------------------------------------------------------------------
enum class NOpcode : uint8_t {
    // 内存访问指令 (0x00-0x0F)
    LMM = 0x00,    // Load Memory to Register (立即数→寄存器)
    ST = 0x01,     // Store (寄存器→直接地址)
    LEA = 0x02,    // Load Effective Address
    LOAD = 0x03,   // Load (间接寻址：R1=[R2])
    STORE = 0x04,  // Store (间接寻址：[R1]=R2)
    LOADA = 0x05,  // Load Absolute (绝对地址→寄存器)
    STOREA = 0x06, // Store Absolute (寄存器→绝对地址)

    // 算术运算指令 (0x10-0x1F)
    ADD = 0x10,  // 加法（寄存器）
    ADDI = 0x11, // 加法（立即数）
    SUB = 0x12,  // 减法（寄存器）
    SUBI = 0x13, // 减法（立即数）
    MUL = 0x14,  // 乘法（寄存器）
    MULI = 0x15, // 乘法（立即数）
    DIV = 0x16,  // 除法（寄存器）
    DIVI = 0x17, // 除法（立即数）
    MOD = 0x18,  // 取模（寄存器）
    MODI = 0x19, // 取模（立即数）
    NOT = 0x1A,  // 按位取反
    NEG = 0x1B,  // 算术取负

    // 逻辑运算指令 (0x20-0x2F)
    AND = 0x20,  // 按位与（寄存器）
    ANDI = 0x21, // 按位与（立即数）
    OR = 0x22,   // 按位或（寄存器）
    ORI = 0x23,  // 按位或（立即数）
    XOR = 0x24,  // 按位异或（寄存器）
    XORI = 0x25, // 按位异或（立即数）
    SHL = 0x26,  // 逻辑左移（寄存器）
    SHLI = 0x27, // 逻辑左移（8 位立即数）
    SHR = 0x28,  // 逻辑右移（寄存器）
    SHRI = 0x29, // 逻辑右移（8 位立即数）

    // 比较指令 (0x30-0x3F)
    CMP = 0x30,  // 比较（寄存器）
    CMPI = 0x31, // 比较（立即数）
    TEST = 0x32, // 位测试

    // 栈操作指令 (0x40-0x4F)
    PUSH = 0x40,  // 压栈（寄存器）
    PUSHI = 0x41, // 压栈（立即数）
    POP = 0x42,   // 出栈
    ENTER = 0x43, // 建立栈帧
    LEAVE = 0x44, // 清理栈帧

    // 控制流指令 (0x50-0x5F)
    JMP = 0x50, // 无条件跳转
    JZ = 0x51,  // 为 0 跳转（Z=1）
    JNZ = 0x52, // 非 0 跳转（Z=0）
    JN = 0x53,  // 为负跳转（N=1）
    JP = 0x54,  // 为正跳转（P=1）

    // 函数调用指令 (0x60-0x6F)
    CALL = 0x60,  // 内部调用
    CALLX = 0x61, // 外部调用
    RET = 0x62,   // 返回

    // 寄存器操作指令 (0x70-0x7F)
    MOV = 0x70, // 寄存器间移动
    CLR = 0x71, // 清零
    NOP = 0x7F, // 空操作
};

// ----------------------------------------------------------------------------
// Flags 寄存器位定义
// ----------------------------------------------------------------------------
constexpr uint8_t FLAG_Z = (1 << 0); // Zero - 结果为 0
constexpr uint8_t FLAG_N = (1 << 1); // Negative - 结果为负
constexpr uint8_t FLAG_P = (1 << 2); // Positive - 结果为正

// ----------------------------------------------------------------------------
// 指令基类接口
// ----------------------------------------------------------------------------
class NInstructionsInterface
{
public:
    NInstructionsInterface() = default;
    virtual ~NInstructionsInterface() = default;

    // 生成二进制代码
    virtual std::vector<uint8_t> generateInstructionCode() = 0;

    // 生成指令名称（用于反汇编）
    virtual std::string generateInstructionName() = 0;

    // 从文本解析指令
    static NInstructionsInterface * parserInstructionText(std::string & text);

    // 获取操作码
    NOpcode
    getOpcode() const
    {
        return m_opcode;
    }

protected:
    NOpcode m_opcode;
};

// ============================================================================
// 内存访问指令
// ============================================================================

// LMM - Load Memory to Register (立即数→寄存器)
// 格式：lmm R0, 100
// 编码：[0x00][R0][64 00 00 00] (6 字节)
class NInstructionsLMM : public NInstructionsInterface
{
public:
    NInstructionsLMM(NRegister reg, int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

// ST - Store to direct address (寄存器→直接地址)
// 格式：st R0, 0x1000
// 编码：[0x01][R0][00 10 00 00] (6 字节)
class NInstructionsST : public NInstructionsInterface
{
public:
    NInstructionsST(NRegister reg, int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_addr;
};

// LEA - Load Effective Address
// 格式：lea R0, label
// 编码：[0x02][R0][地址 4 字节] (6 字节)
class NInstructionsLEA : public NInstructionsInterface
{
public:
    NInstructionsLEA(NRegister reg, int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_addr;
};

// LOAD - Load from memory (间接寻址)
// 格式：load R1, [R0]
// 编码：[0x03][R1][R0] (3 字节)
class NInstructionsLOAD : public NInstructionsInterface
{
public:
    NInstructionsLOAD(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// STORE - Store to memory (间接寻址)
// 格式：store [R0], R1
// 编码：[0x04][R0][R1] (3 字节)
class NInstructionsSTORE : public NInstructionsInterface
{
public:
    NInstructionsSTORE(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest; // 目标地址寄存器
    NRegister m_src;  // 源数据寄存器
};

// ============================================================================
// 算术运算指令
// ============================================================================

// ADD - Add registers
// 格式：add R0, R1
// 编码：[0x10][R0][R1] (3 字节)
class NInstructionsADD : public NInstructionsInterface
{
public:
    NInstructionsADD(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// ADDI - Add immediate
// 格式：addi R0, 100
// 编码：[0x11][R0][64 00 00 00] (6 字节)
class NInstructionsADDI : public NInstructionsInterface
{
public:
    NInstructionsADDI(NRegister reg, int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

// SUB - Subtract registers
class NInstructionsSUB : public NInstructionsInterface
{
public:
    NInstructionsSUB(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// SUBI - Subtract immediate
class NInstructionsSUBI : public NInstructionsInterface
{
public:
    NInstructionsSUBI(NRegister reg, int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

// MUL - Multiply registers
class NInstructionsMUL : public NInstructionsInterface
{
public:
    NInstructionsMUL(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// MULI - Multiply immediate
class NInstructionsMULI : public NInstructionsInterface
{
public:
    NInstructionsMULI(NRegister reg, int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

// DIV - Divide registers
class NInstructionsDIV : public NInstructionsInterface
{
public:
    NInstructionsDIV(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// DIVI - Divide immediate
class NInstructionsDIVI : public NInstructionsInterface
{
public:
    NInstructionsDIVI(NRegister reg, int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

// MOD - Modulo registers
class NInstructionsMOD : public NInstructionsInterface
{
public:
    NInstructionsMOD(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// MODI - Modulo immediate
class NInstructionsMODI : public NInstructionsInterface
{
public:
    NInstructionsMODI(NRegister reg, int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

// NOT - Bitwise NOT
// 格式：not R0
// 编码：[0x1A][R0] (2 字节)
class NInstructionsNOT : public NInstructionsInterface
{
public:
    NInstructionsNOT(NRegister reg);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

// NEG - Arithmetic NEG (two's complement)
// 格式：neg R0
// 编码：[0x1B][R0] (2 字节)
class NInstructionsNEG : public NInstructionsInterface
{
public:
    NInstructionsNEG(NRegister reg);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

// ============================================================================
// 栈操作指令
// ============================================================================

// PUSH - Push register to stack
// 格式：push R0
// 编码：[0x40][R0] (2 字节)
class NInstructionsPUSH : public NInstructionsInterface
{
public:
    NInstructionsPUSH(NRegister reg);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

// PUSHI - Push immediate to stack
// 格式：pushi 100
// 编码：[0x41][64 00 00 00] (5 字节)
class NInstructionsPUSHI : public NInstructionsInterface
{
public:
    NInstructionsPUSHI(int32_t val);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_val;
};

// POP - Pop from stack to register
// 格式：pop R0
// 编码：[0x42][R0] (2 字节)
class NInstructionsPOP : public NInstructionsInterface
{
public:
    NInstructionsPOP(NRegister reg);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

// ENTER - Enter stack frame
// 格式：enter 32
// 编码：[0x43][20 00] (3 字节，16 位立即数)
class NInstructionsENTER : public NInstructionsInterface
{
public:
    NInstructionsENTER(int16_t size);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int16_t m_size;
};

// LEAVE - Leave stack frame
// 格式：leave
// 编码：[0x44] (1 字节)
class NInstructionsLEAVE : public NInstructionsInterface
{
public:
    NInstructionsLEAVE();
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);
};

// ============================================================================
// 控制流指令
// ============================================================================

// JMP - Unconditional jump
// 格式：jmp 0x1000
// 编码：[0x50][00 10 00 00] (5 字节)
class NInstructionsJMP : public NInstructionsInterface
{
public:
    NInstructionsJMP(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// JZ - Jump if zero (Z=1)
// 格式：jz 0x1000
// 编码：[0x51][00 10 00 00] (5 字节)
class NInstructionsJZ : public NInstructionsInterface
{
public:
    NInstructionsJZ(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// JNZ - Jump if not zero (Z=0)
class NInstructionsJNZ : public NInstructionsInterface
{
public:
    NInstructionsJNZ(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// JN - Jump if negative (N=1)
class NInstructionsJN : public NInstructionsInterface
{
public:
    NInstructionsJN(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// JP - Jump if positive (P=1)
class NInstructionsJP : public NInstructionsInterface
{
public:
    NInstructionsJP(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// ============================================================================
// 函数调用指令
// ============================================================================

// CALL - Call internal function
// 格式：call 0x1000
// 编码：[0x60][00 10 00 00] (5 字节)
class NInstructionsCALL : public NInstructionsInterface
{
public:
    NInstructionsCALL(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// CALLX - Call external function
// 格式：callx 0x08049000
// 编码：[0x61][00 90 04 08] (5 字节)
class NInstructionsCALLX : public NInstructionsInterface
{
public:
    NInstructionsCALLX(int32_t addr);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    int32_t m_addr;
};

// RET - Return from function
// 格式：ret
// 编码：[0x62] (1 字节)
class NInstructionsRET : public NInstructionsInterface
{
public:
    NInstructionsRET();
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);
};

// ============================================================================
// 寄存器操作指令
// ============================================================================

// MOV - Move between registers
// 格式：mov R0, R1
// 编码：[0x70][R0][R1] (3 字节)
class NInstructionsMOV : public NInstructionsInterface
{
public:
    NInstructionsMOV(NRegister dest, NRegister src);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_dest;
    NRegister m_src;
};

// CLR - Clear register
// 格式：clr R0
// 编码：[0x71][R0] (2 字节)
class NInstructionsCLR : public NInstructionsInterface
{
public:
    NInstructionsCLR(NRegister reg);
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

// NOP - No operation
// 格式：nop
// 编码：[0x7F] (1 字节)
class NInstructionsNOP : public NInstructionsInterface
{
public:
    NInstructionsNOP();
    std::vector<uint8_t> generateInstructionCode() override;
    std::string generateInstructionName() override;
    static NInstructionsInterface * parserInstructionText(std::string & text);
};

// ============================================================================
// 全局映射表
// ============================================================================

extern std::map<std::string, NRegister> g_textToRegisterMap;
extern std::map<std::string, NInstructionsInterface * (*)(std::string &)>
  g_opcodeToGeneratorMap;

#endif // NVM_INSTRUCTION_H
