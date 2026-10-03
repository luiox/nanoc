#include "nvm/core.hpp"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
    // --Xss 容量单位进率
    constexpr int32_t KIB = 1024;

    void
    help_handler()
    {
        // 用法属于错误路径输出（仅在参数不足时打印），走 stderr
        fprintf(stderr, "Usage: nvm <input file> [options]\n");
        fprintf(stderr, "Options: --Xss set stack size(b,k,m)\n");
        fprintf(stderr,
                "         --host-lib <path> load host library for dynamic imports\n");
        fprintf(stderr, "                 (repeatable, e.g. --host-lib msvcrt.dll)\n");
        fprintf(stderr,
                "Example: nvm test.nci --host-lib C:/Windows/System32/msvcrt.dll\n");
    }
} // namespace

int
main(int argc, char * argv[])
{
    if (argc < 2) {
        help_handler();
        return 1;
    }

    std::string filename = argv[1];
    int32_t stackSize = DEFAULT_STACK_SIZE;
    std::vector<std::string> hostLibs;

    // 解析命令行参数
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host-lib") {
            // 动态导入宿主库（可多次，按序解析）
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --host-lib missing library path\n");
                return 1;
            }
            hostLibs.push_back(argv[++i]);
            continue;
        }
        if (arg.rfind("--host-lib=", 0) == 0) {
            hostLibs.push_back(arg.substr(strlen("--host-lib=")));
            continue;
        }
        if (arg.find("--Xss=") == 0) {
            std::string sizeStr = arg.substr(strlen("--Xss="));
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
                stackSize = size * KIB;
                break;
            case 'm':
            case 'M':
                stackSize = size * KIB * KIB;
                break;
            default:
                fprintf(stderr, "Error: Invalid stack size unit '%c'\n", unit);
                return 1;
            }
        }
    }

    printf("NanoC Virtual Machine (nvm)\n");
    printf("===========================\n");
    printf("Input file: %s\n", filename.c_str());
    printf("Stack size: %d bytes\n", stackSize);
    for (const auto & lib : hostLibs)
        printf("Host library: %s\n", lib.c_str());
    printf("\n");

    // 创建虚拟机实例
    NVirtualMachine vm(stackSize);

    try {
        // 加载字节码文件
        vm.load(filename);

        // 宿主库解析动态导入（load 后 start 前）：按符号名解析并登记在
        // 导入表伪地址上，未解析/无包装器则失败退出
        for (const auto & lib : hostLibs) {
            if (!vm.loadHostLibrary(lib)) {
                fprintf(stderr,
                        "Error: failed to resolve dynamic imports from '%s'\n",
                        lib.c_str());
                return 1;
            }
        }

        // 执行程序
        vm.start();
    }
    catch (const std::exception & e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}