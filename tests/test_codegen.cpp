#include "ncc/codegen.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

// v2.1 代码生成断言：产物必须是 nas 可汇编的 NCI v2.1 汇编文本。
// 助记符约定见 doc/Bytecode Format Specification v2.1.md：
// enter/leave/ret 帧管理、cmp/test 置 flags + jz/jnz/jn/jp 条件跳转、
// load/store [R6] 间接寻址、R4=SP（addi R4 清栈）、R0 返回值。

namespace {

    std::string compile(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();
        CodeGenerator codegen;
        return codegen.generate(*program);
    }

} // namespace

// 全局变量声明：数据段标号 + 缺省 main（enter/leave/ret，由栈底哨兵终止）
TEST(CodegenTest, GlobalVariableDeclaration) {
    std::string assembly = compile("int x;");

    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("; NanoC Generated Assembly") != std::string::npos);
    EXPECT_TRUE(assembly.find(".g_x:") != std::string::npos);
    EXPECT_TRUE(assembly.find("dd 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("main:") != std::string::npos);
    EXPECT_TRUE(assembly.find("enter 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("leave") != std::string::npos);
    EXPECT_TRUE(assembly.find("ret") != std::string::npos);
    // 废弃指令不得再出现
    EXPECT_TRUE(assembly.find("trap") == std::string::npos);
    EXPECT_TRUE(assembly.find("jic") == std::string::npos);
}

// 带初始化的全局变量：main 开头经 LEA/STORE 初始化，数据段占位 dd 0
TEST(CodegenTest, GlobalVariableWithInitializer) {
    std::string assembly = compile("int x = 10;");

    EXPECT_TRUE(assembly.find("lmm R0, 10") != std::string::npos);
    EXPECT_TRUE(assembly.find("lea R6, .g_x") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R0") != std::string::npos);
    EXPECT_TRUE(assembly.find(".g_x:") != std::string::npos);
    EXPECT_TRUE(assembly.find("dd 0") != std::string::npos);
}

// 函数帧：v2.1 用 enter/leave/ret，不再手工 push BP / lmm BP, SP
TEST(CodegenTest, FunctionFrame) {
    std::string assembly = compile("int main() { return 0; }");

    EXPECT_TRUE(assembly.find("main:") != std::string::npos);
    EXPECT_TRUE(assembly.find("enter 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("leave") != std::string::npos);
    EXPECT_TRUE(assembly.find("ret") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm BP, SP") == std::string::npos);
}

// 局部变量：enter 预留槽位，BP 相对寻址经 mov R6, R5 + subi R6, n 取地址
TEST(CodegenTest, LocalVariable) {
    std::string assembly = compile("int main() { int x = 5; return x; }");

    EXPECT_TRUE(assembly.find("enter 4") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 5") != std::string::npos);
    EXPECT_TRUE(assembly.find("mov R6, R5") != std::string::npos);
    EXPECT_TRUE(assembly.find("subi R6, 4") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("load R0, [R6]") != std::string::npos);
    // 旧式 BP 相对间接寻址不得出现
    EXPECT_TRUE(assembly.find("[BP-") == std::string::npos);
    EXPECT_TRUE(assembly.find("lea R0, [BP") == std::string::npos);
}

// 算术表达式：lmm 载入立即数 + 寄存器运算 + push 传递中间值
TEST(CodegenTest, ArithmeticExpression) {
    std::string assembly = compile("int main() { int x = 1 + 2; }");

    EXPECT_TRUE(assembly.find("lmm R0, 1") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 2") != std::string::npos);
    EXPECT_TRUE(assembly.find("add R0, R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("push R0") != std::string::npos);
}

// if 语句：TEST 置 flags + JZ 条件跳转（不再有 eq/jic）
TEST(CodegenTest, IfStatement) {
    std::string assembly =
      compile("int main() { if (1) { return 1; } else { return 0; } }");

    EXPECT_TRUE(assembly.find("test R0, R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("jz L") != std::string::npos);
    EXPECT_TRUE(assembly.find("jmp") != std::string::npos);
    EXPECT_TRUE(assembly.find("eq R0, 0") == std::string::npos);
    EXPECT_TRUE(assembly.find("jic") == std::string::npos);
}

// while 语句：条件测试 + JZ 退出 + JMP 回边
TEST(CodegenTest, WhileStatement) {
    std::string assembly = compile("int main() { while (1) { break; } return 0; }");

    EXPECT_TRUE(assembly.find("L0:") != std::string::npos); // 循环头标号
    EXPECT_TRUE(assembly.find("test R0, R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("jz L") != std::string::npos);
    EXPECT_TRUE(assembly.find("jmp L0") != std::string::npos);
}

// for 语句：i < 3 的条件假出口走 Z|P 复合（jz + jp）
TEST(CodegenTest, ForStatement) {
    std::string assembly =
      compile("int main() { int s = 0; for (int i = 0; i < 3; i = i + 1) { s = s + i; } "
              "return s; }");

    EXPECT_TRUE(assembly.find("cmp R0, R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("jz L") != std::string::npos);
    EXPECT_TRUE(assembly.find("jp L") != std::string::npos); // < 的假条件 = Z 或 P
    EXPECT_TRUE(assembly.find("jmp L") != std::string::npos);
}

// fastcall：前 4 个参数走 R0-R3，被调者溢出到帧槽位
TEST(CodegenTest, FunctionCallFastcall) {
    std::string assembly =
      compile("int add(int a, int b) { return a + b; } int main() { return add(3, 4); }");

    EXPECT_TRUE(assembly.find("add:") != std::string::npos);
    EXPECT_TRUE(assembly.find("enter 8") != std::string::npos); // 两个参数槽位
    EXPECT_TRUE(assembly.find("store [R6], R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 3") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 4") != std::string::npos);
    EXPECT_TRUE(assembly.find("pop R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("pop R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("call add") != std::string::npos);
}

// >4 个参数：第 5 个起压栈，调用后 addi R4 清栈（R4=SP）
TEST(CodegenTest, FunctionCallStackArgs) {
    std::string assembly = compile("int sum6(int a, int b, int c, int d, int e, int f)\n"
                                   "{\n"
                                   "    return a + b + c + d + e + f;\n"
                                   "}\n"
                                   "int main() { return sum6(1, 2, 3, 4, 5, 6); }");

    EXPECT_TRUE(assembly.find("call sum6") != std::string::npos);
    EXPECT_TRUE(assembly.find("addi R4, 8") != std::string::npos); // 2 个栈参 × 4 字节
    // 栈参经 BP 正偏移读取
    EXPECT_TRUE(assembly.find("addi R6, 8") != std::string::npos);
    EXPECT_TRUE(assembly.find("addi R6, 12") != std::string::npos);
}

// 未定义被调函数视为外部宿主符号：extern + callx
TEST(CodegenTest, ExternHostCall) {
    std::string assembly = compile("int main() { printf(10); return 0; }");

    EXPECT_TRUE(assembly.find("extern printf") != std::string::npos);
    EXPECT_TRUE(assembly.find("callx printf") != std::string::npos);
    // 单参数 fastcall 无需清栈
    EXPECT_TRUE(assembly.find("addi R4") == std::string::npos);
}

// 比较作为值：cmp 置 flags 后物化 0/1（jn 分支取真值）
TEST(CodegenTest, ComparisonValue) {
    std::string assembly = compile("int main() { return 1 < 2; }");

    EXPECT_TRUE(assembly.find("cmp R0, R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("jn L") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R2, 1") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R2, 0") != std::string::npos);
    // 旧式比较指令不得出现
    EXPECT_TRUE(assembly.find("lt R0") == std::string::npos);
    EXPECT_TRUE(assembly.find("gt R0") == std::string::npos);
}

// 逻辑与：短路求值 + 结果物化为 0/1
TEST(CodegenTest, LogicalAndShortCircuit) {
    std::string assembly = compile("int main() { return 1 && 0; }");

    // 两个操作数各做一次真值测试
    EXPECT_TRUE(assembly.find("test R0, R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("jz L") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 1") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 0") != std::string::npos);
}

// 逻辑或：左真即短路为真
TEST(CodegenTest, LogicalOrShortCircuit) {
    std::string assembly = compile("int main() { return 0 || 1; }");

    EXPECT_TRUE(assembly.find("test R0, R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("jnz L") != std::string::npos);
}

// 一元运算：取负用 NEG，逻辑非用 test + 物化
TEST(CodegenTest, UnaryOperators) {
    std::string assembly = compile("int main() { return -5; }");
    EXPECT_TRUE(assembly.find("neg R0") != std::string::npos);

    std::string notAssembly = compile("int main() { return !0; }");
    EXPECT_TRUE(notAssembly.find("test R0, R0") != std::string::npos);
    EXPECT_TRUE(notAssembly.find("jnz L") != std::string::npos);
    EXPECT_TRUE(notAssembly.find("lmm R2, 1") != std::string::npos);
}

// 复杂程序：函数定义 + 调用 + if 中 cmp/jz/jn 复合条件（> 的假条件 = Z 或 N）
TEST(CodegenTest, ComplexProgram) {
    std::string assembly = compile("int add(int a, int b) {\n"
                                   "    return a + b;\n"
                                   "}\n"
                                   "\n"
                                   "int main() {\n"
                                   "    int x = 10;\n"
                                   "    int y = 20;\n"
                                   "    int z = add(x, y);\n"
                                   "    if (z > 25) {\n"
                                   "        return 1;\n"
                                   "    }\n"
                                   "    return 0;\n"
                                   "}");

    EXPECT_TRUE(assembly.find("add:") != std::string::npos);
    EXPECT_TRUE(assembly.find("main:") != std::string::npos);
    EXPECT_TRUE(assembly.find("call add") != std::string::npos);
    EXPECT_TRUE(assembly.find("cmp R0, R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("jz L") != std::string::npos);
    EXPECT_TRUE(assembly.find("jn L") != std::string::npos); // > 的假条件 = Z 或 N
}

// break/continue：JMP 到循环出口/回边
TEST(CodegenTest, BreakContinue) {
    std::string assembly = compile("int main() {\n"
                                   "    while (1) {\n"
                                   "        if (1) {\n"
                                   "            break;\n"
                                   "        }\n"
                                   "        continue;\n"
                                   "    }\n"
                                   "    return 0;\n"
                                   "}");

    EXPECT_TRUE(assembly.find("jmp L") != std::string::npos);
}

// 字符字面量按整数码值载入
TEST(CodegenTest, CharLiteral) {
    std::string assembly = compile("int main() { return 'A'; }");

    EXPECT_TRUE(assembly.find("lmm R0, 65") != std::string::npos);
}

// 未定义变量在生成期报错
TEST(CodegenTest, ErrorHandling) {
    EXPECT_THROW({ compile("int main() { return x; }"); }, std::runtime_error);
}

// ---------------------------------------------------------------------------
// R1.2 类型系统扩展（第一批）：字符串字面量 / 指针 / 一维数组
// ---------------------------------------------------------------------------

namespace {

    // 统计子串出现次数（去重/重复断言用）
    std::size_t countOf(const std::string& text, const std::string& needle) {
        std::size_t count = 0;
        for (std::size_t pos = text.find(needle); pos != std::string::npos;
             pos = text.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }

} // namespace

// 字符串字面量落数据段：db 字节串 + 显式 NUL 终止；LEA 取地址
TEST(CodegenTest, StringLiteralDataSegment) {
    std::string assembly = compile("int main() { char* s = \"NanoC\"; return 0; }");

    EXPECT_TRUE(assembly.find(".str0:") != std::string::npos);
    EXPECT_TRUE(assembly.find("db \"NanoC\", 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("lea R0, .str0") != std::string::npos);
}

// 相同字面量去重：两个相同字符串只发一份数据
TEST(CodegenTest, StringLiteralDeduplication) {
    std::string assembly =
      compile("int main() { char* s = \"NanoC\"; char* t = \"NanoC\"; return 0; }");

    EXPECT_EQ(countOf(assembly, "db \"NanoC\", 0"), (std::size_t)1);
    EXPECT_EQ(countOf(assembly, ".str0:"), (std::size_t)1);
    EXPECT_TRUE(assembly.find(".str1") == std::string::npos);
    EXPECT_EQ(countOf(assembly, "lea R0, .str0"), (std::size_t)2);
}

// 不同字面量各占一个标号
TEST(CodegenTest, StringLiteralMultipleEntries) {
    std::string assembly =
      compile("int main() { char* s = \"abc\"; char* t = \"de\"; return 0; }");

    EXPECT_TRUE(assembly.find("db \"abc\", 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("db \"de\", 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("lea R0, .str0") != std::string::npos);
    EXPECT_TRUE(assembly.find("lea R0, .str1") != std::string::npos);
}

// 一维数组栈布局：enter 预留 10 槽，首元素置于块内最低地址槽，
// 元素寻址 = 基址(BP - 4*(slot+size-1)) + 4*i（与指针运算 a[k]==*(a+k) 一致）
TEST(CodegenTest, ArrayStackLayoutAndIndexing) {
    std::string assembly = compile("int main() { int a[10]; a[0] = 1; return a[0]; }");

    EXPECT_TRUE(assembly.find("enter 40") != std::string::npos);
    // 基址：mov R0, R5 + subi R0, 40（a 占槽位 1..10，首元素在最低地址槽）
    EXPECT_TRUE(assembly.find("mov R0, R5") != std::string::npos);
    EXPECT_TRUE(assembly.find("subi R0, 40") != std::string::npos);
    // 变址缩放：lmm R2, 4 + mul R1, R2；元素地址 = 基址 + 4*i
    EXPECT_TRUE(assembly.find("lmm R2, 4") != std::string::npos);
    EXPECT_TRUE(assembly.find("mul R1, R2") != std::string::npos);
    EXPECT_TRUE(assembly.find("add R0, R1") != std::string::npos);
    // 元素读写：地址进 R0/R6 后 LOAD/STORE
    EXPECT_TRUE(assembly.find("load R0, [R0]") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R0") != std::string::npos);
}

// 全局数组：数据段按元素数排布 dd 0
TEST(CodegenTest, GlobalArrayDataSegment) {
    std::string assembly = compile("int g[4]; int main() { g[0] = 1; return g[0]; }");

    EXPECT_TRUE(assembly.find(".g_g:") != std::string::npos);
    EXPECT_EQ(countOf(assembly, "dd 0"), (std::size_t)4);
    // 全局数组寻址：lea 基址 + add R0, R1（向上生长）
    EXPECT_TRUE(assembly.find("lea R0, .g_g") != std::string::npos);
    EXPECT_TRUE(assembly.find("add R0, R1") != std::string::npos);
}

// 指针寻址序列：&x 取地址入 R0、*p 解引用 load R0, [R0]、经指针写回
TEST(CodegenTest, PointerAddressingSequence) {
    std::string assembly =
      compile("int main() { int x = 7; int* p = &x; *p = 9; return *p; }");

    // &x：mov R0, R5 + subi R0, 4
    EXPECT_TRUE(assembly.find("subi R0, 4") != std::string::npos);
    // *p 读写：load R0, [R0]
    EXPECT_TRUE(assembly.find("load R0, [R0]") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R0") != std::string::npos);
}

// 指针算术按 4 字节缩放：p+2 / 1+p / p-1 三种形态
TEST(CodegenTest, PointerArithmeticScaling) {
    std::string assembly = compile(
      "int main() { int a[5]; int* p = a; p = p + 2; p = 1 + p; p = p - 1; return 0; }");

    EXPECT_EQ(countOf(assembly, "lmm R2, 4"), (std::size_t)3);
    EXPECT_EQ(countOf(assembly, "mul R1, R2"), (std::size_t)2); // p+2、p-1 缩放右操作数
    EXPECT_EQ(countOf(assembly, "mul R0, R2"), (std::size_t)1); // 1+p 缩放左操作数
    EXPECT_TRUE(assembly.find("add R0, R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("sub R0, R1") != std::string::npos);
}

// NULL 常量载入 0
TEST(CodegenTest, NullLiteral) {
    std::string assembly = compile("int main() { int* p = NULL; return p == NULL; }");

    EXPECT_TRUE(assembly.find("lmm R0, 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("cmp R0, R1") != std::string::npos);
}

// 数组名退化：传参传首元素地址而非内容
TEST(CodegenTest, ArrayDecayOnArgumentPassing) {
    std::string assembly = compile("int sum(int* a, int n) { return a[0]; }\n"
                                   "int main() { int a[3]; return sum(a, 3); }");

    EXPECT_TRUE(assembly.find("call sum") != std::string::npos);
    // 实参 a：mov R0, R5 + subi R0, 12 + push R0（首元素在最低地址槽，a 占槽 1..3）
    EXPECT_TRUE(assembly.find("mov R0, R5") != std::string::npos);
    EXPECT_TRUE(assembly.find("subi R0, 12") != std::string::npos);
    // 形参 a[i]：指针经槽位载入后再变址
    EXPECT_TRUE(assembly.find("load R0, [R6]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// R1.2 类型系统扩展（第二批）：struct 布局 / 成员寻址 / 拷贝 / sret
// ---------------------------------------------------------------------------

// struct 局部布局：struct Point 占 2 槽、局部 v 1 槽 → enter 12；
// 基址 = BP-8（2 槽块内最低地址），p.y 偏移 4
TEST(CodegenTest, StructLocalLayoutAndMemberAddressing) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "int main() {\n"
                                   "    struct Point p;\n"
                                   "    p.x = 3;\n"
                                   "    int v = p.y;\n"
                                   "    return v;\n"
                                   "}");

    EXPECT_TRUE(assembly.find("enter 12") != std::string::npos);
    EXPECT_TRUE(assembly.find("subi R0, 8") != std::string::npos);
    EXPECT_TRUE(assembly.find("addi R0, 4") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R0") != std::string::npos);
    EXPECT_TRUE(assembly.find("load R0, [R0]") != std::string::npos);
}

// struct 嵌套布局：Rect = 2×Point = 4 字 → enter 16；r.br.y 偏移 3 字 = 12
TEST(CodegenTest, StructNestedMemberOffset) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "struct Rect { struct Point tl; struct Point br; };\n"
                                   "int main() {\n"
                                   "    struct Rect r;\n"
                                   "    r.tl.x = 1;\n"
                                   "    r.br.y = 2;\n"
                                   "    return 0;\n"
                                   "}");

    EXPECT_TRUE(assembly.find("enter 16") != std::string::npos);
    // 成员链逐级累加偏移：r.br → +8（tl 占 2 字），再 .y → +4
    EXPECT_EQ(countOf(assembly, "addi R0, 8"), (std::size_t)1);
    EXPECT_EQ(countOf(assembly, "addi R0, 4"), (std::size_t)1);
}

// struct 整体赋值：逐字拷贝（Point 2 字），源地址 pop 进 R1、目的地址 mov R2, R0
TEST(CodegenTest, StructWholeAssignmentCopy) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "int main() {\n"
                                   "    struct Point p;\n"
                                   "    struct Point q;\n"
                                   "    p.x = 1;\n"
                                   "    q = p;\n"
                                   "    return 0;\n"
                                   "}");

    // emitPopCopyPush 专用序列
    EXPECT_TRUE(assembly.find("pop R1") != std::string::npos);
    EXPECT_TRUE(assembly.find("mov R2, R0") != std::string::npos);
    // 拷贝 2 字：至少 2 次 load/store；目的基址 = q 槽（BP-16）
    EXPECT_GE(countOf(assembly, "load R0, [R6]"), (std::size_t)2);
    EXPECT_TRUE(assembly.find("subi R0, 16") != std::string::npos);
}

// struct 按值传参：实参拷贝到调用者临时槽，副本地址作 fastcall 实参；
// 形参槽位存地址 → 成员访问双重间接
TEST(CodegenTest, StructArgumentPassByValueCopy) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "int sum(struct Point p) { return p.x + p.y; }\n"
                                   "int main() {\n"
                                   "    struct Point a;\n"
                                   "    return sum(a);\n"
                                   "}");

    EXPECT_TRUE(assembly.find("call sum") != std::string::npos);
    // 调用者：副本地址 = BP - 4*slot
    EXPECT_TRUE(assembly.find("subi R0,") != std::string::npos);
    // 被调者：形参槽位存地址 → load R0, [R6] 取地址，再间接取成员
    EXPECT_TRUE(assembly.find("load R0, [R6]") != std::string::npos);
}

// struct 返回（sret）：调用者 R7 传接收槽地址；被调者序言保存 R7，
// return 时逐字拷贝到 [R7]
TEST(CodegenTest, StructReturnSret) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "struct Point make(int x, int y) {\n"
                                   "    struct Point p = {x, y};\n"
                                   "    return p;\n"
                                   "}\n"
                                   "int main() {\n"
                                   "    struct Point a = make(1, 2);\n"
                                   "    return a.x;\n"
                                   "}");

    // 调用者：mov R7, R5 + subi R7, <slot>
    EXPECT_TRUE(assembly.find("mov R7, R5") != std::string::npos);
    EXPECT_TRUE(assembly.find("subi R7,") != std::string::npos);
    // 被调者：R7 溢出到专用槽
    EXPECT_TRUE(assembly.find("store [R6], R7") != std::string::npos);
    // 返回拷贝：load R2, [R6] 取回接收槽地址
    EXPECT_TRUE(assembly.find("load R2, [R6]") != std::string::npos);
}

// struct 数组：元素按布局字数缩放（Point 2 字 → a[1] 变址 ×8；3 元素 6 槽）
TEST(CodegenTest, StructArrayElementScaling) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "int main() {\n"
                                   "    struct Point a[3];\n"
                                   "    a[1].x = 5;\n"
                                   "    return 0;\n"
                                   "}");

    EXPECT_TRUE(assembly.find("enter 24") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R2, 8") != std::string::npos);
    EXPECT_TRUE(assembly.find("mul R1, R2") != std::string::npos);
}

// struct 指针算术：p + 1 按指向类型大小缩放（Point 2 字 → ×8）
TEST(CodegenTest, StructPointerArithmeticScaling) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "int main() {\n"
                                   "    struct Point a[3];\n"
                                   "    struct Point* p = a;\n"
                                   "    p = p + 1;\n"
                                   "    return 0;\n"
                                   "}");

    EXPECT_TRUE(assembly.find("lmm R2, 8") != std::string::npos);
    EXPECT_TRUE(assembly.find("add R0, R1") != std::string::npos);
}

// 全局 struct：数据段按布局字数排布（Point 2 字 + Rect 4 字 = 6 个 dd 0）
TEST(CodegenTest, GlobalStructDataSegment) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "struct Rect { struct Point tl; struct Point br; };\n"
                                   "struct Point g;\n"
                                   "struct Rect gr;\n"
                                   "int main() { return 0; }");

    EXPECT_TRUE(assembly.find(".g_g:") != std::string::npos);
    EXPECT_TRUE(assembly.find(".g_gr:") != std::string::npos);
    EXPECT_EQ(countOf(assembly, "dd 0"), (std::size_t)6);
}

// 全局 struct 初始化器：逐成员发射到 标号+偏移
TEST(CodegenTest, GlobalStructInitializerOffsets) {
    std::string assembly = compile("struct Point { int x; int y; };\n"
                                   "struct Point g = {7, 8};\n"
                                   "int main() { return g.x; }");

    EXPECT_TRUE(assembly.find("lmm R0, 7") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 8") != std::string::npos);
    EXPECT_TRUE(assembly.find("lea R6, .g_g") != std::string::npos);
    EXPECT_TRUE(assembly.find("addi R6, 4") != std::string::npos); // 第二个成员
}

// typedef 透明性：别名声明生成的函数体与裸 struct 完全一致
TEST(CodegenTest, TypedefTransparentCodegen) {
    std::string viaAlias = compile("typedef struct { int x; int y; } Pair;\n"
                                   "int main() {\n"
                                   "    Pair p;\n"
                                   "    p.x = 3;\n"
                                   "    int v = p.y;\n"
                                   "    return v;\n"
                                   "}");
    std::string viaTag = compile("struct Anon { int x; int y; };\n"
                                 "int main() {\n"
                                 "    struct Anon p;\n"
                                 "    p.x = 3;\n"
                                 "    int v = p.y;\n"
                                 "    return v;\n"
                                 "}");

    const std::string aliasBody = viaAlias.substr(viaAlias.find("main:"));
    const std::string tagBody = viaTag.substr(viaTag.find("main:"));
    EXPECT_EQ(aliasBody, tagBody);
}
