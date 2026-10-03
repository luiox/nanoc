#include "nas/instruction.hpp"
#include "ncc/codegen.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "nvm/core.hpp"
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

// 最小 e2e：.nc 源码 → Lexer/Parser/CodeGenerator → 汇编文本 → Assembler::assemble
// → 落盘 .nci → NVirtualMachine 加载并执行 → 断言 R0 返回值。
// 纯 VM 计算，不注册宿主函数；main 顶层 leave/ret 由 VM 栈底哨兵终止。

namespace {

    // 全链路执行：返回 main 的 R0 返回值
    int32_t runProgram(const std::string& source, const std::string& nciPath) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();

        CodeGenerator codegen;
        std::string assembly = codegen.generate(*program);

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
        vm.start();
        return vm.getRegister(0);
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
