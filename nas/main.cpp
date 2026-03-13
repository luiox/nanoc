#include <algorithm>
#include <fstream>
#include <instructions.hpp>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <string_helper.hpp>
#include <vector>
using namespace std;

// NCO文件头结构
struct NCOHeader {
    char magic[4];      // "NCO\0"
    uint16_t version;   // 版本号 0x0001
    uint16_t flags;     // 标志位
    uint32_t codeSize;  // 代码段大小
    uint32_t entryPoint;// 入口点偏移
};

// 标签信息
struct LabelInfo {
    string name;
    int32_t address;    // 标签对应的地址
};

// 需要回填的跳转指令
struct JumpPatch {
    int32_t offset;     // 指令中地址字段的偏移
    string label;       // 标签名称
};

int main(int argc, char* argv[])
{
    // 解析命令行参数
    string inputFile;
    string outputFile;
    
    if (argc < 2) {
        cerr << "Usage: nas <input.nas> [output.nco]" << endl;
        return 1;
    }
    
    inputFile = argv[1];
    
    // 如果没有指定输出文件，使用输入文件名替换扩展名为.nco
    if (argc >= 3) {
        outputFile = argv[2];
    } else {
        outputFile = inputFile;
        size_t dotPos = outputFile.rfind('.');
        if (dotPos != string::npos) {
            outputFile = outputFile.substr(0, dotPos);
        }
        outputFile += ".nco";
    }
    
    cout << "NanoC Assembler (nas)" << endl;
    cout << "=====================" << endl;
    cout << "Input:  " << inputFile << endl;
    cout << "Output: " << outputFile << endl;
    cout << endl;
    
    ifstream ifile(inputFile);
    if (!ifile.is_open()) {
        cerr << "Error: Cannot open input file " << inputFile << endl;
        return 1;
    }
    
    // 第一遍：收集标签地址
    cout << "Pass 1: Collecting labels..." << endl;
    map<string, int32_t> labels;
    vector<pair<string, int>> rawLines; // 存储原始行和行号
    int32_t currentAddress = 0;
    int lineNumber = 0;
    
    string line;
    while (getline(ifile, line)) {
        lineNumber++;
        
        // 去除头尾的制表符和空格
        trim(line);
        if (line.empty()) continue;
        if (line[0] == ';') continue;
        
        // 去除注释
        auto semicolonPos = line.find(';');
        if (semicolonPos != string::npos) {
            line = line.substr(0, semicolonPos);
            trim(line);
        }
        
        if (line.empty()) continue;
        
        // 检查是否是标签定义（以冒号结尾）
        if (line.back() == ':') {
            string labelName = line.substr(0, line.size() - 1);
            trim(labelName);
            labels[labelName] = currentAddress;
            cout << "  Label: " << labelName << " -> " << currentAddress << endl;
            continue;
        }
        
        // 存储这一行用于第二遍处理
        rawLines.push_back({line, lineNumber});
        
        // 计算指令大小
        // 找出指令的操作码
        size_t i;
        for (i = 0; i < line.size(); ++i) {
            if (!isalpha(line[i])) {
                break;
            }
        }
        string opcode = line.substr(0, i);
        
        // 根据操作码计算指令大小
        if (opcode == "ret" || opcode == "nop") {
            currentAddress += 1; // 无操作数指令
        } else if (opcode == "push" || opcode == "pop" || opcode == "not") {
            currentAddress += 2; // 单寄存器指令
        } else if (opcode == "trap") {
            currentAddress += 2; // trap指令
        } else if (opcode == "jmp" || opcode == "jic" || opcode == "call") {
            currentAddress += 5; // 跳转指令
        } else {
            currentAddress += 6; // 寄存器+立即数指令
        }
    }
    
    cout << "Total code size: " << currentAddress << " bytes" << endl;
    cout << endl;
    
    // 第二遍：生成二进制代码
    cout << "Pass 2: Generating bytecode..." << endl;
    vector<uint8_t> code;
    vector<JumpPatch> jumpPatches;
    
    for (const auto& [rawLine, lineNum] : rawLines) {
        string line = rawLine;
        
        // 找出指令的操作码
        size_t i;
        for (i = 0; i < line.size(); ++i) {
            if (!isalpha(line[i])) {
                break;
            }
        }
        string opcode = line.substr(0, i);
        
        // 调用生成器来从字符串构造对应的对象
        auto it = g_opcodeToGeneratorMap.find(opcode);
        if (it == g_opcodeToGeneratorMap.end()) {
            cerr << "Error at line " << lineNum << ": Unknown opcode '" << opcode << "'" << endl;
            return 1;
        }
        
        auto instruct = it->second(line);
        if (instruct == nullptr) {
            cerr << "Error at line " << lineNum << ": Failed to parse instruction" << endl;
            return 1;
        }
        
        auto gencode = instruct->generateInstructionCode();
        
        // 检查是否是跳转指令，需要处理标签
        if (opcode == "jmp" || opcode == "jic" || opcode == "call") {
            // 提取目标标签
            string rest = line.substr(i);
            trim(rest);
            
            // 检查是否是标签引用（不是数字）
            if (!rest.empty() && !isdigit(rest[0]) && rest[0] != '-') {
                // 是标签引用，记录需要回填
                JumpPatch patch;
                patch.offset = code.size() + 1; // 跳过opcode
                patch.label = rest;
                jumpPatches.push_back(patch);
                
                // 先写入opcode
                code.push_back(gencode[0]);
                // 写入占位符地址
                code.push_back(0);
                code.push_back(0);
                code.push_back(0);
                code.push_back(0);
            } else {
                // 是数字地址，直接写入
                code.insert(code.end(), gencode.begin(), gencode.end());
            }
        } else {
            // 普通指令，直接写入
            code.insert(code.end(), gencode.begin(), gencode.end());
        }
        
        delete instruct;
    }
    
    // 回填跳转地址
    cout << "Patching jump addresses..." << endl;
    for (const auto& patch : jumpPatches) {
        auto it = labels.find(patch.label);
        if (it == labels.end()) {
            cerr << "Error: Undefined label '" << patch.label << "'" << endl;
            return 1;
        }
        
        int32_t target = it->second;
        code[patch.offset] = (target >> 0) & 0xFF;
        code[patch.offset + 1] = (target >> 8) & 0xFF;
        code[patch.offset + 2] = (target >> 16) & 0xFF;
        code[patch.offset + 3] = (target >> 24) & 0xFF;
        
        cout << "  " << patch.label << " -> " << target << endl;
    }
    
    // 查找入口点（main标签）
    int32_t entryPoint = 0;
    auto mainIt = labels.find("main");
    if (mainIt != labels.end()) {
        entryPoint = mainIt->second;
    }
    cout << "Entry point: " << entryPoint << endl;
    cout << endl;
    
    // 写入NCO文件
    cout << "Writing NCO file..." << endl;
    ofstream ofile(outputFile, ios::binary);
    if (!ofile.is_open()) {
        cerr << "Error: Cannot create output file " << outputFile << endl;
        return 1;
    }
    
    // 写入文件头
    NCOHeader header;
    header.magic[0] = 'N';
    header.magic[1] = 'C';
    header.magic[2] = 'O';
    header.magic[3] = '\0';
    header.version = 0x0001;
    header.flags = 0;
    header.codeSize = code.size();
    header.entryPoint = entryPoint;
    
    ofile.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    // 写入代码段
    ofile.write(reinterpret_cast<const char*>(code.data()), code.size());
    ofile.close();
    
    cout << "Success! Generated " << outputFile << " (" << sizeof(header) + code.size() << " bytes)" << endl;
    
    return 0;
}
