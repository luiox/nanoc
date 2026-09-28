#include "nas/instruction.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

// NCI v2.1 目标文件格式测试：钉死的布局为
//   header(32B) | code | data | import table | export table
//   表 entry = int32 nameLen | name | NUL | pad4 | int32 addr | int32 flags
//   数据标号地址 = codeSize + 段内偏移（统一编址）

namespace {

    AssemblyResult assembleOk(const std::string& src) {
        AssemblyResult r = Assembler::assemble(src);
        EXPECT_TRUE(r.ok) << "line " << r.errorLine << ": " << r.errorMessage;
        return r;
    }

    AssemblyResult assembleFail(const std::string& src, int expectLine) {
        AssemblyResult r = Assembler::assemble(src);
        EXPECT_FALSE(r.ok) << "应当报错但成功了";
        EXPECT_EQ(r.errorLine, expectLine);
        return r;
    }

    uint32_t rd32(const std::vector<uint8_t>& img, size_t off) {
        return (uint32_t)img[off] | ((uint32_t)img[off + 1] << 8)
               | ((uint32_t)img[off + 2] << 16) | ((uint32_t)img[off + 3] << 24);
    }

    // 第 which 个导入 entry 的起始偏移（表位于 header+code+data 之后）
    size_t importEntryOffset(const std::vector<uint8_t>& img, int which) {
        size_t off = 32 + rd32(img, 12) + rd32(img, 16);
        for (int i = 0; i < which; ++i) {
            int32_t nameLen = (int32_t)rd32(img, off);
            size_t namePart = 4 + (size_t)nameLen + 1;
            off += namePart + ((4 - namePart % 4) % 4) + 8;
        }
        return off;
    }

    size_t exportEntryOffset(const std::vector<uint8_t>& img, int which) {
        size_t off = 32 + rd32(img, 12) + rd32(img, 16) + rd32(img, 20) * 0;
        // 导入表总大小需逐 entry 走一遍（变长）
        off = 32 + rd32(img, 12) + rd32(img, 16);
        int importCount = (int)rd32(img, 20);
        for (int i = 0; i < importCount; ++i) {
            int32_t nameLen = (int32_t)rd32(img, off);
            size_t namePart = 4 + (size_t)nameLen + 1;
            off += namePart + ((4 - namePart % 4) % 4) + 8;
        }
        for (int i = 0; i < which; ++i) {
            int32_t nameLen = (int32_t)rd32(img, off);
            size_t namePart = 4 + (size_t)nameLen + 1;
            off += namePart + ((4 - namePart % 4) % 4) + 8;
        }
        return off;
    }

} // namespace

// 头部字段：魔数、headerSize、段大小、表计数、入口点
TEST(AssemblerV21Test, HeaderFields) {
    AssemblyResult r = assembleOk("main:\n    nop\n");
    ASSERT_EQ(r.image.size(), (size_t)(32 + 1));
    const uint8_t expectMagic[8] = { 'N', 'a', 'n', 'o', 'C', 0, 0, 0 };
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(r.image[i], expectMagic[i]);
    EXPECT_EQ(rd32(r.image, 8), 32u);
    EXPECT_EQ(rd32(r.image, 12), 1u); // codeSize = nop 1 字节
    EXPECT_EQ(rd32(r.image, 16), 0u); // dataSize
    EXPECT_EQ(rd32(r.image, 20), 0u); // importCount
    EXPECT_EQ(rd32(r.image, 24), 0u); // exportCount
    EXPECT_EQ(rd32(r.image, 28), 0u); // entryPoint = main
    EXPECT_EQ(r.image[32], 0x7F);     // NOP
}

// spec §5.1 Hello World 全布局：代码回填、数据段、导入/导出表逐字节断言
TEST(AssemblerV21Test, HelloWorldSpecLayout) {
    AssemblyResult r = assembleOk("extern printf 0x08049000\n"
                                  "extern exit 0x0804A000\n"
                                  "export main\n"
                                  "\n"
                                  "main:\n"
                                  "    enter 0\n"
                                  "    lea R0, .msg\n"
                                  "    push R0\n"
                                  "    callx printf\n"
                                  "    addi R4, 4\n"
                                  "    lmm R0, 0\n"
                                  "    push R0\n"
                                  "    callx exit\n"
                                  "    leave\n"
                                  "    ret\n"
                                  "\n"
                                  ".msg:\n"
                                  "    db \"Hello\\n\", 0\n");
    uint32_t codeSize = rd32(r.image, 12);
    ASSERT_EQ(codeSize, 37u); // 3+6+2+5+6+6+2+5+1+1
    ASSERT_EQ(rd32(r.image, 16), 7u);
    ASSERT_EQ(rd32(r.image, 20), 2u);
    ASSERT_EQ(rd32(r.image, 24), 1u);
    ASSERT_EQ(rd32(r.image, 28), 0u); // main 在代码 0
    ASSERT_EQ(r.image.size(), (size_t)(32 + 37 + 7 + 40 + 20));

    // lea R0, .msg @3：双操作数 opcode+reg+imm32，imm @5；.msg 地址 = codeSize + 0 = 37
    EXPECT_EQ(r.image[32 + 3], 0x02);
    EXPECT_EQ(rd32(r.image, 32 + 5), 37u);
    // callx printf @11：imm = 导入地址
    EXPECT_EQ(r.image[32 + 11], 0x61);
    EXPECT_EQ(rd32(r.image, 32 + 12), 0x08049000u);
    // callx exit @30
    EXPECT_EQ(r.image[32 + 30], 0x61);
    EXPECT_EQ(rd32(r.image, 32 + 31), 0x0804A000u);

    // 数据段："Hello\n" + \0
    const uint8_t expectData[7] = { 'H', 'e', 'l', 'l', 'o', 0x0A, 0x00 };
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(r.image[32 + 37 + i], expectData[i]);

    // 导入表 entry1（printf）：nameLen=6，名部 4+6+1=11 → pad 1，addr@entry+12
    size_t e0 = importEntryOffset(r.image, 0);
    EXPECT_EQ(rd32(r.image, e0), 6u);
    EXPECT_EQ(r.image[e0 + 4], 'p');
    EXPECT_EQ(r.image[e0 + 10], 0); // NUL
    EXPECT_EQ(r.image[e0 + 11], 0); // pad
    EXPECT_EQ(rd32(r.image, e0 + 12), 0x08049000u);
    EXPECT_EQ(rd32(r.image, e0 + 16), 0u); // fastcall

    // 导入表 entry2（exit）：nameLen=4，名部 9 → pad 3
    size_t e1 = importEntryOffset(r.image, 1);
    EXPECT_EQ(rd32(r.image, e1), 4u);
    EXPECT_EQ(rd32(r.image, e1 + 12), 0x0804A000u);
    for (size_t p = e1 + 9; p < e1 + 12; ++p)
        EXPECT_EQ(r.image[p], 0) << "pad 字节应为 0";

    // 导出表 entry（main）：addr = 0
    size_t x0 = exportEntryOffset(r.image, 0);
    EXPECT_EQ(rd32(r.image, x0), 4u);
    EXPECT_EQ(rd32(r.image, x0 + 12), 0u);
    EXPECT_EQ(rd32(r.image, x0 + 16), 0u);
}

// entry 字节图边界：名字长度 1..4 覆盖 pad 2/1/0/3
TEST(AssemblerV21Test, EntryPaddingMatrix) {
    AssemblyResult r = assembleOk("extern a 0x10\n"
                                  "extern bb 0x11\n"
                                  "extern ccc 0x12\n"
                                  "extern dddd 0x13\n");
    size_t base = 32;
    int expectPad[4] = { 2, 1, 0, 3 };
    const char* names[4] = { "a", "bb", "ccc", "dddd" };
    for (int i = 0; i < 4; ++i) {
        size_t off = importEntryOffset(r.image, i);
        int32_t nameLen = (int32_t)rd32(r.image, off);
        ASSERT_EQ(nameLen, (int32_t)(i + 1));
        for (int k = 0; k <= i; ++k)
            EXPECT_EQ(r.image[off + 4 + k], names[i][k]);
        EXPECT_EQ(r.image[off + 4 + i + 1], 0); // NUL
        for (int p = 0; p < expectPad[i]; ++p)
            EXPECT_EQ(r.image[off + 4 + i + 2 + p], 0);
        EXPECT_EQ(rd32(r.image, off + 4 + i + 2 + expectPad[i]), (uint32_t)(0x10 + i));
    }
    (void)base;
}

// .calling_convention 顺序生效写入 flags；未知值报错
TEST(AssemblerV21Test, CallingConventionFlags) {
    AssemblyResult r = assembleOk(".calling_convention cdecl\n"
                                  "extern printf 0x1000\n"
                                  ".calling_convention fastcall\n"
                                  "extern foo 0x1004\n");
    size_t e0 = importEntryOffset(r.image, 0);
    size_t e1 = importEntryOffset(r.image, 1);
    // printf 名部 4+6+1+pad1=12 → flags@16；foo 名部 4+3+1+pad0=8 → flags@12
    EXPECT_EQ(rd32(r.image, e0 + 16), 1u); // cdecl
    EXPECT_EQ(rd32(r.image, e1 + 12), 0u); // fastcall

    assembleFail(".calling_convention pascal\n", 1);
}

// extern 无地址 = 0（动态链接位）；重复声明后者生效
TEST(AssemblerV21Test, ExternDefaultsAndOverrides) {
    AssemblyResult r = assembleOk("extern malloc\n"
                                  "extern malloc 0x2000\n");
    size_t e0 = importEntryOffset(r.image, 0);
    EXPECT_EQ(rd32(r.image, 20), 1u); // 只保留一条
    EXPECT_EQ(rd32(r.image, e0 + 12), 0x2000u);
}

// db 转义与字符串内注释符；dw/dd 小端；dd 标号引用统一编址回填
TEST(AssemblerV21Test, DataDefinitionBytes) {
    AssemblyResult r = assembleOk("lbl:\n"
                                  "    nop\n"
                                  "    dd lbl, .d2\n"
                                  ".d2:\n"
                                  "    db \"a;b#c\", 0\n"
                                  "    dw 0x1234, -1\n"
                                  "    dd 0x11223344\n");
    uint32_t codeSize = rd32(r.image, 12);
    ASSERT_EQ(codeSize, 1u);
    size_t data = 32 + codeSize;
    // dd lbl → 代码地址 0；dd .d2 → codeSize + 8 = 9
    EXPECT_EQ(rd32(r.image, data + 0), 0u);
    EXPECT_EQ(rd32(r.image, data + 4), 9u);
    const uint8_t expectStr[6] = { 'a', ';', 'b', '#', 'c', 0 };
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(r.image[data + 8 + i], expectStr[i]);
    EXPECT_EQ(r.image[data + 14], 0x34);
    EXPECT_EQ(r.image[data + 15], 0x12);
    EXPECT_EQ(r.image[data + 16], 0xFF);
    EXPECT_EQ(r.image[data + 17], 0xFF);
    EXPECT_EQ(rd32(r.image, data + 18), 0x11223344u);
}

// db 转义序列逐字节
TEST(AssemblerV21Test, StringEscapes) {
    AssemblyResult r = assembleOk("db \"A\\n\\t\\r\\0\\\\\\\"B\"\n");
    size_t data = 32;
    const uint8_t expect[8] = { 'A', 0x0A, 0x09, 0x0D, 0x00, 0x5C, 0x22, 'B' };
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(r.image[data + i], expect[i]);
}

// entryPoint 规则：main 优先 → 第一个 export → 0
TEST(AssemblerV21Test, EntryPointRules) {
    AssemblyResult r = assembleOk("export foo\n"
                                  "export main\n"
                                  "foo:\n    nop\n"
                                  "main:\n    nop\n");
    EXPECT_EQ(rd32(r.image, 28), 1u); // main 在代码 1

    r = assembleOk("export foo\n"
                   "export bar\n"
                   "foo:\n    nop\n"
                   "bar:\n    nop\n");
    EXPECT_EQ(rd32(r.image, 28), 0u); // 第一个 export

    r = assembleOk("    nop\n");
    EXPECT_EQ(rd32(r.image, 28), 0u);
}

// 错误路径：未声明 callx / 未定义标号 / 重复标号 / 悬空标号 / export 未定义
TEST(AssemblerV21Test, ErrorPaths) {
    assembleFail("callx printf\n", 1);             // 未 extern
    assembleFail("    jmp nowhere\n", 1);          // 未定义标号
    assembleFail("a:\n    nop\na:\n    nop\n", 3); // 重复标号
    assembleFail("dangling:\n", 1);                // 悬空标号
    assembleFail("export ghost\n    nop\n", 1);    // export 未定义
    assembleFail("extern bad zzz\n", 1);           // extern 地址非法
    assembleFail("    dd \"str\"\n", 1);           // dd 不接受字符串
    assembleFail("    db lbl\n", 1);               // db 不接受标号
}

// CRLF 源码容忍
TEST(AssemblerV21Test, CrlfTolerant) {
    AssemblyResult r = assembleOk("main:\r\n    nop\r\n    ret\r\n");
    EXPECT_EQ(rd32(r.image, 12), 2u);
    EXPECT_EQ(rd32(r.image, 28), 0u);
}
