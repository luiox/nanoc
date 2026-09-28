// Immediate Arithmetic Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executeADDI()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < 8) {
            m_registers[reg] += val;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeSUBI()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < 8) {
            m_registers[reg] -= val;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeMULI()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < 8) {
            m_registers[reg] *= val;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeDIVI()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < 8 && val != 0) {
            m_registers[reg] /= val;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeMODI()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *(int32_t *)&m_code[m_pc + 2];
        if (reg < 8 && val != 0) {
            m_registers[reg] %= val;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeNEG()
{
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < 8) {
            m_registers[reg] = -m_registers[reg];
            m_pc += 2;
        }
    }
}
