#include "nas/instruction.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "nvm/core.hpp"
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

// 最小 e2e：.nc 源码 → Lexer/Parser → ir::lower → CodeGenerator（IR 发射）→
// 汇编文本 → Assembler::assemble → 落盘 .nci → NVirtualMachine 加载并执行 →
// 断言 R0 返回值。
// 纯 VM 计算，不注册宿主函数；main 顶层 leave/ret 由 VM 栈底哨兵终止。

namespace {

    // 宿主函数固定地址（extern 指令内联回填，参照 test_integration_e2e）
    constexpr int32_t kHostStrlenAddr = 0x7F000002;

    // 全链路执行：返回 main 的 R0 返回值；hostName/hostFn 提供时把
    // `extern <name>` 回填为显式地址并注册宿主函数（VM 的按名解析不回填
    // callx 立即数，addr=0 导入无法在运行期命中宿主）
    int32_t runProgram(const std::string& source,
                       const std::string& nciPath,
                       const std::string& hostName = "",
                       NHostFunction hostFn = nullptr) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();
        auto lowered = ir::lower(*program);
        if (lowered.is_err()) {
            ADD_FAILURE() << "lower failed: " << lowered.unwrap_err();
            return -1;
        }
        ir::Module module = std::move(lowered).unwrap(); // Module 只移动

        CodeGenerator codegen;
        std::string assembly = codegen.generate(module);
        if (hostFn != nullptr) {
            std::ostringstream pinned;
            pinned << "extern " << hostName << " 0x" << std::hex << kHostStrlenAddr
                   << "\n";
            const std::string bare = "extern " + hostName + "\n";
            const std::size_t pos = assembly.find(bare);
            EXPECT_NE(pos, std::string::npos) << "extern line not found:\n" << assembly;
            if (pos != std::string::npos) {
                assembly.replace(pos, bare.size(), pinned.str());
            }
        }

        AssemblyResult result = Assembler::assemble(assembly);
        EXPECT_TRUE(result.ok) << "line " << result.errorLine << ": "
                               << result.errorMessage << "\n--- assembly ---\n"
                               << assembly;

        std::ofstream ofs(nciPath, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(result.image.data()),
                  (std::streamsize)result.image.size());
        ofs.close();

        NVirtualMachine vm(8 * 1024 * 1024);
        vm.load(nciPath);
        if (hostFn != nullptr) {
            vm.registerHostFunction(kHostStrlenAddr, hostFn);
        }
        vm.start();
        return vm.getRegister(0);
    }

    // 宿主 strlen 桩：从 VM 统一内存按字节读 C 字符串取长度
    int32_t hostStrlen(int32_t* regs, int8_t* mem, int32_t memSize) {
        int32_t addr = regs[0];
        int32_t n = 0;
        while (addr + n < memSize && mem[addr + n] != 0) {
            ++n;
        }
        return n;
    }

} // namespace

// 递归阶乘：函数递归 + fastcall 传参 + cmp/jp 条件出口
TEST(CodegenE2ETest, RecursiveFactorial) {
    std::string source = "int factorial(int n) {\n"
                         "    if (n <= 1) { return 1; }\n"
                         "    return n * factorial(n - 1);\n"
                         "}\n"
                         "int main() { return factorial(5); }";
    EXPECT_EQ(runProgram(source, "codegen_e2e_factorial.nci"), 120);
    std::remove("codegen_e2e_factorial.nci");
}

// while 循环累加：1+2+...+10 = 55
TEST(CodegenE2ETest, WhileLoopSum) {
    std::string source = "int main() {\n"
                         "    int sum = 0;\n"
                         "    int i = 1;\n"
                         "    while (i <= 10) {\n"
                         "        sum = sum + i;\n"
                         "        i = i + 1;\n"
                         "    }\n"
                         "    return sum;\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_while.nci"), 55);
    std::remove("codegen_e2e_while.nci");
}

// >4 个参数：第 5/6 个经调用者栈传递，调用后 addi R4 清栈
TEST(CodegenE2ETest, StackArgumentCall) {
    std::string source = "int sum6(int a, int b, int c, int d, int e, int f)\n"
                         "{\n"
                         "    return a + b + c + d + e + f;\n"
                         "}\n"
                         "int main() { return sum6(1, 2, 3, 4, 5, 6); }";
    EXPECT_EQ(runProgram(source, "codegen_e2e_stackargs.nci"), 21);
    std::remove("codegen_e2e_stackargs.nci");
}

// 全局变量初始化 + 复合条件（>= / <=）与短路逻辑
TEST(CodegenE2ETest, GlobalInitAndCompositeCondition) {
    std::string source = "int g = 42;\n"
                         "int main() {\n"
                         "    int r = 0;\n"
                         "    if (g >= 40 && g <= 50) { r = g; }\n"
                         "    return r;\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_globals.nci"), 42);
    std::remove("codegen_e2e_globals.nci");
}

// ---------------------------------------------------------------------------
// R1.2 类型系统扩展（第一批）：数组 / 指针 / 字符串字面量
// ---------------------------------------------------------------------------

// 一维数组：求和（传参退化）+ 原地逆序（下标读写），1+2+3+4+5=15，
// 逆序后 a[0]=5、a[4]=1 → 15 + 5*10 + 1 = 66
TEST(CodegenE2ETest, ArraySumAndReverse) {
    std::string source = "int sum(int* a, int n) {\n"
                         "    int s = 0;\n"
                         "    for (int i = 0; i < n; i = i + 1) {\n"
                         "        s = s + a[i];\n"
                         "    }\n"
                         "    return s;\n"
                         "}\n"
                         "int reverse(int* a, int n) {\n"
                         "    int i = 0;\n"
                         "    int j = n - 1;\n"
                         "    while (i < j) {\n"
                         "        int t = a[i];\n"
                         "        a[i] = a[j];\n"
                         "        a[j] = t;\n"
                         "        i = i + 1;\n"
                         "        j = j - 1;\n"
                         "    }\n"
                         "    return 0;\n"
                         "}\n"
                         "int main() {\n"
                         "    int a[5];\n"
                         "    a[0] = 1;\n"
                         "    a[1] = 2;\n"
                         "    a[2] = 3;\n"
                         "    a[3] = 4;\n"
                         "    a[4] = 5;\n"
                         "    int total = sum(a, 5);\n"
                         "    reverse(a, 5);\n"
                         "    total = total + a[0] * 10;\n"
                         "    return total + a[4];\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_array.nci"), 66);
    std::remove("codegen_e2e_array.nci");
}

// 指针：取址、解引用读写、经形参指针交换调用者变量（3,7 → 7,3）
TEST(CodegenE2ETest, PointerSwapThroughDereference) {
    std::string source = "void swap(int* x, int* y) {\n"
                         "    int t = *x;\n"
                         "    *x = *y;\n"
                         "    *y = t;\n"
                         "}\n"
                         "int main() {\n"
                         "    int a = 3;\n"
                         "    int b = 7;\n"
                         "    swap(&a, &b);\n"
                         "    return a * 10 + b;\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_ptrswap.nci"), 73);
    std::remove("codegen_e2e_ptrswap.nci");
}

// 指针遍历数组：p = a 后经 p[i] 累加，等价 a[i]
TEST(CodegenE2ETest, PointerWalksArray) {
    std::string source = "int main() {\n"
                         "    int a[4];\n"
                         "    int i = 0;\n"
                         "    while (i < 4) {\n"
                         "        a[i] = i + 1;\n"
                         "        i = i + 1;\n"
                         "    }\n"
                         "    int* p = a;\n"
                         "    int s = 0;\n"
                         "    for (int k = 0; k < 4; k = k + 1) {\n"
                         "        s = s + p[k];\n"
                         "    }\n"
                         "    return s + (p > 0);\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_ptrwalk.nci"), 11);
    std::remove("codegen_e2e_ptrwalk.nci");
}

// char 数组按 4 字节槽读写元素（'A'=65、'B'=66）
TEST(CodegenE2ETest, CharArrayElementReadWrite) {
    std::string source = "int main() {\n"
                         "    char buf[4];\n"
                         "    buf[0] = 'A';\n"
                         "    buf[1] = 'B';\n"
                         "    buf[2] = buf[0] + 1;\n"
                         "    return buf[0] + buf[1] + buf[2];\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_chararray.nci"), 65 + 66 + 66);
    std::remove("codegen_e2e_chararray.nci");
}

// 字符串字面量落数据段，指针传给宿主 strlen 桩，按字节读长度
TEST(CodegenE2ETest, StringLiteralToHostStrlen) {
    std::string source = "int main() {\n"
                         "    char* s = \"NanoC\";\n"
                         "    char* empty = \"\";\n"
                         "    return strlen(s) * 10 + strlen(empty);\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_strlen.nci", "strlen", hostStrlen), 50);
    std::remove("codegen_e2e_strlen.nci");
}

// extern 声明（PRD R3）+ 宿主桩：语言级声明路径的 callx 发射与返回值取回。
// 声明提供签名（返回类型进 IR），宿主函数翻倍实参验证参数与返回通路
TEST(CodegenE2ETest, ExternDeclaredHostCall) {
    std::string source = "extern int dbl(int v);\n"
                         "int main() { return dbl(20) + 2; }";
    EXPECT_EQ(runProgram(source,
                         "codegen_e2e_extern.nci",
                         "dbl",
                         [](int32_t* regs, int8_t*, int32_t) { return regs[0] * 2; }),
              42);
    std::remove("codegen_e2e_extern.nci");
}

// ---------------------------------------------------------------------------
// R1.2 类型系统扩展（第二批）：struct 传参/返回 / 自引用链表 / typedef
// ---------------------------------------------------------------------------

// struct 按值传参 + sret 返回：矩形面积 = (4-1)*(6-2) = 12；
// 嵌套成员链、整体拷贝、struct 数组混合运算
TEST(CodegenE2ETest, StructParameterAndReturn) {
    std::string source = "struct Point { int x; int y; };\n"
                         "struct Rect { struct Point tl; struct Point br; };\n"
                         "int area(struct Rect r) {\n"
                         "    return (r.br.x - r.tl.x) * (r.br.y - r.tl.y);\n"
                         "}\n"
                         "struct Point make(int x, int y) {\n"
                         "    struct Point p = {x, y};\n"
                         "    return p;\n"
                         "}\n"
                         "int main() {\n"
                         "    struct Rect r;\n"
                         "    r.tl = make(1, 2);\n"
                         "    r.br = make(4, 6);\n"
                         "    struct Point copy = r.tl;\n"
                         "    struct Point arr[2];\n"
                         "    arr[0] = make(10, 20);\n"
                         "    arr[1] = arr[0];\n"
                         "    arr[1].x = arr[1].x + 1;\n"
                         "    return area(r) * 100 + arr[1].x + copy.y;\n"
                         "}";
    // area=12 → 1200 + 11 + 2 = 1213
    EXPECT_EQ(runProgram(source, "codegen_e2e_struct_rect.nci"), 1213);
    std::remove("codegen_e2e_struct_rect.nci");
}

// 自引用 struct 指针：-> 构建链表 1..5，遍历求和 = 15；NULL 终止判断
TEST(CodegenE2ETest, StructLinkedListBuildAndSum) {
    std::string source = "struct Node { int v; struct Node* next; };\n"
                         "int main() {\n"
                         "    struct Node n1;\n"
                         "    struct Node n2;\n"
                         "    struct Node n3;\n"
                         "    struct Node n4;\n"
                         "    struct Node n5;\n"
                         "    n1.v = 1;\n"
                         "    n2.v = 2;\n"
                         "    n3.v = 3;\n"
                         "    n4.v = 4;\n"
                         "    n5.v = 5;\n"
                         "    n1.next = &n2;\n"
                         "    n2.next = &n3;\n"
                         "    n3.next = &n4;\n"
                         "    n4.next = &n5;\n"
                         "    n5.next = NULL;\n"
                         "    int sum = 0;\n"
                         "    struct Node* p = &n1;\n"
                         "    while (p != NULL) {\n"
                         "        sum = sum + p->v;\n"
                         "        p = p->next;\n"
                         "    }\n"
                         "    return sum;\n"
                         "}";
    EXPECT_EQ(runProgram(source, "codegen_e2e_struct_list.nci"), 15);
    std::remove("codegen_e2e_struct_list.nci");
}

// 链表倒序构建（头插）+ 经函数传 struct 指针统计：验证 -> 写链与跨函数读链
// 头插法经 struct 数组构建链表 + 经函数传 struct 指针统计：
// 验证 -> 写链、&nodes[i] 取元素地址与跨函数读链
// （循环体内声明的局部变量跨迭代复用同一槽位，故节点取自 struct 数组）
TEST(CodegenE2ETest, StructLinkedListHeadInsert) {
    std::string source = "struct Node { int v; struct Node* next; };\n"
                         "int countGE(struct Node* head, int threshold) {\n"
                         "    int n = 0;\n"
                         "    struct Node* p = head;\n"
                         "    while (p) {\n"
                         "        if (p->v >= threshold) {\n"
                         "            n = n + 1;\n"
                         "        }\n"
                         "        p = p->next;\n"
                         "    }\n"
                         "    return n;\n"
                         "}\n"
                         "int main() {\n"
                         "    struct Node nodes[6];\n"
                         "    struct Node* head = NULL;\n"
                         "    int i = 5;\n"
                         "    while (i >= 0) {\n"
                         "        nodes[i].v = (i + 1) * (i + 1);\n"
                         "        nodes[i].next = head;\n"
                         "        head = &nodes[i];\n"
                         "        i = i - 1;\n"
                         "    }\n"
                         "    return countGE(head, 10) * 10 + countGE(head, 100);\n"
                         "}";
    // 平方序列 1,4,9,16,25,36（头插后 head=1）；>=10 有 3 个、>=100 有 0 个
    EXPECT_EQ(runProgram(source, "codegen_e2e_struct_headinsert.nci"), 30);
    std::remove("codegen_e2e_struct_headinsert.nci");
}

TEST(CodegenE2ETest, TypedefTransparentMixedUse) {
    std::string source = "struct Pair { int a; int b; };\n"
                         "typedef struct Pair PairT;\n"
                         "typedef int MyInt;\n"
                         "int pick(PairT p) {\n"
                         "    return p.a * 10 + p.b;\n"
                         "}\n"
                         "int main() {\n"
                         "    struct Pair x = {3, 4};\n"
                         "    PairT y = x;\n"
                         "    y.a = 5;\n"
                         "    MyInt scale = 2;\n"
                         "    return pick(y) * scale + x.a;\n"
                         "}";
    // pick({5,4}) = 54 → 108 + 3 = 111
    EXPECT_EQ(runProgram(source, "codegen_e2e_typedef.nci"), 111);
    std::remove("codegen_e2e_typedef.nci");
}
