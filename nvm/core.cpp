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
}
NVirtualMachine::~NVirtualMachine() { free(m_stack); }

void
NVirtualMachine::load(std::string filename)
{
    // if (NULL == vm || NULL == file_name)
    //     return;
    // FILE * pf = fopen(file_name, "rb");
    // if (pf == NULL) {
    //     printf("Error: Cannot open file %s\n", file_name);
    //     exit(EXIT_FAILURE);
    // }

    // // 计算文件大小
    // fseek(pf, 0, SEEK_END);
    // int64_t file_size = ftell(pf);
    // vm->code_size = file_size;

    // // 申请内存
    // int8_t * files = (int8_t *)malloc(file_size);
    // if (files == NULL) {
    //     printf("Error: Cannot allocate memory for file %s\n", file_name);
    //     exit(EXIT_FAILURE);
    // }

    // // 将文件内容全部读入
    // fseek(pf, 0, SEEK_SET);
    // fread(files, sizeof(int8_t), file_size, pf);
    // fclose(pf);

    // vm->code = files;
    // // debug code
    // printf("file info:\n%s", vm->code);
}

void
NVirtualMachine::start()
{
    // if (NULL == vm || NULL == vm->code || NULL == vm->stack)
    //     return;
    // // printf("Vm in Nvm_run: %X\n", vm);
    // char buffer[64] = { 0 };
    // char instruct[16] = { 0 };
    // int32_t n = 0;
    // for (int32_t i = 0; i < vm->code_size; i++) {
    //     if (vm->code[i] == '\n') {
    //         // 去除前后的空格
    //         nvm_trim(buffer);
    //         // 提取出指令
    //         for (int32_t j = 0; j < 16; j++) {
    //             if (buffer[j] == ' ') {
    //                 strncpy(instruct, buffer, j);
    //                 break;
    //             }
    //         }
    //         // 运行指令
    //         instruct_run_handler(vm, instruct, buffer);

    //         n = 0;
    //         // 清空指令缓冲区
    //         memset(buffer, 0, sizeof(buffer));
    //     }
    //     buffer[n++] = vm->code[i];
    // }

    // run_instruct_handler();
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
