// 立即数算逻指令 handler：ADDI/SUBI/MULI/DIVI/MODI/ANDI/ORI/XORI（6 字节
// 「opcode + reg + imm32」，结果回写寄存器）、SHLI/SHRI（3 字节，移位量为
// imm8，不做掩码）、NEG（2 字节单寄存器取负）。DIVI/MODI 除零/模零语义与
// 寄存器形态一致：指令不生效且 pc 不前进（规范 §9.3 UB，程序不得依赖）。
#include "nvm/core.hpp"

void
NVirtualMachine::executeADDI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] += val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeSUBI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] -= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeMULI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] *= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeDIVI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT && val != 0) {
            m_registers[reg] /= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeMODI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT && val != 0) {
            m_registers[reg] %= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeNEG()
{
    if (m_pc + INSTR_LEN_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < REGISTER_COUNT) {
            m_registers[reg] = -m_registers[reg];
            m_pc += INSTR_LEN_REG;
        }
    }
}

// 以下立即数逻辑/移位指令与寄存器形式（handlers_compare_logic.cpp）语义一致，
// 均不修改 flags（flags 仅由 CMP/CMPI/TEST 设置）

void
NVirtualMachine::executeANDI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] &= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeORI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] |= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeXORI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] ^= val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

// SHLI R, IMM8：移位量为单字节立即数（指令长 3），不做掩码，与 SHL 一致
void
NVirtualMachine::executeSHLI()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        uint8_t shamt = m_code[m_pc + 2];
        if (reg < REGISTER_COUNT) {
            m_registers[reg] <<= shamt;
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

// SHRI R, IMM8：int32 算术右移（符号位扩展），与 SHR 一致
void
NVirtualMachine::executeSHRI()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        uint8_t shamt = m_code[m_pc + 2];
        if (reg < REGISTER_COUNT) {
            m_registers[reg] >>= shamt;
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}
