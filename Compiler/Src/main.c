#include <stdio.h>

void help_handler()
{
    printf("Usage: ncc <input file> [options]\n");
    printf("compiler <input file> [options]\n");
}

int main(int argc, char* argv[])
{
    // 根据传入参数的个数来进行不同情况处理
    if(argc == 1) {
        // 这种情况是直接运行，或者是没有指定任何命令行参数
        // 输出使用说明
        help_handler();
    }
    else if(argc == 2) {

    }
    printf("Hello!\n");
    return 0;
}