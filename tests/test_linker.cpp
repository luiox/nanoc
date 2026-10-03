// NCI v2.1 链接器用例（PRD R7）：字节级（header/平移/符号表）+ 执行级（跨模块
// callx/数据引用/宿主注册）+ 错误路径。目标模块统一用真实汇编器产出。
#include "nas/instruction.hpp"
#include "nas/linker.hpp"
#include "nvm/core.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ==== helpers ====

// 汇编源码 → 目标镜像（断言汇编成功）
static std::vector<uint8_t> asmObj(const std::string& src) {
    AssemblyResult r = Assembler::assemble(src);
    EXPECT_TRUE(r.ok) << "line " << r.errorLine << ": " << r.errorMessage;
    return r.image;
}

// 追加小端 32 位整数
static void putI32(std::vector<uint8_t>& v, int32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// 读取镜像内小端 32 位整数
static int32_t getI32(const std::vector<uint8_t>& v, size_t off) {
    int32_t x = 0;
    memcpy(&x, v.data() + off, sizeof(x));
    return x;
}

// 写二进制文件
static void writeBinary(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// 写临时文件并加载进 VM（load 读取完整文件后即可删除）
static void
writeAndLoad(NVirtualMachine& vm, const std::vector<uint8_t>& bytes, const char* name) {
    writeBinary(name, bytes);
    vm.load(name);
    std::remove(name);
}

// 测试宿主函数：返回 R0 + R1
static int32_t hostAdd(int32_t* regs, int8_t*, int32_t) { return regs[0] + regs[1]; }

// ==== 字节级：header / 重定位 / 导出地址 ====

// 两模块链接：已解析 callx 改写为平移后内部地址、导入移除、导出地址平移、
// 代码顺序拼接、pad 字段保持
TEST(LinkerTest, TwoModulesHeaderCallxRewriteAndExportShift) {
    auto a = asmObj("extern bfunc\n"
                    "export main\n"
                    "main:\n"
                    "    lmm R0, 5\n"
                    "    callx bfunc\n"
                    "    ret\n");
    auto b = asmObj("export bfunc\n"
                    "bfunc:\n"
                    "    addi R0, 37\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    ASSERT_EQ(r.image.size(), 91u); // 32 + code(12+7) + 导出表 2*20
    EXPECT_EQ(memcmp(r.image.data(), "NanoC\0\0\0", 8), 0);
    EXPECT_EQ(getI32(r.image, 8), 32);  // headerSize
    EXPECT_EQ(getI32(r.image, 12), 19); // codeSize = 12 + 7
    EXPECT_EQ(getI32(r.image, 16), 0);  // dataSize
    EXPECT_EQ(getI32(r.image, 20), 0);  // importCount：已解析导入移除
    EXPECT_EQ(getI32(r.image, 24), 2);  // exportCount
    EXPECT_EQ(getI32(r.image, 28), 0);  // entryPoint = main

    // 模块 1 代码原样前移：LMM 立即数不平移，callx imm 改写为 bfunc 平移后地址 12
    EXPECT_EQ(getI32(r.image, 32 + 2), 5);
    EXPECT_EQ(r.image[32 + 6], 0x61);
    EXPECT_EQ(getI32(r.image, 32 + 7), 12);
    EXPECT_EQ(r.image[32 + 11], 0x62);
    // 模块 2 代码原样拼接在 codeBase = 12 处
    static const uint8_t bCode[] = { 0x11, 0x00, 0x25, 0x00, 0x00, 0x00, 0x62 };
    EXPECT_EQ(memcmp(r.image.data() + 32 + 12, bCode, sizeof(bCode)), 0);

    // 导出表字节级断言（含 pad）：main@0（pad 3）+ bfunc@12（pad 2）
    auto appendExport =
      [](std::vector<uint8_t>& v, const std::string& name, int32_t addr) {
          const size_t start = v.size();
          putI32(v, static_cast<int32_t>(name.size()));
          v.insert(v.end(), name.begin(), name.end());
          v.push_back(0); // NUL
          while ((v.size() - start) % 4 != 0)
              v.push_back(0); // pad（entry 起始基准 4 对齐）
          putI32(v, addr);
          putI32(v, 0); // flags 恒 0
      };
    std::vector<uint8_t> expectExports;
    appendExport(expectExports, "main", 0);
    appendExport(expectExports, "bfunc", 12);
    ASSERT_EQ(r.image.size() - 32 - 19, expectExports.size());
    EXPECT_EQ(
      memcmp(r.image.data() + 32 + 19, expectExports.data(), expectExports.size()),
      0);
}

// 数据段顺序拼接 + 统一编址平移：模块内数据标号按 dataBase 平移，跨模块数据引用
// 经导入解析改写，数据段本身不做扫描重定位（dd 常量保持原值）
TEST(LinkerTest, DataSegmentMergeAndCrossModuleLea) {
    auto a = asmObj("extern bdata\n"
                    "export main\n"
                    "main:\n"
                    "    loada R0, bdata\n"
                    "    lea R1, .val\n"
                    "    ret\n"
                    ".val:\n"
                    "    dd 77\n");
    auto b = asmObj("export bdata\n"
                    "bdata:\n"
                    "    dd 2\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    EXPECT_EQ(getI32(r.image, 12), 13); // codeSize = 13 + 0（模块 2 无代码）
    EXPECT_EQ(getI32(r.image, 16), 8);  // dataSize = 4 + 4
    EXPECT_EQ(getI32(r.image, 20), 0);  // bdata 导入已内部解析
    EXPECT_EQ(getI32(r.image, 24), 2);
    EXPECT_EQ(getI32(r.image, 28), 0); // entryPoint = main

    // loada R0, bdata：imm 0（导入地址）→ 改写为 bdata 平移后数据地址 17
    // （dataBase_B = codeSize_A + dataSize_A = 13 + 4）
    EXPECT_EQ(r.image[32], 0x05);
    EXPECT_EQ(getI32(r.image, 32 + 2), 17);
    // lea R1, .val：模块内数据地址 13 → +dataBase_A(0) → 13
    EXPECT_EQ(r.image[32 + 6], 0x02);
    EXPECT_EQ(getI32(r.image, 32 + 8), 13);
    // 数据段顺序拼接；dd 2 虽落在模块 2 统一编址范围内也不平移（无重定位信息）
    static const uint8_t expectData[] = {
        0x4D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
    };
    EXPECT_EQ(memcmp(r.image.data() + 32 + 13, expectData, sizeof(expectData)), 0);
    // 导出数据标号：bdata = 17
    EXPECT_EQ(getI32(r.image, 32 + 13 + 8 + 20 + 12), 17);
}

// 模块 2 的 CALL/JMP（代码地址）与 ST/LOADA/LEA（数据地址）按各自基址平移
TEST(LinkerTest, SecondModuleCodeAndDataRelocation) {
    auto a = asmObj("export main\n"
                    "main:\n"
                    "    ret\n");
    auto b = asmObj("export bstart\n"
                    "bstart:\n"
                    "    call .h\n"
                    "    jmp .h\n"
                    "    st R2, .d\n"
                    "    loada R3, .d\n"
                    "    lea R2, .d\n"
                    "    ret\n"
                    ".h:\n"
                    "    ret\n"
                    ".d:\n"
                    "    dd 9\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    EXPECT_EQ(getI32(r.image, 12), 31); // codeSize = 1 + 30
    EXPECT_EQ(getI32(r.image, 16), 4);
    EXPECT_EQ(r.image[32], 0x62); // 模块 1 的 ret
    // 模块 2 代码整体平移 codeBase = 1：call/jmp 目标 .h(29) → 30
    EXPECT_EQ(r.image[33], 0x60);
    EXPECT_EQ(getI32(r.image, 34), 30);
    EXPECT_EQ(r.image[38], 0x50);
    EXPECT_EQ(getI32(r.image, 39), 30);
    // 数据地址 30（c_B + 0）→ +dataBase_B(1) → 31
    EXPECT_EQ(r.image[43], 0x01);
    EXPECT_EQ(getI32(r.image, 45), 31); // st
    EXPECT_EQ(r.image[49], 0x05);
    EXPECT_EQ(getI32(r.image, 51), 31); // loada
    EXPECT_EQ(r.image[55], 0x02);
    EXPECT_EQ(getI32(r.image, 57), 31); // lea
    // dd 9 不做数据段重定位
    EXPECT_EQ(getI32(r.image, 32 + 31), 9);
}

// 单模块链接是恒等变换（基址为 0、未解析导入原序保留、entryPoint 重算不变）
TEST(LinkerTest, SingleModuleLinkIsByteIdentical) {
    auto a = asmObj("extern printf 0x7F000001\n"
                    ".calling_convention cdecl\n"
                    "extern puts 0x7F000002\n"
                    "export main\n"
                    "main:\n"
                    "    enter 0\n"
                    "    lea R0, .msg\n"
                    "    push R0\n"
                    "    callx puts\n"
                    "    addi R4, 4\n"
                    "    callx printf\n"
                    "    leave\n"
                    "    ret\n"
                    ".msg:\n"
                    "    db \"hi\\n\", 0\n");
    LinkResult r = Linker::linkImages({ a });
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(r.image, a);
}

// ==== 字节级：导入表保留 / 去重 / 静态绑定 ====

// 未解析导入逐字节保留（含 pad 字段），importCount 计数正确
TEST(LinkerTest, UnresolvedImportPreservedByteLevel) {
    auto a = asmObj("extern xy 0x7F000009\n"
                    "export main\n"
                    "main:\n"
                    "    callx xy\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    EXPECT_EQ(getI32(r.image, 20), 1); // importCount
    std::vector<uint8_t> expectImport;
    putI32(expectImport, 2);
    expectImport.insert(expectImport.end(), { 'x', 'y' });
    expectImport.push_back(0);
    expectImport.push_back(0); // pad 1（4+2+1=7 → 补至 8）
    putI32(expectImport, 0x7F000009);
    putI32(expectImport, 0);
    const size_t importOff = 32 + getI32(r.image, 12) + getI32(r.image, 16);
    // 导入表 + 导出表（main）
    ASSERT_EQ(r.image.size() - importOff, expectImport.size() + 20u);
    EXPECT_EQ(
      memcmp(r.image.data() + importOff, expectImport.data(), expectImport.size()),
      0);
}

// 跨模块重名导入去重为一条（addr/flags 取首次出现）
TEST(LinkerTest, ImportDedupAcrossModules) {
    auto a = asmObj("extern h 0x7F000001\n"
                    "export main\n"
                    "main:\n"
                    "    callx h\n"
                    "    ret\n");
    auto b = asmObj("extern h 0x7F000001\n"
                    "export bfn\n"
                    "bfn:\n"
                    "    callx h\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    EXPECT_EQ(getI32(r.image, 20), 1);
    const size_t importOff = 32 + getI32(r.image, 12);
    ASSERT_EQ(getI32(r.image, importOff), 1); // nameLen
    EXPECT_EQ(r.image[importOff + 4], 'h');
    EXPECT_EQ(getI32(r.image, importOff + 8), 0x7F000001);
}

// 显式静态宿主绑定（addr != 0）即使名字命中导出也不改写：callx imm 与导入原样保留
TEST(LinkerTest, StaticHostBindingNotRewritten) {
    auto a = asmObj("extern myadd 0x7F000001\n"
                    "export main\n"
                    "main:\n"
                    "    callx myadd\n"
                    "    ret\n");
    auto b = asmObj("export myadd\n"
                    "myadd:\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    EXPECT_EQ(getI32(r.image, 20), 1);
    EXPECT_EQ(r.image[32], 0x61);               // callx myadd
    EXPECT_EQ(getI32(r.image, 33), 0x7F000001); // imm 保持静态宿主地址
    EXPECT_EQ(r.image[32 + 5], 0x62);           // 模块 1 ret
    EXPECT_EQ(r.image[32 + 6], 0x62);           // 模块 2 ret（codeBase = 6）
}

// 手工构造最小 v2.1 目标（用于 nas 不再产出的旧格式 addr=0 导入路径回归）
static std::vector<uint8_t>
handObj(const std::vector<uint8_t>& code,
        const std::vector<std::tuple<std::string, int32_t, int32_t>>& imports,
        const std::vector<std::tuple<std::string, int32_t>>& exports) {
    std::vector<uint8_t> v;
    const char magic[8] = { 'N', 'a', 'n', 'o', 'C', 0, 0, 0 };
    v.insert(v.end(), magic, magic + 8);
    putI32(v, 32);                                // headerSize
    putI32(v, static_cast<int32_t>(code.size())); // codeSize
    putI32(v, 0);                                 // dataSize
    putI32(v, static_cast<int32_t>(imports.size()));
    putI32(v, static_cast<int32_t>(exports.size()));
    putI32(v, 0); // entryPoint
    v.insert(v.end(), code.begin(), code.end());
    for (const auto& [name, addr, flags] : imports) {
        const size_t entryStart = v.size();
        putI32(v, static_cast<int32_t>(name.size()));
        v.insert(v.end(), name.begin(), name.end());
        v.push_back(0);
        while ((v.size() - entryStart) % 4 != 0)
            v.push_back(0);
        putI32(v, addr);
        putI32(v, flags);
    }
    for (const auto& [name, addr] : exports) {
        const size_t entryStart = v.size();
        putI32(v, static_cast<int32_t>(name.size()));
        v.insert(v.end(), name.begin(), name.end());
        v.push_back(0);
        while ((v.size() - entryStart) % 4 != 0)
            v.push_back(0);
        putI32(v, addr);
        putI32(v, 0);
    }
    return v;
}

// ==== 错误路径 ====

TEST(LinkerTest, EmptyInputError) {
    EXPECT_FALSE(Linker::linkImages({}).ok);
    LinkResult files = Linker::linkFiles({});
    EXPECT_FALSE(files.ok);
    LinkResult missing = Linker::linkFiles({ "no_such_object.nci" });
    EXPECT_FALSE(missing.ok);
    EXPECT_NE(missing.errorMessage.find("no_such_object.nci"), std::string::npos);
}

TEST(LinkerTest, BadMagicError) {
    auto a = asmObj("export main\n"
                    "main:\n"
                    "    ret\n");
    auto b = a;
    b[7] = 0x58; // 破坏 magic 第 8 字节
    LinkResult r = Linker::linkImages({ a, b });
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.errorMessage.find("魔数"), std::string::npos);
}

TEST(LinkerTest, UnknownOpcodeError) {
    auto a = asmObj("export main\n"
                    "main:\n"
                    "    ret\n");
    a[32] = 0xFF; // 代码段首字节改为未定义操作码
    LinkResult r = Linker::linkImages({ a });
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.errorMessage.find("操作码"), std::string::npos);
}

TEST(LinkerTest, TruncatedInstructionError) {
    auto a = asmObj("export main\n"
                    "main:\n"
                    "    ret\n");
    a[32] = 0x00; // LMM 需 6 字节，代码段仅 1 字节
    LinkResult r = Linker::linkImages({ a });
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.errorMessage.find("越过代码段末尾"), std::string::npos);
}

TEST(LinkerTest, DuplicateExportError) {
    auto a = asmObj("export f\n"
                    "f:\n"
                    "    ret\n");
    auto b = asmObj("export f\n"
                    "f:\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.errorMessage.find("重复导出"), std::string::npos);
    EXPECT_NE(r.errorMessage.find("f"), std::string::npos);
}

// 动态导入（伪地址 + flags bit2）跨模块内部解析：站点按值一对一命中改写为
// 平移后地址，命中导入移除；未命中的动态导入保留伪地址与 bit2 待加载期解析
TEST(LinkerTest, DynamicExternCrossModuleResolved) {
    auto a = asmObj("extern f\n"
                    "extern g\n"
                    "export main\n"
                    "main:\n"
                    "    callx f\n"
                    "    ret\n");
    auto b = asmObj("export f\n"
                    "f:\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    EXPECT_EQ(getI32(r.image, 20), 1);     // f 已解析移除，g 保留
    EXPECT_EQ(r.image[32], 0x61);          // callx f
    EXPECT_EQ(getI32(r.image, 32 + 1), 6); // 改写为 f 平移后内部地址（codeBase_B = 6）
    EXPECT_EQ(r.image[32 + 6], 0x62);      // 模块 b 的 ret（codeBase = 6）
    // 保留的 g 导入 entry：伪宿主地址 0x7E000004（声明序第二）+ flags bit2
    const size_t importOff = 32 + getI32(r.image, 12) + getI32(r.image, 16);
    EXPECT_EQ(getI32(r.image, importOff), 1); // nameLen
    EXPECT_EQ(r.image[importOff + 4], 'g');
    EXPECT_EQ(getI32(r.image, importOff + 8), 0x7E000004);
    EXPECT_EQ(getI32(r.image, importOff + 12), 0x4);
}

// 同一模块多个动态导入跨模块同名字解析：未解析动态导入站点 imm 归一为输出表
// 首现伪地址（模块间声明序不同导致伪地址差异时由链接器统一）
TEST(LinkerTest, DynamicExternPseudoAddressHarmonized) {
    auto a = asmObj("extern h\n"
                    "extern f\n"
                    "export main\n"
                    "main:\n"
                    "    callx h\n"
                    "    ret\n");
    auto b = asmObj("extern f\n"
                    "extern h\n"
                    "export bfn\n"
                    "bfn:\n"
                    "    callx h\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    // f 未命中导出保留为动态导入；h 同名去重，addr 取首现（模块 a 的 0x7E000000）
    EXPECT_EQ(getI32(r.image, 20), 2);
    const size_t importOff = 32 + getI32(r.image, 12) + getI32(r.image, 16);
    // 导入表按首现序：h（0x7E000000, bit2）、f（0x7E000004, bit2）
    EXPECT_EQ(r.image[importOff + 4], 'h');
    EXPECT_EQ(getI32(r.image, importOff + 8), 0x7E000000);
    EXPECT_EQ(getI32(r.image, importOff + 12), 0x4);
    // h 名部 4+1+1+pad2=8 + addr/flags 8 = 16 字节 → f entry @importOff+16
    EXPECT_EQ(getI32(r.image, importOff + 16), 1); // f nameLen
    EXPECT_EQ(r.image[importOff + 20], 'f');
    EXPECT_EQ(getI32(r.image, importOff + 24), 0x7E000004);
    EXPECT_EQ(getI32(r.image, importOff + 28), 0x4);
    // 模块 a 站点 callx h：imm 已是首现伪地址（恒等）；模块 b 站点 callx h：
    // b 内 h 伪地址为 0x7E000004，归一改写为首现 0x7E000000
    EXPECT_EQ(getI32(r.image, 32 + 1), 0x7E000000);     // 模块 a（codeBase 0）
    EXPECT_EQ(getI32(r.image, 32 + 6 + 1), 0x7E000000); // 模块 b（codeBase 6）
}

// 旧格式 addr=0 导入（手工构造，nas 已不产出）共享地址值且其一可内部解析：
// 站点无法按值消歧 → 报错（不静默错链）
TEST(LinkerTest, AmbiguousZeroAddrImportsError) {
    // 模块 a：callx 0（imm=0），导入 f/g 均 addr=0 flags=0
    std::vector<uint8_t> aCode = { 0x61, 0, 0, 0, 0, 0x62 };
    auto a = handObj(aCode, { { "f", 0, 0 }, { "g", 0, 0 } }, { { "main", 0 } });
    // 模块 b：导出 f
    std::vector<uint8_t> bCode = { 0x62 };
    auto b = handObj(bCode, {}, { { "f", 0 } });
    LinkResult r = Linker::linkImages({ a, b });
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.errorMessage.find("f"), std::string::npos);
    EXPECT_NE(r.errorMessage.find("消歧"), std::string::npos);
}

// 重名导入调用约定冲突 → 报错
TEST(LinkerTest, ImportFlagsConflictError) {
    auto a = asmObj("extern h\n"
                    "export main\n"
                    "main:\n"
                    "    callx h\n"
                    "    ret\n");
    auto b = asmObj(".calling_convention cdecl\n"
                    "extern h\n"
                    "export bfn\n"
                    "bfn:\n"
                    "    callx h\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.errorMessage.find("h"), std::string::npos);
}

// ==== entryPoint 规则 ====

TEST(LinkerTest, EntryPointMainExportWinsOverFirstExport) {
    auto a = asmObj("export aaa\n"
                    "export main\n"
                    "main:\n"
                    "    ret\n"
                    "aaa:\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a });
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(getI32(r.image, 28), 0); // main@0 优先于第一个导出 aaa@1
}

TEST(LinkerTest, EntryPointFirstExportWhenNoMain) {
    auto a = asmObj("export foo\n"
                    "foo:\n"
                    "    ret\n");
    auto b = asmObj("export bar\n"
                    "bar:\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(getI32(r.image, 28), 0); // 第一个导出符号 foo@0
}

TEST(LinkerTest, EntryPointZeroWithoutExports) {
    auto a = asmObj("    ret\n");
    LinkResult r = Linker::linkImages({ a });
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(getI32(r.image, 24), 0); // exportCount
    EXPECT_EQ(getI32(r.image, 28), 0);
}

// ==== 执行级 ====

// 跨模块调用：A callx B 导出函数（B 内计算），加载链接产物执行断言 R0
TEST(LinkerTest, CrossModuleCallExecution) {
    auto a = asmObj("extern bfunc\n"
                    "export main\n"
                    "main:\n"
                    "    lmm R0, 5\n"
                    "    callx bfunc\n"
                    "    ret\n");
    auto b = asmObj("export bfunc\n"
                    "bfunc:\n"
                    "    addi R0, 37\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    NVirtualMachine vm(64 * 1024);
    writeAndLoad(vm, r.image, "test_linker_call.nci");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42);         // B 内 addi R0, 37 生效
    EXPECT_EQ(vm.getImports().size(), 0u);    // 无残留导入
    EXPECT_EQ(vm.getSP(), vm.getStackSize()); // callx 内部路径压栈 + ret 与哨兵复原
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
}

// 跨模块数据引用：A 的代码 loada B 导出的数据标号
TEST(LinkerTest, CrossModuleDataExecution) {
    auto a = asmObj("extern bdata\n"
                    "export main\n"
                    "main:\n"
                    "    loada R0, bdata\n"
                    "    ret\n");
    auto b = asmObj("export bdata\n"
                    "bdata:\n"
                    "    dd 2\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    NVirtualMachine vm(64 * 1024);
    writeAndLoad(vm, r.image, "test_linker_data.nci");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 2); // 读到 B 的 dd 2
}

// 未解析导入保留 + 静态地址宿主注册照常
TEST(LinkerTest, UnresolvedImportHostRegistrationExecution) {
    auto a = asmObj("extern hostadd 0x7F000005\n"
                    "export main\n"
                    "main:\n"
                    "    lmm R0, 40\n"
                    "    lmm R1, 2\n"
                    "    callx hostadd\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    NVirtualMachine vm(64 * 1024);
    writeAndLoad(vm, r.image, "test_linker_host.nci");
    ASSERT_EQ(vm.getImports().size(), 1u);
    EXPECT_EQ(vm.getImports()[0].name, "hostadd");
    EXPECT_EQ(vm.getImports()[0].addr, 0x7F000005);
    vm.registerHostFunction(0x7F000005, hostAdd);
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 42);
}

// 同一程序内 CALLX 双语义并存：内部地址（链接改写）按 CALL、宿主静态地址查表
TEST(LinkerTest, CallxMixedInternalAndHostExecution) {
    auto a = asmObj("extern bfunc\n"
                    "extern hostadd 0x7F000002\n"
                    "export main\n"
                    "main:\n"
                    "    lmm R0, 1\n"
                    "    callx bfunc\n"
                    "    lmm R1, 10\n"
                    "    callx hostadd\n"
                    "    ret\n");
    auto b = asmObj("export bfunc\n"
                    "bfunc:\n"
                    "    addi R0, 1\n"
                    "    ret\n");
    LinkResult r = Linker::linkImages({ a, b });
    ASSERT_TRUE(r.ok) << r.errorMessage;

    NVirtualMachine vm(64 * 1024);
    writeAndLoad(vm, r.image, "test_linker_mixed.nci");
    vm.registerHostFunction(0x7F000002, hostAdd);
    vm.start();

    // callx bfunc → 内部（1+1=2）；callx hostadd → 宿主（2+10=12）
    EXPECT_EQ(vm.getRegister(0), 12);
}

// linkFiles 与 linkImages 结果一致（真实文件读取路径）
TEST(LinkerTest, LinkFilesMatchesLinkImages) {
    auto a = asmObj("extern bfunc\n"
                    "export main\n"
                    "main:\n"
                    "    lmm R0, 5\n"
                    "    callx bfunc\n"
                    "    ret\n");
    auto b = asmObj("export bfunc\n"
                    "bfunc:\n"
                    "    addi R0, 37\n"
                    "    ret\n");
    writeBinary("test_linker_in_a.nci", a);
    writeBinary("test_linker_in_b.nci", b);
    LinkResult r1 = Linker::linkFiles({ "test_linker_in_a.nci", "test_linker_in_b.nci" });
    std::remove("test_linker_in_a.nci");
    std::remove("test_linker_in_b.nci");
    LinkResult r2 = Linker::linkImages({ a, b });
    ASSERT_TRUE(r1.ok) << r1.errorMessage;
    ASSERT_TRUE(r2.ok) << r2.errorMessage;
    EXPECT_EQ(r1.image, r2.image);
}

// ==== VM 侧 CALLX 双语义（不经链接器，直接验证 executeCALLX）====

// 内部地址（0 < addr < codeSize）按 CALL 处理：压返回地址、跳转、返回值生效
TEST(LinkerTest, CallxInternalAddressBehavesLikeCall) {
    auto a = asmObj("    callx 6\n" // 数字操作数：不经导入，imm = 6（代码段内）
                    "    ret\n"
                    "    lmm R0, 99\n"
                    "    ret\n");
    NVirtualMachine vm(64 * 1024);
    writeAndLoad(vm, a, "test_linker_callx_internal.nci");
    vm.start();

    EXPECT_EQ(vm.getRegister(0), 99);
    EXPECT_EQ(vm.getSP(), vm.getStackSize()); // 内部路径压栈已被 ret 消费
}

// addr=0（未解析动态导入的残留形态）不走内部跳转：宿主查表未命中 → 报错停止
TEST(LinkerTest, CallxZeroAddressKeepsHostPath) {
    auto a = asmObj("    callx 0\n"
                    "    lmm R0, 99\n");
    NVirtualMachine vm(64 * 1024);
    writeAndLoad(vm, a, "test_linker_callx_zero.nci");

    testing::internal::CaptureStdout();
    vm.start();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("CALLX"), std::string::npos);
    EXPECT_EQ(vm.getRegister(0), 0);         // 后继指令未执行
    EXPECT_EQ(vm.getPC(), vm.getCodeSize()); // 停在错误处
}
