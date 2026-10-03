// Call and Return Instructions Handlers
#include "nvm/core.hpp"

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

// CALLX IMM32：外部（宿主）调用。宿主地址命中分发表时直接调用 C 函数：
// 不压返回地址、不跳转，返回值写入 R0，pc 越过 5 字节指令。
// 调用约定（fastcall/cdecl）只影响调用方是否清栈，VM 不代为清理。
// 未命中的地址属于运行时错误：报错并停止执行
void
NVirtualMachine::executeCALLX()
{
    if (m_pc + 5 > m_codeSize)
        return;
    int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
    auto it = m_hostFunctions.find(addr);
    if (it == m_hostFunctions.end()) {
        printf(
          "Error: CALLX: unresolved host function at 0x%08X (pc=%d), execution stopped\n",
          (uint32_t)addr,
          (int32_t)m_pc);
        m_pc = (int32_t)m_codeSize;
        return;
    }
    m_registers[0] = it->second(m_registers, m_stack, m_stackSize);
    m_pc += 5;
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
