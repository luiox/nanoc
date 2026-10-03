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
