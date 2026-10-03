#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

#include <cstdint>

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

#endif // NVM_INSTRUCTION_H
