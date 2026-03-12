#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "../ncc/lexer.h"
#include "../ncc/parser.h"
#include "../ncc/codegen.h"
#include <string>
#include <vector>

// 测试基本变量声明代码生成
TEST(CodegenTest, VariableDeclaration) {
    std::string source = "int x;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("; NanoC Generated Assembly") != std::string::npos);
    EXPECT_TRUE(assembly.find("main:") != std::string::npos);
}

// 测试带初始化的变量声明代码生成
TEST(CodegenTest, VariableDeclarationWithInitializer) {
    std::string source = "int x = 10;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("lmm R0, 10") != std::string::npos);
    EXPECT_TRUE(assembly.find("st R0") != std::string::npos);
}

// 测试函数声明代码生成
TEST(CodegenTest, FunctionDeclaration) {
    std::string source = "int main() { return 0; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("main:") != std::string::npos);
    EXPECT_TRUE(assembly.find("push BP") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm BP, SP") != std::string::npos);
    EXPECT_TRUE(assembly.find("ret") != std::string::npos);
}

// 测试算术表达式代码生成
TEST(CodegenTest, ArithmeticExpression) {
    std::string source = "int main() { int x = 1 + 2; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("lmm R0, 1") != std::string::npos);
    EXPECT_TRUE(assembly.find("lmm R0, 2") != std::string::npos);
    EXPECT_TRUE(assembly.find("add R0, R1") != std::string::npos);
}

// 测试if语句代码生成
TEST(CodegenTest, IfStatement) {
    std::string source = "int main() { if (1) { return 1; } else { return 0; } }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("eq R0, 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("jic") != std::string::npos);
    EXPECT_TRUE(assembly.find("jmp") != std::string::npos);
}

// 测试while语句代码生成
TEST(CodegenTest, WhileStatement) {
    std::string source = "int main() { while (1) { return 0; } }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("L0:") != std::string::npos); // 循环标签
    EXPECT_TRUE(assembly.find("eq R0, 0") != std::string::npos);
    EXPECT_TRUE(assembly.find("jic") != std::string::npos);
    EXPECT_TRUE(assembly.find("jmp") != std::string::npos);
}

// 测试函数调用代码生成
TEST(CodegenTest, FunctionCall) {
    std::string source = "int main() { printf(10); }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("call printf") != std::string::npos);
    EXPECT_TRUE(assembly.find("add SP") != std::string::npos); // 清理参数
}

// 测试复杂程序代码生成
TEST(CodegenTest, ComplexProgram) {
    std::string source = 
        "int add(int a, int b) {\n"
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
        "}";
    
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("add:") != std::string::npos);
    EXPECT_TRUE(assembly.find("main:") != std::string::npos);
    EXPECT_TRUE(assembly.find("call add") != std::string::npos);
    EXPECT_TRUE(assembly.find("gt R0, R1") != std::string::npos);
}

// 测试break和continue语句代码生成
TEST(CodegenTest, BreakContinue) {
    std::string source = 
        "int main() {\n"
        "    while (1) {\n"
        "        if (1) {\n"
        "            break;\n"
        "        }\n"
        "        continue;\n"
        "    }\n"
        "    return 0;\n"
        "}";
    
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    std::string assembly = codegen.generate(*program);
    
    EXPECT_FALSE(assembly.empty());
    EXPECT_TRUE(assembly.find("jmp") != std::string::npos);
}

// 测试错误处理
TEST(CodegenTest, ErrorHandling) {
    std::string source = "int main() { return x; }"; // 未定义的变量
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    CodeGenerator codegen;
    
    EXPECT_THROW({
        std::string assembly = codegen.generate(*program);
    }, std::runtime_error);
}