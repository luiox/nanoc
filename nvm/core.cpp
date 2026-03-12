#include <core.hpp>
#include <ctype.h>
#include <instructions.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_helper.hpp>

NVirtualMachine::NVirtualMachine(int32_t stackSize)
{
    m_stack = static_cast<int8_t *>(malloc(stackSize));
    m_stackSize = stackSize;
    m_codeSize = 0;
    m_ax = 0;
    m_bp = 0;
    m_code = NULL;
    m_pc = 0;
    m_sp = stackSize; // 栈指针初始化为栈顶
    
    // 初始化寄存器
    for (int i = 0; i < 8; i++) {
        m_registers[i] = 0;
    }
}
NVirtualMachine::~NVirtualMachine() { free(m_stack); }

void
NVirtualMachine::load(std::string filename)
{
    FILE * pf = fopen(filename.c_str(), "rb");
    if (pf == NULL) {
        printf("Error: Cannot open file %s\n", filename.c_str());
        exit(EXIT_FAILURE);
    }

    // 计算文件大小
    fseek(pf, 0, SEEK_END);
    int64_t file_size = ftell(pf);
    m_codeSize = file_size;

    // 申请内存
    m_code = static_cast<int8_t *>(malloc(file_size));
    if (m_code == NULL) {
        printf("Error: Cannot allocate memory for file %s\n", filename.c_str());
        exit(EXIT_FAILURE);
    }

    // 将文件内容全部读入
    fseek(pf, 0, SEEK_SET);
    fread(m_code, sizeof(int8_t), file_size, pf);
    fclose(pf);
}

void
NVirtualMachine::start()
{
    if (m_code == NULL || m_stack == NULL) {
        printf("Error: Code or stack not initialized\n");
        return;
    }
    
    printf("Starting virtual machine...\n");
    
    // 简单的指令执行循环
    while (m_pc < m_codeSize) {
        // 读取操作码
        uint8_t opcode = static_cast<uint8_t>(m_code[m_pc]);
        
        printf("PC: %d, Opcode: 0x%02X\n", m_pc, opcode);
        
        // 根据操作码执行指令
        switch (static_cast<NOpcode>(opcode)) {
            case NOpcode::LMM: {
                // LMM指令：lmm reg, imm
                // 格式：opcode(1) + register(1) + value(4)
                if (m_pc + 6 <= m_codeSize) {
                    uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
                    int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
                    
                    if (reg < 8) {
                        m_registers[reg] = value;
                        printf("LMM: R%d = %d\n", reg, value);
                    }
                    
                    m_pc += 6;
                } else {
                    printf("Error: LMM instruction out of bounds\n");
                    return;
                }
                break;
            }
            
            case NOpcode::ADD: {
                // ADD指令：add reg, imm
                // 格式：opcode(1) + register(1) + value(4)
                if (m_pc + 6 <= m_codeSize) {
                    uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
                    int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
                    
                    if (reg < 8) {
                        m_registers[reg] += value;
                        printf("ADD: R%d += %d (result: %d)\n", reg, value, m_registers[reg]);
                    }
                    
                    m_pc += 6;
                } else {
                    printf("Error: ADD instruction out of bounds\n");
                    return;
                }
                break;
            }
            
            case NOpcode::SUB: {
                // SUB指令：sub reg, imm
                // 格式：opcode(1) + register(1) + value(4)
                if (m_pc + 6 <= m_codeSize) {
                    uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
                    int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
                    
                    if (reg < 8) {
                        m_registers[reg] -= value;
                        printf("SUB: R%d -= %d (result: %d)\n", reg, value, m_registers[reg]);
                    }
                    
                    m_pc += 6;
                } else {
                    printf("Error: SUB instruction out of bounds\n");
                    return;
                }
                break;
            }
            
            case NOpcode::CALL: {
                // CALL指令：call target
                // 格式：opcode(1) + target(4)
                if (m_pc + 5 <= m_codeSize) {
                    int32_t target = *reinterpret_cast<int32_t*>(&m_code[m_pc + 1]);
                    
                    // 将返回地址压栈
                    m_sp -= 4;
                    *reinterpret_cast<int32_t*>(&m_stack[m_sp]) = m_pc + 5;
                    
                    printf("CALL: target=%d, return address=%d\n", target, m_pc + 5);
                    
                    // 跳转到目标地址
                    m_pc = target;
                } else {
                    printf("Error: CALL instruction out of bounds\n");
                    return;
                }
                break;
            }
            
            case NOpcode::RET: {
                // RET指令：ret
                // 格式：opcode(1)
                if (m_sp + 4 <= m_stackSize) {
                    // 从栈中弹出返回地址
                    int32_t returnAddr = *reinterpret_cast<int32_t*>(&m_stack[m_sp]);
                    m_sp += 4;
                    
                    printf("RET: return to %d\n", returnAddr);
                    
                    // 跳转到返回地址
                    m_pc = returnAddr;
                } else {
                    printf("Error: RET instruction - stack underflow\n");
                    return;
                }
                break;
            }
            
            case NOpcode::TRAP: {
                // TRAP指令：trap type
                // 格式：opcode(1) + type(1)
                if (m_pc + 2 <= m_codeSize) {
                    uint8_t trapType = static_cast<uint8_t>(m_code[m_pc + 1]);
                    
                    printf("TRAP: type=%d\n", trapType);
                    
                    switch (static_cast<NTrapType>(trapType)) {
                        case NTrapType::HALT:
                            printf("Program halted\n");
                            return;
                        case NTrapType::OUTCH:
                            // 输出字符（这里简化处理）
                            printf("Output: %c\n", static_cast<char>(m_ax));
                            break;
                        case NTrapType::GETCH:
                            // 获取字符（这里简化处理）
                            printf("Input character: ");
                            m_ax = getchar();
                            break;
                        default:
                            printf("Unknown trap type: %d\n", trapType);
                            break;
                    }
                    
                    m_pc += 2;
                } else {
                    printf("Error: TRAP instruction out of bounds\n");
                    return;
                }
                break;
            }
            
            default:
                printf("Unknown opcode: 0x%02X\n", opcode);
                m_pc++;
                break;
        }
    }
    
    printf("Program execution completed\n");
}

void
NVirtualMachine::print_info()
{
    printf("Nvm current infomation:\n");
    printf("stack_size: %d bytes\n", m_stackSize);
    printf("bp: %d\n", m_bp);
    printf("pc: %d\n", m_pc);
    printf("ax: %d\n", m_ax);
    printf("sp: %d\n", m_sp);
}

void
NVirtualMachine::print_stack(int32_t start, int32_t end)
{
    printf("Nvm current stack infomation from %X to %X:\n", start, end);

    for (int8_t * i = m_stack + start; i < m_stack + end; i++) {
        printf("%X ", *i);
        if ((i - m_stack) % 16 == 0)
            printf("\n");
    }
}

int32_t
NVirtualMachine::getRegister(int32_t regIndex)
{
    if (regIndex >= 0 && regIndex < 8) {
        return m_registers[regIndex];
    }
    return 0;
}

void
NVirtualMachine::setRegister(int32_t regIndex, int32_t value)
{
    if (regIndex >= 0 && regIndex < 8) {
        m_registers[regIndex] = value;
    }
}

int32_t
NVirtualMachine::getPC()
{
    return m_pc;
}

void
NVirtualMachine::setPC(int32_t value)
{
    m_pc = value;
}

int32_t
NVirtualMachine::getSP()
{
    return m_sp;
}

void
NVirtualMachine::setSP(int32_t value)
{
    m_sp = value;
}

int32_t
NVirtualMachine::getBP()
{
    return m_bp;
}

void
NVirtualMachine::setBP(int32_t value)
{
    m_bp = value;
}

int32_t
NVirtualMachine::getAX()
{
    return m_ax;
}

void
NVirtualMachine::setAX(int32_t value)
{
    m_ax = value;
}

int32_t
NVirtualMachine::getStackSize()
{
    return m_stackSize;
}

int64_t
NVirtualMachine::getCodeSize()
{
    return m_codeSize;
}

int8_t*
NVirtualMachine::getStack()
{
    return m_stack;
}

int8_t*
NVirtualMachine::getCode()
{
    return m_code;
}
