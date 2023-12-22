#include <core.h>
#include <ctype.h>
#include <instructions.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_helper.h>


void
Nvm_init(struct Nvm * vm, int64_t stack_size)
{
    if (NULL == vm)
        return;
    vm->stack = (int8_t *)malloc(stack_size);
    vm->stack_size = stack_size;
    vm->code_size = 0;
    vm->ax = 0;
    vm->bp = 0;
    vm->code = NULL;
    vm->pc = 0;
}

void
Nvm_destroy(struct Nvm * vm)
{
    if (NULL == vm)
        return;

    if (NULL != vm->code) {
        free(vm->code);
        vm->code = NULL;
    }

    if (NULL != vm->stack) {
        free(vm->stack);
        vm->stack = NULL;
    }
}

void
Nvm_load_file(struct Nvm * vm, char * file_name)
{
    if (NULL == vm || NULL == file_name)
        return;
    FILE * pf = fopen(file_name, "rb");
    if (pf == NULL) {
        printf("Error: Cannot open file %s\n", file_name);
        exit(EXIT_FAILURE);
    }

    // 计算文件大小
    fseek(pf, 0, SEEK_END);
    int64_t file_size = ftell(pf);
    vm->code_size = file_size;

    // 申请内存
    int8_t * files = (int8_t *)malloc(file_size);
    if (files == NULL) {
        printf("Error: Cannot allocate memory for file %s\n", file_name);
        exit(EXIT_FAILURE);
    }

    // 将文件内容全部读入
    fseek(pf, 0, SEEK_SET);
    fread(files, sizeof(int8_t), file_size, pf);
    fclose(pf);
    
    vm->code = files;
    // debug code
    printf("file info:\n%s", vm->code);
}

void
Nvm_run(struct Nvm * vm)
{
    if (NULL == vm || NULL == vm->code || NULL == vm->stack)
        return;
    // printf("Vm in Nvm_run: %X\n", vm);
    char buffer[64] = { 0 };
    char instruct[16] = { 0 };
    int32_t n = 0;
    for (int32_t i = 0; i < vm->code_size; i++) {
        if (vm->code[i] == '\n') {
            // 去除前后的空格
            nvm_trim(buffer);
            // 提取出指令
            for (int32_t j = 0; j < 16; j++) {
                if (buffer[j] == ' ') {
                    strncpy(instruct, buffer, j);
                    break;
                }
            }
            // 运行指令
            instruct_run_handler(vm,instruct,buffer);

              n = 0;
            // 清空指令缓冲区
            memset(buffer, 0, sizeof(buffer));
        }
        buffer[n++] = vm->code[i];
    }

    // run_instruct_handler();
}

void
Nvm_print_info(struct Nvm * vm)
{
    if (NULL == vm)
        return;
    // printf("Vm in Nvm_print_info: %X\n", vm);
    printf("Nvm current infomation:\n");
    printf("stack_size: %ld bytes\n", vm->stack_size);
    printf("bp: %ld\n", vm->bp);
    printf("pc: %ld\n", vm->pc);
    printf("ax: %ld\n", vm->ax);
    printf("sp: %ld\n", vm->sp);
}

void
Nvm_print_stack_info(struct Nvm * vm, int64_t start, int64_t end)
{
    if (NULL == vm)
        return;
    printf("Nvm current stack infomation from %X to %X:\n", start, end);

    for (int8_t * i = vm->stack + start; i < vm->stack + end; i++) {
        printf("%X ", *i);
        if ((i - vm->stack) % 16 == 0)
            printf("\n");
    }
}
