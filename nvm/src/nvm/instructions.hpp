#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

#include <cstdint>

// NCI v2.1 Opcode Definition
enum class NOpcode : uint8_t {
    // Memory access (0x00-0x0F)
    LMM = 0x00,
    ST = 0x01,
    LEA = 0x02,
    LOAD = 0x03,
    STORE = 0x04,
    LOADA = 0x05,
    STOREA = 0x06,

    // Arithmetic (0x10-0x1F)
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

    // Logic (0x20-0x2F)
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

    // Compare (0x30-0x3F)
    CMP = 0x30,
    CMPI = 0x31,
    TEST = 0x32,

    // Stack (0x40-0x4F)
    PUSH = 0x40,
    PUSHI = 0x41,
    POP = 0x42,
    ENTER = 0x43,
    LEAVE = 0x44,

    // Control flow (0x50-0x5F)
    JMP = 0x50,
    JZ = 0x51,
    JNZ = 0x52,
    JN = 0x53,
    JP = 0x54,

    // Function call (0x60-0x6F)
    CALL = 0x60,
    CALLX = 0x61,
    RET = 0x62,

    // Register ops (0x70-0x7F)
    MOV = 0x70,
    CLR = 0x71,
    NOP = 0x7F,
};

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
