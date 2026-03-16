// Memory Access Instructions Handlers
#include "core.hpp"

void
NVirtualMachine::executeLMM()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 2]);
        if (reg < 8) {
            m_registers[reg] = val;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeST()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 2]);
        if (reg < 8 && addr >= 0 && addr < m_stackSize) {
            *reinterpret_cast<int32_t *>(&m_stack[addr]) = m_registers[reg];
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeLEA()
{
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 2]);
        if (reg < 8) {
            m_registers[reg] = addr;
            m_pc += 6;
        }
    }
}

void
NVirtualMachine::executeLOAD()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            int32_t addr = m_registers[src];
            if (addr >= 0 && addr < m_stackSize) {
                m_registers[dest] = *reinterpret_cast<int32_t *>(&m_stack[addr]);
            }
            m_pc += 3;
        }
    }
}

void
NVirtualMachine::executeSTORE()
{
    if (m_pc + 3 <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < 8 && src < 8) {
            int32_t addr = m_registers[dest];
            if (addr >= 0 && addr < m_stackSize) {
                *reinterpret_cast<int32_t *>(&m_stack[addr]) = m_registers[src];
            }
            m_pc += 3;
        }
    }
}
