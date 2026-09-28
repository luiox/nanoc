#include "nvm/core.hpp"
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
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
    std::vector<uint8_t> testData = { 0x00, 0x00, 0x0A }; // lmm R0, 10
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
    EXPECT_TRUE(output.find("PC=") != std::string::npos);
}

// 测试虚拟机指令执行
// TODO(#5): 旧 v1 指令集用例（TRAP 停机 + trace 断言），按 NCI v2.1 重写后解除禁用
TEST(VMTest, DISABLED_InstructionExecution) {
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
// TODO(#5): 旧 v1 指令集用例（TRAP 停机 + trace 断言），按 NCI v2.1 重写后解除禁用
TEST(VMTest, DISABLED_MULInstruction) {
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
// TODO(#5): 旧 v1 指令集用例（TRAP 停机 + trace 断言），按 NCI v2.1 重写后解除禁用
TEST(VMTest, DISABLED_DIVInstruction) {
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
// TODO(#5): 旧 v1 指令集用例（TRAP 停机 + trace 断言），按 NCI v2.1 重写后解除禁用
TEST(VMTest, DISABLED_ComparisonInstructions) {
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
// TODO(#5): 旧 v1 指令集用例（TRAP 停机 + trace 断言），按 NCI v2.1 重写后解除禁用
TEST(VMTest, DISABLED_StackInstructions) {
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
// TODO(#5): 旧 v1 指令集用例（TRAP 停机 + trace 断言），按 NCI v2.1 重写后解除禁用
TEST(VMTest, DISABLED_JumpInstructions) {
    std::string testFile = "test_jump.nca";
    std::ofstream ofs(testFile, std::ios::binary);

    // jmp 11 (opcode=22, target=11) - 跳过第一条LMM指令
    // 位置0-4: jmp指令(5字节)
    // 位置5-10: LMM R0, 999 (6字节)
    // 位置11-16: LMM R0, 100 (6字节)
    // 位置17-18: trap (2字节)
    ofs.put(0x16);       // JMP opcode
    int32_t target = 11; // 跳到第二条LMM指令
    ofs.write(reinterpret_cast<const char*>(&target), sizeof(target));

    // 这里应该被跳过 (位置5-10)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value1 = 999;
    ofs.write(reinterpret_cast<const char*>(&value1), sizeof(value1));

    // lmm R0, 100 (opcode=0, reg=0, value=100) (位置11-16)
    ofs.put(0x00); // LMM opcode
    ofs.put(0x00); // R0
    int32_t value2 = 100;
    ofs.write(reinterpret_cast<const char*>(&value2), sizeof(value2));

    // trap 0 (opcode=26, type=2 for HALT) (位置17-18)
    ofs.put(0x1A); // TRAP opcode
    ofs.put(0x02); // HALT type

    ofs.close();

    NVirtualMachine vm(1024);
    vm.load(testFile);

    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(output.find("JMP: target=11") != std::string::npos);
    EXPECT_TRUE(output.find("LMM: R0 = 100") != std::string::npos);
    EXPECT_TRUE(output.find("LMM: R0 = 999") == std::string::npos); // 应该被跳过

    std::remove(testFile.c_str());
}

// ==== NCI v2.1 新增 10 条指令执行用例（Phase 2）====

// 追加小端 32 位立即数
static void appendImm32(std::vector<uint8_t>& code, int32_t v) {
    code.push_back(static_cast<uint8_t>(v & 0xFF));
    code.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// 将裸字节码写入临时 .nca 文件并加载（无 NanoC 魔数时 VM 按裸代码加载）
static void loadRawCode(NVirtualMachine& vm, const std::vector<uint8_t>& code) {
    std::string testFile = "test_vm_v21_new_isa.nca";
    std::ofstream ofs(testFile, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(code.data()),
              static_cast<std::streamsize>(code.size()));
    ofs.close();
    vm.load(testFile);
    std::remove(testFile.c_str());
}

// 读取栈上 32 位值（小端）
static int32_t readStackInt32(NVirtualMachine& vm, int32_t addr) {
    int32_t v = 0;
    memcpy(&v, vm.getStack() + addr, sizeof(v));
    return v;
}

// LOADA/STOREA 绝对寻址走内存：pushi 压立即数，storea 写入、loada 读回
TEST(VMTest, LoadStoreAbsoluteMemory) {
    NVirtualMachine vm(64 * 1024);
    const int32_t addr = 256;

    std::vector<uint8_t> code;
    code.push_back(0x41);
    appendImm32(code, 0x77); // pushi 0x77
    code.push_back(0x42);
    code.push_back(0x01); // pop R1（v2.1 POP=0x42）
    code.push_back(0x06);
    code.push_back(0x01);
    appendImm32(code, addr); // storea R1, addr
    code.push_back(0x05);
    code.push_back(0x00);
    appendImm32(code, addr); // loada R0, addr
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 0x77);
    EXPECT_EQ(readStackInt32(vm, addr), 0x77);
    // 栈净效应：pushi/pop 相互抵消，仅剩栈底哨兵
    EXPECT_EQ(vm.getSP(), vm.getStackSize() - 4);
}

// ANDI/ORI/XORI/SHLI/SHRI 对寄存器的效果（不修改 flags，与寄存器形式一致）
TEST(VMTest, ImmediateLogicShiftRegisters) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 0xF0); // lmm R0, 0xF0
    code.push_back(0x21);
    code.push_back(0x00);
    appendImm32(code, 0x3C); // andi R0, 0x3C → 0x30
    code.push_back(0x23);
    code.push_back(0x01);
    appendImm32(code, 0x0F); // ori R1, 0x0F → 0x0F
    code.push_back(0x25);
    code.push_back(0x01);
    appendImm32(code, 0x3F); // xori R1, 0x3F → 0x30
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 3); // lmm R2, 3
    code.push_back(0x27);
    code.push_back(0x02);
    code.push_back(4); // shli R2, 4 → 48
    code.push_back(0x00);
    code.push_back(0x03);
    appendImm32(code, 48); // lmm R3, 48
    code.push_back(0x29);
    code.push_back(0x03);
    code.push_back(2); // shri R3, 2 → 12
    code.push_back(0x00);
    code.push_back(0x04);
    appendImm32(code, -8); // lmm R4, -8
    code.push_back(0x29);
    code.push_back(0x04);
    code.push_back(1); // shri R4, 1 → -4（算术右移）
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 0x30);
    EXPECT_EQ(vm.getRegister(1), 0x30);
    EXPECT_EQ(vm.getRegister(2), 48);
    EXPECT_EQ(vm.getRegister(3), 12);
    EXPECT_EQ(vm.getRegister(4), -4);
}

// JN：cmp 结果为负（N 置位）时跳转
TEST(VMTest, JNJumpsOnNegativeFlag) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, -5); // @0  lmm R0, -5
    code.push_back(0x00);
    code.push_back(0x01);
    appendImm32(code, 0); // @6  lmm R1, 0
    code.push_back(0x30);
    code.push_back(0x00);
    code.push_back(0x01); // @12 cmp R0, R1 → N
    code.push_back(0x53);
    appendImm32(code, 26); // @15 jn 26
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 111); // @20 lmm R2, 111（应被跳过）
    code.push_back(0x00);
    code.push_back(0x03);
    appendImm32(code, 222); // @26 lmm R3, 222
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(3), 222);
    EXPECT_EQ(vm.getRegister(2), 0);
}

// JN：结果为零/正（N 未置位）时顺序执行
TEST(VMTest, JNNotTakenWhenNonNegative) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 7); // @0  lmm R0, 7
    code.push_back(0x00);
    code.push_back(0x01);
    appendImm32(code, 7); // @6  lmm R1, 7
    code.push_back(0x30);
    code.push_back(0x00);
    code.push_back(0x01); // @12 cmp R0, R1 → Z
    code.push_back(0x53);
    appendImm32(code, 20); // @15 jn 20（不跳）
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 55); // @20 lmm R2, 55（应执行）
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(2), 55);
}

// JP：cmp 结果为正（P 置位）时跳转
TEST(VMTest, JPJumpsOnPositiveFlag) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 5); // @0  lmm R0, 5
    code.push_back(0x00);
    code.push_back(0x01);
    appendImm32(code, 3); // @6  lmm R1, 3
    code.push_back(0x30);
    code.push_back(0x00);
    code.push_back(0x01); // @12 cmp R0, R1 → P
    code.push_back(0x54);
    appendImm32(code, 26); // @15 jp 26
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 111); // @20 lmm R2, 111（应被跳过）
    code.push_back(0x00);
    code.push_back(0x03);
    appendImm32(code, 222); // @26 lmm R3, 222
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(3), 222);
    EXPECT_EQ(vm.getRegister(2), 0);
}

// JP：结果为零/负（P 未置位）时顺序执行
TEST(VMTest, JPNotTakenWhenNonPositive) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, -5); // @0  lmm R0, -5
    code.push_back(0x00);
    code.push_back(0x01);
    appendImm32(code, 0); // @6  lmm R1, 0
    code.push_back(0x30);
    code.push_back(0x00);
    code.push_back(0x01); // @12 cmp R0, R1 → N
    code.push_back(0x54);
    appendImm32(code, 20); // @15 jp 20（不跳）
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 55); // @20 lmm R2, 55（应执行）
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(2), 55);
}

// 组合小程序：call/leave/ret 全链路 + 栈底哨兵返回地址触发停机
// 布局（33 字节）：
//   @0  enter 0     @3  lmm R0,5    @9  call 19    @14 mov R6,R0
//   @17 leave       @18 ret         @19 enter 0    @22 lmm R1,3
//   @28 add R0,R1   @31 leave       @32 ret → 弹出哨兵(=codeSize) 结束循环
TEST(VMTest, CallLeaveRetWithSentinelTermination) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x43);
    code.push_back(0x00);
    code.push_back(0x00); // @0  enter 0
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 5); // @3  lmm R0, 5
    code.push_back(0x60);
    appendImm32(code, 19); // @9  call 19
    code.push_back(0x70);
    code.push_back(0x06);
    code.push_back(0x00); // @14 mov R6, R0
    code.push_back(0x44); // @17 leave
    code.push_back(0x62); // @18 ret
    code.push_back(0x43);
    code.push_back(0x00);
    code.push_back(0x00); // @19 enter 0
    code.push_back(0x00);
    code.push_back(0x01);
    appendImm32(code, 3); // @22 lmm R1, 3
    code.push_back(0x10);
    code.push_back(0x00);
    code.push_back(0x01); // @28 add R0, R1
    code.push_back(0x44); // @31 leave
    code.push_back(0x62); // @32 ret
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 8);          // 5 + 3
    EXPECT_EQ(vm.getRegister(6), 8);          // main 保存返回值
    EXPECT_EQ(vm.getSP(), vm.getStackSize()); // 哨兵已被 ret 消费，栈复原
    EXPECT_EQ(vm.getBP(), 0);                 // 帧链完整回收
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());  // 停在代码段末尾（哨兵地址）
}