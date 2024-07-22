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
    // DEBUG_INFO("ins=%s",ins.c_str());
    // 先去除opcode的字符串
    ins.replace(ins.find("lmm"), strlen("lmm"), "");
    trim(ins);
    // DEBUG_INFO("ins=%s",ins.c_str());
    // 剩下的，找到逗号，然后分割为两个字符串
    auto pos = ins.find(",");
    // DEBUG_INFO("pos=%u",pos);
    std::string reg = ins.substr(0, pos - 1);
    std::string num = ins.substr(pos + 1);

    // DEBUG_INFO("reg=%s",reg.c_str());
    // DEBUG_INFO("num=%s",num.c_str());
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

    return nullptr;
}

NInstructionsInterface *
NInstructionsLEA::parserInstructionText(std::string & text)
{

    return nullptr;
}
NInstructionsInterface *
NInstructionsADD::parserInstructionText(std::string & text)
{

    return nullptr;
}
NInstructionsInterface *
NInstructionsSUB::parserInstructionText(std::string & text)
{

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
