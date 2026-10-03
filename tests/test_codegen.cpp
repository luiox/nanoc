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
