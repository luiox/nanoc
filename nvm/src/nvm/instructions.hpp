#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

#include <cstdint>
#include <cstring>

// NCI v2.1 opcode 权威定义在 nas/src/nas/instruction.hpp（本头文件曾重复
// 定义 NOpcode，nvm 侧零引用；集成期删除重复，FLAG_*/NRegister 保留）

// Flags register bits
constexpr uint8_t FLAG_Z = (1 << 0);
constexpr uint8_t FLAG_N = (1 << 1);
constexpr uint8_t FLAG_P = (1 << 2);

// Register encoding (3 bits)
enum class NRegister : uint8_t {
    R0 = 0,
    R1 = 1,
    R2 = 2,
    R3 = 3,
    R4 = 4,
    R5 = 5,
    R6 = 6,
    R7 = 7,
};

// 通用寄存器数量（R0-R7，与 NRegister 编码一致；R4=SP、R5=BP）
constexpr int32_t REGISTER_COUNT = 8;

// 寄存器角色下标（规范 v2.1）：R4=SP、R5=BP（m_registers 别名）
constexpr int32_t SP_REGISTER_INDEX = 4;
constexpr int32_t BP_REGISTER_INDEX = 5;

// 指令长度（字节，规范 §3.1 指令长度表；按操作数形态命名，取指边界检查用）
constexpr int32_t INSTR_LEN_NONE = 1; // 仅 opcode：LEAVE/RET/NOP
constexpr int32_t INSTR_LEN_REG = 2;  // opcode + reg：PUSH/POP/NOT/NEG/CLR
constexpr int32_t INSTR_LEN_REG_REG =
  3; // opcode + reg + reg：算逻/访存寄存器形式
     // （SHLI/SHRI 的 imm8、ENTER 的 imm16 同为 3 字节）
constexpr int32_t INSTR_LEN_IMM32 =
  5; // opcode + imm32：PUSHI/JMP/JZ/JNZ/JN/JP/CALL/CALLX
constexpr int32_t INSTR_LEN_REG_IMM32 =
  6; // opcode + reg + imm32：LMM/ST/LEA/LOADA/STOREA
     // 与各立即数算逻（ADDI/CMPI/...）

// 32 位栈槽宽度（int32；压栈/出栈/调用返回地址均占 4 字节）
constexpr int32_t STACK_SLOT_SIZE = 4;

// ==== 取指 / 统一内存读写辅助（core 与 handlers 共用）====
// 统一内存按小端编码（规范 §3.1）；memcpy 实现避免对齐与严格别名问题，
// 地址越界检查由调用方（各 handler 的取指边界判断）负责

// 从代码流读取操作数（小端 int32/int16）
inline int32_t
readI32(const int8_t * code, int64_t offset)
{
    int32_t v;
    memcpy(&v, code + offset, sizeof(v));
    return v;
}

inline int16_t
readI16(const int8_t * code, int64_t offset)
{
    int16_t v;
    memcpy(&v, code + offset, sizeof(v));
    return v;
}

// 统一内存 int32 槽读写（栈槽数据 / 哨兵返回地址）
inline int32_t
memRead32(const int8_t * mem, int32_t addr)
{
    int32_t v;
    memcpy(&v, mem + addr, sizeof(v));
    return v;
}

inline void
memWrite32(int8_t * mem, int32_t addr, int32_t v)
{
    memcpy(mem + addr, &v, sizeof(v));
}

#endif // NVM_INSTRUCTION_H
