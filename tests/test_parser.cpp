#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

// 测试基本变量声明解析
TEST(ParserTest, VariableDeclaration) {
    std::string source = "int x;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto varDecl = dynamic_cast<VarDeclaration*>(program->declarations[0].get());
    ASSERT_NE(varDecl, nullptr);
    EXPECT_EQ(varDecl->type, "int");
    EXPECT_EQ(varDecl->name, "x");
    EXPECT_EQ(varDecl->initializer, nullptr);
}

// 测试带初始化的变量声明
TEST(ParserTest, VariableDeclarationWithInitializer) {
    std::string source = "int x = 10;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto varDecl = dynamic_cast<VarDeclaration*>(program->declarations[0].get());
    ASSERT_NE(varDecl, nullptr);
    EXPECT_EQ(varDecl->type, "int");
    EXPECT_EQ(varDecl->name, "x");
    ASSERT_NE(varDecl->initializer, nullptr);

    auto literal = dynamic_cast<IntegerLiteral*>(varDecl->initializer.get());
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->value, 10);
}

// 测试函数声明解析
TEST(ParserTest, FunctionDeclaration) {
    std::string source = "int main() { return 0; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    EXPECT_EQ(funcDecl->returnType, "int");
    EXPECT_EQ(funcDecl->name, "main");
    EXPECT_EQ(funcDecl->parameters.size(), 0);
    ASSERT_NE(funcDecl->body, nullptr);
}

// 测试带参数的函数声明
TEST(ParserTest, FunctionDeclarationWithParameters) {
    std::string source = "int add(int a, int b) { return a + b; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    EXPECT_EQ(funcDecl->returnType, "int");
    EXPECT_EQ(funcDecl->name, "add");
    ASSERT_EQ(funcDecl->parameters.size(), 2);

    EXPECT_EQ(funcDecl->parameters[0]->type, "int");
    EXPECT_EQ(funcDecl->parameters[0]->name, "a");
    EXPECT_EQ(funcDecl->parameters[1]->type, "int");
    EXPECT_EQ(funcDecl->parameters[1]->name, "b");
}

// 测试if语句解析
TEST(ParserTest, IfStatement) {
    std::string source = "int main() { if (x > 0) { return 1; } else { return 0; } }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_NE(funcDecl->body, nullptr);

    auto compound = dynamic_cast<CompoundStmt*>(funcDecl->body.get());
    ASSERT_NE(compound, nullptr);
    ASSERT_EQ(compound->statements.size(), 1);

    auto ifStmt = dynamic_cast<IfStmt*>(compound->statements[0].get());
    ASSERT_NE(ifStmt, nullptr);
    ASSERT_NE(ifStmt->condition, nullptr);
    ASSERT_NE(ifStmt->thenBranch, nullptr);
    ASSERT_NE(ifStmt->elseBranch, nullptr);
}

// 测试while语句解析
TEST(ParserTest, WhileStatement) {
    std::string source = "int main() { while (x > 0) { x = x - 1; } }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_NE(funcDecl->body, nullptr);

    auto compound = dynamic_cast<CompoundStmt*>(funcDecl->body.get());
    ASSERT_NE(compound, nullptr);
    ASSERT_EQ(compound->statements.size(), 1);

    auto whileStmt = dynamic_cast<WhileStmt*>(compound->statements[0].get());
    ASSERT_NE(whileStmt, nullptr);
    ASSERT_NE(whileStmt->condition, nullptr);
    ASSERT_NE(whileStmt->body, nullptr);
}

// 测试for语句解析
TEST(ParserTest, ForStatement) {
    std::string source = "int main() { for (int i = 0; i < 10; i = i + 1) { } }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_NE(funcDecl->body, nullptr);

    auto compound = dynamic_cast<CompoundStmt*>(funcDecl->body.get());
    ASSERT_NE(compound, nullptr);
    ASSERT_EQ(compound->statements.size(), 1);

    auto forStmt = dynamic_cast<ForStmt*>(compound->statements[0].get());
    ASSERT_NE(forStmt, nullptr);
    ASSERT_NE(forStmt->init, nullptr);
    ASSERT_NE(forStmt->condition, nullptr);
    ASSERT_NE(forStmt->increment, nullptr);
    ASSERT_NE(forStmt->body, nullptr);
}

// 测试表达式解析
TEST(ParserTest, ExpressionParsing) {
    std::string source = "int main() { int x = 1 + 2 * 3; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_NE(funcDecl->body, nullptr);

    auto compound = dynamic_cast<CompoundStmt*>(funcDecl->body.get());
    ASSERT_NE(compound, nullptr);
    ASSERT_EQ(compound->statements.size(), 1);

    auto varDecl = dynamic_cast<StmtVarDeclaration*>(compound->statements[0].get());
    ASSERT_NE(varDecl, nullptr);
    ASSERT_NE(varDecl->initializer, nullptr);

    // 验证表达式结构：1 + (2 * 3)
    auto binaryExpr = dynamic_cast<BinaryExpr*>(varDecl->initializer.get());
    ASSERT_NE(binaryExpr, nullptr);
    EXPECT_EQ(binaryExpr->op, "+");

    auto leftLiteral = dynamic_cast<IntegerLiteral*>(binaryExpr->left.get());
    ASSERT_NE(leftLiteral, nullptr);
    EXPECT_EQ(leftLiteral->value, 1);

    auto rightBinary = dynamic_cast<BinaryExpr*>(binaryExpr->right.get());
    ASSERT_NE(rightBinary, nullptr);
    EXPECT_EQ(rightBinary->op, "*");
}

// 测试函数调用解析
TEST(ParserTest, FunctionCall) {
    std::string source = "int main() { printf(10); }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 1);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[0].get());
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_NE(funcDecl->body, nullptr);

    auto compound = dynamic_cast<CompoundStmt*>(funcDecl->body.get());
    ASSERT_NE(compound, nullptr);
    ASSERT_EQ(compound->statements.size(), 1);

    auto exprStmt = dynamic_cast<ExprStmt*>(compound->statements[0].get());
    ASSERT_NE(exprStmt, nullptr);
    ASSERT_NE(exprStmt->expression, nullptr);

    auto callExpr = dynamic_cast<CallExpr*>(exprStmt->expression.get());
    ASSERT_NE(callExpr, nullptr);
    EXPECT_EQ(callExpr->callee, "printf");
    ASSERT_EQ(callExpr->arguments.size(), 1);
}

// 测试错误处理
TEST(ParserTest, ErrorHandling) {
    std::string source = "int main() { int x = ; }"; // 缺少表达式
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);

    EXPECT_THROW({ auto program = parser.parse(); }, std::runtime_error);
}
// i32 定宽类型名：与 int 同型，解析层归一化为 "int"（AST/语义/IR 无 i32）
TEST(ParserTest, I32Declarations) {
    std::string source = "i32 x = 3;\n"
                         "i32 add(i32 a, i32 b) { return a + b; }\n";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 2);

    auto varDecl = dynamic_cast<VarDeclaration*>(program->declarations[0].get());
    ASSERT_NE(varDecl, nullptr);
    // 归一化：i32 在 AST 中即 "int"
    EXPECT_EQ(varDecl->type, "int");
    EXPECT_EQ(varDecl->name, "x");
    ASSERT_NE(varDecl->initializer, nullptr);

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[1].get());
    ASSERT_NE(funcDecl, nullptr);
    EXPECT_EQ(funcDecl->returnType, "int");
    EXPECT_EQ(funcDecl->name, "add");
    ASSERT_EQ(funcDecl->parameters.size(), 2);
    EXPECT_EQ(funcDecl->parameters[0]->type, "int");
    EXPECT_EQ(funcDecl->parameters[0]->name, "a");
    EXPECT_EQ(funcDecl->parameters[1]->type, "int");
    EXPECT_EQ(funcDecl->parameters[1]->name, "b");
}

// i32 复合类型：指针 / 数组 / struct 成员 / 局部声明 / typedef 基型
TEST(ParserTest, I32CompositeTypes) {
    std::string source = "struct S { i32 a; i32* p; i32 b[2]; };\n"
                         "i32 g_arr[4];\n"
                         "typedef i32 MyInt;\n"
                         "i32 f(i32* q) {\n"
                         "  i32 local = q[0];\n"
                         "  return local;\n"
                         "}\n";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->declarations.size(), 4);

    auto structDecl = dynamic_cast<StructDeclaration*>(program->declarations[0].get());
    ASSERT_NE(structDecl, nullptr);
    EXPECT_EQ(structDecl->tag, "S");
    ASSERT_EQ(structDecl->fields.size(), 3);
    EXPECT_EQ(structDecl->fields[0]->type, "int");
    EXPECT_EQ(structDecl->fields[0]->pointerDepth, 0);
    EXPECT_EQ(structDecl->fields[1]->type, "int");
    EXPECT_EQ(structDecl->fields[1]->pointerDepth, 1);
    EXPECT_EQ(structDecl->fields[2]->type, "int");
    EXPECT_TRUE(structDecl->fields[2]->isArray);
    EXPECT_EQ(structDecl->fields[2]->arraySize, 2);

    auto arrDecl = dynamic_cast<VarDeclaration*>(program->declarations[1].get());
    ASSERT_NE(arrDecl, nullptr);
    EXPECT_EQ(arrDecl->type, "int");
    EXPECT_TRUE(arrDecl->isArray);
    EXPECT_EQ(arrDecl->arraySize, 4);

    auto typedefDecl = dynamic_cast<TypedefDeclaration*>(program->declarations[2].get());
    ASSERT_NE(typedefDecl, nullptr);
    EXPECT_EQ(typedefDecl->baseType, "int");
    EXPECT_EQ(typedefDecl->alias, "MyInt");

    auto funcDecl = dynamic_cast<FuncDeclaration*>(program->declarations[3].get());
    ASSERT_NE(funcDecl, nullptr);
    EXPECT_EQ(funcDecl->returnType, "int");
    ASSERT_EQ(funcDecl->parameters.size(), 1);
    EXPECT_EQ(funcDecl->parameters[0]->type, "int");
    EXPECT_EQ(funcDecl->parameters[0]->pointerDepth, 1);
}
