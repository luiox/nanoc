#ifndef NAS_INSTRUCTION_H
#define NAS_INSTRUCTION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 调用约定标记（写入导入表 entry 的 flags 低 2 位，见 Bytecode Format Spec v2.1 §4.2）
enum class NCallingConvention : uint8_t {
    FASTCALL = 0, // 缺省：前 4 个整型参数走 R0-R3
    CDECL = 1,    // 可变参数：调用者清栈（如 printf）
};

// 导入表 entry flags bit2：动态导入标记（规范 §2.1）。置位时 addr 为汇编器分配的
// 确定性伪宿主地址，加载期由宿主库按符号名解析
constexpr int32_t IMPORT_FLAG_DYNAMIC = 0x4;

// 无地址 extern 的伪宿主地址分配基址/步长（按声明序递增；与代码/数据地址空间及
// VM 宿主地址分配区 0x7F000000 起（HOST_ADDRESS_BASE）隔离）
constexpr int32_t DYNAMIC_HOST_BASE = 0x7E000000;
constexpr int32_t DYNAMIC_HOST_STEP = 4;

// NCI v2.1 文件头尺寸与魔数（规范 §2）
constexpr int32_t NCI_HEADER_SIZE = 32;
constexpr uint8_t NCI_MAGIC[8] = { 'N', 'a', 'n', 'o', 'C', '\0', '\0', '\0' };

enum class NOpcode : uint8_t {
    LMM = 0x00,
    ST = 0x01,
    LEA = 0x02,
    LOAD = 0x03,
    STORE = 0x04,
    LOADA = 0x05,
    STOREA = 0x06,
    ADD = 0x10,
    ADDI = 0x11,
    SUB = 0x12,
    SUBI = 0x13,
    MUL = 0x14,
    MULI = 0x15,
    DIV = 0x16,
    DIVI = 0x17,
    MOD = 0x18,
    MODI = 0x19,
    NOT = 0x1A,
    NEG = 0x1B,
    AND = 0x20,
    ANDI = 0x21,
    OR = 0x22,
    ORI = 0x23,
    XOR = 0x24,
    XORI = 0x25,
    SHL = 0x26,
    SHLI = 0x27,
    SHR = 0x28,
    SHRI = 0x29,
    CMP = 0x30,
    CMPI = 0x31,
    TEST = 0x32,
    PUSH = 0x40,
    PUSHI = 0x41,
    POP = 0x42,
    ENTER = 0x43,
    LEAVE = 0x44,
    JMP = 0x50,
    JZ = 0x51,
    JNZ = 0x52,
    JN = 0x53,
    JP = 0x54,
    CALL = 0x60,
    CALLX = 0x61,
    RET = 0x62,
    MOV = 0x70,
    CLR = 0x71,
    NOP = 0x7F,
};

class Instruction
{
public:
    NOpcode opcode;
    std::vector<uint8_t> bytes;
    int sourceLine = 0;            // 源码行号（1 起始），由装配驱动填写，用于错误定位
    std::string pendingLabel;      // 非空 = 地址操作数为标号，待第二遍回填
    bool patchFromImports = false; // true = 地址取自导入表（callx 符号），否则取自标号表
    int32_t patchOffset = -1;      // bytes 中待回填 IMM32 的偏移；-1 = 无

    Instruction(NOpcode op)
      : opcode(op)
    {
    }
    virtual ~Instruction() = default;
    virtual void emit() = 0;

protected:
    // 追加 4 字节立即数；若 pendingLabel 非空则写 0 占位并记录回填偏移
    void emitImm32(int32_t v);
};

// 汇编结果：ok=false 时 errorLine（1 起始）与 errorMessage 有效
struct AssemblyResult {
    bool ok = false;
    std::vector<uint8_t> image; // 完整 NCI v2.1 文件镜像
    int errorLine = 0;
    std::string errorMessage;
};

class Assembler
{
public:
    static uint8_t parseRegister(const std::string & s);
    static int32_t parseInt(const std::string & s);
    static std::unique_ptr<Instruction> parseLine(const std::string & line);
    // 整体汇编：两遍扫描，生成 NCI v2.1 完整目标文件
    // （32 字节头 + 代码段 + 数据段 + 导入表 + 导出表）
    static AssemblyResult assemble(const std::string & source);
};

#endif
