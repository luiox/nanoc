#ifndef NVM_CORE_H
#define NVM_CORE_H

#include <stdint.h>

struct Nvm {
    int32_t pc;
    int32_t sp;
    int32_t bp;
    int32_t ax;
    int8_t * stack;
    int8_t * code;
    int64_t stack_size;
    int64_t code_size;
};

void Nvm_init(struct Nvm * vm, int64_t stack_size);

void Nvm_destroy(struct Nvm * vm);

void Nvm_load_file(struct Nvm * vm, char * file_name);

void Nvm_run(struct Nvm * vm);

void Nvm_print_info(struct Nvm * vm);

void Nvm_print_stack_info(struct Nvm * vm, int64_t start, int64_t end);

#endif // !NVM_CORE_H
