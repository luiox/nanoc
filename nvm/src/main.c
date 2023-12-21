#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <instructions.h>

void
help_handler()
{
    printf("Usage: nvm <input file> [options]\n");
    printf("Example: nvm test.nca\n");
    // 现在暂时没有Option
    //printf("Option: \n");
}


void run_file_handler(char* files)
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
        FILE* pf = fopen(argv[1], "rb");
        if(pf == NULL) {
            printf("Error: Cannot open file %s\n", argv[1]);
            exit(EXIT_FAILURE);
        }
        // 计算文件大小
        fseek(pf,0,SEEK_END);
        int64_t file_size = ftell(pf);
        // 申请内存
        char* files = (char*)malloc(file_size);
        if(files == NULL) {
            printf("Error: Cannot allocate memory for file %s\n", argv[1]);
            exit(EXIT_FAILURE);
        }

        // 将文件内容全部读入
        fseek(pf,0,SEEK_SET);
        fread(files, sizeof(char), file_size, pf);
        fclose(pf);

        // 运行一个文件
        run_file_handler(files);

        // 运行完，清理内存
        free(files);
    }
    else {
        
    }
    return EXIT_SUCCESS;
}