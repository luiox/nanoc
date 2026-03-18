#include <core.hpp>
#include <ctype.h>
#include <instructions.hpp>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string_helper.hpp>

std::map<std::string, NRegister> g_textToRegisterMap = {
    { "R0", NRegister::R0 }, { "R1", NRegister::R1 }, { "R2", NRegister::R2 },
    { "R3", NRegister::R3 }, { "R4", NRegister::R4 }, { "R5", NRegister::R5 },
    { "R6", NRegister::R6 }, { "R7", NRegister::R7 }
};

std::map<std::string, NInstructionsInterface * (*)(std::string & text)>
  g_opcodeToGeneratorMap = { 
    { "lmm", &NInstructionsLMM::parserInstructionText }, 
  { "st", &NInstructionsST::parserInstructionText }, 
  { "lea", &NInstructionsLEA::parserInstructionText }, 
  { "add", &NInstructionsADD::parserInstructionText }, 
  { "sub", &NInstructionsSUB::parserInstructionText }, 
  { "mul", &NInstructionsMUL::parserInstructionText }, 
  { "div", &NInstructionsDIV::parserInstructionText },
    { "not", &NInstructionsNOT::parserInstructionText },
  { "and", &NInstructionsAND::parserInstructionText },  
  { "mod", &NInstructionsMOD::parserInstructionText }, 
  { "or", &NInstructionsOR::parserInstructionText }, 
  { "xor", &NInstructionsXOR::parserInstructionText }, 
  { "shl", &NInstructionsSHL::parserInstructionText }, 
  { "shr", &NInstructionsSHR::parserInstructionText }, 
  { "eq", &NInstructionsEQ::parserInstructionText }, 
  { "ne", &NInstructionsNE::parserInstructionText }, 
  { "lt", &NInstructionsLT::parserInstructionText }, 
  { "le", &NInstructionsLE::parserInstructionText }, 
  { "gt", &NInstructionsGT::parserInstructionText }, 
  { "ge", &NInstructionsGE::parserInstructionText }, 
  { "push", &NInstructionsPUSH::parserInstructionText }, 
  { "pop", &NInstructionsPOP::parserInstructionText }, 
  { "jmp", &NInstructionsJMP::parserInstructionText }, 
  { "jic", &NInstructionsJIC::parserInstructionText }, 
  { "call", &NInstructionsCALL::parserInstructionText }, 
  { "ret", &NInstructionsRET::parserInstructionText }, 
  { "trap", &NInstructionsTRAP::parserInstructionText }
};

NInstructionsLMM::NInstructionsLMM(std::string reg, int32_t val)
  : m_reg(g_textToRegisterMap[reg])
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsLMM::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::LMM));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(m_val);
    return code;
}

std::string
NInstructionsLMM::generateInstructionName()
{
    return "lmm";
}

NInstructionsInterface *
NInstructionsLMM::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    // 先去除opcode的字符串
    auto opcodePos = ins.find("lmm");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("lmm"), "");
    trim(ins);
    // 剩下的，找到逗号，然后分割为两个字符串
    auto pos = ins.find(",");
    if (pos == std::string::npos) {
        return nullptr;
    }
    std::string reg = ins.substr(0, pos - 1);
    std::string num = ins.substr(pos + 1);
    trim(reg);
    trim(num);

    if (!reg.empty() && !num.empty()) {
        int32_t val;
        if (num.size() > 2 && num.substr(0, 2) == "0x") {
            // num是十六进制数，需要转换为无符号字符
            val = std::stoi(num, nullptr, 16);
        }
        else {
            val = std::stoi(num, nullptr, 10);
        }

        return new NInstructionsLMM(reg, val);
    }

    return nullptr;
}

NInstructionsInterface *
NInstructionsST::parserInstructionText(std::string & text)
{
    // TODO: 实现ST指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsLEA::parserInstructionText(std::string & text)
{
    // TODO: 实现LEA指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsMUL::parserInstructionText(std::string & text)
{
    // TODO: 实现MUL指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsDIV::parserInstructionText(std::string & text)
{
    // TODO: 实现DIV指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsMOD::parserInstructionText(std::string & text)
{
    // TODO: 实现MOD指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsNOT::parserInstructionText(std::string & text)
{
    // TODO: 实现NOT指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsAND::parserInstructionText(std::string & text)
{
    // TODO: 实现AND指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsOR::parserInstructionText(std::string & text)
{
    // TODO: 实现OR指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsXOR::parserInstructionText(std::string & text)
{
    // TODO: 实现XOR指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsSHL::parserInstructionText(std::string & text)
{
    // TODO: 实现SHL指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsSHR::parserInstructionText(std::string & text)
{
    // TODO: 实现SHR指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsEQ::parserInstructionText(std::string & text)
{
    // TODO: 实现EQ指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsNE::parserInstructionText(std::string & text)
{
    // TODO: 实现NE指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsLT::parserInstructionText(std::string & text)
{
    // TODO: 实现LT指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsLE::parserInstructionText(std::string & text)
{
    // TODO: 实现LE指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsGT::parserInstructionText(std::string & text)
{
    // TODO: 实现GT指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsGE::parserInstructionText(std::string & text)
{
    // TODO: 实现GE指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsPUSH::parserInstructionText(std::string & text)
{
    // TODO: 实现PUSH指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsPOP::parserInstructionText(std::string & text)
{
    // TODO: 实现POP指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsJMP::parserInstructionText(std::string & text)
{
    // TODO: 实现JMP指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsJIC::parserInstructionText(std::string & text)
{
    // TODO: 实现JIC指令解析
    return nullptr;
}

NInstructionsInterface *
NInstructionsTRAP::parserInstructionText(std::string & text)
{
    // TODO: 实现TRAP指令解析
    return nullptr;
}
NInstructionsADD::NInstructionsADD(std::string reg, int32_t val)
  : m_reg(g_textToRegisterMap[reg])
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsADD::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::ADD));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(m_val);
    return code;
}

std::string
NInstructionsADD::generateInstructionName()
{
    return "add";
}

NInstructionsInterface *
NInstructionsADD::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    // 先去除opcode的字符串
    auto opcodePos = ins.find("add");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("add"), "");
    trim(ins);
    // 剩下的，找到逗号，然后分割为两个字符串
    auto pos = ins.find(",");
    if (pos == std::string::npos) {
        return nullptr;
    }
    std::string reg = ins.substr(0, pos - 1);
    std::string num = ins.substr(pos + 1);
    trim(reg);
    trim(num);

    if (!reg.empty() && !num.empty()) {
        int32_t val;
        if (num.size() > 2 && num.substr(0, 2) == "0x") {
            // num是十六进制数，需要转换为无符号字符
            val = std::stoi(num, nullptr, 16);
        }
        else {
            val = std::stoi(num, nullptr, 10);
        }

        return new NInstructionsADD(reg, val);
    }

    return nullptr;
}
NInstructionsSUB::NInstructionsSUB(std::string reg, int32_t val)
  : m_reg(g_textToRegisterMap[reg])
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsSUB::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::SUB));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(m_val);
    return code;
}

std::string
NInstructionsSUB::generateInstructionName()
{
    return "sub";
}

NInstructionsInterface *
NInstructionsSUB::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    // 先去除opcode的字符串
    auto opcodePos = ins.find("sub");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("sub"), "");
    trim(ins);
    // 剩下的，找到逗号，然后分割为两个字符串
    auto pos = ins.find(",");
    if (pos == std::string::npos) {
        return nullptr;
    }
    std::string reg = ins.substr(0, pos - 1);
    std::string num = ins.substr(pos + 1);
    trim(reg);
    trim(num);

    if (!reg.empty() && !num.empty()) {
        int32_t val;
        if (num.size() > 2 && num.substr(0, 2) == "0x") {
            // num是十六进制数，需要转换为无符号字符
            val = std::stoi(num, nullptr, 16);
        }
        else {
            val = std::stoi(num, nullptr, 10);
        }

        return new NInstructionsSUB(reg, val);
    }

    return nullptr;
}

NInstructionsCALL::NInstructionsCALL(std::string target)
  : m_target(target)
{
}

std::vector<uint8_t>
NInstructionsCALL::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::CALL));
    // 注意：这里暂时只生成操作码，目标地址需要在链接时确定
    // 实际实现中需要支持标签解析
    return code;
}

std::string
NInstructionsCALL::generateInstructionName()
{
    return "call";
}

NInstructionsInterface *
NInstructionsCALL::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    // 先去除opcode的字符串
    auto opcodePos = ins.find("call");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("call"), "");
    trim(ins);
    
    if (!ins.empty()) {
        return new NInstructionsCALL(ins);
    }

    return nullptr;
}

NInstructionsRET::NInstructionsRET()
{
}

std::vector<uint8_t>
NInstructionsRET::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::RET));
    return code;
}

std::string
NInstructionsRET::generateInstructionName()
{
    return "ret";
}

NInstructionsInterface *
NInstructionsRET::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    trim(ins);
    
    if (ins == "ret") {
        return new NInstructionsRET();
    }

    return nullptr;
}

// void
// instruct_none_handler(struct Nvm * vm, char * instruct, char * buffer)
// {
// }

// void
// instruct_imm_handler(struct Nvm * vm, char * instruct, char * buffer)
// {
//     // printf("instruct_imm_handler\n");
//     if (NULL == vm || NULL == instruct || NULL == buffer)
//         return;
//     int32_t n = strlen(instruct);
//     char reg[4] = { 0 };
//     int32_t rn = 0;
//     bool f = false;
//     int32_t imm_num = 0;
//     for (int32_t i = n + 1; i < strlen(buffer); i++) {
//         if (isalpha(buffer[i]) && !f) {
//             reg[rn++] = buffer[i];
//         }
//         if (buffer[i] == ',') {
//             f = true;
//         }
//         if (isdigit(buffer[i])) {
//             imm_num = imm_num * 10 + (buffer[i] - '0');
//         }
//         if (buffer[i] == '\n') {
//             break;
//         }
//     }
//     // printf("Vm in imm: %X\n", vm);
//     // printf("reg: %s\n", reg);
//     // printf("imm_num: %d\n", imm_num);
//     if (nvm_strnicmp(reg, "ax", 2) == 0) {
//         // printf("ax in\n");
//         vm->ax = imm_num;
//     }
//     else if (nvm_strnicmp(reg, "bp", 2) == 0) {
//         vm->bp = imm_num;
//     }
//     else if (nvm_strnicmp(reg, "sp", 2) == 0) {
//         vm->sp = imm_num;
//     }
//     else if (nvm_strnicmp(reg, "pc", 2) == 0) {
//         vm->pc = imm_num;
//     }
// }

// void
// instruct_lea_handler(struct Nvm * vm, char * instruct, char * buffer)
// {
// }

// void
// instruct_push_handler(struct Nvm * vm, char * instruct, char * buffer)
// {
// }

// void
// instruct_pop_handler(struct Nvm * vm, char * instruct, char * buffer)
// {
// }

// typedef void (*instruct_func)(struct Nvm *, char *, char *);

// struct instruction_map_node {
//     char * instruct;
//     instruct_func func;
// };

// #define INSTRUCTION_NODE_GEN(name, func)                                                 \
//     {                                                                                    \
//         name, func                                                                       \
//     }

// struct instruction_map_node instruction_map[] = {
//     INSTRUCTION_NODE_GEN("IMM", instruct_imm_handler),
//     INSTRUCTION_NODE_GEN("NONE", instruct_none_handler),
// };

// void
// instruct_run_handler(struct Nvm * vm, char * instruct, char * buffer)
// {
//     // printf("Vm in instruct_run_handler: %X\n", vm);
//     for (int32_t i = 0;
//          i < sizeof(instruction_map) / sizeof(struct instruction_map_node);
//          i++) {
//         // 忽略大小写进行比较
//         if (nvm_stricmp(instruct, instruction_map[i].instruct) == 0) {
//             instruction_map[i].func(vm, instruct, buffer);
//             break;
//         }
//     }
// }
