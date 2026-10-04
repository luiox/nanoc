#include "ncc/preprocessor.hpp"
#include <cctype>

#include <fstream>
#include <sstream>
#include <stdexcept>

// R9 行级预处理实现（支持/不做与全部决策记录见 preprocessor.hpp 类注释）：
// 引号 include 递归展开、guard 三行剥离与对象宏识别、enum 原位改写、
// 固定宽度类型/bool 预置常量表。产物与源文件行对齐（指令行清空），
// 解析诊断的行列与原文件一致。

namespace {

    // NanoC 语言关键字（R9 决策：#define 宏名不得覆盖语言关键字——文本替换
    // 会破坏语法，如 `#define NULL ...`；此类行忽略不登记）
    const std::set<std::string>& nanoCKeywords() {
        static const std::set<std::string> keywords = {
            "int",     "char",   "void",   "if",       "else", "while",
            "for",     "return", "break",  "continue", "NULL", "struct",
            "typedef", "import", "export", "extern",   "i32",
        };
        return keywords;
    }

    // 指令名归类
    bool isConditionalDirective(const std::string& name) {
        return name == "if" || name == "ifdef" || name == "ifndef" || name == "elif"
               || name == "else" || name == "endif";
    }

    struct Tokenizer {
        const std::string& text;
        std::size_t pos = 0;

        explicit Tokenizer(const std::string& source) : text(source) {}

        void skipSpaces() {
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
                ++pos;
            }
        }
        bool atEnd() const { return pos >= text.size(); }
        char peek() const { return pos < text.size() ? text[pos] : '\0'; }
        // 读取标识符；未命中返回空串且不前进
        std::string readWord() {
            skipSpaces();
            if (pos >= text.size()
                || !(std::isalpha(static_cast<unsigned char>(text[pos]))
                     || text[pos] == '_')) {
                return "";
            }
            std::size_t start = pos;
            while (pos < text.size()
                   && (std::isalnum(static_cast<unsigned char>(text[pos]))
                       || text[pos] == '_')) {
                ++pos;
            }
            return text.substr(start, pos - start);
        }
    };

    // 去除注释（// 与 /* */，字符串字面量原样保留）——枚举体解析用
    std::string stripComments(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        bool inBlock = false;
        for (std::size_t i = 0; i < text.size();) {
            char c = text[i];
            if (inBlock) {
                if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                    inBlock = false;
                    i += 2;
                    continue;
                }
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                inBlock = true;
                i += 2;
                continue;
            }
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                while (i < text.size() && text[i] != '\n') {
                    ++i;
                }
                continue;
            }
            if (c == '"') {
                // 字符串原样拷贝（含转义）
                out += c;
                ++i;
                while (i < text.size() && text[i] != '"') {
                    if (text[i] == '\\' && i + 1 < text.size()) {
                        out += text[i];
                        out += text[i + 1];
                        i += 2;
                        continue;
                    }
                    out += text[i];
                    ++i;
                }
                if (i < text.size()) {
                    out += text[i];
                    ++i;
                }
                continue;
            }
            out += c;
            ++i;
        }
        return out;
    }

    // 常量表达式求值（十进制整数、+ - * / %、括号、一元 +/-）。递归下降，
    // 残留非法字符 → false（由调用方报"无法求值"诊断）
    class ConstExprEvaluator {
    public:
        ConstExprEvaluator(const std::string& text) : m_text(text) {}

        bool evaluate(int& out) {
            m_pos = 0;
            if (!parseExpr(out)) {
                return false;
            }
            skipSpaces();
            return m_pos >= m_text.size();
        }

    private:
        const std::string& m_text;
        std::size_t m_pos = 0;

        void skipSpaces() {
            while (m_pos < m_text.size()
                   && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t')) {
                ++m_pos;
            }
        }
        bool parseExpr(int& out) {
            if (!parseTerm(out)) {
                return false;
            }
            while (true) {
                skipSpaces();
                if (m_pos < m_text.size()
                    && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
                    char op = m_text[m_pos++];
                    int rhs = 0;
                    if (!parseTerm(rhs)) {
                        return false;
                    }
                    out = op == '+' ? out + rhs : out - rhs;
                    continue;
                }
                return true;
            }
        }
        bool parseTerm(int& out) {
            if (!parseFactor(out)) {
                return false;
            }
            while (true) {
                skipSpaces();
                if (m_pos < m_text.size()
                    && (m_text[m_pos] == '*' || m_text[m_pos] == '/'
                        || m_text[m_pos] == '%')) {
                    char op = m_text[m_pos++];
                    int rhs = 0;
                    if (!parseFactor(rhs)) {
                        return false;
                    }
                    if ((op == '/' || op == '%') && rhs == 0) {
                        return false; // 除零按无法求值处理
                    }
                    out = op == '*' ? out * rhs : op == '/' ? out / rhs : out % rhs;
                    continue;
                }
                return true;
            }
        }
        bool parseFactor(int& out) {
            skipSpaces();
            if (m_pos >= m_text.size()) {
                return false;
            }
            char c = m_text[m_pos];
            if (c == '-') {
                ++m_pos;
                if (!parseFactor(out)) {
                    return false;
                }
                out = -out;
                return true;
            }
            if (c == '+') {
                ++m_pos;
                return parseFactor(out);
            }
            if (c == '(') {
                ++m_pos;
                if (!parseExpr(out)) {
                    return false;
                }
                skipSpaces();
                if (m_pos >= m_text.size() || m_text[m_pos] != ')') {
                    return false;
                }
                ++m_pos;
                return true;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t start = m_pos;
                while (m_pos < m_text.size()
                       && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
                    ++m_pos;
                }
                try {
                    out = std::stoi(m_text.substr(start, m_pos - start));
                } catch (const std::exception&) {
                    return false;
                }
                return true;
            }
            return false; // 残留标识符/未知字符（展开失败的场合）
        }
    };

} // namespace

// ---- 基础工具 ----

bool Preprocessor::isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Preprocessor::isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string Preprocessor::trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> Preprocessor::splitLines(const std::string& source) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : source) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (c != '\r') { // CRLF 源码统一按 LF 处理（行对齐不受影响）
            line += c;
        }
    }
    lines.push_back(line);
    return lines;
}

bool Preprocessor::matchWord(const std::string& text,
                             std::size_t pos,
                             std::string& word) {
    if (pos >= text.size() || !isIdentStart(text[pos])) {
        return false;
    }
    std::size_t end = pos;
    while (end < text.size() && isIdentChar(text[end])) {
        ++end;
    }
    word = text.substr(pos, end - pos);
    return true;
}

void Preprocessor::reportError(const std::string& display,
                               int line,
                               int col,
                               const std::string& message,
                               std::vector<Diagnostic>& diagnostics) const {
    Diagnostic diagnostic;
    diagnostic.file = display;
    diagnostic.line = line;
    diagnostic.column = col;
    diagnostic.severity = DiagnosticSeverity::Error;
    diagnostic.message = message;
    diagnostics.push_back(std::move(diagnostic));
}

// ---- 预置常量（固定宽度类型/bool，决策记录见类注释）----

void Preprocessor::seedPresetConstants() {
    if (m_seeded) {
        return;
    }
    m_seeded = true;
    // VM 字宽 32 位：全部定宽整数类型折叠为 int（64 位宽度丢失——决策记录）
    for (const char* name : { "int8_t",
                              "int16_t",
                              "int32_t",
                              "int64_t",
                              "uint8_t",
                              "uint16_t",
                              "uint32_t",
                              "uint64_t",
                              "size_t",
                              "ssize_t",
                              "ptrdiff_t",
                              "intptr_t",
                              "uintptr_t",
                              "bool",
                              "_Bool" }) {
        m_constants.emplace(name, "int");
    }
    m_constants.emplace("true", "1");
    m_constants.emplace("false", "0");
}

// ---- 指令识别 ----

bool Preprocessor::scanDirective(const std::string& line, Directive& directive) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= line.size() || line[i] != '#') {
        return false;
    }
    directive.col = i + 1; // '#' 列（1 起）
    Tokenizer tokenizer(line);
    tokenizer.pos = i + 1;
    std::string name = tokenizer.readWord();
    if (name.empty()) {
        // `#` 后无指令名（null directive）：按不支持处理，name 置空串
        directive.name = "";
        directive.rest = "";
        return true;
    }
    directive.name = name;
    directive.rest = trim(line.substr(tokenizer.pos));
    return true;
}

std::set<int>
Preprocessor::detectGuardLines(const std::vector<Directive>& directives) const {
    // 形态：#ifndef G（允许中间夹 #pragma once）→ #define G → ... → #endif
    // （末条指令），中间无条件指令。guard 宏不进常量表（三行整体剥离）。
    std::set<int> guardLines;
    if (directives.size() < 3) {
        return guardLines;
    }
    const Directive& first = directives.front();
    if (first.name != "ifndef") {
        return guardLines;
    }
    Tokenizer firstRest(first.rest);
    const std::string guardName = firstRest.readWord();
    if (guardName.empty() || !trim(first.rest.substr(firstRest.pos)).empty()) {
        return guardLines;
    }

    std::size_t defineIndex = 0;
    bool foundDefine = false;
    for (std::size_t k = 1; k + 1 < directives.size(); ++k) {
        const Directive& directive = directives[k];
        if (directive.name == "define") {
            Tokenizer rest(directive.rest);
            if (rest.readWord() == guardName) {
                defineIndex = k;
                foundDefine = true;
                break;
            }
            return guardLines; // 首个 define 不是 guard 宏 → 非 guard 形态
        }
        if (directive.name == "pragma" && directive.rest == "once") {
            continue; // guard 与 #pragma once 混用
        }
        return guardLines; // #ifndef 与 #define 之间出现其他指令 → 非 guard
    }
    if (!foundDefine) {
        return guardLines;
    }

    const Directive& last = directives.back();
    if (last.name != "endif" || last.index <= directives[defineIndex].index) {
        return guardLines;
    }
    for (std::size_t k = defineIndex + 1; k + 1 < directives.size(); ++k) {
        if (isConditionalDirective(directives[k].name)) {
            return guardLines; // guard 体内嵌套条件编译 → 不识别（后续报错）
        }
    }

    guardLines.insert(first.index);
    guardLines.insert(directives[defineIndex].index);
    guardLines.insert(last.index);
    return guardLines;
}

// ---- 常量替换 ----

void Preprocessor::expandText(const std::string& src, std::string& out) const {
    bool inBlock = false;
    bool inString = false;
    bool inChar = false;
    for (std::size_t i = 0; i < src.size();) {
        char c = src[i];
        if (inBlock) {
            out += c;
            if (c == '*' && i + 1 < src.size() && src[i + 1] == '/') {
                out += '/';
                i += 2;
                inBlock = false;
                continue;
            }
            ++i;
            continue;
        }
        if (inString || inChar) {
            out += c;
            if (c == '\\' && i + 1 < src.size()) {
                out += src[i + 1];
                i += 2;
                continue;
            }
            if ((inString && c == '"') || (inChar && c == '\'')) {
                inString = false;
                inChar = false;
            }
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            out += "/*";
            i += 2;
            inBlock = true;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size()) {
                out += src[i++];
            }
            break;
        }
        if (c == '"') {
            inString = true;
            out += c;
            ++i;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            out += c;
            ++i;
            continue;
        }
        std::string word;
        if (isIdentStart(c) && matchWord(src, i, word)) {
            const auto it = m_constants.find(word);
            if (it != m_constants.end()) {
                out += it->second;
            } else {
                out += word;
            }
            i += word.size();
            continue;
        }
        out += c;
        ++i;
    }
}

std::string Preprocessor::expandLine(const std::string& line) const {
    std::string out;
    expandText(line, out);
    return out;
}

// ---- 常量表达式求值 ----

bool Preprocessor::evalConstExpr(const std::string& text, int& out) const {
    return ConstExprEvaluator(text).evaluate(out);
}

// ---- enum 改写 ----

bool Preprocessor::registerEnumerators(const std::string& body,
                                       const std::string& display,
                                       int lineNumber,
                                       std::vector<Diagnostic>& diagnostics) {
    // 逗号分隔（括号感知）→ `NAME [= EXPR]`；隐式值 = 前值 + 1（首项 0）
    int previous = -1;
    std::size_t i = 0;
    while (i <= body.size()) {
        // 项起点：跳过空白
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) {
            ++i;
        }
        if (i >= body.size()) {
            break;
        }
        // 取到下一顶层逗号
        std::size_t start = i;
        int depth = 0;
        while (i < body.size()) {
            char c = body[i];
            if (c == '(' || c == '[') {
                ++depth;
            } else if (c == ')' || c == ']') {
                --depth;
            } else if (c == ',' && depth == 0) {
                break;
            }
            ++i;
        }
        std::string item = trim(body.substr(start, i - start));
        ++i; // 跳过逗号（或越过末尾）

        if (item.empty()) {
            reportError(display, lineNumber, 1, "empty enum enumerator", diagnostics);
            return false;
        }
        std::string name;
        if (!matchWord(item, 0, name)) {
            reportError(display,
                        lineNumber,
                        1,
                        "invalid enum enumerator '" + item + "'",
                        diagnostics);
            return false;
        }
        std::string rest = trim(item.substr(name.size()));
        int value = 0;
        if (rest.empty()) {
            value = previous + 1;
        } else if (rest[0] != '=') {
            reportError(display,
                        lineNumber,
                        1,
                        "invalid enum enumerator '" + item + "'",
                        diagnostics);
            return false;
        } else {
            const std::string expr = expandLine(trim(rest.substr(1)));
            if (!evalConstExpr(expr, value)) {
                reportError(display,
                            lineNumber,
                            1,
                            "cannot evaluate enum constant '" + name
                              + "' (only integer "
                                "constants with + - * / % and parentheses are supported)",
                            diagnostics);
                return false;
            }
        }
        m_constants[name] = std::to_string(value);
        previous = value;
    }
    return true;
}

bool Preprocessor::rewriteEnum(const std::string& text,
                               std::size_t& i,
                               std::string& out,
                               const std::string& display,
                               int& lineCounter,
                               std::vector<Diagnostic>& diagnostics) {
    // 调用约定：text[i..] 为整词 `enum`，调用方尚未输出任何字符。
    // span 内每个 '\n' 原样保留（行对齐）并同步 lineCounter
    const std::size_t enumStart = i;
    const int startLine = lineCounter;
    auto pad = [&](std::size_t from, std::size_t to) {
        for (std::size_t q = from; q < to; ++q) {
            if (text[q] == '\n') {
                ++lineCounter;
                out += '\n';
            } else {
                out += ' ';
            }
        }
    };

    std::size_t j = i + 4;

    // 可选 Tag（enum Color）
    std::size_t tagEnd = 0;
    bool hasTag = false;
    while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
        ++j;
    }
    std::string tag;
    if (matchWord(text, j, tag)) {
        hasTag = true;
        j += tag.size();
        tagEnd = j;
    }

    // 定义形态：`enum [Tag] { ... }`
    std::size_t k = j;
    while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) {
        ++k;
    }
    if (k < text.size() && text[k] == '{') {
        // 找匹配 '}'（注释/字符串感知、花括号计数）
        std::size_t p = k + 1;
        int depth = 1;
        bool inBlock = false;
        bool inString = false;
        while (p < text.size() && depth > 0) {
            char c = text[p];
            if (inBlock) {
                if (c == '*' && p + 1 < text.size() && text[p + 1] == '/') {
                    inBlock = false;
                    p += 2;
                    continue;
                }
                ++p;
                continue;
            }
            if (inString) {
                if (c == '\\') {
                    p += 2;
                    continue;
                }
                if (c == '"') {
                    inString = false;
                }
                ++p;
                continue;
            }
            if (c == '/' && p + 1 < text.size() && text[p + 1] == '*') {
                inBlock = true;
                p += 2;
                continue;
            }
            if (c == '"') {
                inString = true;
                ++p;
                continue;
            }
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
            }
            ++p;
        }
        if (depth != 0) {
            reportError(display,
                        startLine,
                        1,
                        "unterminated enum definition",
                        diagnostics);
            return false;
        }
        const std::size_t spanEnd = p; // '}' 之后

        // 枚举体：去注释后逐项登记
        const std::string body = stripComments(text.substr(k + 1, spanEnd - k - 2));
        if (!registerEnumerators(body, display, startLine, diagnostics)) {
            return false;
        }

        // 纯定义 `enum E { ... };` 整体消失；带声明符的形态（typedef enum
        // {...} Alias; / enum E {...} v;）原位改写为 `int`。判定：span 后
        // 首个非空白字符为 ';'（或已到文件尾）→ 纯定义
        std::size_t m = spanEnd;
        while (m < text.size() && std::isspace(static_cast<unsigned char>(text[m]))) {
            ++m;
        }
        const bool bareDefinition = m >= text.size() || text[m] == ';';

        if (!bareDefinition) {
            out += "int";
        }
        pad(enumStart + (bareDefinition ? 4 : 3), spanEnd);
        i = spanEnd;

        // 纯定义：吞掉紧随的 ';'（含跨行空白，空白原样保留以对齐行号）
        if (bareDefinition && m < text.size()) {
            pad(i, m);
            out += ' ';
            i = m + 1;
        }
        return true;
    }

    // 类型引用形态：`enum [Tag]` → `int`（C 枚举即 int 尺寸）
    out += "int";
    const std::size_t consumed = hasTag ? tagEnd : enumStart + 4;
    pad(enumStart + 3, consumed);
    i = consumed;
    return true;
}

// ---- 主流程 ----

bool Preprocessor::process(const std::filesystem::path& path,
                           const std::string& canonical,
                           const std::string& display,
                           bool isHeader,
                           std::vector<PrepFile>& out,
                           std::vector<Diagnostic>& diagnostics) {
    seedPresetConstants();
    return processFile(path, canonical, display, isHeader, out, diagnostics);
}

bool Preprocessor::processFile(const std::filesystem::path& path,
                               const std::string& canonical,
                               const std::string& display,
                               bool isHeader,
                               std::vector<PrepFile>& out,
                               std::vector<Diagnostic>& diagnostics) {
    // 幂等 memo（头文件）：进入即标记——重复/循环 include 静默跳过，声明只在
    // 首次 include 处展开一次（与 C guard 在首次处理时即生效同口径）
    if (isHeader) {
        if (m_processed.find(canonical) != m_processed.end()) {
            return true;
        }
        m_processed.insert(canonical);
    }

    // 读文件
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        reportError(display,
                    1,
                    1,
                    "cannot open input file '" + display + "'",
                    diagnostics);
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::vector<std::string> lines = splitLines(buffer.str());

    // 预扫描指令行：guard 识别
    std::vector<Directive> directives;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Directive directive;
        if (scanDirective(lines[index], directive)) {
            directive.index = static_cast<int>(index);
            directives.push_back(std::move(directive));
        }
    }
    const std::set<int> guardLines = detectGuardLines(directives);

    const std::filesystem::path dir = path.parent_path();

    // 单趟扫描（拼接全文）：指令处理 + 常量替换 + enum 改写。enum 定义可
    // 跨行，故在拼接文本上以字符游标扫描；输出逐字符对齐（换行原样保留，
    // 指令行清空），行号与源文件一致
    std::string joined;
    joined.reserve(buffer.str().size());
    for (const std::string& line : lines) {
        joined += line;
        joined += '\n';
    }

    std::string text;
    text.reserve(joined.size());
    int lineNumber = 1;
    bool inBlock = false;
    bool inString = false;
    bool inChar = false;
    bool atLineStart = true;
    std::size_t i = 0;
    while (i < joined.size()) {
        const char c = joined[i];

        // 注释/字符串字面量原样拷贝（跨行保持行计数）
        if (inBlock) {
            text += c;
            if (c == '\n') {
                ++lineNumber;
                atLineStart = true;
            }
            if (c == '*' && i + 1 < joined.size() && joined[i + 1] == '/') {
                text += '/';
                i += 2;
                inBlock = false;
                continue;
            }
            ++i;
            continue;
        }
        if (inString || inChar) {
            text += c;
            if (c == '\\' && i + 1 < joined.size()) {
                text += joined[i + 1];
                i += 2;
                continue;
            }
            if ((inString && c == '"') || (inChar && c == '\'')) {
                inString = false;
                inChar = false;
            }
            if (c == '\n') {
                ++lineNumber;
                atLineStart = true;
            }
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < joined.size() && joined[i + 1] == '*') {
            text += "/*";
            i += 2;
            inBlock = true;
            continue;
        }
        if (c == '/' && i + 1 < joined.size() && joined[i + 1] == '/') {
            const std::size_t eol = joined.find('\n', i);
            text += joined.substr(i, eol == std::string::npos ? eol : eol - i);
            i = eol == std::string::npos ? joined.size() : eol;
            continue;
        }
        if (c == '"') {
            inString = true;
            text += c;
            ++i;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            text += c;
            ++i;
            continue;
        }
        if (c == '\n') {
            text += c;
            ++lineNumber;
            ++i;
            atLineStart = true;
            continue;
        }

        // 行首指令（前导空白后跟 '#'）
        if (atLineStart) {
            std::size_t j = i;
            while (j < joined.size() && (joined[j] == ' ' || joined[j] == '\t')) {
                ++j;
            }
            if (j < joined.size() && joined[j] == '#') {
                // 前导空白原样拷贝（近似保持列位置）
                text += joined.substr(i, j - i);
                atLineStart = false;
                const std::size_t eol = joined.find('\n', j);
                const std::string directiveLine =
                  joined.substr(j, eol == std::string::npos ? eol : eol - j);
                Directive directive;
                if (!scanDirective(directiveLine, directive)) {
                    reportError(display, lineNumber, 1, "invalid directive", diagnostics);
                    return false;
                }
                directive.index = lineNumber - 1;

                // include guard 三行：整体剥离（guard 宏不登记）
                if (guardLines.count(directive.index) > 0) {
                    i = eol == std::string::npos ? joined.size() : eol;
                    continue;
                }

                if (directive.name == "include") {
                    if (directive.rest.empty() || directive.rest[0] != '"') {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "only quoted #include \"path.h\" is supported "
                                    "(R9); '<...>' form is not",
                                    diagnostics);
                        return false;
                    }
                    const std::size_t closing = directive.rest.find('"', 1);
                    if (closing == std::string::npos) {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "unterminated #include file name",
                                    diagnostics);
                        return false;
                    }
                    const std::string target = directive.rest.substr(1, closing - 1);
                    if (target.size() < 2 || target.substr(target.size() - 2) != ".h") {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "#include target '" + target
                                      + "' is not supported: only .h headers are in "
                                        "the R9 subset",
                                    diagnostics);
                        return false;
                    }
                    // 相对当前文件目录解析
                    const std::filesystem::path headerPath =
                      dir / std::filesystem::path(target);
                    std::error_code ec;
                    if (!std::filesystem::exists(headerPath, ec)) {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "cannot find header '" + target + "' (looked for '"
                                      + std::filesystem::absolute(headerPath)
                                          .lexically_normal()
                                          .generic_string()
                                      + "')",
                                    diagnostics);
                        return false;
                    }
                    const std::string headerCanonical =
                      std::filesystem::absolute(headerPath)
                        .lexically_normal()
                        .generic_string();
                    const std::string headerDisplay =
                      headerPath.lexically_normal().generic_string();
                    // 递归展开：头文件段先于本文件段（out 顺序 = 声明合并顺序）
                    if (!processFile(headerPath,
                                     headerCanonical,
                                     headerDisplay,
                                     true,
                                     out,
                                     diagnostics)) {
                        return false;
                    }
                } else if (directive.name == "define") {
                    Tokenizer tokenizer(directive.rest);
                    const std::string name = tokenizer.readWord();
                    if (name.empty()) {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "#define requires a macro name",
                                    diagnostics);
                        return false;
                    }
                    if (!tokenizer.atEnd() && tokenizer.peek() == '(') {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "function-like macros are not supported (R9): '"
                                      + name + "'",
                                    diagnostics);
                        return false;
                    }
                    // NanoC 关键字不可被文本替换：忽略该行（决策记录见类注释）
                    if (nanoCKeywords().find(name) != nanoCKeywords().end()) {
                        i = eol == std::string::npos ? joined.size() : eol;
                        continue;
                    }
                    const std::string value = trim(directive.rest.substr(tokenizer.pos));
                    if (value.find('\\') != std::string::npos) {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "#define line continuation is not supported (R9)",
                                    diagnostics);
                        return false;
                    }
                    // 登记时按当前常量表做单层展开（值不求值，文本替换口径）
                    m_constants[name] = expandLine(value);
                } else if (directive.name == "pragma") {
                    if (directive.rest == "once") {
                        m_processed.insert(canonical);
                    } else {
                        reportError(display,
                                    lineNumber,
                                    static_cast<int>(directive.col),
                                    "unsupported pragma '" + directive.rest
                                      + "' (only '#pragma once' is supported, R9)",
                                    diagnostics);
                        return false;
                    }
                } else if (isConditionalDirective(directive.name)) {
                    reportError(display,
                                lineNumber,
                                static_cast<int>(directive.col),
                                "conditional compilation is not supported (R9): '#"
                                  + directive.name
                                  + "'; only the include guard form '#ifndef X' / "
                                    "'#define X' / '#endif' is recognized",
                                diagnostics);
                    return false;
                } else if (directive.name == "error") {
                    reportError(display,
                                lineNumber,
                                static_cast<int>(directive.col),
                                "#error"
                                  + (directive.rest.empty() ? "" : " " + directive.rest),
                                diagnostics);
                    return false;
                } else {
                    reportError(display,
                                lineNumber,
                                static_cast<int>(directive.col),
                                "unsupported preprocessor directive '#" + directive.name
                                  + "' (R9 subset: include/define/pragma once)",
                                diagnostics);
                    return false;
                }

                // 指令行整体清空：游标跳到行尾（换行由主循环输出，保持对齐）
                i = eol == std::string::npos ? joined.size() : eol;
                continue;
            }
            atLineStart = false;
        }

        // 标识符：常量替换 +（头文件）enum 改写（决策：enum 仅在头文件内
        // 改写，语言本体不新增 enum 语法）
        std::string word;
        if (isIdentStart(c) && matchWord(joined, i, word)) {
            if (isHeader && word == "enum") {
                if (!rewriteEnum(joined, i, text, display, lineNumber, diagnostics)) {
                    return false;
                }
                atLineStart = false;
                continue;
            }
            const auto it = m_constants.find(word);
            text += it != m_constants.end() ? it->second : word;
            i += word.size();
            atLineStart = false;
            continue;
        }

        text += c;
        ++i;
        atLineStart = false;
    }

    PrepFile file;
    file.canonical = canonical;
    file.display = display;
    file.isHeader = isHeader;
    file.text = std::move(text);
    out.push_back(std::move(file));
    return true;
}
