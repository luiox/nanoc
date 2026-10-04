// 寄存器算逻指令 handler：ADD/SUB/MUL/DIV/MOD（3 字节「opcode + reg + reg」，
// 结果回写 dest）、NOT（2 字节按位取反）。DIV/MOD 除零/模零时指令不生效且
// pc 不前进（规范 §9.3 UB，程序不得依赖）；有符号溢出同为 UB（§9.2）。
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
