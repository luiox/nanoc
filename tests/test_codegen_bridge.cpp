#include "nas/instruction.hpp"
#include "ncc/codegen.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

// 桥接测试：ncc 产物必须能被 nas 直接汇编。
// 代表性 .nc 源码 → Lexer/Parser/CodeGenerator → 汇编文本 → Assembler::assemble
// 必须返回 ok（覆盖 extern 宿主调用、算术、控制流、函数、栈参、全局变量、递归）。

namespace {

    void expectAssemblable(const std::string& source, const std::string& tag) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();

        CodeGenerator codegen;
        std::string assembly = codegen.generate(*program);
        ASSERT_FALSE(assembly.empty()) << tag;

        AssemblyResult result = Assembler::assemble(assembly);
        ASSERT_TRUE(result.ok) << tag << " | line " << result.errorLine << ": "
                               << result.errorMessage << "\n--- assembly ---\n"
                               << assembly;
    }

} // namespace

// 外部宿主调用（printf 经 extern + callx）
TEST(CodegenBridgeTest, HelloExternCall) {
    expectAssemblable("int main() { printf(72); printf(10); return 0; }", "hello");
}

// 算术与局部变量
TEST(CodegenBridgeTest, Arithmetic) {
    expectAssemblable("int main() { int a = 10; int b = 3; int sum = a + b; "
                      "int diff = a - b; int prod = a * b; int quot = a / b; "
                      "int rem = a % b; return sum + diff + prod + quot + rem; }",
                      "arithmetic");
}

// 控制流：if/else、while、for、break、continue、一元运算、比较、短路逻辑
TEST(CodegenBridgeTest, ControlFlow) {
    expectAssemblable(
      "int main() {\n"
      "    int x = 10;\n"
      "    int result = 0;\n"
      "    if (x > 5) { result = 1; } else { result = 0; }\n"
      "    if (x == 10 && result >= 1) { result = result + 2; }\n"
      "    if (x != 9 || result <= 2) { result = result + 4; }\n"
      "    if (!(result < 0)) { result = -result; }\n"
      "    while (result > 0) { result = result - 1; if (result == 3) { break; } }\n"
      "    for (int i = 0; i < 5; i = i + 1) { if (i == 2) { continue; } "
      "result = result + i; }\n"
      "    return result;\n"
      "}",
      "control_flow");
}

// 函数定义与调用：fastcall 传参、返回值、>4 个参数走栈
TEST(CodegenBridgeTest, FunctionDefinitionAndCall) {
    expectAssemblable("int add(int a, int b) { return a + b; }\n"
                      "int max(int a, int b) { if (a > b) { return a; } return b; }\n"
                      "int sum6(int a, int b, int c, int d, int e, int f)\n"
                      "{\n"
                      "    return a + b + c + d + e + f;\n"
                      "}\n"
                      "int main()\n"
                      "{\n"
                      "    int r = add(1, 2);\n"
                      "    r = max(r, 5);\n"
                      "    r = sum6(r, 1, 2, 3, 4, 5);\n"
                      "    return r;\n"
                      "}",
                      "functions");
}

// 全局变量：数据段标号 + main 开头初始化
TEST(CodegenBridgeTest, Globals) {
    expectAssemblable("int g = 42; int counter; int main() { counter = g + 1; "
                      "return counter; }",
                      "globals");
}

// 递归：函数调用自身
TEST(CodegenBridgeTest, Recursion) {
    expectAssemblable("int factorial(int n) {\n"
                      "    if (n <= 1) { return 1; }\n"
                      "    return n * factorial(n - 1);\n"
                      "}\n"
                      "int main() { return factorial(5); }",
                      "recursion");
}
