// 访存指令 handler（统一内存 = m_stack，数据段与运行栈共用，见 core.hpp）：
// LMM 立即数装入寄存器；LEA 装入地址常量（纯地址计算，不查越界）；
// LOAD/STORE 寄存器间接读写；LOADA/STOREA 绝对地址读写；ST 绝对地址写。
// 越界访问不做错误上报（读不回写寄存器、写被丢弃），各 handler 的边界
// 判断细节以函数内实现为准。
#include "nvm/core.hpp"

void
NVirtualMachine::executeLMM()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t val = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] = val;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeST()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t addr = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT && addr >= 0 && addr < m_stackSize) {
            memWrite32(m_stack, addr, m_registers[reg]);
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeLEA()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t addr = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            m_registers[reg] = addr;
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

void
NVirtualMachine::executeLOAD()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            int32_t addr = m_registers[src];
            if (addr >= 0 && addr < m_stackSize) {
                m_registers[dest] = memRead32(m_stack, addr);
            }
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

void
NVirtualMachine::executeSTORE()
{
    if (m_pc + INSTR_LEN_REG_REG <= m_codeSize) {
        uint8_t dest = m_code[m_pc + 1];
        uint8_t src = m_code[m_pc + 2];
        if (dest < REGISTER_COUNT && src < REGISTER_COUNT) {
            int32_t addr = m_registers[dest];
            if (addr >= 0 && addr < m_stackSize) {
                memWrite32(m_stack, addr, m_registers[src]);
            }
            m_pc += INSTR_LEN_REG_REG;
        }
    }
}

// LOADA R, IMM32：LOAD 的绝对寻址形式，R = mem[IMM32]
void
NVirtualMachine::executeLOADA()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t addr = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            if (addr >= 0 && addr < m_stackSize) {
                m_registers[reg] = memRead32(m_stack, addr);
            }
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}

// STOREA R, IMM32：STORE 的绝对寻址形式，mem[IMM32] = R
void
NVirtualMachine::executeSTOREA()
{
    if (m_pc + INSTR_LEN_REG_IMM32 <= m_codeSize) {
        uint8_t reg = m_code[m_pc + 1];
        int32_t addr = readI32(m_code, m_pc + 2);
        if (reg < REGISTER_COUNT) {
            if (addr >= 0 && addr < m_stackSize) {
                memWrite32(m_stack, addr, m_registers[reg]);
            }
            m_pc += INSTR_LEN_REG_IMM32;
        }
    }
}
