// Stack and Control Flow Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executePUSH()
{
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < 8 && m_sp >= 4) {
            m_sp -= 4;
            *reinterpret_cast<int32_t *>(&m_stack[m_sp]) = m_registers[reg];
            m_pc += 2;
        }
    }
}

void
NVirtualMachine::executePUSHI()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t val = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        if (m_sp >= 4) {
            m_sp -= 4;
            *reinterpret_cast<int32_t *>(&m_stack[m_sp]) = val;
            m_pc += 5;
        }
    }
}

void
NVirtualMachine::executePOP()
{
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < 8 && m_sp + 4 <= m_stackSize) {
            m_registers[reg] = *reinterpret_cast<int32_t *>(&m_stack[m_sp]);
            m_sp += 4;
            m_pc += 2;
        }
    }
}

void
NVirtualMachine::executeENTER()
{
    if (m_pc + 3 <= m_codeSize) {
        int16_t size = *reinterpret_cast<const int16_t *>(&m_code[m_pc + 1]);
        m_sp -= 4;
        *reinterpret_cast<int32_t *>(&m_stack[m_sp]) = m_bp;
        m_bp = m_sp;
        m_sp -= size;
        m_pc += 3;
    }
}

void
NVirtualMachine::executeLEAVE()
{
    if (m_pc + 1 <= m_codeSize) {
        m_sp = m_bp;
        m_bp = *reinterpret_cast<int32_t *>(&m_stack[m_sp]);
        m_sp += 4;
        m_pc += 1;
    }
}

void
NVirtualMachine::executeJMP()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        m_pc = addr;
    }
}

void
NVirtualMachine::executeJZ()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        if (m_flags & FLAG_Z)
            m_pc = addr;
        else
            m_pc += 5;
    }
}

void
NVirtualMachine::executeJNZ()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        if (!(m_flags & FLAG_Z))
            m_pc = addr;
        else
            m_pc += 5;
    }
}

// JN：N（bit1，负）置位则跳转
void
NVirtualMachine::executeJN()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        if (m_flags & FLAG_N)
            m_pc = addr;
        else
            m_pc += 5;
    }
}

// JP：P（bit2，正）置位则跳转
void
NVirtualMachine::executeJP()
{
    if (m_pc + 5 <= m_codeSize) {
        int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
        if (m_flags & FLAG_P)
            m_pc = addr;
        else
            m_pc += 5;
    }
}
