#include "../ncc/lexer.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

// 防止Windows宏冲突
#ifdef type
#undef type
#endif

// 测试基本关键字识别
TEST(LexerTest, Keywords) {
    std::string source = "int char void if else while for return break continue";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    // 应该有10个关键字token + 1个EOF token
    ASSERT_EQ(tokens.size(), 11);
    
    EXPECT_EQ(tokens[0].tokenType, TokenType::KEYWORD_INT);
    EXPECT_EQ(tokens[0].value, "int");
    
    EXPECT_EQ(tokens[1].tokenType, TokenType::KEYWORD_CHAR);
    EXPECT_EQ(tokens[1].value, "char");
    
    EXPECT_EQ(tokens[2].tokenType, TokenType::KEYWORD_VOID);
    EXPECT_EQ(tokens[2].value, "void");
    
    EXPECT_EQ(tokens[3].tokenType, TokenType::KEYWORD_IF);
    EXPECT_EQ(tokens[3].value, "if");
    
    EXPECT_EQ(tokens[4].tokenType, TokenType::KEYWORD_ELSE);
    EXPECT_EQ(tokens[4].value, "else");
    
    EXPECT_EQ(tokens[5].tokenType, TokenType::KEYWORD_WHILE);
    EXPECT_EQ(tokens[5].value, "while");
    
    EXPECT_EQ(tokens[6].tokenType, TokenType::KEYWORD_FOR);
    EXPECT_EQ(tokens[6].value, "for");
    
    EXPECT_EQ(tokens[7].tokenType, TokenType::KEYWORD_RETURN);
    EXPECT_EQ(tokens[7].value, "return");
    
    EXPECT_EQ(tokens[8].tokenType, TokenType::KEYWORD_BREAK);
    EXPECT_EQ(tokens[8].value, "break");
    
    EXPECT_EQ(tokens[9].tokenType, TokenType::KEYWORD_CONTINUE);
    EXPECT_EQ(tokens[9].value, "continue");
    
    EXPECT_EQ(tokens[10].tokenType, TokenType::TOKEN_EOF);
}

// 测试标识符识别
TEST(LexerTest, Identifiers) {
    std::string source = "variable_name _private_var var123";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 4); // 3个标识符 + 1个EOF
    
    EXPECT_EQ(tokens[0].tokenType, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[0].value, "variable_name");
    
    EXPECT_EQ(tokens[1].tokenType, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "_private_var");
    
    EXPECT_EQ(tokens[2].tokenType, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].value, "var123");
}

// 测试整数常量识别
TEST(LexerTest, IntegerConstants) {
    std::string source = "0 123 456789";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 4); // 3个整数 + 1个EOF
    
    EXPECT_EQ(tokens[0].tokenType, TokenType::INTEGER_CONSTANT);
    EXPECT_EQ(tokens[0].value, "0");
    
    EXPECT_EQ(tokens[1].tokenType, TokenType::INTEGER_CONSTANT);
    EXPECT_EQ(tokens[1].value, "123");
    
    EXPECT_EQ(tokens[2].tokenType, TokenType::INTEGER_CONSTANT);
    EXPECT_EQ(tokens[2].value, "456789");
}

// 测试字符常量识别
TEST(LexerTest, CharConstants) {
    std::string source = "'a' 'Z' '0'";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 4); // 3个字符 + 1个EOF
    
    EXPECT_EQ(tokens[0].type, TokenType::CHAR_CONSTANT);
    EXPECT_EQ(tokens[0].value, "a");
    
    EXPECT_EQ(tokens[1].type, TokenType::CHAR_CONSTANT);
    EXPECT_EQ(tokens[1].value, "Z");
    
    EXPECT_EQ(tokens[2].type, TokenType::CHAR_CONSTANT);
    EXPECT_EQ(tokens[2].value, "0");
}

// 测试运算符识别
TEST(LexerTest, Operators) {
    std::string source = "+ - * / % = == != < <= > >= && || !";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 16); // 15个运算符 + 1个EOF
    
    EXPECT_EQ(tokens[0].type, TokenType::OPERATOR_PLUS);
    EXPECT_EQ(tokens[1].type, TokenType::OPERATOR_MINUS);
    EXPECT_EQ(tokens[2].type, TokenType::OPERATOR_MULTIPLY);
    EXPECT_EQ(tokens[3].type, TokenType::OPERATOR_DIVIDE);
    EXPECT_EQ(tokens[4].type, TokenType::OPERATOR_MODULO);
    EXPECT_EQ(tokens[5].type, TokenType::OPERATOR_ASSIGN);
    EXPECT_EQ(tokens[6].type, TokenType::OPERATOR_EQUAL);
    EXPECT_EQ(tokens[7].type, TokenType::OPERATOR_NOT_EQUAL);
    EXPECT_EQ(tokens[8].type, TokenType::OPERATOR_LESS);
    EXPECT_EQ(tokens[9].type, TokenType::OPERATOR_LESS_EQUAL);
    EXPECT_EQ(tokens[10].type, TokenType::OPERATOR_GREATER);
    EXPECT_EQ(tokens[11].type, TokenType::OPERATOR_GREATER_EQUAL);
    EXPECT_EQ(tokens[12].type, TokenType::OPERATOR_LOGICAL_AND);
    EXPECT_EQ(tokens[13].type, TokenType::OPERATOR_LOGICAL_OR);
    EXPECT_EQ(tokens[14].type, TokenType::OPERATOR_LOGICAL_NOT);
}

// 测试分隔符识别
TEST(LexerTest, Delimiters) {
    std::string source = "; , ( ) { } [ ]";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 9); // 8个分隔符 + 1个EOF
    
    EXPECT_EQ(tokens[0].type, TokenType::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[1].type, TokenType::DELIMITER_COMMA);
    EXPECT_EQ(tokens[2].type, TokenType::DELIMITER_LPAREN);
    EXPECT_EQ(tokens[3].type, TokenType::DELIMITER_RPAREN);
    EXPECT_EQ(tokens[4].type, TokenType::DELIMITER_LBRACE);
    EXPECT_EQ(tokens[5].type, TokenType::DELIMITER_RBRACE);
    EXPECT_EQ(tokens[6].type, TokenType::DELIMITER_LBRACKET);
    EXPECT_EQ(tokens[7].type, TokenType::DELIMITER_RBRACKET);
}

// 测试注释处理
TEST(LexerTest, Comments) {
    std::string source = "// This is a comment\nint x; /* multi-line\ncomment */ int y;";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    // 应该有：int, x, ;, int, y, ;, EOF
    ASSERT_EQ(tokens.size(), 7);
    
    EXPECT_EQ(tokens[0].type, TokenType::KEYWORD_INT);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "x");
    EXPECT_EQ(tokens[2].type, TokenType::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[3].type, TokenType::KEYWORD_INT);
    EXPECT_EQ(tokens[4].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[4].value, "y");
    EXPECT_EQ(tokens[5].type, TokenType::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[6].type, TokenType::TOKEN_EOF);
}

// 测试简单程序
TEST(LexerTest, SimpleProgram) {
    std::string source = "int main() {\n    int a = 10;\n    return a;\n}";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    // 验证token序列
    ASSERT_EQ(tokens[0].type, TokenType::KEYWORD_INT);
    ASSERT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[1].value, "main");
    ASSERT_EQ(tokens[2].type, TokenType::DELIMITER_LPAREN);
    ASSERT_EQ(tokens[3].type, TokenType::DELIMITER_RPAREN);
    ASSERT_EQ(tokens[4].type, TokenType::DELIMITER_LBRACE);
    ASSERT_EQ(tokens[5].type, TokenType::KEYWORD_INT);
    ASSERT_EQ(tokens[6].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[6].value, "a");
    ASSERT_EQ(tokens[7].type, TokenType::OPERATOR_ASSIGN);
    ASSERT_EQ(tokens[8].type, TokenType::INTEGER_CONSTANT);
    ASSERT_EQ(tokens[8].value, "10");
    ASSERT_EQ(tokens[9].type, TokenType::DELIMITER_SEMICOLON);
    ASSERT_EQ(tokens[10].type, TokenType::KEYWORD_RETURN);
    ASSERT_EQ(tokens[11].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[11].value, "a");
    ASSERT_EQ(tokens[12].type, TokenType::DELIMITER_SEMICOLON);
    ASSERT_EQ(tokens[13].type, TokenType::DELIMITER_RBRACE);
    ASSERT_EQ(tokens[14].type, TokenType::TOKEN_EOF);
}

// 测试行号和列号跟踪
TEST(LexerTest, LineAndColumn) {
    std::string source = "int\nx\ny";
    Lexer lexer(source);
    
    Token token1 = lexer.nextToken();
    EXPECT_EQ(token1.type, TokenType::KEYWORD_INT);
    EXPECT_EQ(token1.line, 1);
    EXPECT_EQ(token1.column, 1);
    
    Token token2 = lexer.nextToken();
    EXPECT_EQ(token2.type, TokenType::IDENTIFIER);
    EXPECT_EQ(token2.value, "x");
    EXPECT_EQ(token2.line, 2);
    EXPECT_EQ(token2.column, 1);
    
    Token token3 = lexer.nextToken();
    EXPECT_EQ(token3.type, TokenType::IDENTIFIER);
    EXPECT_EQ(token3.value, "y");
    EXPECT_EQ(token3.line, 3);
    EXPECT_EQ(token3.column, 1);
}

int main(int argc, char **argv) {
    // 初始化spdlog
    spdlog::set_level(spdlog::level::debug);
    
    // 初始化gtest
    ::testing::InitGoogleTest(&argc, argv);
    
    return RUN_ALL_TESTS();
}