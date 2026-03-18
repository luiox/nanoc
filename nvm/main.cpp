#include <core.hpp>
#include <ctype.h>
#include <instructions.hpp>
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
    // 测试指令解析
    std::string ins = "lmm R0, 9";
    std::cout << "Testing instruction: " << ins << std::endl;
    auto it = NInstructionsLMM::parserInstructionText(ins);
    if (it == nullptr) {
        std::cout << "Failed to parse instruction" << std::endl;
        return 1;
    }
    
    auto code = it->generateInstructionCode();
    std::cout << "Generated code: ";
    for (auto byte : code) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
    
    delete it;
    
    // 测试ADD指令
    std::string addIns = "add R1, 5";
    std::cout << "Testing instruction: " << addIns << std::endl;
    auto addIt = NInstructionsADD::parserInstructionText(addIns);
    if (addIt == nullptr) {
        std::cout << "Failed to parse ADD instruction" << std::endl;
        return 1;
    }
    
    auto addCode = addIt->generateInstructionCode();
    std::cout << "Generated code: ";
    for (auto byte : addCode) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
    
    delete addIt;
    
    // 测试CALL指令
    std::string callIns = "call my_function";
    std::cout << "Testing instruction: " << callIns << std::endl;
    auto callIt = NInstructionsCALL::parserInstructionText(callIns);
    if (callIt == nullptr) {
        std::cout << "Failed to parse CALL instruction" << std::endl;
        return 1;
    }
    
    auto callCode = callIt->generateInstructionCode();
    std::cout << "Generated code: ";
    for (auto byte : callCode) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
    
    delete callIt;
    
    // 测试RET指令
    std::string retIns = "ret";
    std::cout << "Testing instruction: " << retIns << std::endl;
    auto retIt = NInstructionsRET::parserInstructionText(retIns);
    if (retIt == nullptr) {
        std::cout << "Failed to parse RET instruction" << std::endl;
        return 1;
    }
    
    auto retCode = retIt->generateInstructionCode();
    std::cout << "Generated code: ";
    for (auto byte : retCode) {
        printf("%02x ", byte);
    }
    std::cout << std::endl;
    
    delete retIt;
    
    std::cout << "All tests passed!" << std::endl;
    return EXIT_SUCCESS;
}