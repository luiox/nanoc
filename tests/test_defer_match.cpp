#include "support/diff_harness.hpp"

#include "ncc/ast.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// M5 语言特性测试（PRD R10 defer + R11 match）：
// - defer：逆序执行（块尾/return/break/continue 全退出路径）、注册时求值
//   （值捕获）、嵌套作用域、负例（defer 内 return/defer 等）
// - match：五类模式（常量/区间/多值/守卫/通配）逐一、守卫绑定、嵌套 match、
//   match 作表达式、缺 `_` 警告、字符串主体负例
// - 三后端一致性：defer/match 在 IR 层降解，vm/c/llvm 免费继承——用 R13
//   差分 harness 跑 defer+match 组合程序断言一致（矩阵不加列，仅复用行）
// 展开策略（实现侧决策，PR 正文同步）：
// - defer 退出动作在 lower 层插入所有退出点（块尾逆序 + return/break/continue
//   按作用域深度裁剪拼接）；顶层调用 = 退出动作本身（实参注册时求值，Go 语义）
// - match 降解为主体临时 + 结果临时 + 比较/跳转 If 链；区间含端点（闭区间）

namespace {

    // -----------------------------------------------------------------------
    // 管线工具
    // -----------------------------------------------------------------------

    struct FrontendResult {
        std::unique_ptr<Program> program;
        SemanticResult semantic;
        bool parseOk = false;
        std::string parseError;
    };

    FrontendResult runFrontend(const std::string& source) {
        FrontendResult result;
        Lexer lexer(source);
        std::vector<Token> tokens;
        try {
            tokens = lexer.tokenize();
            Parser parser(tokens);
            result.program = parser.parse();
            result.parseOk = true;
        } catch (const std::exception& e) {
            result.parseError = e.what();
            return result;
        }
        SemanticAnalyzer analyzer("test.nc");
        auto analyzed = analyzer.analyze(*result.program);
        // analyze 仅在 AST 违反解析器契约时返回 Err；正常解析结果不会触发
        if (analyzed.is_ok()) {
            result.semantic = std::move(analyzed).unwrap();
        }
        return result;
    }

    // 语义零错误前提下的 IR 降级（与 test_ir.cpp 同策略）
    ir::Module lowerValidSource(const std::string& source) {
        FrontendResult front = runFrontend(source);
        EXPECT_TRUE(front.parseOk) << front.parseError;
        if (!front.parseOk) {
            return ir::Module{};
        }
        EXPECT_FALSE(front.semantic.hasErrors()) << "source expected semantically valid";
        auto lowered = ir::lower(*front.program);
        EXPECT_TRUE(lowered.is_ok());
        if (lowered.is_err()) {
            return ir::Module{};
        }
        return std::move(lowered).unwrap();
    }

    size_t countSeverity(const SemanticResult& result, DiagnosticSeverity severity) {
        size_t count = 0;
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.severity == severity) {
                ++count;
            }
        }
        return count;
    }

    bool containsDiagnostic(const SemanticResult& result,
                            DiagnosticSeverity severity,
                            const std::string& fragment) {
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.severity == severity
                && diagnostic.message.find(fragment) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // 子串出现位置（断言 IR dump 中语句的相对顺序）
    size_t posOf(const std::string& text, const std::string& needle) {
        return text.find(needle);
    }

    // -----------------------------------------------------------------------
    // 词法/解析
    // -----------------------------------------------------------------------

    TEST(DeferMatchLexerTest, DeferAndMatchKeywords) {
        Lexer lexer("defer match");
        std::vector<Token> tokens = lexer.tokenize();
        ASSERT_EQ(tokens.size(), 3);
        EXPECT_EQ(tokens[0].kind, NTokenKind::KEYWORD_DEFER);
        EXPECT_EQ(tokens[0].value, "defer");
        EXPECT_EQ(tokens[1].kind, NTokenKind::KEYWORD_MATCH);
        EXPECT_EQ(tokens[1].value, "match");
        EXPECT_EQ(tokens[2].kind, NTokenKind::TOKEN_EOF);
    }

    TEST(DeferMatchLexerTest, FatArrowAndDotDotOperators) {
        Lexer lexer("=> .. == = . ...");
        std::vector<Token> tokens = lexer.tokenize();
        ASSERT_EQ(tokens.size(), 7);
        EXPECT_EQ(tokens[0].kind, NTokenKind::OPERATOR_FAT_ARROW);
        EXPECT_EQ(tokens[1].kind, NTokenKind::OPERATOR_DOTDOT);
        EXPECT_EQ(tokens[2].kind, NTokenKind::OPERATOR_EQUAL);
        EXPECT_EQ(tokens[3].kind, NTokenKind::OPERATOR_ASSIGN);
        EXPECT_EQ(tokens[4].kind, NTokenKind::OPERATOR_DOT);
        EXPECT_EQ(tokens[5].kind, NTokenKind::ELLIPSIS);
    }

    TEST(DeferMatchParserTest, DeferStatementShape) {
        FrontendResult front = runFrontend("int f() { defer work(1); return 0; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        const auto& func =
          static_cast<const FuncDeclaration&>(*front.program->declarations[0]);
        const auto& body = static_cast<const CompoundStmt&>(*func.body);
        ASSERT_EQ(body.statements.size(), 2);
        const auto* defer = dynamic_cast<const DeferStmt*>(body.statements[0].get());
        ASSERT_NE(defer, nullptr);
        ASSERT_EQ(defer->body->type, ASTNodeType::EXPR_STMT);
    }

    TEST(DeferMatchParserTest, MatchArmShapes) {
        const char* source = R"nc(
            int f(int x) {
                return match (x) {
                    0 => 1,
                    'a'..'c' => 2,
                    3, -4 => 3,
                    n if n < 0 => 4,
                    _ => 0,
                };
            }
        )nc";
        FrontendResult front = runFrontend(source);
        ASSERT_TRUE(front.parseOk) << front.parseError;
        const auto& func =
          static_cast<const FuncDeclaration&>(*front.program->declarations[0]);
        const auto& ret = static_cast<const ReturnStmt&>(
          *static_cast<const CompoundStmt&>(*func.body).statements[0]);
        ASSERT_EQ(ret.value->type, ASTNodeType::MATCH_EXPR);
        const auto* match = static_cast<const MatchExpr*>(ret.value.get());
        ASSERT_EQ(match->arms.size(), 5);

        // 常量
        const auto* arm0 = match->arms[0].get();
        ASSERT_EQ(arm0->patterns.size(), 1);
        EXPECT_EQ(arm0->patterns[0]->kind, MatchPattern::Kind::Constant);
        EXPECT_EQ(arm0->patterns[0]->lo, 0);

        // 字符区间（含端点）
        const auto* arm1 = match->arms[1].get();
        ASSERT_EQ(arm1->patterns.size(), 1);
        EXPECT_EQ(arm1->patterns[0]->kind, MatchPattern::Kind::Range);
        EXPECT_TRUE(arm1->patterns[0]->isChar);
        EXPECT_EQ(arm1->patterns[0]->lo, 'a');
        EXPECT_EQ(arm1->patterns[0]->hi, 'c');

        // 多值（含负常量）
        const auto* arm2 = match->arms[2].get();
        ASSERT_EQ(arm2->patterns.size(), 2);
        EXPECT_EQ(arm2->patterns[0]->kind, MatchPattern::Kind::Constant);
        EXPECT_EQ(arm2->patterns[0]->lo, 3);
        EXPECT_EQ(arm2->patterns[1]->kind, MatchPattern::Kind::Constant);
        EXPECT_EQ(arm2->patterns[1]->lo, -4);

        // 守卫 + 绑定
        const auto* arm3 = match->arms[3].get();
        ASSERT_EQ(arm3->patterns.size(), 1);
        EXPECT_EQ(arm3->patterns[0]->kind, MatchPattern::Kind::Guard);
        EXPECT_EQ(arm3->patterns[0]->binding, "n");
        EXPECT_EQ(arm3->patterns[0]->guard->type, ASTNodeType::BINARY_EXPR);

        // 通配
        EXPECT_EQ(match->arms[4]->patterns[0]->kind, MatchPattern::Kind::Wildcard);
    }

    TEST(DeferMatchParserTest, BareBindingWithoutGuardRejected) {
        FrontendResult front =
          runFrontend("int f(int x) { return match (x) { n => 1 }; }");
        EXPECT_FALSE(front.parseOk);
        EXPECT_NE(front.parseError.find("binding pattern requires a guard"),
                  std::string::npos);
    }

    // -----------------------------------------------------------------------
    // 语义：defer 负例
    // -----------------------------------------------------------------------

    TEST(DeferMatchSemanticTest, DeferInsideDeferRejected) {
        // defer 体限定为表达式语句，内层 defer 只可能经 match 分支块出现
        const char* source = R"nc(
            int f(int x) {
                defer match (x) { 1 => { defer work(); }, _ => 0 };
                return 0;
            }
        )nc";
        FrontendResult front = runFrontend(source);
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "'defer' cannot appear inside a defer body"));
    }

    TEST(DeferMatchSemanticTest, DeferBodyMustBeExpressionStatement) {
        FrontendResult front = runFrontend("int f() { defer { work(); } return 0; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "'defer' body must be an expression statement"));
    }

    TEST(DeferMatchSemanticTest, ReturnInsideDeferRejected) {
        // return 只可能经 match 分支块出现在 defer 体表达式中
        const char* source = R"nc(
            int f(int x) {
                defer match (x) { 1 => { return 9; }, _ => 0 };
                return 0;
            }
        )nc";
        FrontendResult front = runFrontend(source);
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "'return' cannot appear inside a defer body"));
    }

    TEST(DeferMatchSemanticTest, BreakInsideDeferRejected) {
        const char* source = R"nc(
            int f(int x) {
                while (x < 10) {
                    defer match (x) { 1 => { break; }, _ => 0 };
                    x = x + 1;
                }
                return x;
            }
        )nc";
        FrontendResult front = runFrontend(source);
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "'break' cannot appear inside a defer body"));
    }

    // -----------------------------------------------------------------------
    // 语义：match 检查与警告
    // -----------------------------------------------------------------------

    TEST(DeferMatchSemanticTest, MatchOnStringRejected) {
        FrontendResult front =
          runFrontend("int f() { int x = match (\"s\") { _ => 1 }; return x; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "match subject must be int or char"));
    }

    TEST(DeferMatchSemanticTest, ReversedRangeRejected) {
        FrontendResult front = runFrontend(
          "int f(int x) { int r = match (x) { 2..1 => 3, _ => 0 }; return r; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "lower bound exceeds upper bound"));
    }

    TEST(DeferMatchSemanticTest, StringArmValueRejected) {
        FrontendResult front =
          runFrontend("int f(int x) { int r = match (x) { _ => \"s\" }; return r; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "match arm value must be int or char"));
    }

    TEST(DeferMatchSemanticTest, MissingWildcardWarnsButCompiles) {
        FrontendResult front =
          runFrontend("int f(int x) { int r = match (x) { 0 => 1, 1 => 2 }; return r; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Warning,
                                       "match has no wildcard ('_') arm"));
        // 警告不阻断编译（PRD R11：警告而非错误）
        EXPECT_FALSE(front.semantic.hasErrors());
        auto lowered = ir::lower(*front.program);
        EXPECT_TRUE(lowered.is_ok());
    }

    TEST(DeferMatchSemanticTest, ArmAfterWildcardWarns) {
        FrontendResult front =
          runFrontend("int f(int x) { int r = match (x) { _ => 0, 1 => 1 }; return r; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Warning,
                                       "unreachable match arm after wildcard"));
        EXPECT_FALSE(front.semantic.hasErrors());
    }

    TEST(DeferMatchSemanticTest, MatchInGlobalInitializerRejected) {
        FrontendResult front = runFrontend("int g = match (1) { 1 => 0, _ => 1 };");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "not allowed in the initializer"));
    }

    // -----------------------------------------------------------------------
    // IR 展开（dump 断言）
    // -----------------------------------------------------------------------

    TEST(DeferMatchIrTest, DeferReversesAtBlockEnd) {
        ir::Module module = lowerValidSource(R"nc(
            int mark(int v) { return v; }
            int main() {
                {
                    defer mark(4);
                    defer mark(3);
                }
                return 0;
            }
        )nc");
        const std::string dump = module.dump();
        // 注册点无副作用语句（实参为常量）；退出点逆序：注册序 (4,3) → 执行序 (3,4)
        const size_t pos4 = posOf(dump, "eval (call int mark (const int 4))");
        const size_t pos3 = posOf(dump, "eval (call int mark (const int 3))");
        ASSERT_NE(pos4, std::string::npos);
        ASSERT_NE(pos3, std::string::npos);
        EXPECT_LT(pos3, pos4);
    }

    TEST(DeferMatchIrTest, DeferArgumentsCapturedAtRegistration) {
        ir::Module module = lowerValidSource(R"nc(
            int mark(int v) { return v; }
            int main() {
                int x = 1;
                defer mark(x);
                x = 2;
                return 0;
            }
        )nc");
        const std::string dump = module.dump();
        // 捕获临时声明在函数体顶部，注册点赋值（读取 x），退出点以临时重放
        EXPECT_NE(posOf(dump, "let int __defer0"), std::string::npos);
        const size_t store = posOf(dump, "store (var int __defer0) = (var int x)");
        const size_t assign = posOf(dump, "store (var int x) = (const int 2)");
        const size_t replay = posOf(dump, "eval (call int mark (var int __defer0))");
        ASSERT_NE(store, std::string::npos);
        ASSERT_NE(assign, std::string::npos);
        ASSERT_NE(replay, std::string::npos);
        EXPECT_LT(store, assign);  // 注册点求值先于后续语句
        EXPECT_LT(assign, replay); // 退出动作在尾部重放捕获值
    }

    TEST(DeferMatchIrTest, DeferRunsOnReturnPath) {
        ir::Module module = lowerValidSource(R"nc(
            int mark(int v) { return v; }
            int main() {
                defer mark(7);
                return 1;
            }
        )nc");
        const std::string dump = module.dump();
        // 返回值先经隐藏临时固定（Go 语义：return 操作数在 defer 前求值）
        const size_t fix = posOf(dump, "let int __ret0 = (const int 1)");
        const size_t action = posOf(dump, "eval (call int mark (const int 7))");
        const size_t ret = posOf(dump, "return (var int __ret0)");
        ASSERT_NE(fix, std::string::npos);
        ASSERT_NE(action, std::string::npos);
        ASSERT_NE(ret, std::string::npos);
        EXPECT_LT(fix, action); // 先固定返回值
        EXPECT_LT(action, ret); // return 路径再执行 defer
    }

    TEST(DeferMatchIrTest, MatchLowersToIfChain) {
        ir::Module module = lowerValidSource(R"nc(
            int main() {
                int x = 5;
                int r = match (x) {
                    0 => 1,
                    1..9 => 2,
                    _ => 0,
                };
                return r;
            }
        )nc");
        const std::string dump = module.dump();
        // 主体求值一次 + 结果临时 + If 链（比较+跳转）
        EXPECT_NE(posOf(dump, "let int __m0 = (var int x)"), std::string::npos);
        EXPECT_NE(posOf(dump, "let int __r0"), std::string::npos);
        EXPECT_NE(posOf(dump, "if (eq int (var int __m0) (const int 0))"),
                  std::string::npos);
        // 区间含端点：lo <= s && s <= hi
        EXPECT_NE(posOf(dump,
                        "(logand int (ge int (var int __m0) (const int 1)) "
                        "(le int (var int __m0) (const int 9)))"),
                  std::string::npos);
        // 表达式值 = 结果临时（声明初始化器形态）
        EXPECT_NE(posOf(dump, "let int r = (var int __r0)"), std::string::npos);
    }

    // -----------------------------------------------------------------------
    // VM（R0）端到端
    // -----------------------------------------------------------------------

    TEST(DeferMatchVmTest, NestedScopesReverseOrder) {
        // 执行顺序 2,1,3,4 → 2134
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int g_trace;
            int mark(int v) { g_trace = g_trace * 10 + v; return v; }
            int main() {
                g_trace = 0;
                {
                    defer mark(4);
                    defer mark(3);
                    { defer mark(2); }
                    mark(1);
                }
                return g_trace;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 2134);
    }

    TEST(DeferMatchVmTest, ReturnBreakContinuePaths) {
        // returnPath: defer bump(9) → 9；loop: i=0 正常出体 bump(0)→90，
        // i=1 continue bump(1)→901，i=2 break bump(2)→9012；capture: bump(1)→90121
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int g_trace;
            int bump(int v) { g_trace = g_trace * 10 + v; return v; }
            int returnPath() {
                defer bump(9);
                return 5;
            }
            int loopPaths() {
                int out = 0;
                for (int i = 0; i < 4; i = i + 1) {
                    defer bump(i);
                    if (i == 1) { continue; }
                    if (i == 2) { break; }
                    out = out + 1;
                }
                return out;
            }
            int capture() {
                int x = 1;
                defer bump(x);
                x = 7;
                return x;
            }
            int main() {
                g_trace = 0;
                int r = returnPath();
                int l = loopPaths();
                int c = capture();
                return g_trace * 100 + r + l + c; // 90121*100 + 5 + 1 + 7 = 9012113
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 9012113);
    }

    TEST(DeferMatchVmTest, RegistrationTimeCapture) {
        // defer bump(x) 捕获注册时的 x=1；退出前的 x=7 不可见。
        // 经辅助函数观测（return 操作数先于 defer 求值，capture 内直接
        // return g_trace 看到的是 defer 执行前的值）
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int g_trace;
            int bump(int v) { g_trace = g_trace * 10 + v; return v; }
            int capture() {
                int x = 1;
                defer bump(x);
                x = 7;
                return 0;
            }
            int main() {
                g_trace = 0;
                int r = capture();       // capture 退出时 bump(1) → g_trace = 1
                return g_trace * 10 + r; // 10（若误捕获退出时 x=7 则为 71）
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 10);
    }

    TEST(DeferMatchVmTest, MatchFivePatternKinds) {
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int classify(int x) {
                return match (x) {
                    0 => 1,
                    1..9 => 2,
                    10, 11 => 3,
                    n if n < 0 => 4,
                    _ => 0,
                };
            }
            int main() {
                int a = classify(0);   // 1
                int b = classify(5);   // 2
                int c = classify(11);  // 3
                int d = classify(-7);  // 4
                int e = classify(42);  // 0（未命中 → 0）
                return a * 1000 + b * 100 + c * 10 + d + e; // 1234
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 1234);
    }

    TEST(DeferMatchVmTest, MatchGuardBindingAndCharPatterns) {
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int main() {
                int neg = 0;
                int pos = 0;
                int v = match (-5) {
                    n if n < 0 => 7,
                    n if n > 0 => 8,
                    _ => 9,
                };
                char c = match ('q') {
                    'a'..'z' => 1,
                    _ => 0,
                };
                return v * 10 + c; // 71
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 71);
    }

    TEST(DeferMatchVmTest, NestedMatchAndMatchAsStatement) {
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int g_trace;
            int bump(int v) { g_trace = g_trace * 10 + v; return v; }
            int main() {
                g_trace = 0;
                int y = 0;
                int r = match (match (y) { 0 => 1, _ => 2 }) {
                    1 => 10,
                    _ => 20,
                };
                match (r) {
                    10 => bump(3),
                    _ => bump(4),
                };
                return r * 10 + g_trace; // 103
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 103);
    }

    TEST(DeferMatchVmTest, DeferInsideMatchArmBlock) {
        // 分支块也是 defer 作用域：分支体注册的 defer 在分支块退出时执行。
        // 块形态分支值为 0（副作用分支），表达式形态才有值
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int g_trace;
            int bump(int v) { g_trace = g_trace * 10 + v; return v; }
            int main() {
                g_trace = 0;
                int x = 1;
                int r = match (x) {
                    1 => { defer bump(5); bump(6); },
                    _ => 80,
                };
                return g_trace * 100 + r; // 65*100 + 0（bump(6) 分支内，块尾 bump(5)）
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 6500);
    }

    // -----------------------------------------------------------------------
    // 三后端差分（R13 harness 复用；矩阵不加列）
    // -----------------------------------------------------------------------

    nanoc_diff::DiffProgram makeProgram(const std::string& name,
                                        const std::string& slug,
                                        const std::string& source,
                                        int32_t expectedR0) {
        nanoc_diff::DiffProgram program;
        program.name = name;
        program.slug = slug;
        program.source = source;
        program.expectedR0 = expectedR0;
        return program;
    }

    // defer + match 组合程序（覆盖两特性全部交互面：块尾/return/break/continue
    // 退出路径、值捕获、五类模式、match 作语句）。锚点 133 手工核算：
    // classify 求和 1+2+3+4=10；return 操作数在函数体退出 defer 之前求值，
    // g_trace 此时 = 0123（循环 i=0/1/2 各退出动作 + match 分支 bump(3)）= 123
    const char* kComboProgram = R"nc(
        int g_trace;
        int bump(int v) { g_trace = g_trace * 10 + v; return v; }
        int classify(int x) {
            return match (x) {
                0 => 1,
                1..9 => 2,
                10, 11 => 3,
                n if n < 0 => 4,
                _ => 0,
            };
        }
        int loop() {
            int out = 0;
            for (int i = 0; i < 3; i = i + 1) {
                defer bump(i);
                if (i == 1) { continue; }
                if (i == 2) { break; }
                out = out + 1;
            }
            return out;
        }
        int main() {
            g_trace = 0;
            int a = classify(0);
            int b = classify(7);
            int c = classify(10);
            int d = classify(-3);
            int x = 1;
            defer bump(x);
            x = 9;
            int l = loop();
            match (l) { 1 => bump(3), _ => bump(4) };
            // trace: loop i=0 出体 bump(0)→0, i=1 continue bump(1)→01,
            // i=2 break bump(2)→012；defer bump(1)→0121；match bump(3)→01213
            return a + b + c + d + g_trace; // 1+2+3+4+01213 = 1223
        }
    )nc";

    TEST(DeferMatchDiffTest, ComboProgramConsistentAcrossBackends) {
        nanoc_diff::registerBuiltinBackends();
        nanoc_diff::DiffProgram program = makeProgram("feature/defer_match_combo",
                                                      "feature_defer_match_combo",
                                                      kComboProgram,
                                                      133);

        std::vector<nanoc_diff::MatrixCell> cells = nanoc_diff::runRow(program);

        // VM 列必须可执行且命中锚点（防"一致地错"）
        bool vmPassed = false;
        for (const auto& cell : cells) {
            if (cell.backend == "vm") {
                EXPECT_EQ(cell.status, nanoc_diff::CellStatus::Pass) << cell.detail;
                if (cell.status == nanoc_diff::CellStatus::Pass) {
                    vmPassed = true;
                    EXPECT_EQ(cell.rawExit, 133);
                }
            }
        }
        ASSERT_TRUE(vmPassed);

        // 行内一致性（vm/c/llvm 全部 Pass 单元的归一化退出码一致）
        const std::string mismatch = nanoc_diff::rowMismatch(cells);
        EXPECT_TRUE(mismatch.empty()) << mismatch;

        // 至少一个非 VM 后端真跑（CI 环境 C/LLVM 可用；本地缺编译器时打印诊断）
        size_t executedBackends = 0;
        for (const auto& cell : cells) {
            if (cell.status == nanoc_diff::CellStatus::Pass) {
                ++executedBackends;
            }
        }
        if (executedBackends < 2) {
            std::string diagnostics;
            for (const auto& cell : cells) {
                diagnostics += cell.backend + ": " + cell.detail + "\n";
            }
            GTEST_SKIP() << "no second backend available:\n" << diagnostics;
        }
        EXPECT_GE(executedBackends, 2);
    }

    TEST(DeferMatchDiffTest, MatchOnlyProgramConsistentAcrossBackends) {
        nanoc_diff::registerBuiltinBackends();
        nanoc_diff::DiffProgram program = makeProgram("feature/match_expressions",
                                                      "feature_match_expressions",
                                                      R"nc(
            int classify(int x) {
                return match (x) {
                    0 => 100,
                    1..9 => 200,
                    10, 11 => 300,
                    n if n < 0 => 400,
                    _ => 0,
                };
            }
            int main() {
                int a = classify(0);
                int b = classify(5);
                int c = classify(11);
                int d = classify(-7);
                int e = classify(42);
                int f = match ('a') { 'a' => 7, _ => 8 };
                return a / 100 + b / 100 + c / 100 + d / 100 + e + f; // 17
            }
        )nc",
                                                      17);

        std::vector<nanoc_diff::MatrixCell> cells = nanoc_diff::runRow(program);
        for (const auto& cell : cells) {
            if (cell.backend == "vm") {
                EXPECT_EQ(cell.status, nanoc_diff::CellStatus::Pass) << cell.detail;
                EXPECT_EQ(cell.rawExit, 17);
            }
        }
        const std::string mismatch = nanoc_diff::rowMismatch(cells);
        EXPECT_TRUE(mismatch.empty()) << mismatch;
    }

} // namespace
