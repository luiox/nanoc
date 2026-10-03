// Immediate Arithmetic Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executeADDI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
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
