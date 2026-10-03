#include "support/diff_harness.hpp"

#include "ncc/ast.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// M6 语言特性测试（PRD R12 coro/yield，决策 A2 无栈协程 = 状态机变换）：
// - 词法/解析：coro/yield 关键字、yield 语句形态（必须带值）、coro 修饰符位置
// - 语义：yield 只在 coro 内、yield×defer 互斥（pending defer 作用域，file:line:col
//   诊断）、coro 直接调用禁止、coro_create 目标必须 coro、返回类型限 int、
//   参数/局部标量限制、内建不可重定义
// - IR 变换（结构断言）：帧存储注入（帧 struct + 全局帧数组 + bump 计数器 +
//   __coro_resume/__coro_done 注入函数）、yield 全部降解为状态存储 + return、
//   跨 yield 局部提升为帧字段、多 coro 时 resume/done 按句柄区间分发
// - VM（R0）端到端：迭代器序列、resume-after-done 幂等、yield 后剩余语句在
//   恢复时继续执行（回归：恢复点 = yield 后继序列入口而非 done 块）、双协程
//   交错、同一 coro 多实例句柄独立
// - 三后端一致性：coro 在 IR 层变换降解，vm/c/llvm 免费继承——R13 harness 跑
//   coro 程序断言一致（矩阵不加列，仅复用行）
// 变换策略（实现侧决策，PR 正文同步）：
// - 每个 coro 函数一个帧类型 __coro_frame_<name>（__state/__done/__retval +
//   参数 + 跨 yield 局部），全局帧槽 __coro_frames_<name>[16]，句柄
//   h = fnid*16 + slot；coro_create 在调用点内联展开（取槽 + 初始化帧）
// - 状态 0 = 入口转移块；yield 点 = store 恢复点 + return 产出值；恢复点 =
//   yield 后继序列入口（不是 done 块——yield 后仍有语句时必须继续执行）

namespace {

    // -----------------------------------------------------------------------
    // 管线工具（与 test_defer_match.cpp 同构）
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

    // 语义零错误前提下的 IR 降级（变换在 lower 尾部自动执行）
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

    const Diagnostic* findDiagnostic(const SemanticResult& result,
                                     const std::string& fragment) {
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.message.find(fragment) != std::string::npos) {
                return &diagnostic;
            }
        }
        return nullptr;
    }

    // -----------------------------------------------------------------------
    // 词法/解析
    // -----------------------------------------------------------------------

    TEST(CoroLexerTest, CoroAndYieldKeywords) {
        Lexer lexer("coro yield");
        std::vector<Token> tokens = lexer.tokenize();
        ASSERT_EQ(tokens.size(), 3);
        EXPECT_EQ(tokens[0].kind, NTokenKind::KEYWORD_CORO);
        EXPECT_EQ(tokens[0].value, "coro");
        EXPECT_EQ(tokens[1].kind, NTokenKind::KEYWORD_YIELD);
        EXPECT_EQ(tokens[1].value, "yield");
        EXPECT_EQ(tokens[2].kind, NTokenKind::TOKEN_EOF);
    }

    TEST(CoroParserTest, CoroFunctionShape) {
        FrontendResult front = runFrontend("coro int f(int n) { yield n; return 0; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        const auto& func =
          static_cast<const FuncDeclaration&>(*front.program->declarations[0]);
        EXPECT_TRUE(func.isCoro);
        const auto& body = static_cast<const CompoundStmt&>(*func.body);
        ASSERT_EQ(body.statements.size(), 2);
        const auto* yield = dynamic_cast<const YieldStmt*>(body.statements[0].get());
        ASSERT_NE(yield, nullptr);
        ASSERT_NE(yield->value, nullptr);
        EXPECT_EQ(yield->value->type, ASTNodeType::IDENTIFIER_EXPR);
    }

    TEST(CoroParserTest, PlainFunctionNotCoro) {
        FrontendResult front = runFrontend("int f() { return 0; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        const auto& func =
          static_cast<const FuncDeclaration&>(*front.program->declarations[0]);
        EXPECT_FALSE(func.isCoro);
    }

    TEST(CoroParserTest, BareYieldRejected) {
        // PRD R12：yield 必须带产出值
        FrontendResult front = runFrontend("coro int f() { yield; return 0; }");
        EXPECT_FALSE(front.parseOk);
    }

    TEST(CoroParserTest, CoroCannotPrecedeExport) {
        FrontendResult front = runFrontend("coro export int f() { return 0; }");
        EXPECT_FALSE(front.parseOk);
        EXPECT_NE(front.parseError.find("'coro' cannot precede 'export'"),
                  std::string::npos);
    }

    // -----------------------------------------------------------------------
    // 语义：yield 放置与互斥
    // -----------------------------------------------------------------------

    TEST(CoroSemanticTest, YieldOutsideCoroRejected) {
        FrontendResult front =
          runFrontend("int f() { yield 1; return 0; } int main() { return 0; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "'yield' is only allowed inside a coro function"));
    }

    TEST(CoroSemanticTest, YieldWithPendingDeferRejectedWithPosition) {
        // 同一作用域：defer 注册在 yield 之前 → 互斥（挂起会跳过 defer 执行点）
        FrontendResult front = runFrontend("int dummy() { return 0; }\n"
                                           "coro int f() {\n"
                                           "    defer dummy();\n"
                                           "    yield 1;\n"
                                           "    return 0;\n"
                                           "}\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        const Diagnostic* diagnostic =
          findDiagnostic(front.semantic,
                         "'yield' cannot appear in a scope with a pending defer");
        ASSERT_NE(diagnostic, nullptr);
        EXPECT_EQ(diagnostic->severity, DiagnosticSeverity::Error);
        // file:line:col 定位到 yield（第 4 行第 5 列，行/列均从 1 开始）
        EXPECT_EQ(diagnostic->file, "test.nc");
        EXPECT_EQ(diagnostic->line, 4);
        EXPECT_EQ(diagnostic->column, 5);
        EXPECT_NE(diagnostic->toString().find("test.nc:4:5: error:"), std::string::npos);
    }

    TEST(CoroSemanticTest, YieldWithPendingDeferInOuterScopeRejected) {
        // 外层作用域的 pending defer 同样互斥（挂起恢复后仍会跳过其执行点）
        FrontendResult front = runFrontend("int dummy() { return 0; }\n"
                                           "coro int f() {\n"
                                           "    defer dummy();\n"
                                           "    {\n"
                                           "        yield 1;\n"
                                           "    }\n"
                                           "    return 0;\n"
                                           "}\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(
          containsDiagnostic(front.semantic, DiagnosticSeverity::Error, "pending defer"));
    }

    TEST(CoroSemanticTest, DeferRegisteredAfterYieldIsLegal) {
        // yield 在前、defer 注册在后（同一作用域）：yield 时无 pending defer，
        // defer 在恢复后继续执行到块尾才触发——合法（对应 e2e 回归用例）
        FrontendResult front = runFrontend("int dummy() { return 0; }\n"
                                           "coro int f() {\n"
                                           "    {\n"
                                           "        yield 1;\n"
                                           "        defer dummy();\n"
                                           "    }\n"
                                           "    return 0;\n"
                                           "}\n"
                                           "int main() { return 0; }\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_FALSE(front.semantic.hasErrors());
        auto lowered = ir::lower(*front.program);
        EXPECT_TRUE(lowered.is_ok());
    }

    // -----------------------------------------------------------------------
    // 语义：coro 契约
    // -----------------------------------------------------------------------

    TEST(CoroSemanticTest, DirectCoroCallRejected) {
        FrontendResult front = runFrontend("coro int f() { yield 1; return 0; }\n"
                                           "int main() { int v = f(); return v; }\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(
          front.semantic,
          DiagnosticSeverity::Error,
          "coro function 'f' cannot be called directly; use coro_create"));
    }

    TEST(CoroSemanticTest, CoroCreateOnNonCoroRejected) {
        FrontendResult front =
          runFrontend("int f() { return 0; }\n"
                      "int main() { int h = coro_create(f); return h; }\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "'f' is not a coro function"));
    }

    TEST(CoroSemanticTest, CoroMustReturnInt) {
        FrontendResult front = runFrontend("coro void f() { yield 1; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "coro function 'f' must return int"));
    }

    TEST(CoroSemanticTest, CoroParameterMustBeScalar) {
        FrontendResult front =
          runFrontend("struct S { int x; };\n"
                      "coro int f(struct S s) { yield 1; return 0; }\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "coro parameter 's' has non-scalar type"));
    }

    TEST(CoroSemanticTest, CoroLocalMustBeScalar) {
        FrontendResult front =
          runFrontend("struct S { int x; };\n"
                      "coro int f() { struct S s; yield 1; return 0; }\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "coro local variable 's' has non-scalar type"));
    }

    TEST(CoroSemanticTest, CoroCreateArgumentCountChecked) {
        FrontendResult front =
          runFrontend("coro int f(int a) { yield a; return 0; }\n"
                      "int main() { int h = coro_create(f); return h; }\n");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "argument(s) for coro 'f'"));
    }

    TEST(CoroSemanticTest, BuiltinCoroOperationRedefinitionRejected) {
        FrontendResult front = runFrontend("int coro_create() { return 0; }");
        ASSERT_TRUE(front.parseOk) << front.parseError;
        EXPECT_TRUE(containsDiagnostic(front.semantic,
                                       DiagnosticSeverity::Error,
                                       "built-in coro operation"));
    }

    // -----------------------------------------------------------------------
    // IR 变换（结构断言）
    // -----------------------------------------------------------------------

    TEST(CoroIrTest, NoCoroNoInjection) {
        // 无 coro 的程序不注入任何 coro 运行时（零开销）
        ir::Module module = lowerValidSource("int main() { return 7; }");
        const std::string dump = module.dump();
        EXPECT_EQ(dump.find("__coro_"), std::string::npos);
    }

    TEST(CoroIrTest, FrameStorageAndRuntimeInjected) {
        ir::Module module = lowerValidSource(
          "coro int gen(int n) { yield n; return -1; }\n"
          "int main() { int h = coro_create(gen, 3); return coro_resume(h); }\n");
        const std::string dump = module.dump();
        // 帧 struct：状态/完成标志/返回值 + 提升的参数
        EXPECT_NE(dump.find("struct __coro_frame_gen"), std::string::npos);
        EXPECT_NE(dump.find("field int n"), std::string::npos);
        EXPECT_NE(dump.find("field int __state"), std::string::npos);
        EXPECT_NE(dump.find("field int __done"), std::string::npos);
        EXPECT_NE(dump.find("field int __retval"), std::string::npos);
        // 全局帧槽（16 实例）与 bump 计数器
        EXPECT_NE(dump.find("global struct __coro_frame_gen[16] __coro_frames_gen"),
                  std::string::npos);
        EXPECT_NE(dump.find("global int __coro_next_gen"), std::string::npos);
        // 注入的分发函数
        EXPECT_NE(dump.find("func int __coro_resume"), std::string::npos);
        EXPECT_NE(dump.find("func int __coro_done"), std::string::npos);
    }

    TEST(CoroIrTest, YieldDegradedToStateMachine) {
        ir::Module module = lowerValidSource(
          "coro int gen() { yield 7; return -1; }\n"
          "int main() { int h = coro_create(gen); return coro_resume(h); }\n");
        const std::string dump = module.dump();
        // 变换后 coro 函数体内不存在 IrYieldStmt（全部降解为状态存储 + return）
        EXPECT_EQ(dump.find("yield ("), std::string::npos);
        // 状态机骨架：fp = &frames[h] + while(1) 内按 __state 分发
        EXPECT_NE(dump.find("let struct __coro_frame_gen* __coro_fp"), std::string::npos);
        EXPECT_NE(dump.find("(eq int (member int (var struct __coro_frame_gen* "
                            "__coro_fp) ->__state) (const int 0))"),
                  std::string::npos);
        // done 块：置位 __done 并记录返回值
        EXPECT_NE(dump.find("->__done) = (const int 1)"), std::string::npos);
    }

    TEST(CoroIrTest, LocalsPromotedToFrameFields) {
        ir::Module module = lowerValidSource(
          "coro int gen(int n) {\n"
          "    for (int i = 0; i < n; i = i + 1) { yield i; }\n"
          "    return -1;\n"
          "}\n"
          "int main() { int h = coro_create(gen, 2); return coro_resume(h); }\n");
        const std::string dump = module.dump();
        // 跨 yield 局部 i 提升为帧字段，读写经 fp->i
        EXPECT_NE(dump.find("field int i"), std::string::npos);
        EXPECT_NE(dump.find(") ->i)"), std::string::npos);
        // 状态块内不再有 let int i 声明（已转换为帧 Store），也不存在改名字段
        EXPECT_EQ(dump.find("let int i "), std::string::npos);
        EXPECT_EQ(dump.find("->__i"), std::string::npos);
    }

    TEST(CoroIrTest, ResumeDispatchCoversAllCoros) {
        ir::Module module =
          lowerValidSource("coro int a() { yield 1; return 0; }\n"
                           "coro int b() { yield 2; return 0; }\n"
                           "int main() {\n"
                           "    int ha = coro_create(a);\n"
                           "    int hb = coro_create(b);\n"
                           "    return coro_resume(ha) + coro_resume(hb);\n"
                           "}\n");
        const std::string dump = module.dump();
        // 句柄 h = fnid*16 + slot：分发按 16 切区间
        EXPECT_NE(dump.find("(lt int (var int h) (const int 16))"), std::string::npos);
        EXPECT_NE(dump.find("(sub int (var int h) (const int 16))"), std::string::npos);
    }

    TEST(CoroIrTest, CoroCreateInlineExpansionInCaller) {
        ir::Module module = lowerValidSource(
          "coro int gen() { yield 7; return 0; }\n"
          "int main() { int h = coro_create(gen); return coro_resume(h); }\n");
        const std::string dump = module.dump();
        // coro_create 内联展开：取 bump 槽位 + 初始化 __state=0/__done=0
        // （初始化目标是帧数组元素——index 表达式，成员用点号形态）
        EXPECT_NE(dump.find("(var int __coro_next_gen)"), std::string::npos);
        EXPECT_NE(dump.find(".__state) = (const int 0)"), std::string::npos);
        EXPECT_NE(dump.find(".__done) = (const int 0)"), std::string::npos);
    }

    // -----------------------------------------------------------------------
    // VM（R0）端到端
    // -----------------------------------------------------------------------

    TEST(CoroVmTest, IteratorYieldSequence) {
        // gen(3) 产出 0,1,2 后返回 -1：got = 123；哨兵值校验挂在返回 9 上
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            coro int gen(int n) {
                for (int i = 0; i < n; i = i + 1) { yield i; }
                return -1;
            }
            int main() {
                int h = coro_create(gen, 3);
                int got = 0;
                int v = coro_resume(h);
                while (v >= 0) {
                    got = got * 10 + v + 1;
                    v = coro_resume(h);
                }
                if (v != -1) { return 9; }
                return got;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 123);
    }

    TEST(CoroVmTest, ResumeAfterDoneIdempotent) {
        // 完成后 resume 幂等返回 __retval（99），coro_done 置位
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            coro int f() { yield 7; return 99; }
            int main() {
                int h = coro_create(f);
                int a = coro_resume(h);
                int b = coro_resume(h);
                int c = coro_resume(h);
                int d = coro_resume(h);
                if (a != 7 || b != 99 || c != 99 || d != 99) { return 8; }
                if (coro_done(h) != 1) { return 9; }
                return 1;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 1);
    }

    TEST(CoroVmTest, StatementsAfterYieldRunOnResume) {
        // 回归（恢复点 = yield 后继序列入口而非 done 块）：yield 之后同块内
        // 还有语句（含 defer 注册）时，恢复后必须继续执行
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int dummyCalled = 0;
            int dummy() { dummyCalled = 1; return 0; }
            coro int f() {
                {
                    yield 7;
                    defer dummy();
                }
                return 0;
            }
            int main() {
                int h = coro_create(f);
                int v = coro_resume(h);
                int fin = coro_resume(h);
                if (v != 7) { return 10; }
                if (dummyCalled != 1) { return 11; }
                if (fin != 0) { return 12; }
                return 99;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 99);
    }

    TEST(CoroVmTest, YieldInsideWhileAndConditional) {
        // gen(4) 跳过 1：产出 0,2,3 后返回 -1；got = 0→1, 2→13, 3→134
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            coro int gen(int n) {
                int i = 0;
                while (i < n) {
                    if (i != 1) { yield i; }
                    i = i + 1;
                }
                return -1;
            }
            int main() {
                int h = coro_create(gen, 4);
                int got = 0;
                int count = 0;
                int v = coro_resume(h);
                while (v >= 0) {
                    got = got * 10 + v + 1;
                    count = count + 1;
                    v = coro_resume(h);
                }
                if (count != 3) { return 7; }
                return got;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 134);
    }

    TEST(CoroVmTest, BreakExitsToLoopRestNotDone) {
        // 回归（break 目标 = 循环出口而非 done 块）：break 后循环外的语句
        // （含 yield）必须继续可达；while 前缀（i 的初始化）不得随回边重跑
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            int g = 0;
            coro int f() {
                yield 5;
                int i = 0;
                while (i < 10) {
                    i = i + 1;
                    if (i == 3) { break; }
                }
                g = g + 100;
                yield g;
                return -1;
            }
            coro int g2() {
                for (int k = 0; k < 10; k = k + 1) {
                    if (k == 2) { break; }
                }
                yield 7;
                return -1;
            }
            int main() {
                int h = coro_create(f);
                int v = coro_resume(h);
                if (v != 5) { return 7; }
                v = coro_resume(h);
                if (v != 100) { return 8; }
                v = coro_resume(h);
                if (v != -1) { return 9; }
                int h2 = coro_create(g2);
                v = coro_resume(h2);
                if (v != 7) { return 10; }
                return 42;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 42);
    }

    TEST(CoroVmTest, TwoCoroutinesInterleave) {
        // 双协程各自独立推进：句柄独立、帧槽独立、完成后 retval 幂等
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            coro int letters() {
                yield 1;
                yield 2;
                yield 3;
                return 26;
            }
            coro int digits(int base) {
                yield base;
                yield base + 10;
                return base + 20;
            }
            int main() {
                int ha = coro_create(letters);
                int hb = coro_create(digits, 5);
                int a1 = coro_resume(ha);
                int b1 = coro_resume(hb);
                int a2 = coro_resume(ha);
                int b2 = coro_resume(hb);
                int a3 = coro_resume(ha);
                int b3 = coro_resume(hb);
                int a4 = coro_resume(ha);
                int b4 = coro_resume(hb);
                if (a1 != 1 || b1 != 5 || a2 != 2 || b2 != 15) { return 200; }
                if (a3 != 3 || b3 != 25 || a4 != 26 || b4 != 25) { return 201; }
                if (coro_done(ha) != 1 || coro_done(hb) != 1) { return 202; }
                return a1 + b1 + a2 + b2 + a3 - 3;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 23);
    }

    TEST(CoroVmTest, MultipleInstancesOfSameCoro) {
        // 同一 coro 三个实例交错推进：bump 分槽互不串扰
        // 各实例产出 (base+1, base+2, base+3)，三轮总和 33+36+39 = 108
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            coro int step(int base) {
                yield base + 1;
                yield base + 2;
                return base + 3;
            }
            int main() {
                int a = coro_create(step, 0);
                int b = coro_create(step, 10);
                int c = coro_create(step, 20);
                int total = 0;
                int i = 0;
                while (i < 3) {
                    total = total + coro_resume(a);
                    total = total + coro_resume(b);
                    total = total + coro_resume(c);
                    i = i + 1;
                }
                return total;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        EXPECT_EQ(result.r0, 108);
    }

    TEST(CoroVmTest, YieldValueIsFullExpression) {
        // 产出值为任意表达式（跨 yield 局部参与计算）
        nanoc_diff::VmResult result = nanoc_diff::runOnVm(R"nc(
            coro int fib() {
                int a = 0;
                int b = 1;
                while (a < 20) {
                    yield a;
                    int t = a + b;
                    a = b;
                    b = t;
                }
                return -1;
            }
            int main() {
                int h = coro_create(fib);
                int seq = 0;
                int v = coro_resume(h);
                while (v >= 0) {
                    seq = seq * 10 + (v / 10 + 1);
                    v = coro_resume(h);
                }
                if (v != -1) { return 9; }
                return seq;
            }
        )nc");
        ASSERT_TRUE(result.ok) << result.diagnostics;
        // fib 序列 0,1,1,2,3,5,8,13（a<20 时）；v/10+1 依次 1,1,1,1,1,1,1,2
        // → seq = 11111112（int32 内）
        EXPECT_EQ(result.r0, 11111112);
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

    TEST(CoroDiffTest, IteratorProgramConsistentAcrossBackends) {
        nanoc_diff::registerBuiltinBackends();
        nanoc_diff::DiffProgram program = makeProgram("feature/coro_iterator_diff",
                                                      "feature_coro_iterator_diff",
                                                      R"nc(coro int gen(int n) {
    for (int i = 0; i < n; i = i + 1) { yield i * i; }
    return -1;
}
int main() {
    int h = coro_create(gen, 5);
    int sum = 0;
    int v = coro_resume(h);
    while (v >= 0) {
        sum = sum + v;
        v = coro_resume(h);
    }
    return sum;
})nc",
                                                      30);

        std::vector<nanoc_diff::MatrixCell> cells = nanoc_diff::runRow(program);

        // VM 列必须可执行且命中锚点（防"一致地错"）
        bool vmPassed = false;
        for (const auto& cell : cells) {
            if (cell.backend == "vm") {
                EXPECT_EQ(cell.status, nanoc_diff::CellStatus::Pass) << cell.detail;
                if (cell.status == nanoc_diff::CellStatus::Pass) {
                    vmPassed = true;
                    EXPECT_EQ(cell.rawExit, 30);
                }
            }
        }
        ASSERT_TRUE(vmPassed);

        const std::string mismatch = nanoc_diff::rowMismatch(cells);
        EXPECT_TRUE(mismatch.empty()) << mismatch;
    }

    TEST(CoroDiffTest, TwoCoroProgramConsistentAcrossBackends) {
        nanoc_diff::registerBuiltinBackends();
        nanoc_diff::DiffProgram program = makeProgram("feature/coro_two_interleave_diff",
                                                      "feature_coro_two_interleave_diff",
                                                      R"nc(coro int letters() {
    yield 1;
    yield 2;
    return 10;
}
coro int digits(int base) {
    yield base;
    return base + 20;
}
int main() {
    int ha = coro_create(letters);
    int hb = coro_create(digits, 4);
    int r = coro_resume(ha) * 1000;
    r = r + coro_resume(hb) * 100;
    r = r + coro_resume(ha) * 10;
    r = r + coro_resume(hb);
    if (r != 1444) { return 200; }
    int a3 = coro_resume(ha);
    if (a3 != 10) { return 202; }
    if (coro_done(ha) != 1 || coro_done(hb) != 1) { return 201; }
    return 45;
})nc",
                                                      45);

        std::vector<nanoc_diff::MatrixCell> cells = nanoc_diff::runRow(program);
        for (const auto& cell : cells) {
            if (cell.backend == "vm") {
                EXPECT_EQ(cell.status, nanoc_diff::CellStatus::Pass) << cell.detail;
                EXPECT_EQ(cell.rawExit, 45);
            }
        }
        const std::string mismatch = nanoc_diff::rowMismatch(cells);
        EXPECT_TRUE(mismatch.empty()) << mismatch;
    }

} // namespace
