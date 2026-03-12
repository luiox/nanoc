#ifndef NCC_LEXER_H
#define NCC_LEXER_H

#include <string>
#include <vector>
#include <map>

// Token类型枚举
enum class TokenType : int {
    // 关键字
    KEYWORD_INT,
    KEYWORD_CHAR,
    KEYWORD_VOID,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_WHILE,
    KEYWORD_FOR,
    KEYWORD_RETURN,
    KEYWORD_BREAK,
    KEYWORD_CONTINUE,
    
    // 标识符
    IDENTIFIER,
    
    // 常量
    INTEGER_CONSTANT,
    CHAR_CONSTANT,
    
    // 运算符
    OPERATOR_PLUS,      // +
    OPERATOR_MINUS,     // -
    OPERATOR_MULTIPLY,  // *
    OPERATOR_DIVIDE,    // /
    OPERATOR_MODULO,    // %
    OPERATOR_ASSIGN,    // =
    OPERATOR_EQUAL,     // ==
    OPERATOR_NOT_EQUAL, // !=
    OPERATOR_LESS,      // <
    OPERATOR_LESS_EQUAL, // <=
    OPERATOR_GREATER,   // >
    OPERATOR_GREATER_EQUAL, // >=
    OPERATOR_LOGICAL_AND, // &&
    OPERATOR_LOGICAL_OR,  // ||
    OPERATOR_LOGICAL_NOT, // !
    
    // 分隔符
    DELIMITER_SEMICOLON,    // ;
    DELIMITER_COMMA,        // ,
    DELIMITER_LPAREN,       // (
    DELIMITER_RPAREN,       // )
    DELIMITER_LBRACE,       // {
    DELIMITER_RBRACE,       // }
    DELIMITER_LBRACKET,     // [
    DELIMITER_RBRACKET,     // ]
    
    // 特殊Token
    TOKEN_EOF,          // 文件结束
    TOKEN_UNKNOWN       // 未知Token
};

// Token结构
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    
    Token(TokenType t, const std::string& v, int l, int c) 
        : type(t), value(v), line(l), column(c) {}
};

// 词法分析器类
class Lexer {
public:
    Lexer(const std::string& source);
    
    // 获取下一个Token
    Token nextToken();
    
    // 获取所有Token
    std::vector<Token> tokenize();
    
    // 获取当前位置
    int getCurrentLine() const { return m_line; }
    int getCurrentColumn() const { return m_column; }
    
private:
    std::string m_source;
    size_t m_pos;
    int m_line;
    int m_column;
    
    // 关键字映射
    std::map<std::string, TokenType> m_keywords;
    
    // 辅助函数
    char currentChar() const;
    char peekChar() const;
    void advance();
    void skipWhitespace();
    void skipComment();
    
    // 识别Token的函数
    Token readIdentifier();
    Token readNumber();
    Token readChar();
    Token readOperator();
    Token readDelimiter();
    
    // 初始化关键字映射
    void initKeywords();
};

#endif // NCC_LEXER_H