#ifndef NAS_INSTRUCTION_H
#define NAS_INSTRUCTION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    Instruction(NOpcode op)
      : opcode(op)
    {
    }
    virtual ~Instruction() = default;
    virtual void emit() = 0;
};

class Assembler
{
public:
    static uint8_t parseRegister(const std::string & s);
    static int32_t parseInt(const std::string & s);
    static std::unique_ptr<Instruction> parseLine(const std::string & line);
};

#endif
