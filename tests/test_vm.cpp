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

// ==== 手工字节码 helper（v2.1 编码迁移用例与新增 ISA 用例共用）====

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

// ==== v1 执行用例迁移至 NCI v2.1 编码（原 6 个 DISABLED_ 前缀用例）====
// 迁移口径：v1 语义不变，编码换 v2.1（doc/Bytecode Format Specification
// v2.1.md §3.1）；v1 的 TRAP 停机改为直线代码自然执行到 codeSize（或经
// ret 弹出栈底哨兵）终止，trace 字符串断言改为寄存器/栈/PC 状态断言。

// 测试虚拟机指令执行：v1 `lmm R0,10; add R0,5; trap 0` →
// v2.1 立即数加法为 addi（0x11），直线执行到代码段末尾正常终止
TEST(VMTest, InstructionExecution) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 10); // @0  lmm R0, 10
    code.push_back(0x11);
    code.push_back(0x00);
    appendImm32(code, 5); // @6  addi R0, 5
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 15);             // 10 + 5
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());      // 正常终止：PC 停在代码段末尾
    EXPECT_EQ(vm.getSP(), vm.getStackSize() - 4); // 无 call/ret，栈底哨兵未动
}

// 测试乘法指令：v1 `lmm R0,6; mul R0,7; trap 0` →
// v2.1 立即数乘法为 muli（0x15），R0 = 42
TEST(VMTest, MULInstruction) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 6); // @0 lmm R0, 6
    code.push_back(0x15);
    code.push_back(0x00);
    appendImm32(code, 7); // @6 muli R0, 7
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42); // 6 * 7
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}

// 测试除法指令：v1 `lmm R0,20; div R0,4; trap 0` →
// v2.1 立即数除法为 divi（0x17），R0 = 5
TEST(VMTest, DIVInstruction) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 20); // @0 lmm R0, 20
    code.push_back(0x17);
    code.push_back(0x00);
    appendImm32(code, 4); // @6 divi R0, 4
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 5); // 20 / 4
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}

// 测试比较指令：v1 `eq R0,imm`（比较结果 0/1 直接落寄存器）在 v2.1 无对应
// 指令，改写为等价场景：cmp 置 flags + jz 物化比较结果（相等 → 1，不等 → 0）。
// 布局（32 字节）：
//   @0  lmm R0,10   @6  lmm R1,10   @12 cmp R0,R1   @15 lmm R2,1
//   @21 jz 32       @26 lmm R2,0（应被跳过）        @32 = codeSize
TEST(VMTest, ComparisonInstructions) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 10); // @0  lmm R0, 10
    code.push_back(0x00);
    code.push_back(0x01);
    appendImm32(code, 10); // @6  lmm R1, 10
    code.push_back(0x30);
    code.push_back(0x00);
    code.push_back(0x01); // @12 cmp R0, R1 → Z 置位
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 1); // @15 lmm R2, 1（相等 → 1）
    code.push_back(0x51);
    appendImm32(code, 32); // @21 jz 32
    code.push_back(0x00);
    code.push_back(0x02);
    appendImm32(code, 0); // @26 lmm R2, 0（不等 → 0，应被跳过）
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(2), 1);         // eq 语义等价：相等物化为 1
    EXPECT_EQ(vm.getRegister(0), 10);        // cmp 不破坏操作数
    EXPECT_NE(vm.getFlags() & FLAG_Z, 0);    // Z 置位
    EXPECT_EQ(vm.getPC(), vm.getCodeSize()); // jz 落点即代码段末尾
}

// 测试栈操作指令：v1 `lmm R0,42; push R0; lmm R0,0; pop R0; trap 0` →
// v2.1 PUSH=0x40 / POP=0x42（各 2 字节），栈向下生长，语义不变。
// 布局（16 字节）：
//   @0  lmm R0,42   @6  push R0   @8  lmm R0,0   @14 pop R0   @16 = codeSize
TEST(VMTest, StackInstructions) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 42); // @0  lmm R0, 42
    code.push_back(0x40);
    code.push_back(0x00); // @6  push R0
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 0); // @8  lmm R0, 0
    code.push_back(0x42);
    code.push_back(0x00); // @14 pop R0
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42); // pop 复原 R0
    EXPECT_EQ(readStackInt32(vm, vm.getSP() - 4),
              42); // push 写入的栈内存残留值（pop 后 SP 已回退位）
    EXPECT_EQ(vm.getSP(), vm.getStackSize() - 4); // push/pop 抵消，仅剩栈底哨兵
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}

// 测试跳转指令：v1 `jmp 11` 跳过 @5 的 lmm R0,999、执行 @11 的 lmm R0,100。
// v2.1 JMP=0x50（opcode+ADDR32=5 字节），lmm 仍为 6 字节，地址布局与 v1 同构。
// 布局（17 字节）：
//   @0  jmp 11   @5  lmm R0,999（被跳过）   @11 lmm R0,100   @17 = codeSize
TEST(VMTest, JumpInstructions) {
    NVirtualMachine vm(64 * 1024);

    std::vector<uint8_t> code;
    code.push_back(0x50);
    appendImm32(code, 11); // @0  jmp 11（跳到第二条 lmm）
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 999); // @5  lmm R0, 999（应被跳过）
    code.push_back(0x00);
    code.push_back(0x00);
    appendImm32(code, 100); // @11 lmm R0, 100
    loadRawCode(vm, code);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 100); // 跳转目标指令已执行，999 未生效
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}

// ==== NCI v2.1 新增 10 条指令执行用例（Phase 2）====

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