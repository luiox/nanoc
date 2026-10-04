// 比较与位逻辑指令 handler：CMP/CMPI/TEST 置 flags（三态映射见
// setCompareFlags），是条件跳转的唯一依据；AND/OR/XOR/SHL/SHR 只回写结果、
// 不动 flags（不能直接作条件判定源）。
#include "nvm/core.hpp"

// 结果三态映射：=0 → Z，<0 → N，>0 → P；每次比较整体重置 flags
void
NVirtualMachine::setCompareFlags(int32_t result)
{
    m_flags = 0;
    if (result == 0)
        m_flags |= FLAG_Z;
    if (result < 0)
        m_flags |= FLAG_N;
    if (result > 0)
        m_flags |= FLAG_P;
}

void
NVirtualMachine::executeCMP()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            setCompareFlags(m_registers[dest] - m_registers[src]);
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeCMPI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            setCompareFlags(m_registers[reg] - val);
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeTEST()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            setCompareFlags(m_registers[dest] & m_registers[src]);
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeAND()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] &= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeOR()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] |= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeXOR()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] ^= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeSHL()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] <<= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeSHR()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] >>= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}
