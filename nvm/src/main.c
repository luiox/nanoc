#include <core.h>
#include <instructions.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void
help_handler()
{
    printf("Usage: nvm <input file> [options]\n");
    printf("Example: nvm test.nca\n");
    // 现在暂时没有Option
    // printf("Option: \n");
}

void
run_file_handler(char * files)
{
    // 输出文件内容，用于测试是否正确读入
    printf("%s\n", files);
}

int
main(int argc, char * argv[])
{
    // 根据传入参数的个数来进行不同情况处理
    if (argc == 1) {
        // 这种情况是直接运行，或者是没有指定任何命令行参数
        // 输出使用说明
        help_handler();
        exit(EXIT_FAILURE);
    }
    else if (argc == 2) {
        // 单独运行一个nca文件的情况
        struct Nvm vm;
        // 初始化虚拟机
        Nvm_init(&vm, 1024 * 1024);// 默认1MB的栈大小
        // 装载汇编文件
        Nvm_load_file(&vm, argv[1]);
        // printf("Vm in main: %X\n", &vm);
        // 运行虚拟机
        Nvm_run(&vm);
        // 打印调试信息
        Nvm_print_info(&vm);
        // 运行完，清理内存
        Nvm_destroy(&vm);
    }
    else {
    }
    return EXIT_SUCCESS;
}