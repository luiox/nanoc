#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <instructions.hpp>
#include <string>

// 测试LMM指令解析
TEST(InstructionTest, LMMParsing) {
    std::string instruction = "lmm R0, 10";
    auto parsed = NInstructionsLMM::parserInstructionText(instruction);
    
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->generateInstructionName(), "lmm");
    
    delete parsed;
}

// 测试LMM指令代码生成
TEST(InstructionTest, LMMCodeGeneration) {
    NInstructionsLMM lmm("R0", 10);
    auto code = lmm.generateInstructionCode();
    
    EXPECT_EQ(code.size(), 3); // opcode + register + value
    EXPECT_EQ(code[0], static_cast<uint8_t>(NOpcode::LMM));
    EXPECT_EQ(code[1], static_cast<uint8_t>(NRegister::R0));
    EXPECT_EQ(code[2], 10);
}

// 测试十六进制立即数解析
TEST(InstructionTest, HexImmediateParsing) {
    std::string instruction = "lmm R1, 0xFF";
    auto parsed = NInstructionsLMM::parserInstructionText(instruction);
    
    ASSERT_NE(parsed, nullptr);
    
    auto code = parsed->generateInstructionCode();
    EXPECT_EQ(code[2], 255); // 0xFF = 255
    
    delete parsed;
}

// 测试无效指令解析
TEST(InstructionTest, InvalidInstructionParsing) {
    std::string instruction = "invalid R0, 10";
    auto parsed = NInstructionsLMM::parserInstructionText(instruction);
    
    EXPECT_EQ(parsed, nullptr);
}

// 测试寄存器映射
TEST(InstructionTest, RegisterMapping) {
    EXPECT_EQ(g_textToRegisterMap["R0"], NRegister::R0);
    EXPECT_EQ(g_textToRegisterMap["R1"], NRegister::R1);
    EXPECT_EQ(g_textToRegisterMap["R7"], NRegister::R7);
}

// 测试操作码映射
TEST(InstructionTest, OpcodeMapping) {
    EXPECT_NE(g_opcodeToGeneratorMap.find("lmm"), g_opcodeToGeneratorMap.end());
    EXPECT_NE(g_opcodeToGeneratorMap.find("add"), g_opcodeToGeneratorMap.end());
    EXPECT_NE(g_opcodeToGeneratorMap.find("call"), g_opcodeToGeneratorMap.end());
    EXPECT_NE(g_opcodeToGeneratorMap.find("ret"), g_opcodeToGeneratorMap.end());
}

// 测试ADD指令解析
TEST(InstructionTest, ADDParsing) {
    std::string instruction = "add R0, 5";
    auto parsed = NInstructionsADD::parserInstructionText(instruction);
    
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->generateInstructionName(), "add");
    
    delete parsed;
}

// 测试SUB指令解析
TEST(InstructionTest, SUBParsing) {
    std::string instruction = "sub R1, 10";
    auto parsed = NInstructionsSUB::parserInstructionText(instruction);
    
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->generateInstructionName(), "sub");
    
    delete parsed;
}

// 测试CALL指令解析
TEST(InstructionTest, CALLParsing) {
    std::string instruction = "call my_function";
    auto parsed = NInstructionsCALL::parserInstructionText(instruction);
    
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->generateInstructionName(), "call");
    
    delete parsed;
}

// 测试RET指令解析
TEST(InstructionTest, RETParsing) {
    std::string instruction = "ret";
    auto parsed = NInstructionsRET::parserInstructionText(instruction);
    
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->generateInstructionName(), "ret");
    
    delete parsed;
}

int main(int argc, char **argv) {
    // 初始化spdlog
    spdlog::set_level(spdlog::level::debug);
    
    // 初始化gtest
    ::testing::InitGoogleTest(&argc, argv);
    
    return RUN_ALL_TESTS();
}