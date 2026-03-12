#include "../ncc/lexer.hpp"
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
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::KEYWORD_INT);
    EXPECT_EQ(tokens[0].value, "int");
    
    EXPECT_EQ(tokens[1].kind, NTokenKind::KEYWORD_CHAR);
    EXPECT_EQ(tokens[1].value, "char");
    
    EXPECT_EQ(tokens[2].kind, NTokenKind::KEYWORD_VOID);
    EXPECT_EQ(tokens[2].value, "void");
    
    EXPECT_EQ(tokens[3].kind, NTokenKind::KEYWORD_IF);
    EXPECT_EQ(tokens[3].value, "if");
    
    EXPECT_EQ(tokens[4].kind, NTokenKind::KEYWORD_ELSE);
    EXPECT_EQ(tokens[4].value, "else");
    
    EXPECT_EQ(tokens[5].kind, NTokenKind::KEYWORD_WHILE);
    EXPECT_EQ(tokens[5].value, "while");
    
    EXPECT_EQ(tokens[6].kind, NTokenKind::KEYWORD_FOR);
    EXPECT_EQ(tokens[6].value, "for");
    
    EXPECT_EQ(tokens[7].kind, NTokenKind::KEYWORD_RETURN);
    EXPECT_EQ(tokens[7].value, "return");
    
    EXPECT_EQ(tokens[8].kind, NTokenKind::KEYWORD_BREAK);
    EXPECT_EQ(tokens[8].value, "break");
    
    EXPECT_EQ(tokens[9].kind, NTokenKind::KEYWORD_CONTINUE);
    EXPECT_EQ(tokens[9].value, "continue");
    
    EXPECT_EQ(tokens[10].kind, NTokenKind::TOKEN_EOF);
}

// 测试标识符识别
TEST(LexerTest, Identifiers) {
    std::string source = "variable_name _private_var var123";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 4); // 3个标识符 + 1个EOF
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(tokens[0].value, "variable_name");
    
    EXPECT_EQ(tokens[1].kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "_private_var");
    
    EXPECT_EQ(tokens[2].kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(tokens[2].value, "var123");
}

// 测试整数常量识别
TEST(LexerTest, IntegerConstants) {
    std::string source = "0 123 456789";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 4); // 3个整数 + 1个EOF
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::INTEGER_CONSTANT);
    EXPECT_EQ(tokens[0].value, "0");
    
    EXPECT_EQ(tokens[1].kind, NTokenKind::INTEGER_CONSTANT);
    EXPECT_EQ(tokens[1].value, "123");
    
    EXPECT_EQ(tokens[2].kind, NTokenKind::INTEGER_CONSTANT);
    EXPECT_EQ(tokens[2].value, "456789");
}

// 测试字符常量识别
TEST(LexerTest, CharConstants) {
    std::string source = "'a' 'Z' '0'";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 4); // 3个字符 + 1个EOF
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::CHAR_CONSTANT);
    EXPECT_EQ(tokens[0].value, "a");
    
    EXPECT_EQ(tokens[1].kind, NTokenKind::CHAR_CONSTANT);
    EXPECT_EQ(tokens[1].value, "Z");
    
    EXPECT_EQ(tokens[2].kind, NTokenKind::CHAR_CONSTANT);
    EXPECT_EQ(tokens[2].value, "0");
}

// 测试运算符识别
TEST(LexerTest, Operators) {
    std::string source = "+ - * / % = == != < <= > >= && || !";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 16); // 15个运算符 + 1个EOF
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::OPERATOR_PLUS);
    EXPECT_EQ(tokens[1].kind, NTokenKind::OPERATOR_MINUS);
    EXPECT_EQ(tokens[2].kind, NTokenKind::OPERATOR_MULTIPLY);
    EXPECT_EQ(tokens[3].kind, NTokenKind::OPERATOR_DIVIDE);
    EXPECT_EQ(tokens[4].kind, NTokenKind::OPERATOR_MODULO);
    EXPECT_EQ(tokens[5].kind, NTokenKind::OPERATOR_ASSIGN);
    EXPECT_EQ(tokens[6].kind, NTokenKind::OPERATOR_EQUAL);
    EXPECT_EQ(tokens[7].kind, NTokenKind::OPERATOR_NOT_EQUAL);
    EXPECT_EQ(tokens[8].kind, NTokenKind::OPERATOR_LESS);
    EXPECT_EQ(tokens[9].kind, NTokenKind::OPERATOR_LESS_EQUAL);
    EXPECT_EQ(tokens[10].kind, NTokenKind::OPERATOR_GREATER);
    EXPECT_EQ(tokens[11].kind, NTokenKind::OPERATOR_GREATER_EQUAL);
    EXPECT_EQ(tokens[12].kind, NTokenKind::OPERATOR_LOGICAL_AND);
    EXPECT_EQ(tokens[13].kind, NTokenKind::OPERATOR_LOGICAL_OR);
    EXPECT_EQ(tokens[14].kind, NTokenKind::OPERATOR_LOGICAL_NOT);
}

// 测试分隔符识别
TEST(LexerTest, Delimiters) {
    std::string source = "; , ( ) { } [ ]";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 9); // 8个分隔符 + 1个EOF
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[1].kind, NTokenKind::DELIMITER_COMMA);
    EXPECT_EQ(tokens[2].kind, NTokenKind::DELIMITER_LPAREN);
    EXPECT_EQ(tokens[3].kind, NTokenKind::DELIMITER_RPAREN);
    EXPECT_EQ(tokens[4].kind, NTokenKind::DELIMITER_LBRACE);
    EXPECT_EQ(tokens[5].kind, NTokenKind::DELIMITER_RBRACE);
    EXPECT_EQ(tokens[6].kind, NTokenKind::DELIMITER_LBRACKET);
    EXPECT_EQ(tokens[7].kind, NTokenKind::DELIMITER_RBRACKET);
}

// 测试注释处理
TEST(LexerTest, Comments) {
    std::string source = "// This is a comment\nint x; /* multi-line\ncomment */ int y;";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    // 应该有：int, x, ;, int, y, ;, EOF
    ASSERT_EQ(tokens.size(), 7);
    
    EXPECT_EQ(tokens[0].kind, NTokenKind::KEYWORD_INT);
    EXPECT_EQ(tokens[1].kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "x");
    EXPECT_EQ(tokens[2].kind, NTokenKind::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[3].kind, NTokenKind::KEYWORD_INT);
    EXPECT_EQ(tokens[4].kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(tokens[4].value, "y");
    EXPECT_EQ(tokens[5].kind, NTokenKind::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[6].kind, NTokenKind::TOKEN_EOF);
}

// 测试简单程序
TEST(LexerTest, SimpleProgram) {
    std::string source = "int main() {\n    int a = 10;\n    return a;\n}";
    Lexer lexer(source);
    
    std::vector<Token> tokens = lexer.tokenize();
    
    // 验证token序列
    ASSERT_EQ(tokens[0].kind, NTokenKind::KEYWORD_INT);
    ASSERT_EQ(tokens[1].kind, NTokenKind::IDENTIFIER);
    ASSERT_EQ(tokens[1].value, "main");
    ASSERT_EQ(tokens[2].kind, NTokenKind::DELIMITER_LPAREN);
    ASSERT_EQ(tokens[3].kind, NTokenKind::DELIMITER_RPAREN);
    ASSERT_EQ(tokens[4].kind, NTokenKind::DELIMITER_LBRACE);
    ASSERT_EQ(tokens[5].kind, NTokenKind::KEYWORD_INT);
    ASSERT_EQ(tokens[6].kind, NTokenKind::IDENTIFIER);
    ASSERT_EQ(tokens[6].value, "a");
    ASSERT_EQ(tokens[7].kind, NTokenKind::OPERATOR_ASSIGN);
    ASSERT_EQ(tokens[8].kind, NTokenKind::INTEGER_CONSTANT);
    ASSERT_EQ(tokens[8].value, "10");
    ASSERT_EQ(tokens[9].kind, NTokenKind::DELIMITER_SEMICOLON);
    ASSERT_EQ(tokens[10].kind, NTokenKind::KEYWORD_RETURN);
    ASSERT_EQ(tokens[11].kind, NTokenKind::IDENTIFIER);
    ASSERT_EQ(tokens[11].value, "a");
    ASSERT_EQ(tokens[12].kind, NTokenKind::DELIMITER_SEMICOLON);
    ASSERT_EQ(tokens[13].kind, NTokenKind::DELIMITER_RBRACE);
    ASSERT_EQ(tokens[14].kind, NTokenKind::TOKEN_EOF);
}

// 测试行号和列号跟踪
TEST(LexerTest, LineAndColumn) {
    std::string source = "int\nx\ny";
    Lexer lexer(source);
    
    Token token1 = lexer.nextToken();
    EXPECT_EQ(token1.kind, NTokenKind::KEYWORD_INT);
    EXPECT_EQ(token1.line, 1);
    EXPECT_EQ(token1.column, 1);
    
    Token token2 = lexer.nextToken();
    EXPECT_EQ(token2.kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(token2.value, "x");
    EXPECT_EQ(token2.line, 2);
    EXPECT_EQ(token2.column, 1);
    
    Token token3 = lexer.nextToken();
    EXPECT_EQ(token3.kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(token3.value, "y");
    EXPECT_EQ(token3.line, 3);
    EXPECT_EQ(token3.column, 1);
}

