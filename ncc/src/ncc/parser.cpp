#include "ncc/parser.hpp"
#include <sstream>
#include <stdexcept>

// 递归下降解析器实现（接口/文法约定见 parser.hpp）：token 流 → AST。
// 语句/声明按首 token 分发，typedef 别名在构造期对 token 预扫描收集（分发
// 消歧的前置依赖）；i32（int 的定宽规范名）在解析层归一化为 int，AST 及
// 之后各层不再出现。import/export/extern 与 R9 头文件模式（C 声明子集）
// 的语法收口在本层；defer/match/yield（PRD R10-R12）按追加式纪律挂在
// 既有分发链尾部。

namespace {

    // R9 头文件模式接受的 C 限定符/修饰符（语义忽略——决策记录：
    // const/volatile/static/inline/register 为纯限定符；unsigned/signed/
    // long/short 按宿主基础类型解释，VM 字宽 32 位、无符号语义不建模）
    bool isHeaderQualifierWord(const std::string& value) {
        return value == "const" || value == "volatile" || value == "static"
               || value == "inline" || value == "register" || value == "unsigned"
               || value == "signed" || value == "long" || value == "short";
    }

} // namespace

Parser::Parser(const std::vector<Token>& tokens,
               std::string fileName,
               const std::set<std::string>& externalTypedefNames)
  : m_tokens(tokens), m_pos(0), m_fileName(std::move(fileName)) {
    // 预扫描 typedef 别名（文件作用域）。语句/声明按首 token 分发，需要先于
    // 解析知道哪些标识符是类型别名。别名恒为 `typedef ... <ident> ;` 中
    // 分号前最后一个标识符；花括号内的 struct 成员名不计（按深度屏蔽），
    // 因此 `typedef struct { int w; } Pair;` 正确收集 Pair 而非 w
    for (size_t i = 0; i < m_tokens.size(); ++i) {
        if (m_tokens[i].kind != NTokenKind::KEYWORD_TYPEDEF) {
            continue;
        }
        int depth = 0;
        size_t lastIdentifier = 0;
        bool hasIdentifier = false;
        for (size_t j = i + 1; j < m_tokens.size(); ++j) {
            const NTokenKind kind = m_tokens[j].kind;
            if (kind == NTokenKind::DELIMITER_LBRACE) {
                ++depth;
                continue;
            }
            if (kind == NTokenKind::DELIMITER_RBRACE) {
                --depth;
                continue;
            }
            if (depth != 0) {
                continue;
            }
            if (kind == NTokenKind::IDENTIFIER) {
                lastIdentifier = j;
                hasIdentifier = true;
            }
            if (kind == NTokenKind::DELIMITER_SEMICOLON) {
                break;
            }
        }
        if (hasIdentifier) {
            m_typedefNames.insert(m_tokens[lastIdentifier].value);
        }
    }
    // 单元级已知别名（PRD R9）：头文件先于包含者解析，其 typedef 名注入
    // 后续文件，跨文件 `PointT p;` 才能按类型声明解析
    m_typedefNames.insert(externalTypedefNames.begin(), externalTypedefNames.end());
}

bool Parser::isTypedefName(const std::string& name) const {
    return m_typedefNames.find(name) != m_typedefNames.end();
}

Token Parser::currentToken() const {
    if (m_pos >= m_tokens.size()) {
        return Token(NTokenKind::TOKEN_EOF, "", 0, 0);
    }
    return m_tokens[m_pos];
}

Token Parser::peekToken() const {
    if (m_pos + 1 >= m_tokens.size()) {
        return Token(NTokenKind::TOKEN_EOF, "", 0, 0);
    }
    return m_tokens[m_pos + 1];
}

void Parser::advance() {
    if (m_pos < m_tokens.size()) {
        m_pos++;
    }
}

bool Parser::match(NTokenKind kind) {
    if (currentToken().kind == kind) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(NTokenKind kind) {
    if (currentToken().kind == kind) {
        advance();
        return true;
    }
    error("Expected token kind " + std::to_string(static_cast<int>(kind)) + " but got "
          + std::to_string(static_cast<int>(currentToken().kind)));
    return false;
}

void Parser::error(const std::string& message) {
    Token token = currentToken();
    throw ParseError(m_fileName, token.line, token.column, message);
}

void Parser::errorAt(const Token& token, const std::string& message) {
    throw ParseError(m_fileName, token.line, token.column, message);
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>(currentToken().line, currentToken().column);

    // import 只允许出现在文件顶部（PRD R2a）：一旦出现任何声明，
    // 其后的 import 位置报错（带行号）
    bool seenDeclaration = false;
    while (currentToken().kind != NTokenKind::TOKEN_EOF) {
        if (currentToken().kind == NTokenKind::KEYWORD_IMPORT) {
            if (seenDeclaration) {
                error("import is only allowed at the top of the file, before all "
                      "declarations");
            }
            program->imports.push_back(parseImportDirective());
            continue;
        }
        auto decl = parseDeclaration();
        if (decl) {
            program->declarations.push_back(std::move(decl));
        }
        seenDeclaration = true;
    }

    return program;
}

// import 指令：`import math;`（同目录模块名）或 `import "util/helpers.nc";`
// （显式路径，相对当前文件）
ImportDirective Parser::parseImportDirective() {
    ImportDirective directive;
    directive.line = currentToken().line;
    directive.column = currentToken().column;
    advance(); // 消费 import

    if (currentToken().kind == NTokenKind::STRING_CONSTANT) {
        directive.target = currentToken().value;
        directive.quoted = true;
        advance();
    } else if (currentToken().kind == NTokenKind::IDENTIFIER) {
        directive.target = currentToken().value;
        directive.quoted = false;
        advance();
    } else {
        error("expected module name or quoted path after 'import'");
    }

    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        error("expected ';' after import");
    }
    advance();
    return directive;
}

std::unique_ptr<Decl> Parser::parseDeclaration(bool isExported, bool isCoro) {
    // export 前缀（PRD R2a）：递归解析声明并校验目标种类——只允许顶层
    // 函数与全局变量；struct/typedef 不支持导出
    if (!isExported && currentToken().kind == NTokenKind::KEYWORD_EXPORT) {
        const Token exportToken = currentToken();
        advance();
        if (currentToken().kind == NTokenKind::KEYWORD_EXPORT) {
            errorAt(exportToken, "duplicate 'export'");
        }
        if (currentToken().kind == NTokenKind::KEYWORD_EXTERN) {
            errorAt(exportToken, "'export' cannot be applied to 'extern' declarations");
        }
        auto decl = parseDeclaration(true, isCoro);
        if (decl->type == ASTNodeType::STRUCT_DECLARATION
            || decl->type == ASTNodeType::TYPEDEF_DECLARATION) {
            errorAt(exportToken,
                    "'export' can only be applied to top-level functions and global "
                    "variables");
        }
        return decl;
    }

    // coro 前缀（PRD R12）：递归解析声明并校验目标——只允许函数声明。
    // 顺序约定：`export coro` 合法（export 先解析后递归携带 isCoro），
    // `coro export` 拒绝（提示正确顺序）
    if (!isCoro && currentToken().kind == NTokenKind::KEYWORD_CORO) {
        const Token coroToken = currentToken();
        advance();
        if (currentToken().kind == NTokenKind::KEYWORD_CORO) {
            errorAt(coroToken, "duplicate 'coro'");
        }
        if (currentToken().kind == NTokenKind::KEYWORD_EXPORT) {
            errorAt(coroToken,
                    "'coro' cannot precede 'export'; write 'export coro int f(...)'");
        }
        if (currentToken().kind == NTokenKind::KEYWORD_EXTERN) {
            errorAt(coroToken, "'coro' cannot be applied to 'extern' declarations");
        }
        auto decl = parseDeclaration(isExported, true);
        if (decl == nullptr || decl->type != ASTNodeType::FUNC_DECLARATION) {
            errorAt(coroToken, "'coro' can only be applied to function declarations");
        }
        return decl;
    }

    // extern 声明（PRD R3）：只出现在文件作用域，导入宿主提供的 C 函数
    if (currentToken().kind == NTokenKind::KEYWORD_EXTERN) {
        if (isExported) {
            error("'export' cannot be applied to 'extern' declarations");
        }
        return parseExternDeclaration();
    }

    // typedef 只出现在文件作用域
    if (currentToken().kind == NTokenKind::KEYWORD_TYPEDEF) {
        return parseTypedefDeclaration();
    }

    // 类型开头：builtin 关键字、struct、typedef 别名；头文件模式（PRD R9）
    // 另接受 C 限定符/修饰符开头的声明（const unsigned int 等，语义忽略）。
    // i32 与 int 同型，走同一分支
    if (currentToken().kind == NTokenKind::KEYWORD_INT
        || currentToken().kind == NTokenKind::KEYWORD_I32
        || currentToken().kind == NTokenKind::KEYWORD_CHAR
        || currentToken().kind == NTokenKind::KEYWORD_VOID
        || currentToken().kind == NTokenKind::KEYWORD_STRUCT
        || (currentToken().kind == NTokenKind::IDENTIFIER
            && isTypedefName(currentToken().value))
        || (m_headerMode && currentToken().kind == NTokenKind::IDENTIFIER
            && isHeaderQualifierWord(currentToken().value))) {

        // 保存当前位置
        size_t startPos = m_pos;

        bool isStructTag = false;
        parseTypePrefix(isStructTag, m_tokens[startPos].line, m_tokens[startPos].column);

        // struct Tag 后跟 { → 定义；跟 ; → 前向声明；否则回退按变量/函数声明解析
        if (isStructTag) {
            if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
                const Token& kw = m_tokens[startPos];
                std::string tag = m_tokens[startPos + 1].value;
                auto structDecl = parseStructBody(tag, kw.line, kw.column);
                expect(NTokenKind::DELIMITER_SEMICOLON);
                return structDecl;
            }
            if (currentToken().kind == NTokenKind::DELIMITER_SEMICOLON) {
                const Token& kw = m_tokens[startPos];
                auto structDecl =
                  std::make_unique<StructDeclaration>(m_tokens[startPos + 1].value,
                                                      kw.line,
                                                      kw.column);
                structDecl->isForward = true;
                advance(); // 消费 ;
                return structDecl;
            }
        }

        // 名称探测（当前位置已在类型前缀之后）：判断函数声明还是变量声明
        while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
            advance();
        }
        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected identifier after type");
        }
        advance();

        if (currentToken().kind == NTokenKind::DELIMITER_LPAREN) {
            // R9 头文件函数原型：`int f(int x);`（参数表后随 ';'，无函数体）
            if (m_headerMode && isPrototypeForm(m_pos)) {
                m_pos = startPos; // 回退（含类型前缀上的限定符）
                return parsePrototypeDeclaration();
            }
            // 函数声明
            m_pos = startPos; // 回退
            auto funcDecl = parseFuncDeclaration(isCoro);
            if (isExported) {
                funcDecl->isExported = true;
            }
            return funcDecl;
        }
        // 变量声明
        m_pos = startPos; // 回退
        auto varDecl = parseVarDeclaration();
        if (isExported) {
            varDecl->isExported = true;
        }
        return varDecl;
    }

    if (isExported) {
        error("'export' must be followed by a function or global variable declaration");
    }
    error("Expected declaration");
    return nullptr;
}

// 类型前缀：builtin 关键字、`struct Tag` 或 typedef 别名；line/column 返回
// 首个 token 位置。头文件模式（PRD R9）先消费 C 限定符/修饰符链（语义忽略，
// 裸修饰符按 int 解释）
std::string Parser::parseTypePrefix(bool& isStructTag, int& line, int& column) {
    isStructTag = false;
    line = currentToken().line;
    column = currentToken().column;

    if (m_headerMode) {
        const bool sawBaseModifier = skipHeaderQualifiers();
        // `unsigned x` / `long f(void)`：修饰符即基类型（按 int 解释——决策
        // 记录：VM 字宽 32 位，无 unsigned/long 尺寸建模）
        if (sawBaseModifier && !isTypeStart()) {
            return "int";
        }
    }

    if (currentToken().kind == NTokenKind::KEYWORD_STRUCT) {
        advance();
        isStructTag = true;
        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected struct tag after 'struct'");
        }
        std::string tag = currentToken().value;
        advance();
        return tag;
    }

    if (currentToken().kind == NTokenKind::KEYWORD_INT
        || currentToken().kind == NTokenKind::KEYWORD_I32
        || currentToken().kind == NTokenKind::KEYWORD_CHAR
        || currentToken().kind == NTokenKind::KEYWORD_VOID) {
        // i32 是 int 的定宽规范名：解析层即归一化为 "int"，AST/语义/IR/后端
        // 均只见 "int"（int 退役为别名时后端零改动）
        std::string type =
          (currentToken().kind == NTokenKind::KEYWORD_I32) ? "int" : currentToken().value;
        advance();
        return type;
    }

    // typedef 别名作类型名（透明展开由语义/代码生成完成）
    if (currentToken().kind == NTokenKind::IDENTIFIER
        && isTypedefName(currentToken().value)) {
        std::string alias = currentToken().value;
        advance();
        return alias;
    }

    error("Expected type keyword");
    return "";
}

// R9 头文件模式：消费 C 限定符/修饰符链。决策记录：const/volatile/static/
// inline/register 为纯限定符（语义忽略——static 的内部链接、register 的
// 存储提示均不建模）；unsigned/signed/long/short 按宿主基础类型解释（VM
// 字宽 32 位，无符号/长型尺寸不建模，统一按 int 参与类型检查）。float/
// double 显式报不支持（VM 无浮点，宁报错不误编译）。
bool Parser::skipHeaderQualifiers() {
    bool sawBaseModifier = false;
    while (currentToken().kind == NTokenKind::IDENTIFIER
           && isHeaderQualifierWord(currentToken().value)) {
        const std::string& value = currentToken().value;
        if (value == "unsigned" || value == "signed" || value == "long"
            || value == "short") {
            sawBaseModifier = true;
        }
        advance();
    }
    if (currentToken().kind == NTokenKind::IDENTIFIER
        && (currentToken().value == "float" || currentToken().value == "double")) {
        error("floating-point types are not supported (R9 header subset)");
    }
    return sawBaseModifier;
}

// 当前 token 是否开始一个类型（builtin/struct/typedef 别名）
bool Parser::isTypeStart() const {
    switch (currentToken().kind) {
    case NTokenKind::KEYWORD_INT:
    case NTokenKind::KEYWORD_I32:
    case NTokenKind::KEYWORD_CHAR:
    case NTokenKind::KEYWORD_VOID:
    case NTokenKind::KEYWORD_STRUCT:
        return true;
    case NTokenKind::IDENTIFIER:
        return isTypedefName(currentToken().value);
    default:
        return false;
    }
}

// 从 '(' 起扫描 `(...)` 是否后随 ';'（头文件原型形态判定，仅词法形态；
// 括号配平扫描有 TOKEN_EOF/末尾兜底，不会越界）
bool Parser::isPrototypeForm(std::size_t lparenPos) const {
    int depth = 0;
    for (std::size_t i = lparenPos; i < m_tokens.size(); ++i) {
        const NTokenKind kind = m_tokens[i].kind;
        if (kind == NTokenKind::DELIMITER_LPAREN) {
            ++depth;
            continue;
        }
        if (kind == NTokenKind::DELIMITER_RPAREN) {
            --depth;
            if (depth == 0) {
                return i + 1 < m_tokens.size()
                       && m_tokens[i + 1].kind == NTokenKind::DELIMITER_SEMICOLON;
            }
        }
        if (kind == NTokenKind::TOKEN_EOF) {
            break;
        }
    }
    return false;
}

// struct 主体：`{ field; field; ... }`（不含结尾分号；tag 调用方已消费）
std::unique_ptr<StructDeclaration>
Parser::parseStructBody(const std::string& tag, int line, int column) {
    auto structDecl = std::make_unique<StructDeclaration>(tag, line, column);
    expect(NTokenKind::DELIMITER_LBRACE);

    while (currentToken().kind != NTokenKind::DELIMITER_RBRACE
           && currentToken().kind != NTokenKind::TOKEN_EOF) {
        if (currentToken().kind == NTokenKind::KEYWORD_TYPEDEF) {
            error("typedef is not allowed inside a struct");
        }
        bool fieldIsStruct = false;
        int fieldLine = 0;
        int fieldColumn = 0;
        std::string fieldType = parseTypePrefix(fieldIsStruct, fieldLine, fieldColumn);

        int pointerDepth = 0;
        while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
            ++pointerDepth;
            advance();
        }

        if (currentToken().kind != NTokenKind::IDENTIFIER) {
            error("Expected member name");
        }
        std::string fieldName = currentToken().value;
        advance();

        auto field =
          std::make_unique<VarDeclaration>(fieldType, fieldName, fieldLine, fieldColumn);
        field->isStructTag = fieldIsStruct;
        field->pointerDepth = pointerDepth;

        // 数组成员：int arr[4];
        if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            field->isArray = true;
            field->arrayDims = 1;
            advance();
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                error("Expected integer constant array size");
            }
            field->arraySize = std::stoi(currentToken().value);
            advance();
            expect(NTokenKind::DELIMITER_RBRACKET);
            while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                ++field->arrayDims;
                advance();
                if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                    error("Expected integer constant array size");
                }
                advance();
                expect(NTokenKind::DELIMITER_RBRACKET);
            }
        }

        if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
            error("struct field initializers are not supported");
        }

        expect(NTokenKind::DELIMITER_SEMICOLON);
        structDecl->fields.push_back(std::move(field));
    }

    expect(NTokenKind::DELIMITER_RBRACE);
    return structDecl;
}

// typedef：typedef int T; / typedef struct Point T; / typedef struct { .. } T;
//          typedef struct Point { .. } T; / typedef int* P;
std::unique_ptr<Decl> Parser::parseTypedefDeclaration() {
    int line = currentToken().line;
    int column = currentToken().column;
    advance(); // 消费 typedef

    // R9 头文件模式：限定符/修饰符（typedef unsigned int u32;）
    std::string headerForcedBase; // 非空 = 裸修饰符即基类型（按 int 解释）
    if (m_headerMode) {
        const bool sawBaseModifier = skipHeaderQualifiers();
        if (sawBaseModifier && !isTypeStart()) {
            headerForcedBase = "int";
        }
    }

    auto decl = std::make_unique<TypedefDeclaration>("", line, column);

    if (!headerForcedBase.empty()) {
        // `typedef unsigned u32;` → int
        decl->baseType = headerForcedBase;
    } else if (currentToken().kind == NTokenKind::KEYWORD_STRUCT) {
        int structLine = currentToken().line;
        int structColumn = currentToken().column;
        decl->baseIsStruct = true;
        advance(); // 消费 struct

        if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
            // 匿名定义：内部标签由解析器生成
            std::string anonTag = "__anon_" + std::to_string(m_anonCounter++);
            decl->structDef = parseStructBody(anonTag, structLine, structColumn);
            decl->baseType = anonTag;
        } else if (currentToken().kind == NTokenKind::IDENTIFIER) {
            decl->baseType = currentToken().value;
            advance();
            if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
                // typedef struct Tag { ... } Alias;：定义与别名一体
                decl->structDef =
                  parseStructBody(decl->baseType, structLine, structColumn);
            }
        } else {
            error("Expected struct tag after 'struct'");
        }
    } else if (currentToken().kind == NTokenKind::KEYWORD_INT
               || currentToken().kind == NTokenKind::KEYWORD_I32
               || currentToken().kind == NTokenKind::KEYWORD_CHAR
               || currentToken().kind == NTokenKind::KEYWORD_VOID) {
        // `typedef i32 MyInt;`：i32 归一化为 "int"（与变量/函数声明一致）
        decl->baseType =
          (currentToken().kind == NTokenKind::KEYWORD_I32) ? "int" : currentToken().value;
        advance();
    } else {
        error("Expected type after 'typedef'");
    }

    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++decl->pointerDepth;
        advance();
    }

    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected typedef name");
    }
    decl->alias = currentToken().value;
    advance();

    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        error("array typedefs are not supported");
    }

    expect(NTokenKind::DELIMITER_SEMICOLON);
    return decl;
}

std::unique_ptr<VarDeclaration> Parser::parseVarDeclaration() {
    // 获取类型（builtin 或 struct Tag）
    bool isStructTag = false;
    int line = 0;
    int column = 0;
    std::string type = parseTypePrefix(isStructTag, line, column);

    // struct 定义/前向声明不允许出现在变量声明位置
    if (isStructTag
        && (currentToken().kind == NTokenKind::DELIMITER_LBRACE
            || currentToken().kind == NTokenKind::DELIMITER_SEMICOLON)) {
        error("struct definition is only allowed at file scope");
    }

    // 指针层级
    int pointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++pointerDepth;
        advance();
    }

    // 获取名称
    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected identifier");
    }

    std::string name = currentToken().value;
    advance();

    auto varDecl = std::make_unique<VarDeclaration>(type, name, line, column);
    varDecl->isStructTag = isStructTag;
    varDecl->pointerDepth = pointerDepth;

    // 数组后缀：int a[10]；多维在此一并解析（arrayDims≥2 由语义分析报不支持）
    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        varDecl->isArray = true;
        varDecl->arrayDims = 1;
        advance();
        if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
            error("Expected integer constant array size");
        }
        varDecl->arraySize = std::stoi(currentToken().value);
        advance();
        expect(NTokenKind::DELIMITER_RBRACKET);
        while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            ++varDecl->arrayDims;
            advance();
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                error("Expected integer constant array size");
            }
            advance();
            expect(NTokenKind::DELIMITER_RBRACKET);
        }
    }

    // 检查是否有初始化（{ ... } → 逐成员初始化器）
    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseInitializer();
    }

    // 期望分号
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return varDecl;
}

// 声明初始化：`{ e1, e2 }` → InitListExpr，否则普通表达式
std::unique_ptr<Expr> Parser::parseInitializer() {
    if (currentToken().kind != NTokenKind::DELIMITER_LBRACE) {
        return parseExpression();
    }
    int line = currentToken().line;
    int column = currentToken().column;
    advance();
    auto init = std::make_unique<InitListExpr>(line, column);
    if (currentToken().kind != NTokenKind::DELIMITER_RBRACE) {
        do {
            init->values.push_back(parseExpression());
        } while (match(NTokenKind::DELIMITER_COMMA));
    }
    expect(NTokenKind::DELIMITER_RBRACE);
    return init;
}

std::unique_ptr<Stmt> Parser::parseVarDeclarationStmt() {
    // 获取类型（builtin 或 struct Tag）
    bool isStructTag = false;
    int line = 0;
    int column = 0;
    std::string type = parseTypePrefix(isStructTag, line, column);

    // 函数体内不允许 struct 定义/前向声明（局部 struct 变量引用全局标签合法）
    if (isStructTag
        && (currentToken().kind == NTokenKind::DELIMITER_LBRACE
            || currentToken().kind == NTokenKind::DELIMITER_SEMICOLON)) {
        error("struct definition is only allowed at file scope");
    }

    // 指针层级
    int pointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++pointerDepth;
        advance();
    }

    // 获取名称
    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected identifier");
    }

    std::string name = currentToken().value;
    advance();

    auto varDecl = std::make_unique<StmtVarDeclaration>(type, name, line, column);
    varDecl->isStructTag = isStructTag;
    varDecl->pointerDepth = pointerDepth;

    // 数组后缀：与 parseVarDeclaration 相同的多维解析策略
    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
        varDecl->isArray = true;
        varDecl->arrayDims = 1;
        advance();
        if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
            error("Expected integer constant array size");
        }
        varDecl->arraySize = std::stoi(currentToken().value);
        advance();
        expect(NTokenKind::DELIMITER_RBRACKET);
        while (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            ++varDecl->arrayDims;
            advance();
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT) {
                error("Expected integer constant array size");
            }
            advance();
            expect(NTokenKind::DELIMITER_RBRACKET);
        }
    }

    // 检查是否有初始化（{ ... } → 逐成员初始化器）
    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        advance();
        varDecl->initializer = parseInitializer();
    }

    // 期望分号
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return varDecl;
}

std::unique_ptr<FuncDeclaration> Parser::parseFuncDeclaration(bool isCoro) {
    // 获取返回类型（builtin 或 struct Tag）
    bool returnIsStruct = false;
    int line = 0;
    int column = 0;
    std::string returnType = parseTypePrefix(returnIsStruct, line, column);

    // 返回类型指针层级：int* f()
    int returnPointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++returnPointerDepth;
        advance();
    }

    // 获取函数名
    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected function name");
    }

    std::string name = currentToken().value;
    advance();

    auto funcDecl = std::make_unique<FuncDeclaration>(returnType, name, line, column);
    funcDecl->returnIsStruct = returnIsStruct;
    funcDecl->returnPointerDepth = returnPointerDepth;
    funcDecl->isCoro = isCoro; // PRD R12：coro 修饰的协程函数

    // 解析参数列表
    expect(NTokenKind::DELIMITER_LPAREN);

    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        // R9 头文件模式：`(void)` 空参表（C 头文件惯例，语言本体不支持）
        if (m_headerMode && currentToken().kind == NTokenKind::KEYWORD_VOID
            && peekToken().kind == NTokenKind::DELIMITER_RPAREN) {
            advance();
        } else {
            do {
                // 解析参数（builtin 或 struct Tag）
                bool paramIsStruct = false;
                int paramLine = 0;
                int paramColumn = 0;
                std::string paramType =
                  parseTypePrefix(paramIsStruct, paramLine, paramColumn);
                (void)paramLine;
                (void)paramColumn;

                // 参数指针层级：int f(int* a, char* s)
                int paramPointerDepth = 0;
                while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
                    ++paramPointerDepth;
                    advance();
                }

                if (currentToken().kind != NTokenKind::IDENTIFIER) {
                    error("Expected parameter name");
                }

                std::string paramName = currentToken().value;
                advance();

                // 数组形参不支持：数组实参传给指针形参即完成退化（PRD R1.2）
                if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                    error(
                      "array parameters are not supported; declare the parameter as a "
                      "pointer");
                }

                // 参数沿用既有约定记录函数声明位置（诊断落点与既有负例一致）
                auto param =
                  std::make_unique<VarDeclaration>(paramType, paramName, line, column);
                param->isStructTag = paramIsStruct;
                param->pointerDepth = paramPointerDepth;
                funcDecl->parameters.push_back(std::move(param));

            } while (match(NTokenKind::DELIMITER_COMMA));
        }
    }

    expect(NTokenKind::DELIMITER_RPAREN);

    // 解析函数体
    funcDecl->body = parseCompoundStatement();

    return funcDecl;
}

// extern 声明（PRD R3）：`extern int puts(char* s);` / `extern void exit(int);` /
// `extern int printf(char* fmt, ...);`。形态与函数声明一致但无函数体（';' 收尾），
// 仅允许文件作用域（函数体内由 parseStatement 报错）
std::unique_ptr<FuncDeclaration> Parser::parseExternDeclaration() {
    const Token externToken = currentToken();
    advance(); // 消费 extern

    // 返回类型（builtin 或 struct Tag）
    bool returnIsStruct = false;
    int line = 0;
    int column = 0;
    std::string returnType = parseTypePrefix(returnIsStruct, line, column);

    // 返回类型指针层级：extern char* getenv(char* name);
    int returnPointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++returnPointerDepth;
        advance();
    }

    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected function name after 'extern' declaration type");
    }
    std::string name = currentToken().value;
    advance();

    auto funcDecl = std::make_unique<FuncDeclaration>(returnType, name, line, column);
    funcDecl->returnIsStruct = returnIsStruct;
    funcDecl->returnPointerDepth = returnPointerDepth;
    funcDecl->isExtern = true;

    // 参数列表：与函数声明同形，尾部可带 ...（varargs）
    expect(NTokenKind::DELIMITER_LPAREN);
    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        // R9 头文件模式：`(void)` 空参表
        if (m_headerMode && currentToken().kind == NTokenKind::KEYWORD_VOID
            && peekToken().kind == NTokenKind::DELIMITER_RPAREN) {
            advance();
        } else {
            do {
                if (currentToken().kind == NTokenKind::ELLIPSIS) {
                    advance();
                    funcDecl->isVariadic = true;
                    break; // ... 必须是最后一个参数
                }
                bool paramIsStruct = false;
                int paramLine = 0;
                int paramColumn = 0;
                std::string paramType =
                  parseTypePrefix(paramIsStruct, paramLine, paramColumn);
                (void)paramLine;
                (void)paramColumn;

                int paramPointerDepth = 0;
                while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
                    ++paramPointerDepth;
                    advance();
                }

                if (currentToken().kind != NTokenKind::IDENTIFIER) {
                    error("Expected parameter name in 'extern' declaration");
                }
                std::string paramName = currentToken().value;
                advance();

                if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                    error(
                      "array parameters are not supported; declare the parameter as a "
                      "pointer");
                }

                auto param =
                  std::make_unique<VarDeclaration>(paramType, paramName, line, column);
                param->isStructTag = paramIsStruct;
                param->pointerDepth = paramPointerDepth;
                funcDecl->parameters.push_back(std::move(param));
            } while (match(NTokenKind::DELIMITER_COMMA));
        }

        // ... 之后只允许 ')'（命名参数不能跟在可变部分之后）
        if (funcDecl->isVariadic && currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
            error("expected ')' after '...' in 'extern' declaration");
        }
    }
    expect(NTokenKind::DELIMITER_RPAREN);

    // extern 声明无函数体：';' 收尾（出现 '{' 视为把定义写在 extern 声明上）
    if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
        errorAt(externToken, "'extern' declaration of '" + name + "' cannot have a body");
    }
    expect(NTokenKind::DELIMITER_SEMICOLON);
    return funcDecl;
}

// R9 头文件函数原型：`int add(int a, int b);` / `int sum(int arr[], int n);` /
// `int f(void);` / `int printf(char* fmt, ...);`。与 extern 声明同构（无函数
// 体）但以 isPrototype 标记：语义层可与同名定义合并（C 原型语义），未被定义
// 的原型经既有 callx 路径解析为宿主外部符号。数组形参按 C 语义退化为指针。
std::unique_ptr<FuncDeclaration> Parser::parsePrototypeDeclaration() {
    bool returnIsStruct = false;
    int line = 0;
    int column = 0;
    std::string returnType = parseTypePrefix(returnIsStruct, line, column);

    // 返回类型指针层级：char* getenv(char* name);
    int returnPointerDepth = 0;
    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        ++returnPointerDepth;
        advance();
    }

    if (currentToken().kind != NTokenKind::IDENTIFIER) {
        error("Expected function name in prototype");
    }
    std::string name = currentToken().value;
    advance();

    auto funcDecl = std::make_unique<FuncDeclaration>(returnType, name, line, column);
    funcDecl->returnIsStruct = returnIsStruct;
    funcDecl->returnPointerDepth = returnPointerDepth;
    funcDecl->isPrototype = true;

    expect(NTokenKind::DELIMITER_LPAREN);
    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        // `(void)` 空参表（C 头文件惯例）
        if (currentToken().kind == NTokenKind::KEYWORD_VOID
            && peekToken().kind == NTokenKind::DELIMITER_RPAREN) {
            advance();
        } else {
            do {
                if (currentToken().kind == NTokenKind::ELLIPSIS) {
                    advance();
                    funcDecl->isVariadic = true;
                    break; // ... 必须是最后一个参数
                }
                bool paramIsStruct = false;
                std::string paramType = parseTypePrefix(paramIsStruct, line, column);

                int paramPointerDepth = 0;
                while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
                    ++paramPointerDepth;
                    advance();
                }

                // 无名形参（`int add(int, int);`）合法，名字留空
                std::string paramName;
                if (currentToken().kind == NTokenKind::IDENTIFIER) {
                    paramName = currentToken().value;
                    advance();
                }

                // 数组形参退化（C 语义）：形参名后缀 `[...]` 折算一级指针
                // （形参数组本就按指针传递，长度文本忽略）
                if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                    ++paramPointerDepth;
                    advance();
                    int bracketDepth = 1;
                    while (bracketDepth > 0
                           && currentToken().kind != NTokenKind::TOKEN_EOF) {
                        if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                            ++bracketDepth;
                        } else if (currentToken().kind
                                   == NTokenKind::DELIMITER_RBRACKET) {
                            --bracketDepth;
                        }
                        advance();
                    }
                    if (bracketDepth != 0) {
                        error("Expected ']' in array parameter of prototype");
                    }
                    if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
                        error("multidimensional array parameters are not supported");
                    }
                }

                auto param =
                  std::make_unique<VarDeclaration>(paramType, paramName, line, column);
                param->isStructTag = paramIsStruct;
                param->pointerDepth = paramPointerDepth;
                funcDecl->parameters.push_back(std::move(param));
            } while (match(NTokenKind::DELIMITER_COMMA));
        }

        // ... 之后只允许 ')'
        if (funcDecl->isVariadic && currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
            error("expected ')' after '...' in prototype");
        }
    }
    expect(NTokenKind::DELIMITER_RPAREN);
    expect(NTokenKind::DELIMITER_SEMICOLON);
    return funcDecl;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    // R9 头文件模式：C 限定符/修饰符开头的局部声明（const int x = 5; /
    // unsigned i = 0;）按变量声明解析（修饰符由 parseTypePrefix 消化）
    if (m_headerMode && currentToken().kind == NTokenKind::IDENTIFIER
        && isHeaderQualifierWord(currentToken().value)) {
        return parseVarDeclarationStmt();
    }
    // typedef 别名开头的语句是局部变量声明（`MyInt x = 5;`）
    if (currentToken().kind == NTokenKind::IDENTIFIER
        && isTypedefName(currentToken().value)) {
        return parseVarDeclarationStmt();
    }
    switch (currentToken().kind) {
    case NTokenKind::DELIMITER_LBRACE:
        return parseCompoundStatement();
    case NTokenKind::KEYWORD_IF:
        return parseIfStatement();
    case NTokenKind::KEYWORD_WHILE:
        return parseWhileStatement();
    case NTokenKind::KEYWORD_FOR:
        return parseForStatement();
    case NTokenKind::KEYWORD_RETURN:
        return parseReturnStatement();
    case NTokenKind::KEYWORD_BREAK:
        return parseBreakStatement();
    case NTokenKind::KEYWORD_CONTINUE:
        return parseContinueStatement();
    case NTokenKind::KEYWORD_INT:
    case NTokenKind::KEYWORD_I32:
    case NTokenKind::KEYWORD_CHAR:
    case NTokenKind::KEYWORD_VOID:
        // 变量声明作为语句处理
        return parseVarDeclarationStmt();
    case NTokenKind::KEYWORD_STRUCT:
        // 局部 struct 变量声明（引用全局标签）合法；定义/前向声明在
        // parseVarDeclarationStmt 中报错
        return parseVarDeclarationStmt();
    case NTokenKind::KEYWORD_TYPEDEF:
        error("typedef declarations are only allowed at file scope");
        return nullptr;
    case NTokenKind::KEYWORD_EXTERN:
        // extern 声明仅限文件作用域（PRD R3）：语句位置一律报错
        error("extern declarations are only allowed at file scope");
        return nullptr;
    case NTokenKind::KEYWORD_DEFER:
        // defer 语句（PRD R10；追加在既有语句分发链之后）
        return parseDeferStatement();
    case NTokenKind::KEYWORD_YIELD:
        // yield 语句（PRD R12；追加在既有语句分发链之后，仅在 coro 函数内
        // 合法——语义层裁决）
        return parseYieldStatement();
    default:
        return parseExprStatement();
    }
}

std::unique_ptr<CompoundStmt> Parser::parseCompoundStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::DELIMITER_LBRACE);

    auto compound = std::make_unique<CompoundStmt>(line, column);

    while (currentToken().kind != NTokenKind::DELIMITER_RBRACE
           && currentToken().kind != NTokenKind::TOKEN_EOF) {
        auto stmt = parseStatement();
        if (stmt) {
            compound->statements.push_back(std::move(stmt));
        }
    }

    expect(NTokenKind::DELIMITER_RBRACE);

    return compound;
}

std::unique_ptr<IfStmt> Parser::parseIfStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_IF);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto ifStmt = std::make_unique<IfStmt>(line, column);
    ifStmt->condition = parseExpression();

    expect(NTokenKind::DELIMITER_RPAREN);

    ifStmt->thenBranch = parseStatement();

    if (currentToken().kind == NTokenKind::KEYWORD_ELSE) {
        advance();
        ifStmt->elseBranch = parseStatement();
    }

    return ifStmt;
}

std::unique_ptr<WhileStmt> Parser::parseWhileStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_WHILE);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto whileStmt = std::make_unique<WhileStmt>(line, column);
    whileStmt->condition = parseExpression();

    expect(NTokenKind::DELIMITER_RPAREN);

    whileStmt->body = parseStatement();

    return whileStmt;
}

std::unique_ptr<ForStmt> Parser::parseForStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_FOR);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto forStmt = std::make_unique<ForStmt>(line, column);

    // 初始化部分
    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        forStmt->init = parseStatement();
    } else {
        advance();
    }

    // 条件部分
    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        forStmt->condition = parseExpression();
    }
    expect(NTokenKind::DELIMITER_SEMICOLON);

    // 增量部分
    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        forStmt->increment = parseExpression();
    }
    expect(NTokenKind::DELIMITER_RPAREN);

    forStmt->body = parseStatement();

    return forStmt;
}

std::unique_ptr<ReturnStmt> Parser::parseReturnStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_RETURN);

    auto returnStmt = std::make_unique<ReturnStmt>(line, column);

    if (currentToken().kind != NTokenKind::DELIMITER_SEMICOLON) {
        returnStmt->value = parseExpression();
    }

    expect(NTokenKind::DELIMITER_SEMICOLON);

    return returnStmt;
}

std::unique_ptr<BreakStmt> Parser::parseBreakStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_BREAK);
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return std::make_unique<BreakStmt>(line, column);
}

std::unique_ptr<ContinueStmt> Parser::parseContinueStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_CONTINUE);
    expect(NTokenKind::DELIMITER_SEMICOLON);

    return std::make_unique<ContinueStmt>(line, column);
}

std::unique_ptr<ExprStmt> Parser::parseExprStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    auto exprStmt = std::make_unique<ExprStmt>(line, column);
    exprStmt->expression = parseExpression();

    expect(NTokenKind::DELIMITER_SEMICOLON);

    return exprStmt;
}

std::unique_ptr<Expr> Parser::parseExpression() { return parseAssignment(); }

std::unique_ptr<Expr> Parser::parseAssignment() {
    auto expr = parseLogicalOr();

    if (currentToken().kind == NTokenKind::OPERATOR_ASSIGN) {
        // 左值形式：标识符（x = v）、下标（a[i] = v）、解引用（*p = v）、
        // 成员访问（p.x = v / p->x = v）
        const bool validTarget = expr->type == ASTNodeType::IDENTIFIER_EXPR
                                 || expr->type == ASTNodeType::INDEX_EXPR
                                 || expr->type == ASTNodeType::MEMBER_EXPR
                                 || (expr->type == ASTNodeType::UNARY_EXPR
                                     && static_cast<const UnaryExpr&>(*expr).op == "*");
        if (!validTarget) {
            error("Invalid assignment target");
        }

        int line = expr->line;
        int column = expr->column;
        auto assignExpr = std::make_unique<AssignExpr>(std::move(expr), line, column);

        advance(); // 跳过 '='
        assignExpr->value = parseAssignment();

        return assignExpr;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (currentToken().kind == NTokenKind::OPERATOR_LOGICAL_OR) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseLogicalAnd();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto expr = parseEquality();

    while (currentToken().kind == NTokenKind::OPERATOR_LOGICAL_AND) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseEquality();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto expr = parseRelational();

    while (currentToken().kind == NTokenKind::OPERATOR_EQUAL
           || currentToken().kind == NTokenKind::OPERATOR_NOT_EQUAL) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseRelational();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseRelational() {
    auto expr = parseAdditive();

    while (currentToken().kind == NTokenKind::OPERATOR_LESS
           || currentToken().kind == NTokenKind::OPERATOR_LESS_EQUAL
           || currentToken().kind == NTokenKind::OPERATOR_GREATER
           || currentToken().kind == NTokenKind::OPERATOR_GREATER_EQUAL) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseAdditive();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    auto expr = parseMultiplicative();

    while (currentToken().kind == NTokenKind::OPERATOR_PLUS
           || currentToken().kind == NTokenKind::OPERATOR_MINUS) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseMultiplicative();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    auto expr = parseUnary();

    while (currentToken().kind == NTokenKind::OPERATOR_MULTIPLY
           || currentToken().kind == NTokenKind::OPERATOR_DIVIDE
           || currentToken().kind == NTokenKind::OPERATOR_MODULO) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        auto binaryExpr = std::make_unique<BinaryExpr>(op, line, column);
        binaryExpr->left = std::move(expr);
        binaryExpr->right = parseUnary();

        expr = std::move(binaryExpr);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (currentToken().kind == NTokenKind::OPERATOR_MINUS
        || currentToken().kind == NTokenKind::OPERATOR_LOGICAL_NOT
        || currentToken().kind == NTokenKind::OPERATOR_AMPERSAND
        || currentToken().kind == NTokenKind::OPERATOR_MULTIPLY) {
        std::string op = currentToken().value;
        int line = currentToken().line;
        int column = currentToken().column;
        advance();

        // 前缀一元：- ! & *（& 只能作用于左值、* 只能作用于指针，由语义检查）
        // * 与 [] 的优先级与 C 一致：*p[i] 解析为 *(p[i])
        auto unaryExpr = std::make_unique<UnaryExpr>(op, line, column);
        unaryExpr->operand = parseUnary();

        return unaryExpr;
    }

    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();

    // 后缀下标与成员访问：a[i]、p[i]、p.x、p->x（多维下标由语义分析报不支持）
    while (true) {
        if (currentToken().kind == NTokenKind::DELIMITER_LBRACKET) {
            int line = currentToken().line;
            int column = currentToken().column;
            advance();

            auto indexExpr = std::make_unique<IndexExpr>(std::move(expr), line, column);
            indexExpr->index = parseExpression();
            expect(NTokenKind::DELIMITER_RBRACKET);

            expr = std::move(indexExpr);
            continue;
        }
        if (currentToken().kind == NTokenKind::OPERATOR_DOT
            || currentToken().kind == NTokenKind::OPERATOR_ARROW) {
            const bool arrow = currentToken().kind == NTokenKind::OPERATOR_ARROW;
            const int line = currentToken().line;
            const int column = currentToken().column;
            advance();

            if (currentToken().kind != NTokenKind::IDENTIFIER) {
                error(arrow ? "Expected member name after '->'"
                            : "Expected member name after '.'");
            }
            std::string member = currentToken().value;
            advance();

            expr =
              std::make_unique<MemberExpr>(std::move(expr), member, arrow, line, column);
            continue;
        }
        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token token = currentToken();

    switch (token.kind) {
    case NTokenKind::INTEGER_CONSTANT: {
        int value = std::stoi(token.value);
        advance();
        return std::make_unique<IntegerLiteral>(value, token.line, token.column);
    }

    case NTokenKind::CHAR_CONSTANT: {
        char value = token.value[0];
        advance();
        return std::make_unique<CharLiteral>(value, token.line, token.column);
    }

    case NTokenKind::STRING_CONSTANT: {
        std::string value = token.value;
        advance();
        return std::make_unique<StringLiteral>(value, token.line, token.column);
    }

    case NTokenKind::KEYWORD_NULL: {
        int line = token.line;
        int column = token.column;
        advance();
        return std::make_unique<NullLiteral>(line, column);
    }

    case NTokenKind::IDENTIFIER: {
        std::string name = token.value;
        int line = token.line;
        int column = token.column;
        advance();

        // 检查是否是函数调用
        if (currentToken().kind == NTokenKind::DELIMITER_LPAREN) {
            return parseCall(name);
        }

        return std::make_unique<IdentifierExpr>(name, line, column);
    }

    case NTokenKind::DELIMITER_LPAREN: {
        advance();
        auto expr = parseExpression();
        expect(NTokenKind::DELIMITER_RPAREN);
        return expr;
    }

    case NTokenKind::KEYWORD_MATCH:
        // match 表达式（PRD R11；追加在 primary 分发链之后）
        return parseMatchExpression();

    default:
        error("Unexpected token: " + token.value);
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parseCall(const std::string& callee) {
    Token token = currentToken();
    auto callExpr = std::make_unique<CallExpr>(callee, token.line, token.column);

    expect(NTokenKind::DELIMITER_LPAREN);

    if (currentToken().kind != NTokenKind::DELIMITER_RPAREN) {
        do {
            callExpr->arguments.push_back(parseExpression());
        } while (match(NTokenKind::DELIMITER_COMMA));
    }

    expect(NTokenKind::DELIMITER_RPAREN);

    return callExpr;
}
// ---------------------------------------------------------------------------
// PRD R10/R11 语言特性（追加在文件尾；只新增函数，不改既有解析逻辑）
// ---------------------------------------------------------------------------

// defer 语句（PRD R10）：`defer <语句>;`。body 接受完整语句形态，但语义层
// 只放行表达式语句（其余形态报错，诊断归语义层统一出）。
std::unique_ptr<Stmt> Parser::parseDeferStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_DEFER);

    auto deferStmt = std::make_unique<DeferStmt>(line, column);
    deferStmt->body = parseStatement();
    return deferStmt;
}

// match 表达式（PRD R11）：
//   match (subject) { 0 => 1, 1..9 => 2, 10, 11 => 3, n if n < 0 => 4, _ => 0, }
// - 模式列表以 ',' 分隔（多值），分支以 '=>' 引导，分支间 ',' 分隔且允许尾逗号
// - 分支体：单表达式或块 `{ ... }`（块形态值为 0，供副作用分支使用）
std::unique_ptr<Expr> Parser::parseMatchExpression() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_MATCH);
    expect(NTokenKind::DELIMITER_LPAREN);

    auto matchExpr = std::make_unique<MatchExpr>(line, column);
    matchExpr->subject = parseExpression();

    expect(NTokenKind::DELIMITER_RPAREN);
    expect(NTokenKind::DELIMITER_LBRACE);

    if (currentToken().kind == NTokenKind::DELIMITER_RBRACE) {
        error("match requires at least one arm");
    }

    while (currentToken().kind != NTokenKind::DELIMITER_RBRACE) {
        auto arm = std::make_unique<MatchArm>();
        arm->line = currentToken().line;
        arm->column = currentToken().column;

        // 模式列表：读到 '=>' 为止（多值以 ',' 分隔）
        while (currentToken().kind != NTokenKind::OPERATOR_FAT_ARROW) {
            arm->patterns.push_back(parseMatchPattern());
            if (!match(NTokenKind::DELIMITER_COMMA)) {
                break;
            }
        }
        expect(NTokenKind::OPERATOR_FAT_ARROW);

        // 分支体：块或单表达式
        if (currentToken().kind == NTokenKind::DELIMITER_LBRACE) {
            arm->blockBody = parseCompoundStatement();
        } else {
            arm->exprBody = parseExpression();
        }

        match(NTokenKind::DELIMITER_COMMA); // 分支间分隔符（允许尾逗号）
        matchExpr->arms.push_back(std::move(arm));
    }

    expect(NTokenKind::DELIMITER_RBRACE);
    return matchExpr;
}

// 单个模式（PRD R11）：
// - 整型/字符常量（支持负号前缀）：`0`、`'a'`、`-1`
// - 区间（含端点，决策记录：闭区间）：`1..9`、`'a'..'z'`、`-3..5`
// - 通配：`_`
// - 守卫绑定：`n if n < 0`（绑定名作用域 = 所在分支；裸绑定名不带 if 报错）
std::unique_ptr<MatchPattern> Parser::parseMatchPattern() {
    Token start = currentToken();

    auto pattern = std::make_unique<MatchPattern>();
    pattern->line = start.line;
    pattern->column = start.column;

    // 通配（'_' 经标识符通道词法化）
    if (start.kind == NTokenKind::IDENTIFIER && start.value == "_") {
        advance();
        pattern->kind = MatchPattern::Kind::Wildcard;
        return pattern;
    }

    bool negate = false;
    if (start.kind == NTokenKind::OPERATOR_MINUS) {
        negate = true;
        advance();
    }

    if (currentToken().kind == NTokenKind::INTEGER_CONSTANT
        || currentToken().kind == NTokenKind::CHAR_CONSTANT) {
        // 字符常量 token 值为原始字符（按其码点作模式值）；整型常量按十进制
        const bool isChar = currentToken().kind == NTokenKind::CHAR_CONSTANT;
        const int magnitude = isChar ? static_cast<unsigned char>(currentToken().value[0])
                                     : std::stoi(currentToken().value);
        const int firstValue = negate ? -magnitude : magnitude;
        advance();

        // 区间：lo..hi（hi 侧同样允许负号）
        if (currentToken().kind == NTokenKind::OPERATOR_DOTDOT) {
            advance();
            bool hiNegate = false;
            if (currentToken().kind == NTokenKind::OPERATOR_MINUS) {
                hiNegate = true;
                advance();
            }
            if (currentToken().kind != NTokenKind::INTEGER_CONSTANT
                && currentToken().kind != NTokenKind::CHAR_CONSTANT) {
                error("expected upper bound after '..' in range pattern");
            }
            const int hiMagnitude =
              currentToken().kind == NTokenKind::CHAR_CONSTANT
                ? static_cast<unsigned char>(currentToken().value[0])
                : std::stoi(currentToken().value);
            pattern->kind = MatchPattern::Kind::Range;
            pattern->isChar = isChar;
            pattern->lo = firstValue;
            pattern->hi = hiNegate ? -hiMagnitude : hiMagnitude;
            advance();
            return pattern;
        }

        pattern->kind = MatchPattern::Kind::Constant;
        pattern->isChar = isChar;
        pattern->lo = firstValue;
        return pattern;
    }

    // 守卫绑定：`n if expr`（裸标识符不带 if 报错——PRD 模式清单不含裸绑定）
    if (start.kind == NTokenKind::IDENTIFIER) {
        advance();
        if (currentToken().kind != NTokenKind::KEYWORD_IF) {
            error("binding pattern requires a guard: use 'name if expr'");
        }
        advance(); // 消费 if
        pattern->kind = MatchPattern::Kind::Guard;
        pattern->binding = start.value;
        pattern->guard = parseExpression();
        return pattern;
    }

    error("invalid match pattern");
    return nullptr;
}

// yield 语句（PRD R12）：`yield <表达式>;`。yield 必须带值（挂起即产出；
// 完成态的返回值由 return 承担）。仅 coro 函数体内合法——语义层裁决。
std::unique_ptr<Stmt> Parser::parseYieldStatement() {
    int line = currentToken().line;
    int column = currentToken().column;

    expect(NTokenKind::KEYWORD_YIELD);

    auto yieldStmt = std::make_unique<YieldStmt>(line, column);
    if (currentToken().kind == NTokenKind::DELIMITER_SEMICOLON) {
        errorAt(currentToken(), "yield requires a value expression");
    }
    yieldStmt->value = parseExpression();
    expect(NTokenKind::DELIMITER_SEMICOLON);
    return yieldStmt;
}
