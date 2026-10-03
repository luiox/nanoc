#ifndef NCC_LEXER_H
#define NCC_LEXER_H

#include <map>
#include <string>
#include <vector>

// Token type enum - 使用NTokenKind避免Windows宏冲突
enum class NTokenKind : int {
    // Keywords
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
    KEYWORD_NULL,    // NULL：空指针常量（PRD R1.2）
    KEYWORD_STRUCT,  // struct：结构体定义/类型引用（PRD R1.2 第二批）
    KEYWORD_TYPEDEF, // typedef：类型别名（PRD R1.2 第二批）
    KEYWORD_IMPORT,  // import：模块导入（PRD R2a 多文件整体编译）
    KEYWORD_EXPORT,  // export：顶层符号导出标记（PRD R2a）
    KEYWORD_EXTERN,  // extern：外部 C 函数声明（PRD R3）
    KEYWORD_DEFER,   // defer：作用域退出时逆序执行（PRD R10；新增追加在表尾）
    KEYWORD_MATCH,   // match：模式匹配表达式（PRD R11；新增追加在表尾）
    KEYWORD_CORO,    // coro：协程函数修饰符（PRD R12；新增追加在表尾）
    KEYWORD_YIELD,   // yield：挂起并产出值（PRD R12；新增追加在表尾）

    // Identifiers
    IDENTIFIER,

    // Constants
    INTEGER_CONSTANT,
    CHAR_CONSTANT,
    STRING_CONSTANT, // 字符串字面量（PRD R1.2）

    // Operators
    OPERATOR_PLUS,          // +
    OPERATOR_MINUS,         // -
    OPERATOR_MULTIPLY,      // *
    OPERATOR_DIVIDE,        // /
    OPERATOR_MODULO,        // %
    OPERATOR_ASSIGN,        // =
    OPERATOR_EQUAL,         // ==
    OPERATOR_NOT_EQUAL,     // !=
    OPERATOR_LESS,          // <
    OPERATOR_LESS_EQUAL,    // <=
    OPERATOR_GREATER,       // >
    OPERATOR_GREATER_EQUAL, // >=
    OPERATOR_LOGICAL_AND,   // &&
    OPERATOR_LOGICAL_OR,    // ||
    OPERATOR_LOGICAL_NOT,   // !
    OPERATOR_AMPERSAND,     // &（取址；&& 已由 LOGICAL_AND 消化）
    OPERATOR_DOT,           // .（struct 成员访问，PRD R1.2 第二批）
    OPERATOR_ARROW,         // ->（struct 指针成员访问，PRD R1.2 第二批）
    ELLIPSIS,               // ...（extern 声明可变参数，PRD R3）

    // Delimiters
    DELIMITER_SEMICOLON, // ;
    DELIMITER_COMMA,     // ,
    DELIMITER_LPAREN,    // (
    DELIMITER_RPAREN,    // )
    DELIMITER_LBRACE,    // {
    DELIMITER_RBRACE,    // }
    DELIMITER_LBRACKET,  // [
    DELIMITER_RBRACKET,  // ]

    // Operators/Delimiters 追加区（PRD R10/R11；新记号只在表尾追加，
    // 不改动既有枚举值——并行分支合并冲突最小化）
    OPERATOR_FAT_ARROW, // =>（match 分支引导，PRD R11）
    OPERATOR_DOTDOT,    // ..（match 区间模式，含端点，PRD R11）

    // Special tokens
    TOKEN_EOF,    // End of file
    TOKEN_UNKNOWN // Unknown token
};

// Token结构
struct Token {
    NTokenKind kind;
    std::string value;
    int line;
    int column;

    Token(NTokenKind k, const std::string& v, int l, int c)
      : kind(k), value(v), line(l), column(c) {}
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
    std::map<std::string, NTokenKind> m_keywords;

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
    Token readString();
    Token readOperator();
    Token readDelimiter();

    // 初始化关键字映射
    void initKeywords();
};

#endif // NCC_LEXER_H