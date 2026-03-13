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
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
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
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("st");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("st"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsST(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsLEA::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("lea");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("lea"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsLEA(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsMUL::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("mul");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("mul"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsMUL(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsDIV::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("div");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("div"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsDIV(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsMOD::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("mod");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("mod"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsMOD(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsNOT::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("not");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("not"), "");
    trim(ins);

    if (!ins.empty()) {
        return new NInstructionsNOT(g_textToRegisterMap[ins]);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsAND::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("and");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("and"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsAND(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsOR::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("or");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("or"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsOR(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsXOR::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("xor");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("xor"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsXOR(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsSHL::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("shl");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("shl"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsSHL(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsSHR::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("shr");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("shr"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsSHR(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsEQ::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("eq");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("eq"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsEQ(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsNE::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("ne");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("ne"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsNE(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsLT::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("lt");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("lt"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsLT(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsLE::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("le");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("le"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsLE(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsGT::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("gt");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("gt"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsGT(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsGE::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("ge");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("ge"), "");
    trim(ins);
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
            val = std::stoi(num, nullptr, 16);
        } else {
            val = std::stoi(num, nullptr, 10);
        }
        return new NInstructionsGE(g_textToRegisterMap[reg], val);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsPUSH::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("push");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("push"), "");
    trim(ins);

    if (!ins.empty()) {
        return new NInstructionsPUSH(g_textToRegisterMap[ins]);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsPOP::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("pop");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("pop"), "");
    trim(ins);

    if (!ins.empty()) {
        return new NInstructionsPOP(g_textToRegisterMap[ins]);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsJMP::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("jmp");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("jmp"), "");
    trim(ins);

    if (!ins.empty()) {
        return new NInstructionsJMP(ins);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsJIC::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("jic");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("jic"), "");
    trim(ins);

    if (!ins.empty()) {
        return new NInstructionsJIC(ins);
    }
    return nullptr;
}

NInstructionsInterface *
NInstructionsTRAP::parserInstructionText(std::string & text)
{
    if (text.empty()) {
        return nullptr;
    }
    auto ins = text;
    auto opcodePos = ins.find("trap");
    if (opcodePos == std::string::npos) {
        return nullptr;
    }
    ins.replace(opcodePos, strlen("trap"), "");
    trim(ins);

    if (!ins.empty()) {
        int32_t val;
        if (ins.size() > 2 && ins.substr(0, 2) == "0x") {
            val = std::stoi(ins, nullptr, 16);
        } else {
            val = std::stoi(ins, nullptr, 10);
        }
        return new NInstructionsTRAP(static_cast<NTrapType>(val));
    }
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
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
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
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
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

// ST指令实现
NInstructionsST::NInstructionsST(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsST::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::ST));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsST::generateInstructionName()
{
    return "st";
}

// LEA指令实现
NInstructionsLEA::NInstructionsLEA(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsLEA::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::LEA));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsLEA::generateInstructionName()
{
    return "lea";
}

// MUL指令实现
NInstructionsMUL::NInstructionsMUL(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsMUL::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::MUL));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsMUL::generateInstructionName()
{
    return "mul";
}

// DIV指令实现
NInstructionsDIV::NInstructionsDIV(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsDIV::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::DIV));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsDIV::generateInstructionName()
{
    return "div";
}

// MOD指令实现
NInstructionsMOD::NInstructionsMOD(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsMOD::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::MOD));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsMOD::generateInstructionName()
{
    return "mod";
}

// NOT指令实现
NInstructionsNOT::NInstructionsNOT(NRegister reg)
  : m_reg(reg)
{
}

std::vector<uint8_t>
NInstructionsNOT::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::NOT));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsNOT::generateInstructionName()
{
    return "not";
}

// AND指令实现
NInstructionsAND::NInstructionsAND(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsAND::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::AND));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsAND::generateInstructionName()
{
    return "and";
}

// OR指令实现
NInstructionsOR::NInstructionsOR(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsOR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::OR));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsOR::generateInstructionName()
{
    return "or";
}

// XOR指令实现
NInstructionsXOR::NInstructionsXOR(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsXOR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::XOR));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsXOR::generateInstructionName()
{
    return "xor";
}

// SHL指令实现
NInstructionsSHL::NInstructionsSHL(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsSHL::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::SHL));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsSHL::generateInstructionName()
{
    return "shl";
}

// SHR指令实现
NInstructionsSHR::NInstructionsSHR(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsSHR::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::SHR));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsSHR::generateInstructionName()
{
    return "shr";
}

// EQ指令实现
NInstructionsEQ::NInstructionsEQ(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsEQ::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::EQ));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsEQ::generateInstructionName()
{
    return "eq";
}

// NE指令实现
NInstructionsNE::NInstructionsNE(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsNE::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::NE));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsNE::generateInstructionName()
{
    return "ne";
}

// LT指令实现
NInstructionsLT::NInstructionsLT(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsLT::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::LT));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsLT::generateInstructionName()
{
    return "lt";
}

// LE指令实现
NInstructionsLE::NInstructionsLE(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsLE::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::LE));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsLE::generateInstructionName()
{
    return "le";
}

// GT指令实现
NInstructionsGT::NInstructionsGT(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsGT::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::GT));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsGT::generateInstructionName()
{
    return "gt";
}

// GE指令实现
NInstructionsGE::NInstructionsGE(NRegister reg, int32_t val)
  : m_reg(reg)
  , m_val(val)
{
}

std::vector<uint8_t>
NInstructionsGE::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::GE));
    code.push_back(static_cast<uint8_t>(m_reg));
    code.push_back(static_cast<uint8_t>((m_val >> 0) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_val >> 24) & 0xFF));
    return code;
}

std::string
NInstructionsGE::generateInstructionName()
{
    return "ge";
}

// PUSH指令实现
NInstructionsPUSH::NInstructionsPUSH(NRegister reg)
  : m_reg(reg)
{
}

std::vector<uint8_t>
NInstructionsPUSH::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::PUSH));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsPUSH::generateInstructionName()
{
    return "push";
}

// POP指令实现
NInstructionsPOP::NInstructionsPOP(NRegister reg)
  : m_reg(reg)
{
}

std::vector<uint8_t>
NInstructionsPOP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::POP));
    code.push_back(static_cast<uint8_t>(m_reg));
    return code;
}

std::string
NInstructionsPOP::generateInstructionName()
{
    return "pop";
}

// JMP指令实现
NInstructionsJMP::NInstructionsJMP(std::string target)
  : m_target(target)
{
}

std::vector<uint8_t>
NInstructionsJMP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::JMP));
    // 注意：这里暂时只生成操作码，目标地址需要在链接时确定
    return code;
}

std::string
NInstructionsJMP::generateInstructionName()
{
    return "jmp";
}

// JIC指令实现
NInstructionsJIC::NInstructionsJIC(std::string target)
  : m_target(target)
{
}

std::vector<uint8_t>
NInstructionsJIC::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::JIC));
    // 注意：这里暂时只生成操作码，目标地址需要在链接时确定
    return code;
}

std::string
NInstructionsJIC::generateInstructionName()
{
    return "jic";
}

// TRAP指令实现
NInstructionsTRAP::NInstructionsTRAP(NTrapType type)
  : m_type(type)
{
}

std::vector<uint8_t>
NInstructionsTRAP::generateInstructionCode()
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(NOpcode::TRAP));
    code.push_back(static_cast<uint8_t>(m_type));
    return code;
}

std::string
NInstructionsTRAP::generateInstructionName()
{
    return "trap";
}
