// Compare and Logic Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executeCMP()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            int32_t result = m_registers[dest] - m_registers[src];
            m_flags = 0;
            if (result == 0)
                m_flags |= FLAG_Z;
            if (result < 0)
                m_flags |= FLAG_N;
            if (result > 0)
                m_flags |= FLAG_P;
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeCMPI()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < REGISTER_COUNT) {
            int32_t result = m_registers[reg] - val;
            m_flags = 0;
            if (result == 0)
                m_flags |= FLAG_Z;
            if (result < 0)
                m_flags |= FLAG_N;
            if (result > 0)
                m_flags |= FLAG_P;
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
            int32_t result = m_registers[dest] & m_registers[src];
            m_flags = 0;
            if (result == 0)
                m_flags |= FLAG_Z;
            if (result < 0)
                m_flags |= FLAG_N;
            if (result > 0)
                m_flags |= FLAG_P;
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
