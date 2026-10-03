// VM 侧 NCI v2.1 加载器、CALLX 宿主分发与动态链接用例
// 手工按钉死布局拼字节构造 v2.1 二进制：header(32B) | code | data | import table | export
// table
#include "nvm/core.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ==== v2.1 手工二进制构造 helper ====

// 追加小端 32 位整数
static void putI32(std::vector<uint8_t>& v, int32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// 表项：int32 nameLen + name + NUL + pad 到 4 字节对齐（以 entry 起始为基准）+ addr +
// flags
static void appendTableEntry(std::vector<uint8_t>& v,
                             const std::string& name,
                             int32_t addr,
                             int32_t flags) {
    const size_t entryStart = v.size();
    putI32(v, static_cast<int32_t>(name.size()));
    v.insert(v.end(), name.begin(), name.end());
    v.push_back(0); // NUL
    while ((v.size() - entryStart) % 4 != 0)
        v.push_back(0);
    putI32(v, addr);
    putI32(v, flags);
}

// 符号表项描述
struct SymSpec {
    std::string name;
    int32_t addr;
    int32_t flags;
};

static void appendTable(std::vector<uint8_t>& v, const std::vector<SymSpec>& syms) {
    for (const auto& s : syms)
        appendTableEntry(v, s.name, s.addr, s.flags);
}

// 组装 v2.1 文件：header(32B) | code | data | import table | export table
static std::vector<uint8_t> buildV21(const std::vector<uint8_t>& code,
                                     const std::vector<uint8_t>& data,
                                     const std::vector<SymSpec>& imports,
                                     const std::vector<SymSpec>& exports,
                                     int32_t entryPoint) {
    std::vector<uint8_t> v;
    const char magic[8] = { 'N', 'a', 'n', 'o', 'C', 0, 0, 0 };
    v.insert(v.end(), magic, magic + 8);
    putI32(v, 32);                                   // headerSize
    putI32(v, static_cast<int32_t>(code.size()));    // codeSize
    putI32(v, static_cast<int32_t>(data.size()));    // dataSize
    putI32(v, static_cast<int32_t>(imports.size())); // importCount
    putI32(v, static_cast<int32_t>(exports.size())); // exportCount
    putI32(v, entryPoint);
    v.insert(v.end(), code.begin(), code.end());
    v.insert(v.end(), data.begin(), data.end());
    appendTable(v, imports);
    appendTable(v, exports);
    return v;
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

// ==== 测试用宿主函数 ====

// fastcall 宿主：返回 R0 + R1
static int32_t host_add(int32_t* regs, int8_t* mem, int32_t memSize) {
    (void)mem;
    (void)memSize;
    return regs[0] + regs[1];
}

// cdecl 宿主：从栈上（mem + R4=SP）读两个参数相加，调用方负责清栈
static int32_t host_add_cdecl(int32_t* regs, int8_t* mem, int32_t memSize) {
    (void)memSize;
    int32_t a = 0;
    int32_t b = 0;
    memcpy(&a, mem + regs[4], 4);
    memcpy(&b, mem + regs[4] + 4, 4);
    return a + b;
}

// 宿主 puts：读 C 字符串输出并返回长度（受 memSize 上界保护）
static int32_t host_puts(int32_t* regs, int8_t* mem, int32_t memSize) {
    const char* s = reinterpret_cast<const char*>(mem + regs[0]);
    int32_t n = 0;
    while (regs[0] + n < memSize && s[n] != '\0')
        n++;
    fwrite(s, 1, static_cast<size_t>(n), stdout);
    return n;
}

// 读取栈上 32 位值（小端）
static int32_t readStackInt32(NVirtualMachine& vm, int32_t addr) {
    int32_t v = 0;
    memcpy(&v, vm.getStack() + addr, sizeof(v));
    return v;
}

// ==== 旧裸格式 fallback ====

// 无魔数文件仍按裸代码加载（整文件当代码，pc 从 0 起）
TEST(LoaderTest, RawFallbackPreserved) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x00, 0x00 }; // lmm R0, imm32
    putI32(code, 42);
    writeAndLoad(vm, code, "test_loader_raw.nca");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42);
    EXPECT_EQ(vm.getCodeSize(), 6); // 整文件 = 代码段
}

// ==== 头部校验错误 ====

// magic 须全 8 字节匹配（"NanoC" 前缀命中严格路径，第 8 字节错即报错）
TEST(LoaderTest, HeaderMagicValidationError) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x62 }; // ret
    auto bytes = buildV21(code, {}, {}, {}, 0);
    bytes[7] = 0x58; // 破坏 magic 第 8 字节
    EXPECT_THROW(writeAndLoad(vm, bytes, "test_loader_bad_magic.nca"),
                 std::runtime_error);
}

// headerSize != 32 报错
TEST(LoaderTest, HeaderSizeValidationError) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x62 };
    auto bytes = buildV21(code, {}, {}, {}, 0);
    bytes[8] = 64; // headerSize = 64
    EXPECT_THROW(writeAndLoad(vm, bytes, "test_loader_bad_hdrsize.nca"),
                 std::runtime_error);
}

// 长度越界（codeSize 超出文件）报错
TEST(LoaderTest, LengthOverflowValidationError) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x62 };
    auto bytes = buildV21(code, {}, {}, {}, 0);
    bytes[12] = 0xFF;
    bytes[13] = 0xFF;
    bytes[14] = 0xFF;
    bytes[15] = 0x7F; // codeSize = 0x7FFFFFFF
    EXPECT_THROW(writeAndLoad(vm, bytes, "test_loader_bad_csize.nca"),
                 std::runtime_error);
}

// 长度非法（负的 dataSize）报错
TEST(LoaderTest, NegativeSizeValidationError) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x62 };
    auto bytes = buildV21(code, {}, {}, {}, 0);
    bytes[19] = 0x80; // dataSize < 0
    EXPECT_THROW(writeAndLoad(vm, bytes, "test_loader_neg_dsize.nca"),
                 std::runtime_error);
}

// ==== 两表解析与数据段 ====

// 导入/导出表逐项断言（名字/addr/flags），表起点非 4 对齐（codeSize=1）验证 entry
// 基准对齐
TEST(LoaderTest, TablesParsedCorrectly) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x62 }; // ret（1 字节 → 表起点为奇数偏移）
    std::vector<uint8_t> data = { 0x11, 0x22, 0x33, 0x44 };
    std::vector<SymSpec> imports = { { "host_add", 0x7F000001, 0 }, // fastcall
                                     { "printf", 0x7F000002, 1 } }; // cdecl
    std::vector<SymSpec> exports = { { "main", 0, 0 } };
    auto bytes = buildV21(code, data, imports, exports, 0);
    writeAndLoad(vm, bytes, "test_loader_tables.nca");

    ASSERT_EQ(vm.getImports().size(), 2u);
    EXPECT_EQ(vm.getImports()[0].name, "host_add");
    EXPECT_EQ(vm.getImports()[0].addr, 0x7F000001);
    EXPECT_EQ(vm.getImports()[0].flags, 0);
    EXPECT_EQ(vm.getImports()[1].name, "printf");
    EXPECT_EQ(vm.getImports()[1].addr, 0x7F000002);
    EXPECT_EQ(vm.getImports()[1].flags, 1);

    ASSERT_EQ(vm.getExports().size(), 1u);
    EXPECT_EQ(vm.getExports()[0].name, "main");
    EXPECT_EQ(vm.getExports()[0].addr, 0);
    EXPECT_EQ(vm.getExports()[0].flags, 0);

    EXPECT_EQ(vm.getCodeSize(), 1);
    EXPECT_EQ(vm.getDataSize(), 4);
    EXPECT_EQ(vm.getPC(), 0); // entryPoint
    // 数据段加载点 = m_stack[codeSize .. codeSize+dataSize)
    EXPECT_EQ(memcmp(vm.getStack() + 1, data.data(), 4), 0);
}

// entryPoint 决定执行起点
TEST(LoaderTest, EntryPointDeterminesStart) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code;
    code = { 0x00, 0x00 };
    putI32(code, 1); // @0  lmm R0, 1
    code.push_back(0x00);
    code.push_back(0x01);
    putI32(code, 2); // @6  lmm R1, 2
    code.push_back(0x00);
    code.push_back(0x02);
    putI32(code, 3);                            // @12 lmm R2, 3
    auto bytes = buildV21(code, {}, {}, {}, 6); // entry = 6
    writeAndLoad(vm, bytes, "test_loader_entry.nca");

    EXPECT_EQ(vm.getPC(), 6);
    vm.start();
    EXPECT_EQ(vm.getRegister(0), 0); // 被跳过
    EXPECT_EQ(vm.getRegister(1), 2);
    EXPECT_EQ(vm.getRegister(2), 3);
}

// 数据段 STOREA 写 → LOADA 读回；文件预置数据同样可读
TEST(LoaderTest, DataSegmentStoreLoadRoundTrip) {
    NVirtualMachine vm(64 * 1024);
    const int32_t codeSize = 24;    // lmm(6) + storea(6) + loada(6) + loada(6)
    const int32_t slot0 = codeSize; // 数据标号地址 = codeSize + 段内偏移
    const int32_t slot1 = codeSize + 4;

    std::vector<uint8_t> data = { 0xEF, 0xBE, 0xAD, 0xDE, 0x00, 0x00, 0x00, 0x00 };
    std::vector<uint8_t> code;
    code = { 0x00, 0x01 };
    putI32(code, 0x5A); // @0  lmm R1, 0x5A
    code.push_back(0x06);
    code.push_back(0x01);
    putI32(code, slot1); // @6  storea R1, slot1
    code.push_back(0x05);
    code.push_back(0x00);
    putI32(code, slot1); // @12 loada R0, slot1
    code.push_back(0x05);
    code.push_back(0x03);
    putI32(code, slot0); // @18 loada R3, slot0
    auto bytes = buildV21(code, data, {}, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_data.nca");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 0x5A);
    EXPECT_EQ(vm.getRegister(3), static_cast<int32_t>(0xDEADBEEF)); // 文件预置数据
    EXPECT_EQ(readStackInt32(vm, slot1), 0x5A);
}

// ==== CALLX 静态宿主调用 ====

// fastcall：R0/R1 传参，宿主返回值写 R0；CALLX 不压返回地址
TEST(LoaderTest, CallxStaticHostFastcall) {
    NVirtualMachine vm(64 * 1024);
    const int32_t HOST_ADDR = 0x7F000001;
    vm.registerHostFunction(HOST_ADDR, host_add);

    std::vector<uint8_t> code;
    code = { 0x00, 0x00 };
    putI32(code, 7); // @0  lmm R0, 7
    code.push_back(0x00);
    code.push_back(0x01);
    putI32(code, 35); // @6  lmm R1, 35
    code.push_back(0x61);
    putI32(code, HOST_ADDR); // @12 callx HOST_ADDR
    std::vector<SymSpec> imports = { { "host_add", HOST_ADDR, 0 } };
    auto bytes = buildV21(code, {}, imports, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_callx_fast.nca");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42);
    EXPECT_EQ(vm.getSP(), vm.getStackSize() - 4); // 仅剩栈底哨兵：CALLX 未压返回地址
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}

// cdecl：宿主从栈上（mem + R4=SP）读参数，调用方 addi R4 清栈
TEST(LoaderTest, CallxStaticHostCdecl) {
    NVirtualMachine vm(64 * 1024);
    const int32_t HOST_ADDR = 0x7F000002;
    vm.registerHostFunction(HOST_ADDR, host_add_cdecl);

    std::vector<uint8_t> code;
    code = { 0x00, 0x00 };
    putI32(code, 20); // @0  lmm R0, 20
    code.push_back(0x40);
    code.push_back(0x00); // @6  push R0
    code.push_back(0x00);
    code.push_back(0x00);
    putI32(code, 22); // @8  lmm R0, 22
    code.push_back(0x40);
    code.push_back(0x00); // @14 push R0
    code.push_back(0x61);
    putI32(code, HOST_ADDR); // @16 callx HOST_ADDR
    code.push_back(0x70);
    code.push_back(0x06);
    code.push_back(0x00); // @21 mov R6, R0
    code.push_back(0x11);
    code.push_back(0x04);
    putI32(code, 8); // @24 addi R4, 8（调用方清栈）
    std::vector<SymSpec> imports = { { "host_add2", HOST_ADDR, 1 } };
    auto bytes = buildV21(code, {}, imports, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_callx_cdecl.nca");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42);
    EXPECT_EQ(vm.getRegister(6), 42);
    EXPECT_EQ(vm.getSP(), vm.getStackSize() - 4); // 压入两参已由 addi 清掉，剩哨兵
}

// CALLX 未解析地址：运行时错误，报错并停止执行（后继指令不执行）
TEST(LoaderTest, CallxUnresolvedAddressStopsExecution) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code;
    code.push_back(0x61);
    putI32(code, 0x12345678); // @0 callx 0x12345678（未注册）
    code.push_back(0x00);
    code.push_back(0x00);
    putI32(code, 99); // @5 lmm R0, 99（不应执行）
    auto bytes = buildV21(code, {}, {}, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_callx_bad.nca");

    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("CALLX"), std::string::npos);
    EXPECT_EQ(vm.getRegister(0), 0);         // 后继指令未执行
    EXPECT_EQ(vm.getPC(), vm.getCodeSize()); // 停在错误处
}

// ==== 动态链接 ====

#ifdef _WIN32

// import "GetTickCount" addr=0 + loadHostLibrary("kernel32.dll")：
// 地址从 0x7F000000 起分配并回填，callx 两次返回非递减
TEST(LoaderTest, DynamicLinkKernel32GetTickCount) {
    NVirtualMachine vm(64 * 1024);
    const int32_t EXPECTED_ADDR = 0x7F000000; // 首个解析符号的分配地址

    std::vector<uint8_t> code;
    code.push_back(0x61);
    putI32(code, EXPECTED_ADDR); // @0 callx GetTickCount
    code.push_back(0x70);
    code.push_back(0x06);
    code.push_back(0x00); // @5 mov R6, R0
    code.push_back(0x61);
    putI32(code, EXPECTED_ADDR); // @8 callx GetTickCount
    std::vector<SymSpec> imports = { { "GetTickCount", 0, 0 } };
    auto bytes = buildV21(code, {}, imports, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_dynlink.nca");

    ASSERT_TRUE(vm.loadHostLibrary("kernel32.dll"));
    ASSERT_EQ(vm.getImports()[0].addr, EXPECTED_ADDR); // 从 0x7F000000 起分配并回填
    vm.start();

    int32_t first = vm.getRegister(6);
    int32_t second = vm.getRegister(0);
    EXPECT_GE(first, 0);
    EXPECT_GE(second, first); // 非递减
}

#endif // _WIN32

// resolveImportsByName：按名注册表解析 addr=0 导入并回填地址
TEST(LoaderTest, ResolveImportsByName) {
    NVirtualMachine vm(64 * 1024);
    vm.registerHostFunction("my_add", host_add); // 按名注册（load 前后皆可）
    const int32_t EXPECTED_ADDR = 0x7F000000;

    std::vector<uint8_t> code;
    code = { 0x00, 0x00 };
    putI32(code, 40); // @0  lmm R0, 40
    code.push_back(0x00);
    code.push_back(0x01);
    putI32(code, 2); // @6  lmm R1, 2
    code.push_back(0x61);
    putI32(code, EXPECTED_ADDR); // @12 callx my_add
    std::vector<SymSpec> imports = { { "my_add", 0, 0 } };
    auto bytes = buildV21(code, {}, imports, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_byname.nca");

    ASSERT_TRUE(vm.resolveImportsByName());
    ASSERT_EQ(vm.getImports()[0].addr, EXPECTED_ADDR);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42);
}

// 按名注册表缺失符号：resolveImportsByName 返回 false 并指明符号
TEST(LoaderTest, ResolveImportsByNameMissingSymbol) {
    NVirtualMachine vm(64 * 1024);
    std::vector<uint8_t> code = { 0x62 }; // ret
    std::vector<SymSpec> imports = { { "no_such_fn", 0, 0 } };
    auto bytes = buildV21(code, {}, imports, {}, 0);
    writeAndLoad(vm, bytes, "test_loader_byname_missing.nca");

    testing::internal::CaptureStdout();
    EXPECT_FALSE(vm.resolveImportsByName());
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("no_such_fn"), std::string::npos);
}

// ==== 组合程序 ====

// 数据段字符串 + lea/push/callx host_puts 读 C 字符串 + addi 清栈 + leave/ret 哨兵终止
TEST(LoaderTest, CombinedProgramHostPuts) {
    NVirtualMachine vm(64 * 1024);
    const int32_t HOST_ADDR = 0x7F000000;
    vm.registerHostFunction(HOST_ADDR, host_puts);

    const std::string msg = "Hello, NanoC!\n";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    data.push_back(0);
    const int32_t codeSize = 24;
    const int32_t MSG_ADDR = codeSize; // 数据标号地址 = codeSize + 段内偏移 0

    std::vector<uint8_t> code;
    code.push_back(0x43);
    code.push_back(0x00);
    code.push_back(0x00); // @0  enter 0
    code.push_back(0x02);
    code.push_back(0x00);
    putI32(code, MSG_ADDR); // @3  lea R0, MSG_ADDR
    code.push_back(0x40);
    code.push_back(0x00); // @9  push R0
    code.push_back(0x61);
    putI32(code, HOST_ADDR); // @11 callx puts
    code.push_back(0x11);
    code.push_back(0x04);
    putI32(code, 4);                                             // @16 addi R4, 4（清栈）
    code.push_back(0x44);                                        // @22 leave
    code.push_back(0x62);                                        // @23 ret → 弹出哨兵终止
    std::vector<SymSpec> imports = { { "puts", HOST_ADDR, 1 } }; // cdecl
    std::vector<SymSpec> exports = { { "main", 0, 0 } };
    auto bytes = buildV21(code, data, imports, exports, 0);
    writeAndLoad(vm, bytes, "test_loader_combined.nca");

    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("Hello, NanoC!"), std::string::npos);
    EXPECT_EQ(vm.getRegister(0), static_cast<int32_t>(msg.size())); // host_puts 返回长度
    EXPECT_EQ(vm.getSP(), vm.getStackSize()); // 哨兵被 ret 消费，栈复原
    EXPECT_EQ(vm.getBP(), 0);
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}
