// Call and Return Instructions Handlers
#include "core.hpp"

void
NVirtualMachine::executeCALL()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        m_sp -= 4;
        *reinterpret_cast<int32_t *>(&m_stack[m_sp]) = m_pc + 5;
        m_pc = addr;
    }
}

void
NVirtualMachine::executeCALLX()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        m_sp -= 4;
        *reinterpret_cast<int32_t *>(&m_stack[m_sp]) = m_pc + 5;
        m_pc = addr;
    }
}

void
NVirtualMachine::executeRET()
{
    if (m_sp + 4 <= m_stackSize) {
        int32_t addr = *reinterpret_cast<int32_t *>(&m_stack[m_sp]);
        m_sp += 4;
        m_pc = addr;
    }
}
