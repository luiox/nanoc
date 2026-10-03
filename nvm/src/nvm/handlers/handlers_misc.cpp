// Miscellaneous Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executeMOV()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] = m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeCLR()
{
    if (m_pc + INSTR_LEN_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < REGISTER_COUNT) {
            m_registers[reg] = 0;
            m_pc += INSTR_LEN_REG;
        }
    }
}

void
NVirtualMachine::executeNOP()
{
    m_pc += INSTR_LEN_NONE;
}
