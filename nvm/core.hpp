#ifndef NVM_CORE_H
#define NVM_CORE_H

#include <stdint.h>
#include <instructions.hpp>
#include <string>

constexpr int32_t DEFAULT_STACK_SIZE = 8*1024*1024;


class NVirtualMachine{
public:
    NVirtualMachine(int32_t stackSize = DEFAULT_STACK_SIZE);
    ~NVirtualMachine();

    void load(std::string filename);

    void start();

    void print_info();

    void print_stack(int32_t start, int32_t end);
private:
    int32_t m_pc;
    int32_t m_sp;
    int32_t m_bp;
    int32_t m_ax;
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
