#include <algorithm>
#include <fstream>
#include <instructions.hpp>
#include <iostream>
#include <regex>
#include <string>
#include <string_helper.hpp>
using namespace std;

int
main()
{
    string fileName = "test.nsm";
    string binaryFileName = "test.nsm";
    ifstream ifile(fileName);
    ofstream ofile(binaryFileName);
    if (!ifile.is_open()) { // 检查文件是否成功打开
        std::cerr << "Error opening ifile." << std::endl;
        return 1;
    }
    if (!ofile.is_open()) { // 检查文件是否成功打开
        std::cerr << "Error opening ofile." << std::endl;
        return 1;
    }

    std::string line; // 用于存储读取的行
    int lineNumber = 0;
    while (std::getline(ifile, line)) { // 读取文件中的每一行
        lineNumber++;
        std::cout << "Line [" << lineNumber << "] : " << line
                  << std::endl; // 输出读取的行

        // 去除头尾的制表符和空格
        trim(line);
        if (line.empty())
            continue; // 如果是空行则忽略
        if (line[0] == ';')
            continue; // 如果是注释行，也忽略

        // 去除注释
        // 查找出字符串中分号的位置，如果有分号，则取开始到分号前为新的字符串
        auto semicolonPos = line.find(';');
        if (semicolonPos != string::npos) {
            line = line.substr(0, semicolonPos);
        }

        // 找出指令的范围
        size_t i;
        for (i = 0; i < line.size(); ++i) {
            if (!isalpha(line[i])) {
                break;
            }
        }
        
        std::string opcode = line.substr(0,i);
        // 调用生成器来从字符串构造对应的对象
        auto instruct = g_opcodeToGeneratorMap[opcode](line);
        if (instruct == nullptr) {
            cout << "指令出错, line:" << lineNumber << endl;
            break;
        }
        auto gencode = instruct->generateInstructionCode();
        // 写入二进制指令
        ofile.write((char *)gencode.data(), gencode.size());
    }

    return 0;
}
