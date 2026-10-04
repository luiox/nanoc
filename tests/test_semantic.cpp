#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// 语义分析模块测试（PRD R1.1）：
// - 正例：examples/*.nc 全部零诊断；遮蔽/前向调用/类型提升等合法程序零诊断
// - 负例：未声明使用、重复定义、类型不匹配、签名不符、缺 return、函数名误用等
// - 诊断断言精确到 file:line:col（Lexer 列号从 1 开始，位置取 token 起始列）
namespace {

    // 解析并分析一段源码；analyze 返回 Err 视为测试失败
    SemanticResult analyzeSource(const std::string& source,
                                 const std::string& fileName = "test.nc") {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        SemanticAnalyzer analyzer(fileName);
        auto analyzed = analyzer.analyze(*program);
        if (analyzed.is_err()) {
            ADD_FAILURE() << "analyze() returned Err: " << analyzed.unwrap_err();
            return SemanticResult{};
        }
        return analyzed.unwrap();
    }

    // 断言诊断条数与每条诊断的完整文本（file:line:col: error: message）
    void expectDiagnostics(const SemanticResult& result,
                           const std::vector<std::string>& expected) {
        ASSERT_EQ(result.diagnostics.len(), expected.size());
        const ca::usize count = std::min(result.diagnostics.len(), expected.size());
        for (ca::usize i = 0; i < count; ++i) {
            EXPECT_EQ(result.diagnostics[i].toString(), expected[i]);
        }
    }

    // 读取 examples/ 下的示例程序：xmake run 的工作目录可能是项目根，也可能是
    // build 输出目录，故从当前目录逐级向上探测；最后再按本源文件位置兜底
    // （__FILE__ 为绝对路径时命中）
    std::string readExample(const std::string& name) {
        std::string prefix;
        for (int depth = 0; depth <= 6; ++depth) {
            std::ifstream in(prefix + "examples/" + name, std::ios::binary);
            if (in) {
                std::ostringstream buffer;
                buffer << in.rdbuf();
                return buffer.str();
            }
            prefix += "../";
        }
        std::string testFile = __FILE__;
        const std::size_t sep = testFile.find_last_of("/\\");
        if (sep != std::string::npos) {
            std::ifstream in(testFile.substr(0, sep) + "/../examples/" + name,
                             std::ios::binary);
            if (in) {
                std::ostringstream buffer;
                buffer << in.rdbuf();
                return buffer.str();
            }
        }
        return "";
    }

    const SymbolSummary* findGlobal(const SemanticResult& result,
                                    const std::string& name) {
        for (const auto& summary : result.globals) {
            if (summary.name == name) {
                return &summary;
            }
        }
        return nullptr;
    }

} // namespace

// ---------------------------------------------------------------------------
// 正例
// ---------------------------------------------------------------------------

// 全部现有示例程序应零诊断
TEST(SemanticTest, PositiveExamplesZeroDiagnostics) {
    const char* names[] = { "hello.nc",
                            "arithmetic.nc",
                            "loop.nc",
                            "control_flow.nc",
                            "functions.nc" };
    for (const char* name : names) {
        std::string source = readExample(name);
        ASSERT_FALSE(source.empty()) << "cannot read example: " << name;
        SemanticResult result = analyzeSource(source, std::string("examples/") + name);
        for (const auto& diagnostic : result.diagnostics) {
            ADD_FAILURE() << name << ": " << diagnostic.toString();
        }
    }
}

// 块作用域遮蔽：内层同名变量合法，出块后外层恢复可见；参数可遮蔽全局
TEST(SemanticTest, PositiveScopeShadowing) {
    std::string source = R"(int g = 1;
int main() {
    int x = 2;
    {
        int x = 3;
        x = x + 1;
    }
    x = x + g;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);

    std::string source2 = R"(int v = 5;
int f(int v) {
    return v;
}
int main() {
    return f(v);
}
)";
    SemanticResult result2 = analyzeSource(source2);
    EXPECT_EQ(result2.diagnostics.len(), 0u);
}

// 前向调用：函数先调用后定义、相互递归均合法（签名统一预登记）
TEST(SemanticTest, PositiveForwardCall) {
    std::string source = R"(int main() {
    return later(1) + twice(2);
}
int later(int x) {
    return x;
}
int twice(int x) {
    return x * 2;
}
int wrap(int x) {
    if (x > 0) {
        return later(x);
    }
    return twice(x);
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// 类型提升：char 参与算术/比较、赋给 int 均隐式提升为 int；int 不能隐式转 char
TEST(SemanticTest, PositiveCharPromotion) {
    std::string source = R"(int main() {
    char c = 'A';
    char d = 'B';
    int sum = c + d;
    int cmp = c < d;
    char e = c;
    int diff = d - 1;
    return sum + cmp + diff + e;
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// char 参数接受 char 实参；char 返回值可提升为 int
TEST(SemanticTest, PositiveCharArgument) {
    std::string source = R"(char id(char c) {
    return c;
}
int main() {
    return id('x');
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// void 函数：无值 return 合法；void 调用作表达式语句合法
TEST(SemanticTest, PositiveVoidFunction) {
    std::string source = R"(void hello() {
    return;
}
int main() {
    hello();
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// for 循环：init 中声明的变量只在循环内可见，循环外重名声明合法
TEST(SemanticTest, PositiveForLoopScope) {
    std::string source = R"(int main() {
    int sum = 0;
    for (int i = 0; i < 3; i = i + 1) {
        sum = sum + i;
    }
    int i = 100;
    return sum + i;
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// 一元运算符与逻辑运算
TEST(SemanticTest, PositiveUnaryAndLogical) {
    std::string source = R"(int main() {
    int x = -5;
    int ok = !0;
    char c = 'a';
    int neg = -c;
    int both = x < 0 && !ok;
    int either = x > 0 || ok;
    return x + ok + neg + both + either;
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// return 覆盖：if/else 两分支都 return 即覆盖全路径
TEST(SemanticTest, PositiveIfElseBothReturn) {
    std::string source = R"(int sign(int x) {
    if (x > 0) {
        return 1;
    } else {
        return 0 - 1;
    }
}
int main() {
    return sign(3);
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
}

// 全局符号摘要：变量/函数分开登记、类型与参数类型可查询
TEST(SemanticTest, PositiveGlobalSymbolSummary) {
    std::string source = R"(int counter = 0;
int add(int a, int b) {
    return a + b;
}
void reset() {
    counter = 0;
}
int main() {
    counter = add(1, 2);
    return counter;
}
)";
    SemanticResult result = analyzeSource(source);
    EXPECT_EQ(result.diagnostics.len(), 0u);
    EXPECT_EQ(result.globals.len(), 4u);

    const SymbolSummary* counter = findGlobal(result, "counter");
    ASSERT_TRUE(counter != nullptr);
    EXPECT_EQ(counter->kind, SymbolKind::Variable);
    EXPECT_EQ(counter->type, "int");

    const SymbolSummary* add = findGlobal(result, "add");
    ASSERT_TRUE(add != nullptr);
    EXPECT_EQ(add->kind, SymbolKind::Function);
    EXPECT_EQ(add->type, "int");
    ASSERT_EQ(add->paramTypes.len(), 2u);
    EXPECT_EQ(add->paramTypes[0], "int");
    EXPECT_EQ(add->paramTypes[1], "int");

    const SymbolSummary* reset = findGlobal(result, "reset");
    ASSERT_TRUE(reset != nullptr);
    EXPECT_EQ(reset->kind, SymbolKind::Function);
    EXPECT_EQ(reset->type, "void");
    EXPECT_EQ(reset->paramTypes.len(), 0u);

    const SymbolSummary* mainFn = findGlobal(result, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_EQ(mainFn->kind, SymbolKind::Function);
    EXPECT_EQ(mainFn->paramTypes.len(), 0u);
}

// ---------------------------------------------------------------------------
// 负例：未声明使用 / 重复定义
// ---------------------------------------------------------------------------

TEST(SemanticTest, NegativeUndeclaredVariableUse) {
    std::string source = R"(int main() {
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:12: error: use of undeclared identifier 'x'" });
}

TEST(SemanticTest, NegativeAssignToUndeclared) {
    std::string source = R"(int main() {
    x = 1;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:5: error: use of undeclared identifier 'x'" });
}

TEST(SemanticTest, NegativeRedeclareVariableSameScope) {
    std::string source = R"(int main() {
    int a = 1;
    int a = 2;
    return a;
}
)";
    SemanticResult result = analyzeSource(source);
    // 声明节点的位置取类型关键字 token（`int` 所在列）
    expectDiagnostics(result, { "test.nc:3:5: error: redefinition of 'a'" });
}

TEST(SemanticTest, NegativeRedefineFunction) {
    std::string source = R"(int f() { return 0; }
int f() { return 1; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:1: error: redefinition of 'f'" });
}

// 同一作用域内变量与函数共享命名空间：全局变量与函数同名冲突
TEST(SemanticTest, NegativeGlobalVarFunctionCollision) {
    std::string source = R"(int f;
int f() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:1:1: error: redefinition of 'f'" });
}

TEST(SemanticTest, NegativeDuplicateGlobalVariable) {
    std::string source = R"(int g;
int g;
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:1: error: redefinition of 'g'" });
}

// 参数与函数体顶层局部变量共用作用域：同名即重复定义
TEST(SemanticTest, NegativeParamRedefinedByLocal) {
    std::string source = R"(int f(int a) {
    int a = 1;
    return a;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:5: error: redefinition of 'a'" });
}

TEST(SemanticTest, NegativeDuplicateParameter) {
    std::string source = R"(int f(int a, int a) {
    return a;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:1:1: error: redefinition of 'a'" });
}

// ---------------------------------------------------------------------------
// 负例：保留名 `_`（match 通配模式专用，规范 §2.3/§6.2）
// ---------------------------------------------------------------------------

// `_` 为 match 通配模式的保留拼写：一切声明位置（全局/局部变量、函数参数、
// 函数名、struct 成员、typedef 名）出现名为 `_` 的声明均报语义错误；诊断
// 位置取各声明的既有落点（类型关键字 / typedef 关键字 / 函数声明位置）
TEST(SemanticTest, NegativeReservedUnderscoreDeclarations) {
    const std::string message =
      "'_' is reserved for the match wildcard pattern and cannot be used as a "
      "declared name";

    struct ReservedCase {
        const char* name;
        const char* source;
        std::string diagnostic;
    };
    const ReservedCase cases[] = {
        // 全局变量（声明位置 = 类型关键字 token）
        { "global variable",
          "int _ = 5;\nint main() { return 0; }\n",
          "test.nc:1:1: error: " + message },
        // 局部变量
        { "local variable",
          "int main() {\n    int _ = 5;\n    return 0;\n}\n",
          "test.nc:2:5: error: " + message },
        // 函数参数（沿用既有约定，位置 = 函数声明位置）
        { "parameter",
          "int f(int _) {\n    return 0;\n}\n",
          "test.nc:1:1: error: " + message },
        // 函数名
        { "function name",
          "int _() {\n    return 0;\n}\n",
          "test.nc:1:1: error: " + message },
        // struct 成员（声明位置 = 成员类型关键字 token）
        { "struct member",
          "struct S {\n    int _;\n};\nint main() { return 0; }\n",
          "test.nc:2:5: error: " + message },
        // typedef 别名（声明位置 = typedef 关键字 token）
        { "typedef alias",
          "typedef int _;\nint main() { return 0; }\n",
          "test.nc:1:1: error: " + message },
    };
    for (const ReservedCase& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        SemanticResult result = analyzeSource(testCase.source);
        expectDiagnostics(result, { testCase.diagnostic });
    }
}

// 正例对照：`_` 仅通配模式保留拼写——下划线前缀标识符照常可用，match 通配
// 分支（解析器在模式入口拦截为 Wildcard，不经声明登记）不受保留名检查影响
TEST(SemanticTest, PositiveUnderscorePrefixAndMatchWildcard) {
    std::string source = R"(int main() {
    int _x = 2;
    int r = match (_x) {
        1 => 10,
        _ => 0,
    };
    return r + _x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, {});
}

// ---------------------------------------------------------------------------
// 负例：类型不匹配（int→char 窄化禁止 / void 误用）
// ---------------------------------------------------------------------------

TEST(SemanticTest, NegativeNarrowingInitialization) {
    std::string source = R"(int main() {
    char c = 65;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: cannot implicitly convert int to char in "
                        "initialization of 'c'" });
}

TEST(SemanticTest, NegativeNarrowingAssignment) {
    std::string source = R"(int main() {
    char c;
    c = 1;
    return c;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: cannot implicitly convert int to char in "
                        "assignment to 'c'" });
}

TEST(SemanticTest, NegativeNarrowingReturn) {
    std::string source = R"(char f() {
    return 65;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: cannot implicitly convert int to char in "
                        "return statement" });
}

TEST(SemanticTest, NegativeVoidVariableDeclaration) {
    std::string source = R"(void v;
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:1:1: error: variable 'v' cannot have void type" });
}

TEST(SemanticTest, NegativeVoidParameter) {
    std::string source = R"(int f(void p) {
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:1:1: error: parameter 'p' cannot have void type" });
}

TEST(SemanticTest, NegativeVoidValueUsed) {
    std::string source = R"(void g() {
    return;
}
int main() {
    int x = g();
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:5: error: void value used in initialization of 'x'" });
}

TEST(SemanticTest, NegativeVoidCondition) {
    std::string source = R"(void g() {
    return;
}
int main() {
    if (g()) {
        return 1;
    }
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:5:10: error: void value used as condition" });
}

// ---------------------------------------------------------------------------
// 负例：函数签名与调用点一致性 / 函数名误用
// ---------------------------------------------------------------------------

TEST(SemanticTest, NegativeWrongArgumentCount) {
    std::string source = R"(int add(int a, int b) {
    return a + b;
}
int main() {
    return add(1);
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:5:15: error: function 'add' expects 2 argument(s), but got 1" });
}

TEST(SemanticTest, NegativeWrongArgumentType) {
    std::string source = R"(char id(char c) {
    return c;
}
int main() {
    return id(5);
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:14: error: cannot implicitly convert int to char in "
                        "argument 1 of call to 'id'" });
}

TEST(SemanticTest, NegativeCallUndeclaredFunction) {
    std::string source = R"(int main() {
    return foo(1);
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:15: error: call to undeclared function 'foo'" });
}

TEST(SemanticTest, NegativeCallVariableAsFunction) {
    std::string source = R"(int main() {
    int x = 1;
    return x();
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:3:13: error: 'x' is not a function" });
}

TEST(SemanticTest, NegativeAssignToFunction) {
    std::string source = R"(int f() { return 0; }
int main() {
    f = 3;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:3:5: error: cannot assign to function 'f'" });
}

TEST(SemanticTest, NegativeUseFunctionAsValue) {
    std::string source = R"(int f() { return 0; }
int main() {
    int x = f;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:3:13: error: function 'f' used as a value" });
}

// ---------------------------------------------------------------------------
// 负例：return 覆盖与 return 值匹配
// ---------------------------------------------------------------------------

TEST(SemanticTest, NegativeVoidFunctionReturnValue) {
    std::string source = R"(void f() {
    return 1;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:2:5: error: void function 'f' should not return a value" });
}

TEST(SemanticTest, NegativeNonVoidBareReturn) {
    std::string source = R"(int f() {
    return;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:2:5: error: non-void function 'f' must return a value" });
}

// 函数体走完没有任何 return → 缺 return（函数声明位置报错）
TEST(SemanticTest, NegativeMissingReturnAtEnd) {
    std::string source = R"(int f() {
    int x = 1;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:1:1: error: missing return statement in non-void function 'f'" });
}

// if 无 else：else 路径可能走完函数 → 保守判定缺 return
TEST(SemanticTest, NegativeMissingReturnIfWithoutElse) {
    std::string source = R"(int f(int x) {
    if (x > 0) {
        return 1;
    }
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:1:1: error: missing return statement in non-void function 'f'" });
}

// while 不提供必然返回保证（条件可能一次都不满足）
TEST(SemanticTest, NegativeMissingReturnLoopNotCovering) {
    std::string source = R"(int f() {
    while (1) {
        return 1;
    }
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:1:1: error: missing return statement in non-void function 'f'" });
}

// ---------------------------------------------------------------------------
// 负例：循环控制语句 / 多错误收集
// ---------------------------------------------------------------------------

TEST(SemanticTest, NegativeBreakOutsideLoop) {
    std::string source = R"(int main() {
    break;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:5: error: 'break' outside of a loop" });
}

TEST(SemanticTest, NegativeContinueOutsideLoop) {
    std::string source = R"(int main() {
    continue;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:5: error: 'continue' outside of a loop" });
}

// 一次分析收集全部诊断而非首错即停；诊断按出现顺序排列
TEST(SemanticTest, NegativeMultipleErrorsCollected) {
    std::string source = R"(int main() {
    int a = 1;
    int a = 2;
    return b;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: redefinition of 'a'",
                        "test.nc:4:12: error: use of undeclared identifier 'b'" });
    EXPECT_EQ(result.errorCount(), 2u);
    EXPECT_TRUE(result.hasErrors());
}

// 诊断结构：严重级别字段与文件名前缀
TEST(SemanticTest, DiagnosticStructureAndFilePrefix) {
    std::string source = R"(int main() {
    return x;
}
)";
    SemanticResult result = analyzeSource(source, "demo.nc");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].severity, DiagnosticSeverity::Error);
    EXPECT_EQ(result.diagnostics[0].file, "demo.nc");
    EXPECT_EQ(result.diagnostics[0].line, 2);
    EXPECT_EQ(result.diagnostics[0].column, 12);
    EXPECT_EQ(result.diagnostics[0].message, "use of undeclared identifier 'x'");
}

// ---------------------------------------------------------------------------
// R1.2 类型系统扩展（第一批）：字符串字面量 / 指针 / 一维数组
// ---------------------------------------------------------------------------

// 正例：指针声明/取址/解引用/算术/比较、NULL、数组声明/下标/退化传参
TEST(SemanticTest, PositivePointersAndArrays) {
    std::string source = R"(int sum(int* a, int n) {
    int s = 0;
    for (int i = 0; i < n; i = i + 1) {
        s = s + a[i];
    }
    return s;
}
int* pick(int* a) {
    return &a[2];
}
int main() {
    int a[10];
    int i = 0;
    while (i < 10) {
        a[i] = i * 2;
        i = i + 1;
    }
    int* p = a;
    p = p + 1;
    p = &a[5];
    int x = *p;
    *p = 7;
    int* q = NULL;
    q = p;
    if (q != NULL) {
        x = x + 1;
    }
    if (p > a + 3) {
        x = x + 1;
    }
    char cbuf[8];
    cbuf[0] = 'A';
    char c = cbuf[0];
    char* s = "hello";
    return sum(a, 10) + x + c + (s != NULL);
}
)";
    SemanticResult result = analyzeSource(source);
    for (const auto& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.toString();
    }
}

// 正例：字符串字面量类型为 char*，可赋给 char*、可整体再赋值；
// 全局符号摘要包含指针与数组类型
TEST(SemanticTest, PositiveStringLiteralAndGlobalSummary) {
    std::string source = R"(int* gp;
int garr[4];
char* gstr = "global";
int main() {
    char* s = "NanoC";
    gstr = s;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    for (const auto& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.toString();
    }
    const SymbolSummary* gp = findGlobal(result, "gp");
    ASSERT_TRUE(gp != nullptr);
    EXPECT_EQ(gp->type, "int*");
    const SymbolSummary* garr = findGlobal(result, "garr");
    ASSERT_TRUE(garr != nullptr);
    EXPECT_EQ(garr->type, "int[]");
    const SymbolSummary* gstr = findGlobal(result, "gstr");
    ASSERT_TRUE(gstr != nullptr);
    EXPECT_EQ(gstr->type, "char*");
}

// 负例：解引用非指针
TEST(SemanticTest, NegativeDereferenceNonPointer) {
    std::string source = R"(int main() {
    int x = 5;
    int y = *x;
    return y;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:3:14: error: cannot dereference non-pointer type 'int'" });
}

// 负例：对右值取址（字面量与函数调用结果都不是左值）
TEST(SemanticTest, NegativeAddressOfRvalue) {
    std::string source = R"(int f() {
    return 0;
}
int main() {
    int* p = &5;
    int* q = &f();
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:15: error: cannot take the address of an rvalue",
                        "test.nc:6:16: error: cannot take the address of an rvalue" });
}

// 负例：对数组名取址（数组名已可退化为指针）
TEST(SemanticTest, NegativeAddressOfArray) {
    std::string source = R"(int main() {
    int a[3];
    int* p = &a;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:3:15: error: cannot take the address of an array (it already "
        "decays to a pointer)" });
}

// 负例：数组整体赋值（数组不是可拷贝左值）
TEST(SemanticTest, NegativeArrayWholeAssign) {
    std::string source = R"(int main() {
    int a[3];
    int b[3];
    a = b;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:5: error: cannot assign to array 'a' (arrays are "
                        "not copyable)" });
}

// 负例：数组初始化不支持（标量与数组来源各一）
TEST(SemanticTest, NegativeArrayInitialization) {
    std::string source = R"(int main() {
    int b[3];
    int a[3] = 5;
    int c[3] = b;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: cannot convert 'int' to 'int[]' in "
                        "initialization of 'a'",
                        "test.nc:4:5: error: cannot copy array of type 'int[]' in "
                        "initialization of 'c'" });
}

// 负例：指针类型不匹配的赋值与传参（int* 与 char* 互不相容）
TEST(SemanticTest, NegativePointerTypeMismatch) {
    std::string source = R"(int f(int* p) {
    return *p;
}
int main() {
    int* p;
    char* s = "str";
    p = s;
    return f(s);
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:7:5: error: incompatible pointer types ('char*' to "
                        "'int*') in assignment to 'p'",
                        "test.nc:8:13: error: incompatible pointer types ('char*' to "
                        "'int*') in argument 1 of call to 'f'" });
}

// 负例：多维数组声明
TEST(SemanticTest, NegativeMultiDimArray) {
    std::string source = R"(int main() {
    int a[2][3];
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:2:5: error: multidimensional arrays are not supported" });
}

// 负例：多级指针声明
TEST(SemanticTest, NegativeMultiLevelPointer) {
    std::string source = R"(int main() {
    int** p;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:2:5: error: multi-level pointers are not supported ('int**')" });
}

// 负例：对一级指针取址（会形成二级指针，同样不支持）
TEST(SemanticTest, NegativeAddressOfPointerVariable) {
    std::string source = R"(int main() {
    int x = 1;
    int* p = &x;
    int** pp = &p;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:5: error: multi-level pointers are not supported "
                        "('int**')",
                        "test.nc:4:16: error: multi-level pointers are not supported" });
}

// 负例：字符串字面量赋给 int（char* 不能转标量）
TEST(SemanticTest, NegativeStringToInt) {
    std::string source = R"(int main() {
    int x = "hello";
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: cannot convert 'char*' to 'int' in "
                        "initialization of 'x'" });
}

// 负例：标量赋给指针（非 0 常量不允许；空指针一律用 NULL）
TEST(SemanticTest, NegativeIntToPointer) {
    std::string source = R"(int main() {
    int* p = 5;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: cannot convert 'int' to 'int*' in "
                        "initialization of 'p'" });
}

// 负例：指针赋给标量
TEST(SemanticTest, NegativePointerToScalar) {
    std::string source = R"(int main() {
    int x = 1;
    int* p = &x;
    int y = p;
    return y;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:5: error: cannot convert 'int*' to 'int' in "
                        "initialization of 'y'" });
}

// 负例：NULL 赋给标量（NULL 只与指针相容）
TEST(SemanticTest, NegativeNullToScalar) {
    std::string source = R"(int main() {
    int x = NULL;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: cannot convert 'NULL' to 'int' in "
                        "initialization of 'x'" });
}

// 负例：下标作用于非数组/非指针
TEST(SemanticTest, NegativeSubscriptNonArray) {
    std::string source = R"(int main() {
    int x = 5;
    x[0] = 1;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:3:5: error: subscripted value is not an array or pointer" });
}

// 负例：下标不是整数（字符串字面量不能作下标）
TEST(SemanticTest, NegativeSubscriptNonInteger) {
    std::string source = R"(int main() {
    int a[3];
    a["x"] = 1;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:7: error: array subscript is not an integer" });
}

// 负例：不同类型指针比较
TEST(SemanticTest, NegativePointerCompareDistinctTypes) {
    std::string source = R"(int main() {
    int* p;
    char* s = "x";
    return p == s;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:14: error: comparison between distinct pointer types "
                        "'int*' and 'char*'" });
}

// 负例：指针与指针相减
TEST(SemanticTest, NegativePointerSubtraction) {
    std::string source = R"(int main() {
    int a[5];
    int* p = &a[1];
    int* q = &a[3];
    return q - p;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:14: error: pointer subtraction is not supported" });
}

// 负例：char* 解引用与下标（字符串按字节打包、VM 无字节级 LOAD，本里程碑拒绝）
TEST(SemanticTest, NegativeCharPointerDereference) {
    std::string source = R"(int main() {
    char* s = "hi";
    char c = *s;
    char d = s[0];
    return c + d;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:3:15: error: cannot dereference 'char*' (string literals are "
        "byte-packed; copy into a char array via a host function instead)",
        "test.nc:4:14: error: cannot index through 'char*' (string literals are "
        "byte-packed; copy into a char array via a host function instead)" });
}

// 负例：解引用 NULL
TEST(SemanticTest, NegativeDereferenceNull) {
    std::string source = R"(int main() {
    int x = *NULL;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:14: error: cannot dereference 'NULL'" });
}

// 负例：对函数名取址（无函数指针）
TEST(SemanticTest, NegativeAddressOfFunction) {
    std::string source = R"(int f() {
    return 0;
}
int main() {
    int* p = &f;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:15: error: cannot take the address of function 'f'" });
}

// 负例：指针数组与零长度数组
TEST(SemanticTest, NegativePointerArrayAndZeroSize) {
    std::string source = R"(int main() {
    int* a[3];
    int b[0];
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: arrays of pointers are not supported "
                        "('int*[...]')",
                        "test.nc:3:5: error: array size must be positive" });
}

// 负例：void* 与 void 数组
TEST(SemanticTest, NegativeVoidPointerAndVoidArray) {
    std::string source = R"(int main() {
    void* p;
    void a[3];
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: 'void*' is not supported",
                        "test.nc:3:5: error: cannot declare array of 'void'" });
}

// 负例：char 数组退化为 int*（基类型不符）
TEST(SemanticTest, NegativeCharArrayToPointerOfOtherBase) {
    std::string source = R"(int main() {
    char cbuf[4];
    int* p = cbuf;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: cannot convert 'char[]' to 'int*' in "
                        "initialization of 'p'" });
}

// ---------------------------------------------------------------------------
// R1.2 类型系统扩展（第二批）：struct 与 typedef
// ---------------------------------------------------------------------------

// 正例：struct 定义/成员访问/嵌套成员链/整体拷贝/struct 数组/初始化器/
// 按值传参/struct 返回/自引用指针/前向声明/typedef 各形态
TEST(SemanticTest, PositiveStructAndTypedef) {
    std::string source = R"(struct Node;
typedef struct Node NodeT;
struct Node { struct Node* next; int v; };
struct Point { int x; int y; };
struct Rect { struct Point tl; struct Point br; };
typedef int MyInt;
typedef struct Point PointT;
typedef struct { int w; int h; } Pair;

int area(struct Rect r) {
    return (r.br.x - r.tl.x) * (r.br.y - r.tl.y);
}
struct Point make(int x, int y) {
    struct Point p = {x, y};
    return p;
}
int pairSum(Pair p) {
    return p.w + p.h;
}
NodeT gnode;
int main() {
    struct Point p = make(2, 3);
    PointT q = p;
    q.x = 10;
    struct Rect r;
    r.tl = p;
    r.br = make(5, 7);
    struct Point arr[3];
    arr[0] = p;
    arr[1].x = 4;
    NodeT n;
    n.v = 1;
    n.next = NULL;
    struct Node* pn = &n;
    pn = pn->next;
    MyInt k = 5;
    Pair pr = {1, 2};
    int total = area(r) + pairSum(pr) + arr[1].x + q.x + k;
    if (pn == NULL) {
        total = total + 1;
    }
    return total + p.y;
}
)";
    SemanticResult result = analyzeSource(source);
    for (const auto& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.toString();
    }

    // 全局符号摘要：struct 类型名与参数类型可读
    const SymbolSummary* area = findGlobal(result, "area");
    ASSERT_TRUE(area != nullptr);
    EXPECT_EQ(area->type, "int");
    ASSERT_EQ(area->paramTypes.len(), 1u);
    EXPECT_EQ(area->paramTypes[0], "struct Rect");

    const SymbolSummary* make = findGlobal(result, "make");
    ASSERT_TRUE(make != nullptr);
    EXPECT_EQ(make->type, "struct Point");

    const SymbolSummary* gnode = findGlobal(result, "gnode");
    ASSERT_TRUE(gnode != nullptr);
    EXPECT_EQ(gnode->type, "struct Node");
}

// 负例：未知成员（诊断落在 `.` 运算符位置）
TEST(SemanticTest, NegativeStructUnknownMember) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p;
    int z = p.z;
    return z;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:14: error: struct 'Point' has no member named 'z'" });
}

// 负例：标量取成员
TEST(SemanticTest, NegativeMemberOnScalar) {
    std::string source = R"(int main() {
    int i = 1;
    int z = i.x;
    return z;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:13: error: member access on non-struct type 'int'" });
}

// 负例：struct 指针用 dot（应使用 ->）
TEST(SemanticTest, NegativeDotOnStructPointer) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p;
    struct Point* pp = &p;
    int z = pp.x;
    return z;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:13: error: member access through pointer type "
                        "'struct Point*'; use '->'" });
}

// 负例：-> 用于非指针
TEST(SemanticTest, NegativeArrowOnNonPointer) {
    std::string source = R"(int main() {
    int x = 1;
    int z = x->y;
    return z;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:3:13: error: '->' requires a pointer to struct, but operand has "
        "type 'int'" });
}

// 负例：-> 用于非 struct 指针
TEST(SemanticTest, NegativeArrowOnNonStructPointer) {
    std::string source = R"(int main() {
    int* p;
    int z = p->x;
    return z;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(
      result,
      { "test.nc:3:13: error: '->' requires a pointer to struct, but operand has "
        "type 'int*'" });
}

// 负例：struct 与标量混算
TEST(SemanticTest, NegativeStructScalarArithmetic) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p;
    int z = p + 1;
    return z;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:4:15: error: invalid operands to binary '+'" });
}

// 负例：不同 struct 之间赋值
TEST(SemanticTest, NegativeStructAssignMismatch) {
    std::string source = R"(struct Point { int x; int y; };
struct Rect { int w; int h; };
int main() {
    struct Point p;
    struct Rect r;
    p = r;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:6:5: error: cannot convert 'struct Rect' to "
                        "'struct Point' in assignment to 'p'" });
}

// 负例：struct 赋给标量
TEST(SemanticTest, NegativeStructToScalar) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p;
    int x = p;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:5: error: cannot convert 'struct Point' to 'int' "
                        "in initialization of 'x'" });
}

// 负例：重复成员名
TEST(SemanticTest, NegativeDuplicateMember) {
    std::string source = R"(struct Point { int x; int x; };
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:1:23: error: duplicate member 'x' in 'struct Point'" });
}

// 负例：typedef 重定义
TEST(SemanticTest, NegativeTypedefRedefinition) {
    std::string source = R"(typedef int T;
typedef char T;
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:1: error: redefinition of 'T'" });
}

// 负例：struct 标签重复完整定义（前向声明 + 定义则合法）
TEST(SemanticTest, NegativeStructTagRedefinition) {
    std::string source = R"(struct P { int a; };
struct P { int b; };
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:1: error: redefinition of 'struct P'" });
}

// 负例：自引用值成员（incomplete，仅自引用指针合法）
TEST(SemanticTest, NegativeSelfReferenceByValue) {
    std::string source = R"(struct A { struct A a; };
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:1:12: error: field 'a' has incomplete type "
                        "'struct A'" });
}

// 负例：引用未定义的 struct 类型
TEST(SemanticTest, NegativeUnknownStructType) {
    std::string source = R"(int main() {
    struct Unknown p;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:2:5: error: unknown type 'struct Unknown'" });
}

// 负例：typedef 名带 struct 前缀（类型命名空间内 tag 与别名同查）
TEST(SemanticTest, NegativeTypedefWithStructPrefix) {
    std::string source = R"(typedef int T;
int main() {
    struct T p;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:3:5: error: unknown type 'struct T'" });
}

// 负例：struct 实参传给不兼容的 struct 形参（诊断落在调用点）
TEST(SemanticTest, NegativeStructArgumentMismatch) {
    std::string source = R"(struct A { int x; };
struct B { int y; };
int f(struct A a) {
    return a.x;
}
int main() {
    struct B b;
    return f(b);
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:8:13: error: cannot convert 'struct B' to "
                        "'struct A' in argument 1 of call to 'f'" });
}

// 负例：初始化器长度不符
TEST(SemanticTest, NegativeInitializerLengthMismatch) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p = {1};
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: initializer for struct 'Point' expects 2 "
                        "value(s), but got 1" });
}

// 负例：初始化器成员类型不符（诊断落在对应成员表达式）
TEST(SemanticTest, NegativeInitializerFieldTypeMismatch) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p = {1, "s"};
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:26: error: cannot convert 'char*' to 'int' in "
                        "initialization of field 'y' of 'p'" });
}

// 负例：前向声明后未定义即实例化（incomplete）
TEST(SemanticTest, NegativeIncompleteVariable) {
    std::string source = R"(struct Fwd;
int main() {
    struct Fwd f;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: variable 'f' has incomplete type "
                        "'struct Fwd'" });
}

// 负例：incomplete struct 作参数类型
TEST(SemanticTest, NegativeIncompleteParameter) {
    std::string source = R"(struct Fwd;
int f(struct Fwd g) {
    return 0;
}
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:1: error: parameter 'g' has incomplete type "
                        "'struct Fwd'" });
}

// 负例：incomplete struct 作返回类型
TEST(SemanticTest, NegativeIncompleteReturnType) {
    std::string source = R"(struct Fwd;
struct Fwd f() {
    struct Fwd g;
    return g;
}
int main() { return 0; }
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:1: error: function 'f' has incomplete return type "
                        "'struct Fwd'",
                        "test.nc:3:5: error: variable 'g' has incomplete type "
                        "'struct Fwd'" });
}

// 负例：incomplete struct 指针解引用取成员
TEST(SemanticTest, NegativeMemberAccessIntoIncomplete) {
    std::string source = R"(struct Fwd;
int main() {
    struct Fwd* p;
    int x = p->a;
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:13: error: member access into incomplete type "
                        "'struct Fwd'" });
}

// 负例：struct 数组带初始化器（嵌套/数组初始化器均不支持）
TEST(SemanticTest, NegativeStructArrayInitializer) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point a[2] = {1, 2};
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:3:5: error: array initializers are not supported" });
}

// 负例：标量目标使用花括号初始化器
TEST(SemanticTest, NegativeBraceInitializerOnScalar) {
    std::string source = R"(int main() {
    int x = {1};
    return x;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:2:5: error: brace initializer is only supported for "
                        "struct types" });
}

// 负例：struct 值作条件
TEST(SemanticTest, NegativeStructCondition) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p;
    if (p) {
        return 1;
    }
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:4:9: error: struct value used as condition "
                        "('struct Point')" });
}

// 负例：数组成员整体赋值（数组不可拷贝）
TEST(SemanticTest, NegativeArrayMemberAssign) {
    std::string source = R"(struct S { int arr[3]; };
int main() {
    struct S a;
    struct S b;
    a.arr = b.arr;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result,
                      { "test.nc:5:6: error: cannot assign to array member 'arr' "
                        "(arrays are not copyable)" });
}

// 负例：struct 值比较
TEST(SemanticTest, NegativeStructCompare) {
    std::string source = R"(struct Point { int x; int y; };
int main() {
    struct Point p;
    struct Point q;
    return p == q;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:5:14: error: invalid operands to binary '=='" });
}

// 负例：匿名 struct 的内部标签不可直接引用
TEST(SemanticTest, NegativeAnonymousStructNotReferable) {
    std::string source = R"(typedef struct { int w; } Pair;
int main() {
    struct Pair p;
    return 0;
}
)";
    SemanticResult result = analyzeSource(source);
    expectDiagnostics(result, { "test.nc:3:5: error: unknown type 'struct Pair'" });
}
