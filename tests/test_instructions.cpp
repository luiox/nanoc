#include "nas/instruction.hpp"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

// 汇编一行文本，返回编码后的字节；解析失败返回空
static std::vector<uint8_t> assemble(const std::string& line) {
    auto instr = Assembler::parseLine(line);
    if (!instr) {
        return {};
    }
    instr->emit();
    return instr->bytes;
}

// 从字节流读取小端 32 位立即数
static int32_t read32(const std::vector<uint8_t>& c, size_t off) {
    return static_cast<int32_t>(c[off]) | (static_cast<int32_t>(c[off + 1]) << 8)
           | (static_cast<int32_t>(c[off + 2]) << 16)
           | (static_cast<int32_t>(c[off + 3]) << 24);
}

// LMM 解析与编码：opcode + reg + imm32（6 字节，见 Bytecode Format Spec v2.1）
TEST(InstructionTest, LMMParsingAndEncoding) {
    auto code = assemble("lmm R0, 10");

    ASSERT_EQ(code.size(), 6u);
    EXPECT_EQ(code[0], static_cast<uint8_t>(NOpcode::LMM));
    EXPECT_EQ(code[1], 0); // R0
    EXPECT_EQ(read32(code, 2), 10);
}

// 十六进制立即数
TEST(InstructionTest, HexImmediateParsing) {
    auto code = assemble("lmm R1, 0xFF");

    ASSERT_EQ(code.size(), 6u);
    EXPECT_EQ(read32(code, 2), 255);
}

// 负立即数按补码编码
TEST(InstructionTest, NegativeImmediate) {
    auto code = assemble("lmm R2, -1");

    ASSERT_EQ(code.size(), 6u);
    EXPECT_EQ(read32(code, 2), -1);
}

// ADD 为寄存器-寄存器指令：opcode + dest + src（3 字节）
TEST(InstructionTest, AddRegRegEncoding) {
    auto code = assemble("add R0, R1");

    ASSERT_EQ(code.size(), 3u);
    EXPECT_EQ(code[0], static_cast<uint8_t>(NOpcode::ADD));
    EXPECT_EQ(code[1], 0);
    EXPECT_EQ(code[2], 1);
}

// PUSH/POP 编码与操作码值（POP 必须是 0x42，防止与 VM 分发再次错位）
TEST(InstructionTest, PushPopEncoding) {
    auto push = assemble("push R3");
    ASSERT_EQ(push.size(), 2u);
    EXPECT_EQ(push[0], static_cast<uint8_t>(NOpcode::PUSH));
    EXPECT_EQ(push[1], 3);

    auto pop = assemble("pop R4");
    ASSERT_EQ(pop.size(), 2u);
    EXPECT_EQ(pop[0], static_cast<uint8_t>(NOpcode::POP));
    EXPECT_EQ(pop[1], 4);
}

// 操作码值与规范 v2.1 对齐（防枚举漂移）
TEST(InstructionTest, OpcodeValuesMatchSpec) {
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::LMM), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::PUSH), 0x40);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::PUSHI), 0x41);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::POP), 0x42);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::ENTER), 0x43);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::LEAVE), 0x44);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::CALLX), 0x61);
    EXPECT_EQ(static_cast<uint8_t>(NOpcode::RET), 0x62);
}

// ENTER/LEAVE/RET 编码
TEST(InstructionTest, FrameInstructionsEncoding) {
    auto enter = assemble("enter 16");
    ASSERT_EQ(enter.size(), 3u);
    EXPECT_EQ(enter[0], static_cast<uint8_t>(NOpcode::ENTER));
    EXPECT_EQ(enter[1], 16);
    EXPECT_EQ(enter[2], 0); // imm16 小端

    auto leave = assemble("leave");
    ASSERT_EQ(leave.size(), 1u);
    EXPECT_EQ(leave[0], static_cast<uint8_t>(NOpcode::LEAVE));

    auto ret = assemble("ret");
    ASSERT_EQ(ret.size(), 1u);
    EXPECT_EQ(ret[0], static_cast<uint8_t>(NOpcode::RET));
}

// 跳转：opcode + addr32（5 字节）
TEST(InstructionTest, JumpEncoding) {
    auto code = assemble("jmp 0x20");

    ASSERT_EQ(code.size(), 5u);
    EXPECT_EQ(code[0], static_cast<uint8_t>(NOpcode::JMP));
    EXPECT_EQ(read32(code, 1), 0x20);
}

// 空白容错：前后空格与多余分隔
TEST(InstructionTest, WhitespaceTolerant) {
    auto code = assemble("  lmm   R5 , 7 ");

    ASSERT_EQ(code.size(), 6u);
    EXPECT_EQ(code[0], static_cast<uint8_t>(NOpcode::LMM));
    EXPECT_EQ(code[1], 5);
    EXPECT_EQ(read32(code, 2), 7);
}

// 无效指令返回 nullptr
TEST(InstructionTest, InvalidInstructionReturnsNull) {
    EXPECT_EQ(Assembler::parseLine("invalid R0, 10"), nullptr);
}

// 空行与注释返回 nullptr
TEST(InstructionTest, CommentAndEmptyReturnNull) {
    EXPECT_EQ(Assembler::parseLine(""), nullptr);
    EXPECT_EQ(Assembler::parseLine("   "), nullptr);
    EXPECT_EQ(Assembler::parseLine("; 这是一个注释"), nullptr);
    EXPECT_EQ(Assembler::parseLine("#pragma once"), nullptr);
}

// 寄存器解析
TEST(InstructionTest, RegisterParsing) {
    EXPECT_EQ(Assembler::parseRegister("R0"), 0);
    EXPECT_EQ(Assembler::parseRegister(" r7 "), 7);
    EXPECT_EQ(Assembler::parseRegister("r3"), 3);
    EXPECT_EQ(Assembler::parseRegister("junk"), 0);
}

// 立即数解析：十进制 / 十六进制 / 负数 / 非法
TEST(InstructionTest, ImmediateParsing) {
    EXPECT_EQ(Assembler::parseInt("42"), 42);
    EXPECT_EQ(Assembler::parseInt("0x10"), 16);
    EXPECT_EQ(Assembler::parseInt("-5"), -5);
    EXPECT_EQ(Assembler::parseInt("junk"), 0);
}
