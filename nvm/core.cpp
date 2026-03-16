#include "core.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

NVirtualMachine::NVirtualMachine(int32_t stackSize)
{
    m_stack = (int8_t *)malloc(stackSize);
    m_stackSize = stackSize;
    m_codeSize = 0;
    m_ax = m_bp = m_flags = m_pc = 0;
    m_code = NULL;
    m_sp = stackSize;
    for (int i = 0; i < 8; i++)
        m_registers[i] = 0;
}

NVirtualMachine::~NVirtualMachine() { free(m_stack); }

void
NVirtualMachine::load(std::string filename)
{
    FILE * pf = fopen(filename.c_str(), "rb");
    if (!pf) {
        printf("Error: Cannot open %s\n", filename.c_str());
        exit(1);
    }
    fseek(pf, 0, SEEK_END);
    int64_t size = ftell(pf);
    int8_t * data = (int8_t *)malloc(size);
    fseek(pf, 0, SEEK_SET);
    fread(data, 1, size, pf);
    fclose(pf);
    if (size >= 32 && data[0] == 'N' && data[1] == 'a' && data[2] == 'n' && data[3] == 'o'
        && data[4] == 'C') {
        m_codeSize = *(int32_t *)&data[12];
        m_pc = *(int32_t *)&data[28];
        m_code = (int8_t *)malloc(m_codeSize);
        memcpy(m_code, data + 32, m_codeSize);
    }
    else {
        m_codeSize = size;
        m_code = data;
        m_pc = 0;
        data = NULL;
    }
    if (data)
        free(data);
}

void
NVirtualMachine::start()
{
    if (!m_code || !m_stack)
        return;
    typedef void (NVirtualMachine::*H)();
    H h[256] = {};
    h[0x00] = &NVirtualMachine::executeLMM;
    h[0x01] = &NVirtualMachine::executeST;
    h[0x02] = &NVirtualMachine::executeLEA;
    h[0x03] = &NVirtualMachine::executeLOAD;
    h[0x04] = &NVirtualMachine::executeSTORE;
    h[0x10] = &NVirtualMachine::executeADD;
    h[0x11] = &NVirtualMachine::executeADDI;
    h[0x12] = &NVirtualMachine::executeSUB;
    h[0x13] = &NVirtualMachine::executeSUBI;
    h[0x14] = &NVirtualMachine::executeMUL;
    h[0x15] = &NVirtualMachine::executeMULI;
    h[0x16] = &NVirtualMachine::executeDIV;
    h[0x17] = &NVirtualMachine::executeDIVI;
    h[0x18] = &NVirtualMachine::executeMOD;
    h[0x19] = &NVirtualMachine::executeMODI;
    h[0x1A] = &NVirtualMachine::executeNOT;
    h[0x1B] = &NVirtualMachine::executeNEG;
    h[0x20] = &NVirtualMachine::executeAND;
    h[0x22] = &NVirtualMachine::executeOR;
    h[0x24] = &NVirtualMachine::executeXOR;
    h[0x26] = &NVirtualMachine::executeSHL;
    h[0x28] = &NVirtualMachine::executeSHR;
    h[0x30] = &NVirtualMachine::executeCMP;
    h[0x31] = &NVirtualMachine::executeCMPI;
    h[0x32] = &NVirtualMachine::executeTEST;
    h[0x40] = &NVirtualMachine::executePUSH;
    h[0x41] = &NVirtualMachine::executePOP;
    h[0x43] = &NVirtualMachine::executeENTER;
    h[0x44] = &NVirtualMachine::executeLEAVE;
    h[0x50] = &NVirtualMachine::executeJMP;
    h[0x51] = &NVirtualMachine::executeJZ;
    h[0x52] = &NVirtualMachine::executeJNZ;
    h[0x60] = &NVirtualMachine::executeCALL;
    h[0x61] = &NVirtualMachine::executeCALLX;
    h[0x62] = &NVirtualMachine::executeRET;
    h[0x70] = &NVirtualMachine::executeMOV;
    h[0x71] = &NVirtualMachine::executeCLR;
    h[0x7F] = &NVirtualMachine::executeNOP;
    while (m_pc < m_codeSize) {
        uint8_t op = m_code[m_pc];
        if (h[op])
            (this->*h[op])();
        else {
            printf("Unknown op 0x%02X at %d\n", op, m_pc);
            break;
        }
    }
}

void
NVirtualMachine::print_info()
{
    printf("PC=%d SP=%d BP=%d AX=%d F=%d\n", m_pc, m_sp, m_bp, m_ax, m_flags);
}

void
NVirtualMachine::print_stack(int32_t s, int32_t e)
{
    for (int i = s; i < e; i += 4)
        printf("[%04X]=%d\n", i, *(int32_t *)&m_stack[i]);
}

int32_t
NVirtualMachine::getRegister(int32_t i)
{
    return i < 8 ? m_registers[i] : 0;
}
void
NVirtualMachine::setRegister(int32_t i, int32_t v)
{
    if (i < 8)
        m_registers[i] = v;
}
int32_t
NVirtualMachine::getPC()
{
    return m_pc;
}
void
NVirtualMachine::setPC(int32_t v)
{
    m_pc = v;
}
int32_t
NVirtualMachine::getSP()
{
    return m_sp;
}
void
NVirtualMachine::setSP(int32_t v)
{
    m_sp = v;
}
int32_t
NVirtualMachine::getBP()
{
    return m_bp;
}
void
NVirtualMachine::setBP(int32_t v)
{
    m_bp = v;
}
int32_t
NVirtualMachine::getAX()
{
    return m_ax;
}
void
NVirtualMachine::setAX(int32_t v)
{
    m_ax = v;
}
int32_t
NVirtualMachine::getFlags()
{
    return m_flags;
}
void
NVirtualMachine::setFlags(int32_t v)
{
    m_flags = v;
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
int8_t *
NVirtualMachine::getStack()
{
    return m_stack;
}
int8_t *
NVirtualMachine::getCode()
{
    return m_code;
}
