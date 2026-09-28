#include "nvm/core.hpp"
#include "nvm/instructions.hpp"
#include <ctype.h>
#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
help_handler()
{
    printf("Usage: nvm <input file> [options]\n");
    printf("Options: --Xss set stack size(b,k,m)\n");
    printf("Example: nvm test.nca --Xss=256k\n");
}

int
main(int argc, char * argv[])
{
    if (argc < 2) {
        help_handler();
        return 1;
    }
    
    std::string filename = argv[1];
    int32_t stackSize = DEFAULT_STACK_SIZE;
    
    // 解析命令行参数
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--Xss=") == 0) {
            std::string sizeStr = arg.substr(6);
            char unit = sizeStr.back();
            sizeStr.pop_back();
            int32_t size = std::stoi(sizeStr);
            
            switch (unit) {
                case 'b':
                case 'B':
                    stackSize = size;
                    break;
                case 'k':
                case 'K':
                    stackSize = size * 1024;
                    break;
                case 'm':
                case 'M':
                    stackSize = size * 1024 * 1024;
                    break;
                default:
                    printf("Error: Invalid stack size unit '%c'\n", unit);
                    return 1;
            }
        }
    }
    
    printf("NanoC Virtual Machine (nvm)\n");
    printf("===========================\n");
    printf("Input file: %s\n", filename.c_str());
    printf("Stack size: %d bytes\n", stackSize);
    printf("\n");
    
    // 创建虚拟机实例
    NVirtualMachine vm(stackSize);
    
    // 加载字节码文件
    vm.load(filename);
    
    // 执行程序
    vm.start();
    
    return 0;
}