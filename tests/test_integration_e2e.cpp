#include "nas/instruction.hpp"
#include "nvm/core.hpp"
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

// 集成 e2e：真实汇编器产物 → VM v2.1 加载 → CALLX 宿主调用。
// 两个 PR 的交界验收点：汇编器的数据标号统一编址（codeSize + 段内偏移）
// 必须与加载器的数据段落点（m_stack[codeSize..]）是同一个地址空间。

namespace {

    // 汇编源码并写出临时 .nci 文件（调用方负责 remove）
    std::string writeImage(const std::string& src, const std::string& path) {
        AssemblyResult r = Assembler::assemble(src);
        EXPECT_TRUE(r.ok) << "line " << r.errorLine << ": " << r.errorMessage;
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(r.image.data()),
                  (std::streamsize)r.image.size());
        ofs.close();
        return path;
    }

    int32_t hostAdd(int32_t* regs, int8_t*, int32_t) { return regs[0] + regs[1]; }

    // 从 VM 统一内存读 C 字符串取长度（mem = 栈缓冲）
    int32_t hostStrlen(int32_t* regs, int8_t* mem, int32_t memSize) {
        int32_t addr = regs[0];
        int32_t n = 0;
        while (addr + n < memSize && mem[addr + n] != 0)
            ++n;
        return n;
    }

} // namespace

// 汇编 → 加载 → 静态宿主调用（fastcall：参数走 R0/R1）
TEST(IntegrationE2ETest, AssembleLoadCallHost) {
    std::string path = writeImage("extern myadd 0x7F000001\n"
                                  "export main\n"
                                  "main:\n"
                                  "    lmm R0, 7\n"
                                  "    lmm R1, 5\n"
                                  "    callx myadd\n"
                                  "    ret\n",
                                  "integration_e2e.nci");
    NVirtualMachine vm(8 * 1024 * 1024);
    vm.load(path);
    ASSERT_EQ(vm.getImports().size(), (size_t)1);
    EXPECT_EQ(vm.getImports()[0].name, "myadd");
    EXPECT_EQ(vm.getImports()[0].addr, 0x7F000001);
    vm.registerHostFunction(vm.getImports()[0].addr, hostAdd);
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 12);
    std::remove(path.c_str());
}

// 数据段统一编址：LEA 取数据标号地址，宿主函数从 mem 同一地址读串
TEST(IntegrationE2ETest, DataSegmentUnifiedAddressing) {
    std::string path = writeImage("extern strlen 0x7F000002\n"
                                  "export main\n"
                                  "main:\n"
                                  "    lea R0, .msg\n"
                                  "    push R0\n"
                                  "    callx strlen\n"
                                  "    addi R4, 4\n"
                                  "    ret\n"
                                  ".msg:\n"
                                  "    db \"NanoC\", 0\n",
                                  "integration_data.nci");
    NVirtualMachine vm(8 * 1024 * 1024);
    vm.load(path);
    vm.registerHostFunction(0x7F000002, hostStrlen);
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 5); // "NanoC" 长度
    std::remove(path.c_str());
}
