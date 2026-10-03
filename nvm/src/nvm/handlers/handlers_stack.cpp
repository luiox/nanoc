// Stack and Control Flow Instructions Handlers
#include "nvm/core.hpp"

void
NVirtualMachine::executePUSH()
{
    if (m_pc + INSTR_LEN_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < REGISTER_COUNT && m_sp >= STACK_SLOT_SIZE) {
            m_sp -= STACK_SLOT_SIZE;
            memWrite32(m_stack, m_sp, m_registers[reg]);
            m_pc += INSTR_LEN_REG;
        }
    }
}

void
NVirtualMachine::executePUSHI()
{
    if (m_pc + INSTR_LEN_IMM32 <= m_codeSize) {
        int32_t val = readI32(m_code, m_pc + 1);
        if (m_sp >= STACK_SLOT_SIZE) {
            m_sp -= STACK_SLOT_SIZE;
            memWrite32(m_stack, m_sp, val);
            m_pc += INSTR_LEN_IMM32;
        }
    }
}

void
NVirtualMachine::executePOP()
{
    if (m_pc + INSTR_LEN_REG <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        if (reg < REGISTER_COUNT && m_sp + STACK_SLOT_SIZE <= m_stackSize) {
            m_registers[reg] = memRead32(m_stack, m_sp);
            m_sp += STACK_SLOT_SIZE;
            m_pc += INSTR_LEN_REG;
        }
    }
}

void
NVirtualMachine::executeENTER()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        int16_t size = readI16(m_code, m_pc + 1);
        m_sp -= STACK_SLOT_SIZE;
        memWrite32(m_stack, m_sp, m_bp);
        m_bp = m_sp;
        m_sp -= size;
        m_pc += INSTR_LEN_REG_REG;
    }
}

void
NVirtualMachine::executeLEAVE()
{
    if (m_pc + INSTR_LEN_NONE <= m_codeSize) {
        m_sp = m_bp;
        m_bp = memRead32(m_stack, m_sp);
        m_sp += STACK_SLOT_SIZE;
        m_pc += INSTR_LEN_NONE;
    }
}

void
NVirtualMachine::executeJMP()
{
    if (m_pc + INSTR_LEN_IMM32 <= m_codeSize) {
        int32_t addr = readI32(m_code, m_pc + 1);
        m_pc = addr;
    }
}

void
NVirtualMachine::executeJZ()
{
    if (m_pc + INSTR_LEN_IMM32 <= m_codeSize) {
        int32_t addr = readI32(m_code, m_pc + 1);
        if (m_flags & FLAG_Z)
            m_pc = addr;
        else
            m_pc += INSTR_LEN_IMM32;
    }
}

void
NVirtualMachine::executeJNZ()
{
    if (m_pc + INSTR_LEN_IMM32 <= m_codeSize) {
        int32_t addr = readI32(m_code, m_pc + 1);
        if (!(m_flags & FLAG_Z))
            m_pc = addr;
        else
            m_pc += INSTR_LEN_IMM32;
    }
}

// JN：N（bit1，负）置位则跳转
void
NVirtualMachine::executeJN()
{
    if (m_pc + INSTR_LEN_IMM32 <= m_codeSize) {
        int32_t addr = readI32(m_code, m_pc + 1);
        if (m_flags & FLAG_N)
            m_pc = addr;
        else
            m_pc += INSTR_LEN_IMM32;
    }
}

// JP：P（bit2，正）置位则跳转
void
NVirtualMachine::executeJP()
{
    if (m_pc + INSTR_LEN_IMM32 <= m_codeSize) {
        int32_t addr = readI32(m_code, m_pc + 1);
        if (m_flags & FLAG_P)
            m_pc = addr;
        else
            m_pc += INSTR_LEN_IMM32;
    }
}
