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

// CALLX IMM32：双语义（链接器把解析到内部符号的 callx 站点改写为平移后的代码地址，
// opcode 不变，见 doc/Bytecode Format Specification v2.1.md §2.2）：
//   1. 0 < addr < codeSize → 内部地址：按 CALL 处理（压返回地址、跳转）。
//      排除 addr=0：未解析的动态导入（addr=0）站点保持 imm=0，须走宿主路径报错，
//      而不能误跳到内部地址 0（代价：callx 无法编码"调用内部地址 0"这一目标）
//   2. 否则查宿主函数表：命中则调用 C 函数（不压返回地址，返回值写 R0，pc 越过
//      5 字节指令）；未命中为运行时错误，报错并停止执行。
// 宿主地址空间与代码段隔离（HOST_ADDRESS_BASE 起），registerHostFunction 注册的
// 宿主地址不得落入 [0, codeSize)。调用约定（fastcall/cdecl）只影响调用方是否清栈
void
NVirtualMachine::executeCALLX()
{
    if (m_pc + 5 > m_codeSize)
        return;
    int32_t addr = *reinterpret_cast<const int32_t *>(&m_code[m_pc + 1]);
    if (addr > 0 && addr < (int32_t)m_codeSize) {
        m_sp -= 4;
        *reinterpret_cast<int32_t *>(&m_stack[m_sp]) = m_pc + 5;
        m_pc = addr;
        return;
    }
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
