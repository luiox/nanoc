#ifndef NVM_CORE_H
#define NVM_CORE_H

#include <instructions.hpp>
#include <stdint.h>
#include <string>

constexpr int32_t DEFAULT_STACK_SIZE = 8 * 1024 * 1024;

class NVirtualMachine
{
public:
    NVirtualMachine(int32_t stackSize = DEFAULT_STACK_SIZE);
    ~NVirtualMachine();

    void load(std::string filename);

    void start();

    void print_info();

    void print_stack(int32_t start, int32_t end);

    // 获取寄存器值
    int32_t getRegister(int32_t regIndex);

    // 设置寄存器值
    void setRegister(int32_t regIndex, int32_t value);

    // 获取PC值
    int32_t getPC();

    // 设置PC值
    void setPC(int32_t value);

    // 获取SP值
    int32_t getSP();

    // 设置SP值
    void setSP(int32_t value);

    // 获取BP值
    int32_t getBP();

    // 设置BP值
    void setBP(int32_t value);

    // 获取AX值
    int32_t getAX();

    // 设置AX值
    void setAX(int32_t value);

    // 获取flags值
    int32_t getFlags();

    // 设置flags值
    void setFlags(int32_t value);

    // 获取栈大小
    int32_t getStackSize();

    // 获取代码大小
    int64_t getCodeSize();

    // 获取栈指针
    int8_t * getStack();

    // 获取代码指针
    int8_t * getCode();

    // 指令执行
    void executeLMM();
    void executeST();
    void executeLEA();
    void executeLOAD();
    void executeSTORE();
    void executeADD();
    void executeADDI();
    void executeSUB();
    void executeSUBI();
    void executeMUL();
    void executeMULI();
    void executeDIV();
    void executeDIVI();
    void executeMOD();
    void executeMODI();
    void executeNOT();
    void executeNEG();
    void executeCMP();
    void executeCMPI();
    void executeTEST();
    void executeAND();
    void executeOR();
    void executeXOR();
    void executeSHL();
    void executeSHR();
    void executePUSH();
    void executePOP();
    void executeENTER();
    void executeLEAVE();
    void executeJMP();
    void executeJZ();
    void executeJNZ();
    void executeCALL();
    void executeCALLX();
    void executeRET();
    void executeMOV();
    void executeCLR();
    void executeNOP();

private:
    int32_t m_pc;
    int32_t m_sp;
    int32_t m_bp;
    int32_t m_ax;
    int32_t m_flags;
    int32_t m_registers[8];
    int8_t * m_stack;
    int8_t * m_code;
    int32_t m_stackSize;
    int64_t m_codeSize;
};
void Nvm_init(struct Nvm * vm, int64_t stack_size);

void Nvm_destroy(struct Nvm * vm);

void Nvm_load_file(struct Nvm * vm, char * file_name);

void Nvm_run(struct Nvm * vm);

void Nvm_print_info(struct Nvm * vm);

void Nvm_print_stack_info(struct Nvm * vm, int64_t start, int64_t end);

#endif // !NVM_CORE_H
