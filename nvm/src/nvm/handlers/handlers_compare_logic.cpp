// Compare and Logic Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executeCMP()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            int32_t result = m_registers[dest] - m_registers[src];
            m_flags = 0;
            if (result == 0)
                m_flags |= FLAG_Z;
            if (result < 0)
                m_flags |= FLAG_N;
            if (result > 0)
                m_flags |= FLAG_P;
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeCMPI()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < 8) {
            int32_t result = m_registers[reg] - val;
            m_flags = 0;
            if (result == 0)
                m_flags |= FLAG_Z;
            if (result < 0)
                m_flags |= FLAG_N;
            if (result > 0)
                m_flags |= FLAG_P;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeTEST()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            int32_t result = m_registers[dest] & m_registers[src];
            m_flags = 0;
            if (result == 0)
                m_flags |= FLAG_Z;
            if (result < 0)
                m_flags |= FLAG_N;
            if (result > 0)
                m_flags |= FLAG_P;
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeAND()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] &= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeOR()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] |= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeXOR()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] ^= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeSHL()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] <<= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeSHR()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] >>= m_registers[src];
            m_pc += 3;
        }
    }
}
