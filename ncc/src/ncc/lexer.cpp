#include "ncc/lexer.hpp"
#include <cctype>
#include <stdexcept>

Lexer::Lexer(const std::string& source)
  : m_source(source), m_pos(0), m_line(1), m_column(1) {
    initKeywords();
}

void Lexer::initKeywords() {
    m_keywords["int"] = NTokenKind::KEYWORD_INT;
    m_keywords["char"] = NTokenKind::KEYWORD_CHAR;
    m_keywords["void"] = NTokenKind::KEYWORD_VOID;
    m_keywords["if"] = NTokenKind::KEYWORD_IF;
    m_keywords["else"] = NTokenKind::KEYWORD_ELSE;
    m_keywords["while"] = NTokenKind::KEYWORD_WHILE;
    m_keywords["for"] = NTokenKind::KEYWORD_FOR;
    m_keywords["return"] = NTokenKind::KEYWORD_RETURN;
    m_keywords["break"] = NTokenKind::KEYWORD_BREAK;
    m_keywords["continue"] = NTokenKind::KEYWORD_CONTINUE;
    m_keywords["NULL"] = NTokenKind::KEYWORD_NULL;
    m_keywords["struct"] = NTokenKind::KEYWORD_STRUCT;
    m_keywords["typedef"] = NTokenKind::KEYWORD_TYPEDEF;
    m_keywords["import"] = NTokenKind::KEYWORD_IMPORT;
    m_keywords["export"] = NTokenKind::KEYWORD_EXPORT;
    m_keywords["extern"] = NTokenKind::KEYWORD_EXTERN;
    // PRD R10/R11 语言特性关键字（追加在表尾，不改既有映射）
    m_keywords["defer"] = NTokenKind::KEYWORD_DEFER;
    m_keywords["match"] = NTokenKind::KEYWORD_MATCH;
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

    while (m_pos < m_source.length()
           && (std::isalnum(currentChar()) || currentChar() == '_')) {
        value += currentChar();
        advance();
    }

    // 检查是否是关键字
    auto it = m_keywords.find(value);
    if (it != m_keywords.end()) {
        return Token(it->second, value, startLine, startColumn);
    }

    return Token(NTokenKind::IDENTIFIER, value, startLine, startColumn);
}

Token Lexer::readNumber() {
    int startLine = m_line;
    int startColumn = m_column;
    std::string value;

    while (m_pos < m_source.length() && std::isdigit(currentChar())) {
        value += currentChar();
        advance();
    }

    return Token(NTokenKind::INTEGER_CONSTANT, value, startLine, startColumn);
}

Token Lexer::readChar() {
    int startLine = m_line;
    int startColumn = m_column;

    advance(); // 跳过开头的单引号

    char c = currentChar();
    advance(); // 跳过字符

    if (currentChar() != '\'') {
        throw std::runtime_error("Expected closing single quote at line "
                                 + std::to_string(m_line));
    }
    advance(); // 跳过结尾的单引号

    std::string value(1, c);
    return Token(NTokenKind::CHAR_CONSTANT, value, startLine, startColumn);
}

// 字符串字面量：value 保留引号内的原文（转义序列原样保留），由数据段发射方
// 原样交给汇编器解码（nas 支持 \n \t \r \0 \\ \"）
Token Lexer::readString() {
    int startLine = m_line;
    int startColumn = m_column;

    advance(); // 跳过开头的双引号

    std::string value;
    while (m_pos < m_source.length()) {
        char c = currentChar();
        if (c == '"') {
            advance(); // 跳过结尾的双引号
            return Token(NTokenKind::STRING_CONSTANT, value, startLine, startColumn);
        }
        if (c == '\n') {
            throw std::runtime_error("Unterminated string literal at line "
                                     + std::to_string(startLine));
        }
        if (c == '\\' && m_pos + 1 < m_source.length()) {
            // 反斜杠与被转义字符原样保留在 value 中（由汇编器/C 后端解码）；
            // 先记反斜杠并前进，下一轮循环记被转义字符
            value += c;
            advance();
            continue;
        }
        value += c;
        advance();
    }
    throw std::runtime_error("Unterminated string literal at line "
                             + std::to_string(startLine));
}

Token Lexer::readOperator() {
    int startLine = m_line;
    int startColumn = m_column;
    char c = currentChar();
    advance();

    switch (c) {
    case '+':
        return Token(NTokenKind::OPERATOR_PLUS, "+", startLine, startColumn);
    case '-':
        if (currentChar() == '>') {
            advance();
            return Token(NTokenKind::OPERATOR_ARROW, "->", startLine, startColumn);
        }
        return Token(NTokenKind::OPERATOR_MINUS, "-", startLine, startColumn);
    case '*':
        return Token(NTokenKind::OPERATOR_MULTIPLY, "*", startLine, startColumn);
    case '/':
        return Token(NTokenKind::OPERATOR_DIVIDE, "/", startLine, startColumn);
    case '%':
        return Token(NTokenKind::OPERATOR_MODULO, "%", startLine, startColumn);
    case '=':
        if (currentChar() == '=') {
            advance();
            return Token(NTokenKind::OPERATOR_EQUAL, "==", startLine, startColumn);
        }
        // =>（match 分支引导，PRD R11；追加在 = 判定之后）
        if (currentChar() == '>') {
            advance();
            return Token(NTokenKind::OPERATOR_FAT_ARROW, "=>", startLine, startColumn);
        }
        return Token(NTokenKind::OPERATOR_ASSIGN, "=", startLine, startColumn);
    case '!':
        if (currentChar() == '=') {
            advance();
            return Token(NTokenKind::OPERATOR_NOT_EQUAL, "!=", startLine, startColumn);
        }
        return Token(NTokenKind::OPERATOR_LOGICAL_NOT, "!", startLine, startColumn);
    case '<':
        if (currentChar() == '=') {
            advance();
            return Token(NTokenKind::OPERATOR_LESS_EQUAL, "<=", startLine, startColumn);
        }
        return Token(NTokenKind::OPERATOR_LESS, "<", startLine, startColumn);
    case '>':
        if (currentChar() == '=') {
            advance();
            return Token(NTokenKind::OPERATOR_GREATER_EQUAL,
                         ">=",
                         startLine,
                         startColumn);
        }
        return Token(NTokenKind::OPERATOR_GREATER, ">", startLine, startColumn);
    case '&':
        if (currentChar() == '&') {
            advance();
            return Token(NTokenKind::OPERATOR_LOGICAL_AND, "&&", startLine, startColumn);
        }
        return Token(NTokenKind::OPERATOR_AMPERSAND, "&", startLine, startColumn);
    case '|':
        if (currentChar() == '|') {
            advance();
            return Token(NTokenKind::OPERATOR_LOGICAL_OR, "||", startLine, startColumn);
        }
        throw std::runtime_error("Expected '|' at line " + std::to_string(m_line));
    case '.':
        // ...（extern 声明可变参数，PRD R3）；单独的 . 是成员访问；
        // ..（match 区间模式，PRD R11，含端点）。
        // switch 前已消费第一个 '.'，此处再按序检查后续字符
        if (currentChar() == '.' && peekChar() == '.') {
            advance();
            advance();
            return Token(NTokenKind::ELLIPSIS, "...", startLine, startColumn);
        }
        if (currentChar() == '.') {
            advance();
            return Token(NTokenKind::OPERATOR_DOTDOT, "..", startLine, startColumn);
        }
        return Token(NTokenKind::OPERATOR_DOT, ".", startLine, startColumn);
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
        return Token(NTokenKind::DELIMITER_SEMICOLON, ";", startLine, startColumn);
    case ',':
        return Token(NTokenKind::DELIMITER_COMMA, ",", startLine, startColumn);
    case '(':
        return Token(NTokenKind::DELIMITER_LPAREN, "(", startLine, startColumn);
    case ')':
        return Token(NTokenKind::DELIMITER_RPAREN, ")", startLine, startColumn);
    case '{':
        return Token(NTokenKind::DELIMITER_LBRACE, "{", startLine, startColumn);
    case '}':
        return Token(NTokenKind::DELIMITER_RBRACE, "}", startLine, startColumn);
    case '[':
        return Token(NTokenKind::DELIMITER_LBRACKET, "[", startLine, startColumn);
    case ']':
        return Token(NTokenKind::DELIMITER_RBRACKET, "]", startLine, startColumn);
    default:
        throw std::runtime_error("Unknown delimiter at line " + std::to_string(m_line));
    }
}

Token Lexer::nextToken() {
    skipWhitespace();

    // 跳过注释
    while (m_pos < m_source.length() && currentChar() == '/'
           && (peekChar() == '/' || peekChar() == '*')) {
        skipComment();
        skipWhitespace();
    }

    if (m_pos >= m_source.length()) {
        return Token(NTokenKind::TOKEN_EOF, "", m_line, m_column);
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

    // 字符串字面量
    if (c == '"') {
        return readString();
    }

    // 运算符
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '=' || c == '!'
        || c == '<' || c == '>' || c == '&' || c == '|' || c == '.') {
        return readOperator();
    }

    // 分隔符
    if (c == ';' || c == ',' || c == '(' || c == ')' || c == '{' || c == '}' || c == '['
        || c == ']') {
        return readDelimiter();
    }

    // 未知字符
    advance();
    return Token(NTokenKind::TOKEN_UNKNOWN, std::string(1, c), m_line, m_column - 1);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        Token token = nextToken();
        tokens.push_back(token);

        if (token.kind == NTokenKind::TOKEN_EOF) {
            break;
        }
    }

    return tokens;
}