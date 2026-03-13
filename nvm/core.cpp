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
    m_flags = 0;
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
    
    // 分配内存并读取整个文件
    int8_t* file_data = static_cast<int8_t *>(malloc(file_size));
    if (file_data == NULL) {
        printf("Error: Cannot allocate memory for file %s\n", filename.c_str());
        fclose(pf);
        exit(EXIT_FAILURE);
    }
    
    fseek(pf, 0, SEEK_SET);
    fread(file_data, sizeof(int8_t), file_size, pf);
    fclose(pf);
    
    // 检查是否是NCO格式（有文件头）
    if (file_size >= 16 && 
        file_data[0] == 'N' && file_data[1] == 'C' && 
        file_data[2] == 'O' && file_data[3] == '\0') {
        // NCO格式：解析文件头
        printf("Loading NCO format bytecode...\n");
        
        // 读取代码段大小
        int32_t code_size = 
            (static_cast<int32_t>(file_data[8]) << 0) |
            (static_cast<int32_t>(file_data[9]) << 8) |
            (static_cast<int32_t>(file_data[10]) << 16) |
            (static_cast<int32_t>(file_data[11]) << 24);
        
        // 读取入口点偏移
        int32_t entry_point = 
            (static_cast<int32_t>(file_data[12]) << 0) |
            (static_cast<int32_t>(file_data[13]) << 8) |
            (static_cast<int32_t>(file_data[14]) << 16) |
            (static_cast<int32_t>(file_data[15]) << 24);
        
        printf("Code size: %d bytes, Entry point: %d\n", code_size, entry_point);
        
        // 分配代码内存
        m_codeSize = code_size;
        m_code = static_cast<int8_t *>(malloc(code_size));
        if (m_code == NULL) {
            printf("Error: Cannot allocate memory for code\n");
            free(file_data);
            exit(EXIT_FAILURE);
        }
        
        // 复制代码段（跳过16字节文件头）
        memcpy(m_code, file_data + 16, code_size);
        
        // 设置PC到入口点
        m_pc = entry_point;
        
    } else {
        // 原始二进制格式（向后兼容）
        printf("Loading raw binary format...\n");
        m_codeSize = file_size;
        m_code = file_data;
        file_data = nullptr; // 防止重复释放
        m_pc = 0;
    }
    
    if (file_data) {
        free(file_data);
    }
}

// 指令处理函数实现
void
NVirtualMachine::executeLMM()
{
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
    }
}

void
NVirtualMachine::executeST()
{
    // ST指令：st reg, addr
    // 格式：opcode(1) + register(1) + address(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t addr = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8 && addr >= 0 && addr < m_stackSize) {
            *reinterpret_cast<int32_t*>(&m_stack[addr]) = m_registers[reg];
            printf("ST: mem[%d] = R%d (%d)\n", addr, reg, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: ST instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeLEA()
{
    // LEA指令：lea reg, addr
    // 格式：opcode(1) + register(1) + address(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t addr = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] = addr;
            printf("LEA: R%d = %d\n", reg, addr);
        }
        
        m_pc += 6;
    } else {
        printf("Error: LEA instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeADD()
{
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
    }
}

void
NVirtualMachine::executeSUB()
{
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
    }
}

void
NVirtualMachine::executeMUL()
{
    // MUL指令：mul reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] *= value;
            printf("MUL: R%d *= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: MUL instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeDIV()
{
    // DIV指令：div reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            if (value == 0) {
                printf("Error: Division by zero\n");
                return;
            }
            m_registers[reg] /= value;
            printf("DIV: R%d /= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: DIV instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeMOD()
{
    // MOD指令：mod reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            if (value == 0) {
                printf("Error: Modulo by zero\n");
                return;
            }
            m_registers[reg] %= value;
            printf("MOD: R%d %%= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: MOD instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeNOT()
{
    // NOT指令：not reg
    // 格式：opcode(1) + register(1)
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        
        if (reg < 8) {
            m_registers[reg] = ~m_registers[reg];
            printf("NOT: R%d = ~R%d (result: %d)\n", reg, reg, m_registers[reg]);
        }
        
        m_pc += 2;
    } else {
        printf("Error: NOT instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeAND()
{
    // AND指令：and reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] &= value;
            printf("AND: R%d &= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: AND instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeOR()
{
    // OR指令：or reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] |= value;
            printf("OR: R%d |= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: OR instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeXOR()
{
    // XOR指令：xor reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] ^= value;
            printf("XOR: R%d ^= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: XOR instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeSHL()
{
    // SHL指令：shl reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] <<= value;
            printf("SHL: R%d <<= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: SHL instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeSHR()
{
    // SHR指令：shr reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_registers[reg] >>= value;
            printf("SHR: R%d >>= %d (result: %d)\n", reg, value, m_registers[reg]);
        }
        
        m_pc += 6;
    } else {
        printf("Error: SHR instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeEQ()
{
    // EQ指令：eq reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_flags = (m_registers[reg] == value) ? 1 : 0;
            printf("EQ: R%d == %d (result: %d)\n", reg, value, m_flags);
        }
        
        m_pc += 6;
    } else {
        printf("Error: EQ instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeNE()
{
    // NE指令：ne reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_flags = (m_registers[reg] != value) ? 1 : 0;
            printf("NE: R%d != %d (result: %d)\n", reg, value, m_flags);
        }
        
        m_pc += 6;
    } else {
        printf("Error: NE instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeLT()
{
    // LT指令：lt reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_flags = (m_registers[reg] < value) ? 1 : 0;
            printf("LT: R%d < %d (result: %d)\n", reg, value, m_flags);
        }
        
        m_pc += 6;
    } else {
        printf("Error: LT instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeLE()
{
    // LE指令：le reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_flags = (m_registers[reg] <= value) ? 1 : 0;
            printf("LE: R%d <= %d (result: %d)\n", reg, value, m_flags);
        }
        
        m_pc += 6;
    } else {
        printf("Error: LE instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeGT()
{
    // GT指令：gt reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_flags = (m_registers[reg] > value) ? 1 : 0;
            printf("GT: R%d > %d (result: %d)\n", reg, value, m_flags);
        }
        
        m_pc += 6;
    } else {
        printf("Error: GT instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeGE()
{
    // GE指令：ge reg, imm
    // 格式：opcode(1) + register(1) + value(4)
    if (m_pc + 6 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        int32_t value = *reinterpret_cast<int32_t*>(&m_code[m_pc + 2]);
        
        if (reg < 8) {
            m_flags = (m_registers[reg] >= value) ? 1 : 0;
            printf("GE: R%d >= %d (result: %d)\n", reg, value, m_flags);
        }
        
        m_pc += 6;
    } else {
        printf("Error: GE instruction out of bounds\n");
    }
}

void
NVirtualMachine::executePUSH()
{
    // PUSH指令：push reg
    // 格式：opcode(1) + register(1)
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        
        if (reg < 8) {
            m_sp -= 4;
            *reinterpret_cast<int32_t*>(&m_stack[m_sp]) = m_registers[reg];
            printf("PUSH: R%d (value: %d)\n", reg, m_registers[reg]);
        }
        
        m_pc += 2;
    } else {
        printf("Error: PUSH instruction out of bounds\n");
    }
}

void
NVirtualMachine::executePOP()
{
    // POP指令：pop reg
    // 格式：opcode(1) + register(1)
    if (m_pc + 2 <= m_codeSize) {
        uint8_t reg = static_cast<uint8_t>(m_code[m_pc + 1]);
        
        if (reg < 8) {
            if (m_sp + 4 <= m_stackSize) {
                m_registers[reg] = *reinterpret_cast<int32_t*>(&m_stack[m_sp]);
                m_sp += 4;
                printf("POP: R%d (value: %d)\n", reg, m_registers[reg]);
            } else {
                printf("Error: POP instruction - stack underflow\n");
            }
        }
        
        m_pc += 2;
    } else {
        printf("Error: POP instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeJMP()
{
    // JMP指令：jmp target
    // 格式：opcode(1) + target(4)
    if (m_pc + 5 <= m_codeSize) {
        int32_t target = *reinterpret_cast<int32_t*>(&m_code[m_pc + 1]);
        
        printf("JMP: target=%d\n", target);
        
        // 跳转到目标地址
        m_pc = target;
    } else {
        printf("Error: JMP instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeJIC()
{
    // JIC指令：jic target
    // 格式：opcode(1) + target(4)
    if (m_pc + 5 <= m_codeSize) {
        int32_t target = *reinterpret_cast<int32_t*>(&m_code[m_pc + 1]);
        
        printf("JIC: target=%d, flags=%d\n", target, m_flags);
        
        // 条件跳转：如果flags为1则跳转
        if (m_flags) {
            m_pc = target;
        } else {
            m_pc += 5;
        }
    } else {
        printf("Error: JIC instruction out of bounds\n");
    }
}

void
NVirtualMachine::executeCALL()
{
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
    }
}

void
NVirtualMachine::executeRET()
{
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
    }
}

void
NVirtualMachine::executeTRAP()
{
    // TRAP指令：trap type
    // 格式：opcode(1) + type(1)
    if (m_pc + 2 <= m_codeSize) {
        uint8_t trapType = static_cast<uint8_t>(m_code[m_pc + 1]);
        
        printf("TRAP: type=%d\n", trapType);
        
        switch (static_cast<NTrapType>(trapType)) {
            case NTrapType::HALT:
                printf("Program halted\n");
                m_pc = m_codeSize; // 停止执行
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
    }
}

void
NVirtualMachine::start()
{
    if (m_code == NULL || m_stack == NULL) {
        printf("Error: Code or stack not initialized\n");
        return;
    }
    
    printf("Starting virtual machine...\n");
    
    // 指令处理函数表
    typedef void (NVirtualMachine::*InstructionHandler)();
    static const InstructionHandler handlers[] = {
        &NVirtualMachine::executeLMM,    // 0
        &NVirtualMachine::executeST,     // 1
        &NVirtualMachine::executeLEA,    // 2
        &NVirtualMachine::executeADD,    // 3
        &NVirtualMachine::executeSUB,    // 4
        &NVirtualMachine::executeMUL,    // 5
        &NVirtualMachine::executeDIV,    // 6
        &NVirtualMachine::executeMOD,    // 7
        &NVirtualMachine::executeNOT,    // 8
        &NVirtualMachine::executeAND,    // 9
        &NVirtualMachine::executeOR,     // 10
        &NVirtualMachine::executeXOR,    // 11
        &NVirtualMachine::executeSHL,    // 12
        &NVirtualMachine::executeSHR,    // 13
        &NVirtualMachine::executeEQ,     // 14
        &NVirtualMachine::executeNE,     // 15
        &NVirtualMachine::executeLT,     // 16
        &NVirtualMachine::executeLE,     // 17
        &NVirtualMachine::executeGT,     // 18
        &NVirtualMachine::executeGE,     // 19
        &NVirtualMachine::executePUSH,   // 20
        &NVirtualMachine::executePOP,    // 21
        &NVirtualMachine::executeJMP,    // 22
        &NVirtualMachine::executeJIC,    // 23
        &NVirtualMachine::executeCALL,   // 24
        &NVirtualMachine::executeRET,    // 25
        &NVirtualMachine::executeTRAP    // 26
    };
    
    // 指令执行循环
    while (m_pc < m_codeSize) {
        // 读取操作码
        uint8_t opcode = static_cast<uint8_t>(m_code[m_pc]);
        
        printf("PC: %d, Opcode: 0x%02X\n", m_pc, opcode);
        
        // 检查操作码是否有效
        if (opcode >= sizeof(handlers) / sizeof(handlers[0])) {
            printf("Error: Unknown opcode: 0x%02X\n", opcode);
            m_pc++;
            continue;
        }
        
        // 调用对应的处理函数
        (this->*handlers[opcode])();
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
    printf("flags: %d\n", m_flags);
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
NVirtualMachine::getFlags()
{
    return m_flags;
}

void
NVirtualMachine::setFlags(int32_t value)
{
    m_flags = value;
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
