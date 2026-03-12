#include "lexer.h"
#include <cctype>
#include <stdexcept>

Lexer::Lexer(const std::string& source) 
    : m_source(source), m_pos(0), m_line(1), m_column(1) {
    initKeywords();
}

void Lexer::initKeywords() {
    m_keywords["int"] = TokenType::KEYWORD_INT;
    m_keywords["char"] = TokenType::KEYWORD_CHAR;
    m_keywords["void"] = TokenType::KEYWORD_VOID;
    m_keywords["if"] = TokenType::KEYWORD_IF;
    m_keywords["else"] = TokenType::KEYWORD_ELSE;
    m_keywords["while"] = TokenType::KEYWORD_WHILE;
    m_keywords["for"] = TokenType::KEYWORD_FOR;
    m_keywords["return"] = TokenType::KEYWORD_RETURN;
    m_keywords["break"] = TokenType::KEYWORD_BREAK;
    m_keywords["continue"] = TokenType::KEYWORD_CONTINUE;
}

char Lexer::currentChar() const {
    if (m_pos >= m_source.length()) {
        return '\0';
    }
    return m_source[m_pos];
}

char Lexer::peekChar() const {
    if (m_pos + 1 >= m_source.length()) {
        return '\0';
    }
    return m_source[m_pos + 1];
}

void Lexer::advance() {
    if (m_pos < m_source.length()) {
        if (m_source[m_pos] == '\n') {
            m_line++;
            m_column = 1;
        } else {
            m_column++;
        }
        m_pos++;
    }
}

void Lexer::skipWhitespace() {
    while (m_pos < m_source.length() && std::isspace(currentChar())) {
        advance();
    }
}

void Lexer::skipComment() {
    // 单行注释
    if (currentChar() == '/' && peekChar() == '/') {
        while (m_pos < m_source.length() && currentChar() != '\n') {
            advance();
        }
    }
    // 多行注释
    else if (currentChar() == '/' && peekChar() == '*') {
        advance(); // 跳过 '/'
        advance(); // 跳过 '*'
        while (m_pos < m_source.length()) {
            if (currentChar() == '*' && peekChar() == '/') {
                advance(); // 跳过 '*'
                advance(); // 跳过 '/'
                break;
            }
            advance();
        }
    }
}

Token Lexer::readIdentifier() {
    int startLine = m_line;
    int startColumn = m_column;
    std::string value;
    
    while (m_pos < m_source.length() && (std::isalnum(currentChar()) || currentChar() == '_')) {
        value += currentChar();
        advance();
    }
    
    // 检查是否是关键字
    auto it = m_keywords.find(value);
    if (it != m_keywords.end()) {
        return Token(it->second, value, startLine, startColumn);
    }
    
    return Token(TokenType::IDENTIFIER, value, startLine, startColumn);
}

Token Lexer::readNumber() {
    int startLine = m_line;
    int startColumn = m_column;
    std::string value;
    
    while (m_pos < m_source.length() && std::isdigit(currentChar())) {
        value += currentChar();
        advance();
    }
    
    return Token(TokenType::INTEGER_CONSTANT, value, startLine, startColumn);
}

Token Lexer::readChar() {
    int startLine = m_line;
    int startColumn = m_column;
    
    advance(); // 跳过开头的单引号
    
    char c = currentChar();
    advance(); // 跳过字符
    
    if (currentChar() != '\'') {
        throw std::runtime_error("Expected closing single quote at line " + std::to_string(m_line));
    }
    advance(); // 跳过结尾的单引号
    
    std::string value(1, c);
    return Token(TokenType::CHAR_CONSTANT, value, startLine, startColumn);
}

Token Lexer::readOperator() {
    int startLine = m_line;
    int startColumn = m_column;
    char c = currentChar();
    advance();
    
    switch (c) {
        case '+':
            return Token(TokenType::OPERATOR_PLUS, "+", startLine, startColumn);
        case '-':
            return Token(TokenType::OPERATOR_MINUS, "-", startLine, startColumn);
        case '*':
            return Token(TokenType::OPERATOR_MULTIPLY, "*", startLine, startColumn);
        case '/':
            return Token(TokenType::OPERATOR_DIVIDE, "/", startLine, startColumn);
        case '%':
            return Token(TokenType::OPERATOR_MODULO, "%", startLine, startColumn);
        case '=':
            if (currentChar() == '=') {
                advance();
                return Token(TokenType::OPERATOR_EQUAL, "==", startLine, startColumn);
            }
            return Token(TokenType::OPERATOR_ASSIGN, "=", startLine, startColumn);
        case '!':
            if (currentChar() == '=') {
                advance();
                return Token(TokenType::OPERATOR_NOT_EQUAL, "!=", startLine, startColumn);
            }
            return Token(TokenType::OPERATOR_LOGICAL_NOT, "!", startLine, startColumn);
        case '<':
            if (currentChar() == '=') {
                advance();
                return Token(TokenType::OPERATOR_LESS_EQUAL, "<=", startLine, startColumn);
            }
            return Token(TokenType::OPERATOR_LESS, "<", startLine, startColumn);
        case '>':
            if (currentChar() == '=') {
                advance();
                return Token(TokenType::OPERATOR_GREATER_EQUAL, ">=", startLine, startColumn);
            }
            return Token(TokenType::OPERATOR_GREATER, ">", startLine, startColumn);
        case '&':
            if (currentChar() == '&') {
                advance();
                return Token(TokenType::OPERATOR_LOGICAL_AND, "&&", startLine, startColumn);
            }
            throw std::runtime_error("Expected '&' at line " + std::to_string(m_line));
        case '|':
            if (currentChar() == '|') {
                advance();
                return Token(TokenType::OPERATOR_LOGICAL_OR, "||", startLine, startColumn);
            }
            throw std::runtime_error("Expected '|' at line " + std::to_string(m_line));
        default:
            throw std::runtime_error("Unknown operator at line " + std::to_string(m_line));
    }
}

Token Lexer::readDelimiter() {
    int startLine = m_line;
    int startColumn = m_column;
    char c = currentChar();
    advance();
    
    switch (c) {
        case ';':
            return Token(TokenType::DELIMITER_SEMICOLON, ";", startLine, startColumn);
        case ',':
            return Token(TokenType::DELIMITER_COMMA, ",", startLine, startColumn);
        case '(':
            return Token(TokenType::DELIMITER_LPAREN, "(", startLine, startColumn);
        case ')':
            return Token(TokenType::DELIMITER_RPAREN, ")", startLine, startColumn);
        case '{':
            return Token(TokenType::DELIMITER_LBRACE, "{", startLine, startColumn);
        case '}':
            return Token(TokenType::DELIMITER_RBRACE, "}", startLine, startColumn);
        case '[':
            return Token(TokenType::DELIMITER_LBRACKET, "[", startLine, startColumn);
        case ']':
            return Token(TokenType::DELIMITER_RBRACKET, "]", startLine, startColumn);
        default:
            throw std::runtime_error("Unknown delimiter at line " + std::to_string(m_line));
    }
}

Token Lexer::nextToken() {
    skipWhitespace();
    
    // 跳过注释
    while (m_pos < m_source.length() && currentChar() == '/' && 
           (peekChar() == '/' || peekChar() == '*')) {
        skipComment();
        skipWhitespace();
    }
    
    if (m_pos >= m_source.length()) {
        return Token(TokenType::TOKEN_EOF, "", m_line, m_column);
    }
    
    char c = currentChar();
    
    // 标识符或关键字
    if (std::isalpha(c) || c == '_') {
        return readIdentifier();
    }
    
    // 数字
    if (std::isdigit(c)) {
        return readNumber();
    }
    
    // 字符常量
    if (c == '\'') {
        return readChar();
    }
    
    // 运算符
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || 
        c == '=' || c == '!' || c == '<' || c == '>' || c == '&' || c == '|') {
        return readOperator();
    }
    
    // 分隔符
    if (c == ';' || c == ',' || c == '(' || c == ')' || 
        c == '{' || c == '}' || c == '[' || c == ']') {
        return readDelimiter();
    }
    
    // 未知字符
    advance();
    return Token(TokenType::TOKEN_UNKNOWN, std::string(1, c), m_line, m_column - 1);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (true) {
        Token token = nextToken();
        tokens.push_back(token);
        
        if (token.type == TokenType::TOKEN_EOF) {
            break;
        }
    }
    
    return tokens;
}