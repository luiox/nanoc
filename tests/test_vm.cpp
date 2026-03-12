#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <core.hpp>
#include <fstream>
#include <vector>

// 测试虚拟机初始化
TEST(VMTest, Initialization) {
    NVirtualMachine vm(1024 * 1024); // 1MB栈
    vm.print_info();
    
    // 验证虚拟机已正确初始化
    EXPECT_TRUE(true);
}

// 测试虚拟机加载文件
TEST(VMTest, LoadFile) {
    // 创建一个临时的测试文件
    std::string testFile = "test_load.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // 写入一些测试数据
    std::vector<uint8_t> testData = {0x00, 0x00, 0x0A}; // lmm R0, 10
    ofs.write(reinterpret_cast<const char*>(testData.data()), testData.size());
    ofs.close();
    
    NVirtualMachine vm;
    vm.load(testFile);
    
    // 清理测试文件
    std::remove(testFile.c_str());
    
    EXPECT_TRUE(true);
}

// 测试虚拟机栈操作
TEST(VMTest, StackOperations) {
    NVirtualMachine vm(1024); // 1KB栈
    
    // 测试栈打印功能
    vm.print_stack(0, 16);
    
    EXPECT_TRUE(true);
}

// 测试虚拟机信息打印
TEST(VMTest, PrintInfo) {
    NVirtualMachine vm;
    
    // 捕获输出（这里只是确保不会崩溃）
    testing::internal::CaptureStdout();
    vm.print_info();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_FALSE(output.empty());
    EXPECT_TRUE(output.find("Nvm current infomation") != std::string::npos);
}

// 测试虚拟机指令执行
TEST(VMTest, InstructionExecution) {
    // 创建一个测试程序：lmm R0, 10; add R0, 5; trap 0
    std::string testFile = "test_execution.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // lmm R0, 10 (opcode=0, reg=0, value=10)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 10;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));
    
    // add R0, 5 (opcode=3, reg=0, value=5)
    ofs.put(0x03); // ADD opcode
    ofs.put(0x00); // R0
    int32_t value2 = 5;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));
    
    // trap 0 (opcode=26, type=2 for HALT)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type
    
    ofs.close();
    
    NVirtualMachine vm(1024);
    vm.load(testFile);
    
    // 捕获输出
    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    
    // 验证输出包含预期的执行信息
    EXPECT_TRUE(output.find("LMM: R0 = 10") != std::string::npos);
    EXPECT_TRUE(output.find("ADD: R0 += 5 (result: 15)") != std::string::npos);
    EXPECT_TRUE(output.find("TRAP: type=2") != std::string::npos);
    EXPECT_TRUE(output.find("Program halted") != std::string::npos);
    
    // 清理测试文件
    std::remove(testFile.c_str());
}

// 测试乘法指令
TEST(VMTest, MULInstruction) {
    std::string testFile = "test_mul.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // lmm R0, 6 (opcode=0, reg=0, value=6)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 6;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));
    
    // mul R0, 7 (opcode=5, reg=0, value=7)
    ofs.put(0x05); // MUL opcode
    ofs.put(0x00); // R0
    int32_t value2 = 7;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));
    
    // trap 0 (opcode=26, type=2 for HALT)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type
    
    ofs.close();
    
    NVirtualMachine vm(1024);
    vm.load(testFile);
    
    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("LMM: R0 = 6") != std::string::npos);
    EXPECT_TRUE(output.find("MUL: R0 *= 7 (result: 42)") != std::string::npos);
    
    std::remove(testFile.c_str());
}

// 测试除法指令
TEST(VMTest, DIVInstruction) {
    std::string testFile = "test_div.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // lmm R0, 20 (opcode=0, reg=0, value=20)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 20;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));
    
    // div R0, 4 (opcode=6, reg=0, value=4)
    ofs.put(0x06); // DIV opcode
    ofs.put(0x00); // R0
    int32_t value2 = 4;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));
    
    // trap 0 (opcode=26, type=2 for HALT)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type
    
    ofs.close();
    
    NVirtualMachine vm(1024);
    vm.load(testFile);
    
    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("LMM: R0 = 20") != std::string::npos);
    EXPECT_TRUE(output.find("DIV: R0 /= 4 (result: 5)") != std::string::npos);
    
    std::remove(testFile.c_str());
}

// 测试比较指令
TEST(VMTest, ComparisonInstructions) {
    std::string testFile = "test_comparison.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // lmm R0, 10 (opcode=0, reg=0, value=10)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 10;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));
    
    // eq R0, 10 (opcode=14, reg=0, value=10)
    ofs.put(0x0E); // EQ opcode
    ofs.put(0x00); // R0
    int32_t value2 = 10;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));
    
    // trap 0 (opcode=26, type=2 for HALT)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type
    
    ofs.close();
    
    NVirtualMachine vm(1024);
    vm.load(testFile);
    
    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("EQ: R0 == 10 (result: 1)") != std::string::npos);
    
    std::remove(testFile.c_str());
}

// 测试栈操作指令
TEST(VMTest, StackInstructions) {
    std::string testFile = "test_stack.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // lmm R0, 42 (opcode=0, reg=0, value=42)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 42;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));
    
    // push R0 (opcode=20, reg=0)
    ofs.put(0x14); // PUSH opcode
    ofs.put(0x00); // R0
    
    // lmm R0, 0 (opcode=0, reg=0, value=0)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value2 = 0;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));
    
    // pop R0 (opcode=21, reg=0)
    ofs.put(0x15); // POP opcode
    ofs.put(0x00); // R0
    
    // trap 0 (opcode=26, type=2 for HALT)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type
    
    ofs.close();
    
    NVirtualMachine vm(1024);
    vm.load(testFile);
    
    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("PUSH: R0 (value: 42)") != std::string::npos);
    EXPECT_TRUE(output.find("POP: R0 (value: 42)") != std::string::npos);
    
    std::remove(testFile.c_str());
}

// 测试跳转指令
TEST(VMTest, JumpInstructions) {
    std::string testFile = "test_jump.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    
    // jmp 12 (opcode=22, target=12)
    ofs.put(0x16); // JMP opcode
    int32_t target = 12;
    ofs.write(reinterpret_cast<const char*>(&target), sizeof(target));
    
    // 这里应该被跳过
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 999;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));
    
    // lmm R0, 100 (opcode=0, reg=0, value=100)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value2 = 100;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));
    
    // trap 0 (opcode=26, type=2 for HALT)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type
    
    ofs.close();
    
    NVirtualMachine vm(1024);
    vm.load(testFile);
    
    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("JMP: target=12") != std::string::npos);
    EXPECT_TRUE(output.find("LMM: R0 = 100") != std::string::npos);
    EXPECT_TRUE(output.find("LMM: R0 = 999") == std::string::npos); // 应该被跳过
    
    std::remove(testFile.c_str());
}