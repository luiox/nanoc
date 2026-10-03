// Arithmetic Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executeADD()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] += m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeSUB()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] -= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeMUL()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            m_registers[dest] *= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeDIV()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT && m_registers[src] != 0) {
            m_registers[dest] /= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeMOD()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT && m_registers[src] != 0) {
            m_registers[dest] %= m_registers[src];
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeNOT()
{
    if (m_pc + INSTR_LEN_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < REGISTER_COUNT) {
            m_registers[reg] = ~m_registers[reg];
            m_pc += INSTR_LEN_REG;
        }
    }
}
