// Arithmetic Instructions Handlers
#include "core.hpp"

void
NVirtualMachine::executeADD()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] += m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeSUB()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] -= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeMUL()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            m_registers[dest] *= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeDIV()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8 && m_registers[src] != 0) {
            m_registers[dest] /= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeMOD()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8 && m_registers[src] != 0) {
            m_registers[dest] %= m_registers[src];
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeNOT()
{
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < 8) {
            m_registers[reg] = ~m_registers[reg];
            m_pc += 2;
        }
    }
}
