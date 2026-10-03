// 宿主库直调通路用例（R3 VM 侧前置）：nas 无地址 extern 产出伪宿主地址 +
// flags bit2 动态导入 → nvm loadHostLibrary 按符号名解析 → 签名包装器适配后
// 直调 msvcrt 真函数。字节级断言见 test_assembler_v21 / test_linker，
// 这里聚焦 VM 侧解析、登记与执行
#include "nas/instruction.hpp"
#include "nvm/core.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ==== helpers ====

// 汇编源码 → 目标镜像（断言汇编成功）
static std::vector<uint8_t> asmImage(const std::string& src) {
    AssemblyResult r = Assembler::assemble(src);
    EXPECT_TRUE(r.ok) << "line " << r.errorLine << ": " << r.errorMessage;
    return r.image;
}

// 写临时文件并加载（load 读取完整文件后即可删除）
static void
writeAndLoad(NVirtualMachine& vm, const std::vector<uint8_t>& bytes, const char* name) {
    std::ofstream ofs(name, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    ofs.close();
    vm.load(name);
    std::remove(name);
}

// 测试宿主函数：返回 R0 + R1（静态绑定回归用）
static int32_t hostAdd(int32_t* regs, int8_t*, int32_t) { return regs[0] + regs[1]; }

#ifdef _WIN32

// ==== msvcrt 直调（伪地址动态导入 → GetProcAddress → 包装器）====

// abs：整型入参/返回值直调；伪地址在解析前后保持不变（callx 站点天然命中）
TEST(HostLibTest, MsvcrtAbsResolvesAtPseudoAddress) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern abs\n"
                        "export main\n"
                        "main:\n"
                        "    lmm R0, -42\n"
                        "    callx abs\n"
                        "    ret\n");
    writeAndLoad(vm, img, "test_hostlib_abs.nci");

    // nas 产物：首个动态导入伪地址 0x7E000000，flags bit2 置位
    ASSERT_EQ(vm.getImports().size(), 1u);
    EXPECT_EQ(vm.getImports()[0].addr, DYNAMIC_HOST_BASE);
    EXPECT_EQ(vm.getImports()[0].flags & IMPORT_FLAG_DYNAMIC, IMPORT_FLAG_DYNAMIC);

    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    // 登记在该导入的伪地址上（不改写、不新分配），callx 站点无需回填
    EXPECT_EQ(vm.getImports()[0].addr, DYNAMIC_HOST_BASE);

    vm.start();
    EXPECT_EQ(vm.getRegister(0), 42);
}

// atoi：统一内存字符串参数经边界保护后传给真函数
TEST(HostLibTest, MsvcrtAtoiReadsUnifiedMemoryString) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern atoi\n"
                        "export main\n"
                        "main:\n"
                        "    lea R0, .s\n"
                        "    callx atoi\n"
                        "    ret\n"
                        ".s:\n"
                        "    db \"-123\", 0\n");
    writeAndLoad(vm, img, "test_hostlib_atoi.nci");
    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    vm.start();
    EXPECT_EQ(vm.getRegister(0), -123);
}

// strlen：size_t 返回值截断为 int32
TEST(HostLibTest, MsvcrtStrlenReturnsLength) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern strlen\n"
                        "export main\n"
                        "main:\n"
                        "    lea R0, .s\n"
                        "    callx strlen\n"
                        "    ret\n"
                        ".s:\n"
                        "    db \"Hello from NanoC\\n\", 0\n");
    writeAndLoad(vm, img, "test_hostlib_strlen.nci");
    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 17);
}

// puts：真函数输出到进程 stdout（msvcrt 与测试宿主分属不同 CRT，输出捕获
// 不可靠，仅断言返回值非负——标准只承诺成功返回非负值，msvcrt 实际返回 0；
// 实际输出经 CLI 冒烟验证）
TEST(HostLibTest, MsvcrtPutsReturnsNonNegative) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern puts\n"
                        "export main\n"
                        "main:\n"
                        "    lea R0, .msg\n"
                        "    callx puts\n"
                        "    ret\n"
                        ".msg:\n"
                        "    db \"Hello from NanoC\\n\", 0\n");
    writeAndLoad(vm, img, "test_hostlib_puts.nci");
    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    vm.start();
    EXPECT_GE(vm.getRegister(0), 0);
}

// 同一程序混合多个动态导入：各自伪地址登记、多站点互不串扰
TEST(HostLibTest, MsvcrtMixedCalls) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern abs\n"
                        "extern atoi\n"
                        "export main\n"
                        "main:\n"
                        "    lmm R0, -13\n"
                        "    callx abs\n"
                        "    mov R6, R0\n"
                        "    lea R0, .s\n"
                        "    callx atoi\n"
                        "    ret\n"
                        ".s:\n"
                        "    db \"-123\", 0\n");
    writeAndLoad(vm, img, "test_hostlib_mixed.nci");
    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    vm.start();
    EXPECT_EQ(vm.getRegister(6), 13);   // abs(-13)
    EXPECT_EQ(vm.getRegister(0), -123); // atoi("-123")
    ASSERT_EQ(vm.getImports().size(), 2u);
    EXPECT_EQ(vm.getImports()[0].addr, DYNAMIC_HOST_BASE);     // abs
    EXPECT_EQ(vm.getImports()[1].addr, DYNAMIC_HOST_BASE + 4); // atoi
}

// ==== 报错路径 ====

// 符号不在库中：返回 false，报错指明符号名与库名
TEST(HostLibTest, MissingSymbolReportsNameAndLibrary) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern no_such_crt_function\n"
                        "export main\n"
                        "main:\n"
                        "    ret\n");
    writeAndLoad(vm, img, "test_hostlib_missing.nci");

    testing::internal::CaptureStdout();
    EXPECT_FALSE(vm.loadHostLibrary("msvcrt.dll"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("no_such_crt_function"), std::string::npos);
    EXPECT_NE(output.find("msvcrt.dll"), std::string::npos);
}

// 符号在库中但不在签名白名单：明确报错，不做错误 ABI 调用
TEST(HostLibTest, SymbolWithoutWrapperRejected) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern qsort\n"
                        "export main\n"
                        "main:\n"
                        "    ret\n");
    writeAndLoad(vm, img, "test_hostlib_qsort.nci");

    testing::internal::CaptureStdout();
    EXPECT_FALSE(vm.loadHostLibrary("msvcrt.dll"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("qsort"), std::string::npos);
    EXPECT_NE(output.find("wrapper"), std::string::npos);
}

// 未提供宿主库（或解析失败仍启动）：callx 站点运行时报错并回查导入表给出符号名
TEST(HostLibTest, UnresolvedDynamicImportRuntimeErrorNamesSymbol) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern puts\n"
                        "export main\n"
                        "main:\n"
                        "    lea R0, .msg\n"
                        "    callx puts\n"
                        "    ret\n"
                        ".msg:\n"
                        "    db \"x\", 0\n");
    writeAndLoad(vm, img, "test_hostlib_unresolved.nci");

    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("puts"), std::string::npos);
    EXPECT_NE(output.find("CALLX"), std::string::npos);
    EXPECT_EQ(vm.getPC(), vm.getCodeSize()); // 停在错误处
}

// ==== 与既有机制的协同 ====

// resolveImportsByName 同样按伪地址登记动态导入（按名注册表路径）
TEST(HostLibTest, ResolveImportsByNameRegistersAtPseudoAddress) {
    NVirtualMachine vm(64 * 1024);
    vm.registerHostFunction("my_add", hostAdd);
    auto img = asmImage("extern my_add\n"
                        "export main\n"
                        "main:\n"
                        "    lmm R0, 40\n"
                        "    lmm R1, 2\n"
                        "    callx my_add\n"
                        "    ret\n");
    writeAndLoad(vm, img, "test_hostlib_byname.nci");

    ASSERT_TRUE(vm.resolveImportsByName());
    EXPECT_EQ(vm.getImports()[0].addr, DYNAMIC_HOST_BASE);
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 42);
}

// 显式地址 extern（静态绑定）不被宿主库解析（即使符号缺失也不报错），
// 按地址注册的宿主函数照常命中
TEST(HostLibTest, StaticBindingUntouchedByHostLibrary) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern hostadd 0x7F000010\n"
                        "export main\n"
                        "main:\n"
                        "    lmm R0, 40\n"
                        "    lmm R1, 2\n"
                        "    callx hostadd\n"
                        "    ret\n");
    writeAndLoad(vm, img, "test_hostlib_static.nci");

    // 静态绑定跳过解析：库中没有 hostadd 也不影响 loadHostLibrary 结果
    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    EXPECT_EQ(vm.getImports()[0].addr, 0x7F000010);
    EXPECT_EQ(vm.getImports()[0].flags & IMPORT_FLAG_DYNAMIC, 0);

    vm.registerHostFunction(0x7F000010, hostAdd);
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 42);
}

// 重复解析幂等：两次 loadHostLibrary 不重复登记、伪地址不变
TEST(HostLibTest, RepeatedResolutionIdempotent) {
    NVirtualMachine vm(64 * 1024);
    auto img = asmImage("extern abs\n"
                        "export main\n"
                        "main:\n"
                        "    lmm R0, -1\n"
                        "    callx abs\n"
                        "    ret\n");
    writeAndLoad(vm, img, "test_hostlib_repeat.nci");

    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
    EXPECT_EQ(vm.getImports()[0].addr, DYNAMIC_HOST_BASE);
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 1);
}

#endif // _WIN32
