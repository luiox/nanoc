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
        trim(line);
        // 正则表达式匹配
        std::string pattern = "^[a-zA-Z]+";
        smatch match;
        std::regex_search(line, match, std::regex(pattern));

        // 检查是否找到匹配
        if (match.empty()) {
            std::cerr << "empty line" << std::endl;
            // return 1;
            continue;
        }
        std::string opcode = match[0];
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
