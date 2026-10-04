// 杂项指令 handler：MOV（寄存器间拷贝）、CLR（寄存器清零）、NOP（空操作）。
// 三者均不访存、不动 flags；非法寄存器号静默不生效（NOP 恒推进 pc），
// 与其余 handler 的防御口径一致。
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
