#include <core.hpp>
#include <ctype.h>
#include <getopt.h>
#include <instructions.hpp>
#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void
help_handler()
{
    printf("Usage: nvm <input file> [options]\n");
    printf("Options: --Xss set stack size(b,k,m)\n");
    printf("Example: nvm test.nca --Xss=256k\n");
}

void
run_file_handler(char * files)
{
    // 输出文件内容，用于测试是否正确读入
    printf("%s\n", files);
}

extern char * optarg;

int
main(int argc, char * argv[])
{
    std::string ins = "lmm R0, 9";
    std::cout << "ins:" << ins << std::endl;
    auto it = NInstructionsLMM::parserInstructionText(ins);
    if (it == nullptr) {
        std::cout << "it is nullptr" << std::endl;
    }
    auto code = it->generateInstructionCode();
    for (auto byte : code) {
        printf("%02x ", byte);
    }
    // struct Nvm vm;
    // int64_t stack_size = 0;
    // char file[64]={0};
    // strcpy(file, argv[1]);
    // // 根据传入参数的个数来进行不同情况处理
    // if (argc == 1) {
    //     // 这种情况是直接运行，或者是没有指定任何命令行参数
    //     // 输出使用说明
    //     help_handler();
    //     exit(EXIT_FAILURE);
    // }else if (argc == 2) {
    //     // 这种情况是只有一个参数，那么就是默认运行参数
    //     stack_size = 1024*1024;
    // }
    // else{
    //     // 解析长参数
    //     int opt;
    //     char * Xss_value = NULL;

    //     static struct option long_options[] = { { "Xss", required_argument, 0, 'x' },
    //                                             { 0, 0, 0, 0 } };

    //     int option_index = 0;
    //     while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1)
    //     {
    //         switch (opt) {
    //         case 'x':
    //             Xss_value = optarg;
    //             printf("Xss value is %s\n", Xss_value);
    //             break;
    //         default:
    //             break;
    //         }
    //     }

    //     for(int32_t i = 0; i < strlen(Xss_value); i++){
    //         if (isdigit(Xss_value[i])) {
    //             stack_size = stack_size * 10 + (Xss_value[i] - '0');

    //         }
    //         if(Xss_value[i] == 'k'){
    //             stack_size = stack_size * 1024;

    //             break;
    //         }
    //         if (Xss_value[i] == 'm') {
    //             stack_size = stack_size * 1024*1024;

    //             break;
    //         }
    //         if(Xss_value[i] == 'b'){
    //             break;
    //         }
    //     }
    // }
    // //printf("stack_size value is %lld\n", stack_size);
    // // 初始化虚拟机
    // Nvm_init(&vm, stack_size); // 默认1MB的栈大小
    // // 装载汇编文件
    // Nvm_load_file(&vm, file);
    // // printf("Vm in main: %X\n", &vm);
    // // 运行虚拟机
    // Nvm_run(&vm);
    // // 打印调试信息
    // Nvm_print_info(&vm);
    // // 运行完，清理内存
    // Nvm_destroy(&vm);

    return EXIT_SUCCESS;
}