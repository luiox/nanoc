#include <core.h>
#include <ctype.h>
#include <instructions.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string_helper.h>

void
instruct_none_handler(struct Nvm * vm, char * instruct, char * buffer)
{
}

void
instruct_imm_handler(struct Nvm * vm, char * instruct, char * buffer)
{
    // printf("instruct_imm_handler\n");
    if (NULL == vm || NULL == instruct || NULL == buffer)
        return;
    int32_t n = strlen(instruct);
    char reg[4] = { 0 };
    int32_t rn = 0;
    bool f = false;
    int32_t imm_num = 0;
    for (int32_t i = n + 1; i < strlen(buffer); i++) {
        if (isalpha(buffer[i]) && !f) {
            reg[rn++] = buffer[i];
        }
        if (buffer[i] == ',') {
            f = true;
        }
        if (isdigit(buffer[i])) {
            imm_num = imm_num * 10 + (buffer[i] - '0');
        }
        if (buffer[i] == '\n') {
            break;
        }
    }
    // printf("Vm in imm: %X\n", vm);
    // printf("reg: %s\n", reg);
    // printf("imm_num: %d\n", imm_num);
    if (nvm_strnicmp(reg, "ax", 2) == 0) {
        // printf("ax in\n");
        vm->ax = imm_num;
    }
    else if (nvm_strnicmp(reg, "bp", 2) == 0) {
        vm->bp = imm_num;
    }
    else if (nvm_strnicmp(reg, "sp", 2) == 0) {
        vm->sp = imm_num;
    }
    else if (nvm_strnicmp(reg, "pc", 2) == 0) {
        vm->pc = imm_num;
    }
}

void
instruct_lea_handler(struct Nvm * vm, char * instruct, char * buffer)
{
}

void
instruct_push_handler(struct Nvm * vm, char * instruct, char * buffer)
{
}

void
instruct_pop_handler(struct Nvm * vm, char * instruct, char * buffer)
{
}

typedef void (*instruct_func)(struct Nvm *, char *, char *);

struct instruction_map_node {
    char * instruct;
    instruct_func func;
};

#define INSTRUCTION_NODE_GEN(name, func)                                                 \
    {                                                                                    \
        name, func                                                                       \
    }

struct instruction_map_node instruction_map[] = {
    INSTRUCTION_NODE_GEN("IMM", instruct_imm_handler),
    INSTRUCTION_NODE_GEN("NONE", instruct_none_handler),
};

void
instruct_run_handler(struct Nvm * vm, char * instruct, char * buffer)
{
    // printf("Vm in instruct_run_handler: %X\n", vm);
    for (int32_t i = 0;
         i < sizeof(instruction_map) / sizeof(struct instruction_map_node);
         i++) {
        // 忽略大小写进行比较
        if (nvm_stricmp(instruct, instruction_map[i].instruct) == 0) {
            instruction_map[i].func(vm, instruct, buffer);
            break;
        }
    }
}
