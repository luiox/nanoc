// 语言级 extern 声明（PRD R3 / M2）测试：
// - 词法：extern 关键字与 ...（ELLIPSIS）token
// - 解析：文件顶层 extern 函数声明（含 varargs）；体内 extern / 带函数体 /
//   ... 后跟命名参数 / export extern 均报 ParseError
// - 语义：extern 进符号表（isExtern 标记）；调用检查（非 varargs 严格个数、
//   varargs ≥ 命名参数数、类型照常）；extern 后同名定义冲突报错
// - IR：module.externs 收集（类型与 varargs 标记），dump 文本
// - NAS 发射：非 varargs → fastcall `extern name`；varargs → `.calling_convention
//   cdecl` + `extern name`、实参全部压栈 + `addi R4, 4*n` 清栈（规范 §4.2）
// - 真宿主 e2e（msvcrt.dll，#42 宿主库通路）：abs/atoi/strlen 返回值、puts
//   输出与返回值、exit 符号解析
// - C 后端：带签名原型（varargs `...`）与 clang 差分（同一程序 VM 与 C 运行
//   结果一致；printf 格式化输出到 stdout 精确断言）
//
// VM 侧 varargs 执行说明：nvm 宿主库白名单（#42）暂无 printf 签名包装器
// （PRD 将 VM varargs 列为 P2、随 R7 导入表落地），因此 printf 的运行期验收
// 走 C 后端（clang 真编译）+ 汇编级 cdecl 发射断言，不驱动 nvm 执行。
#include "nas/instruction.hpp"
#include "ncc/c_backend.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"
#include "nvm/core.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

    // -----------------------------------------------------------------------
    // 前端管线辅助
    // -----------------------------------------------------------------------

    std::vector<Token> lex(const std::string& source) {
        Lexer lexer(source);
        return lexer.tokenize();
    }

    std::unique_ptr<Program> parseSource(const std::string& source) {
        Parser parser(lex(source));
        return parser.parse();
    }

    // 解析并语义分析；analyze 返回 Err 视为测试失败
    SemanticResult analyzeSource(const std::string& source,
                                 const std::string& fileName = "test.nc") {
        Parser parser(lex(source));
        auto program = parser.parse();
        SemanticAnalyzer analyzer(fileName);
        auto analyzed = analyzer.analyze(*program);
        if (analyzed.is_err()) {
            ADD_FAILURE() << "analyze() returned Err: " << analyzed.unwrap_err();
            return SemanticResult{};
        }
        return analyzed.unwrap();
    }

    // 解析 + 语义（必须零错误）+ lower
    ir::Module lowerValidSource(const std::string& source) {
        Parser parser(lex(source));
        auto program = parser.parse();
        SemanticAnalyzer analyzer("test.nc");
        auto analyzed = analyzer.analyze(*program);
        if (analyzed.is_err() || analyzed.unwrap().hasErrors()) {
            ADD_FAILURE() << "source expected semantically valid";
            return ir::Module{};
        }
        auto lowered = ir::lower(*program);
        if (lowered.is_err()) {
            ADD_FAILURE() << "lower() returned Err: " << lowered.unwrap_err();
            return ir::Module{};
        }
        return std::move(lowered).unwrap();
    }

    // 解析 + 语义 + lower + NAS 发射
    std::string emitAssembly(const std::string& source) {
        ir::Module module = lowerValidSource(source);
        CodeGenerator codegen;
        return codegen.generate(module);
    }

    // 断言生成文本包含片段（失败时输出完整生成文本）
    void expectContains(const std::string& text, const std::string& fragment) {
        EXPECT_NE(text.find(fragment), std::string::npos) << "--- generated ---\n"
                                                          << text;
    }

    void expectNotContains(const std::string& text, const std::string& fragment) {
        EXPECT_EQ(text.find(fragment), std::string::npos) << "--- generated ---\n"
                                                          << text;
    }

#ifdef _WIN32

    // -----------------------------------------------------------------------
    // VM 真宿主 e2e（msvcrt.dll；与 tests/test_hostlib.cpp 同一加载通路）
    // -----------------------------------------------------------------------

    struct VmRunOutcome {
        bool ran = false;
        int32_t r0 = 0;
        std::string diagnostics;
    };

    // NanoC 源码 → 语义门禁 → IR → NAS 文本 → 汇编 → nvm 加载（--host-lib
    // msvcrt.dll 的进程内等价）→ 执行到停机，返回 R0
    VmRunOutcome runOnVm(const std::string& source) {
        VmRunOutcome outcome;
        const std::string nciPath = "extern_e2e.nci";
        try {
            Parser parser(lex(source));
            auto program = parser.parse();
            SemanticAnalyzer analyzer("extern_e2e.nc");
            auto analyzed = analyzer.analyze(*program);
            if (analyzed.is_err()) {
                outcome.diagnostics = "analyze Err: " + analyzed.unwrap_err();
                return outcome;
            }
            if (analyzed.unwrap().hasErrors()) {
                std::ostringstream buffer;
                for (const auto& diagnostic : analyzed.unwrap().diagnostics) {
                    buffer << diagnostic.toString() << "\n";
                }
                outcome.diagnostics = "semantic errors:\n" + buffer.str();
                return outcome;
            }

            ir::Module module = lowerValidSource(source);
            CodeGenerator codegen;
            const std::string assembly = codegen.generate(module);

            const AssemblyResult result = Assembler::assemble(assembly);
            if (!result.ok) {
                outcome.diagnostics =
                  "assemble failed line " + std::to_string(result.errorLine) + ": "
                  + result.errorMessage + "\n--- assembly ---\n" + assembly;
                return outcome;
            }

            std::ofstream ofs(nciPath, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(result.image.data()),
                      static_cast<std::streamsize>(result.image.size()));
            ofs.close();

            NVirtualMachine vm(8 * 1024 * 1024);
            vm.load(nciPath);
            if (!vm.loadHostLibrary("msvcrt.dll")) {
                outcome.diagnostics = "loadHostLibrary(msvcrt.dll) failed";
                return outcome;
            }
            vm.start();
            outcome.r0 = vm.getRegister(0);
            outcome.ran = true;
        } catch (const std::exception& e) {
            outcome.diagnostics = std::string("frontend/VM exception: ") + e.what();
        }
        std::remove(nciPath.c_str());
        return outcome;
    }

    // -----------------------------------------------------------------------
    // C 后端差分（clang 真编译；与 tests/test_c_backend.cpp 同策略）
    // -----------------------------------------------------------------------

    bool toolAvailable(const std::string& command) {
        const std::string logPath = "extern_probe.log";
        const int rc =
          std::system((command + " --version > " + logPath + " 2>&1").c_str());
        std::remove(logPath.c_str());
        return rc == 0;
    }

    const std::string& cCompilerCommand() {
        static const std::string cached = [] {
            if (const char* env = std::getenv("NANOC_C_COMPILER")) {
                if (toolAvailable(env)) {
                    return std::string(env);
                }
            }
            for (const char* candidate : { "clang", "gcc" }) {
                if (toolAvailable(candidate)) {
                    return std::string(candidate);
                }
            }
            return std::string();
        }();
        return cached;
    }

    struct CRunOutcome {
        bool compiled = false;
        int exitCode = 0;
        std::string stdoutText;
        std::string diagnostics;
    };

    // emit 文本落盘 → C 编译器编译 → 运行（stdout 重定向落盘后回读）→ 退出码
    CRunOutcome compileAndRunC(const std::string& cSource,
                               const std::string& base,
                               bool captureStdout = false) {
        CRunOutcome outcome;
        const std::string cPath = base + ".c";
        const std::string exePath = base + ".exe";
        const std::string logPath = base + ".log";
        const std::string outPath = base + ".out";

        {
            std::ofstream out(cPath, std::ios::binary);
            out << cSource;
        }
        const std::string compileCommand = cCompilerCommand() + " -O0 " + cPath + " -o "
                                           + exePath + " > " + logPath + " 2>&1";
        if (std::system(compileCommand.c_str()) != 0) {
            std::ifstream log(logPath, std::ios::binary);
            std::ostringstream buffer;
            buffer << log.rdbuf();
            outcome.diagnostics = "compile command: " + compileCommand
                                  + "\ncompiler log:\n" + buffer.str()
                                  + "\n--- generated C ---\n" + cSource;
            std::remove(cPath.c_str());
            std::remove(logPath.c_str());
            return outcome;
        }
        outcome.compiled = true;

        const std::string runCommand =
          captureStdout ? exePath + " > " + outPath + " 2>&1" : exePath;
        outcome.exitCode = std::system(runCommand.c_str());
        if (captureStdout) {
            std::ifstream out(outPath, std::ios::binary);
            std::ostringstream buffer;
            buffer << out.rdbuf();
            // Windows 文本模式 stdout 为 CRLF，断言前归一为 LF
            outcome.stdoutText = buffer.str();
            outcome.stdoutText.erase(
              std::remove(outcome.stdoutText.begin(), outcome.stdoutText.end(), '\r'),
              outcome.stdoutText.end());
        }

        std::remove(cPath.c_str());
        std::remove(exePath.c_str());
        std::remove(logPath.c_str());
        std::remove(outPath.c_str());
        return outcome;
    }

#endif // _WIN32

} // namespace

// ---------------------------------------------------------------------------
// 词法
// ---------------------------------------------------------------------------

TEST(LexerExternTest, ExternKeywordAndEllipsis) {
    const std::vector<Token> tokens = lex("extern int printf(char* fmt, ...);");
    std::vector<NTokenKind> kinds;
    for (const auto& token : tokens) {
        kinds.push_back(token.kind);
    }
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), NTokenKind::KEYWORD_EXTERN),
              kinds.end());
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), NTokenKind::ELLIPSIS), kinds.end());
}

TEST(LexerExternTest, SingleDotStillMemberAccess) {
    const std::vector<Token> tokens = lex("p.x");
    ASSERT_EQ(tokens.size(), 4u); // p . x + EOF
    EXPECT_EQ(tokens[0].kind, NTokenKind::IDENTIFIER);
    EXPECT_EQ(tokens[1].kind, NTokenKind::OPERATOR_DOT);
    EXPECT_EQ(tokens[1].value, ".");
    EXPECT_EQ(tokens[2].kind, NTokenKind::IDENTIFIER);
}

TEST(LexerExternTest, StringEscapePreservedForAssembler) {
    // 转义序列原样保留（\n 两字符），由汇编器解码——printf 格式串依赖
    const std::vector<Token> tokens = lex("\"n=%d\\n\"");
    ASSERT_EQ(tokens.size(), 2u); // 字符串 + EOF
    EXPECT_EQ(tokens[0].kind, NTokenKind::STRING_CONSTANT);
    EXPECT_EQ(tokens[0].value, "n=%d\\n");
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------

TEST(ParserExternTest, NonVariadicExternDeclaration) {
    auto program = parseSource("extern int puts(char* s);");
    ASSERT_EQ(program->declarations.size(), 1u);
    const auto* func =
      static_cast<const FuncDeclaration*>(program->declarations[0].get());
    EXPECT_TRUE(func->isExtern);
    EXPECT_FALSE(func->isVariadic);
    EXPECT_EQ(func->name, "puts");
    EXPECT_EQ(func->returnType, "int");
    ASSERT_EQ(func->parameters.size(), 1u);
    EXPECT_EQ(func->parameters[0]->type, "char");
    EXPECT_EQ(func->parameters[0]->pointerDepth, 1);
    EXPECT_EQ(func->parameters[0]->name, "s");
    EXPECT_EQ(func->body, nullptr);
}

TEST(ParserExternTest, VariadicExternDeclaration) {
    auto program = parseSource("extern int printf(char* fmt, ...);");
    ASSERT_EQ(program->declarations.size(), 1u);
    const auto* func =
      static_cast<const FuncDeclaration*>(program->declarations[0].get());
    EXPECT_TRUE(func->isExtern);
    EXPECT_TRUE(func->isVariadic);
    ASSERT_EQ(func->parameters.size(), 1u);
}

TEST(ParserExternTest, ExternVoidPointerReturn) {
    auto program = parseSource("extern void* malloc(int size);");
    ASSERT_EQ(program->declarations.size(), 1u);
    const auto* func =
      static_cast<const FuncDeclaration*>(program->declarations[0].get());
    EXPECT_TRUE(func->isExtern);
    EXPECT_EQ(func->returnType, "void");
    EXPECT_EQ(func->returnPointerDepth, 1);
}

TEST(ParserExternTest, ExternInFunctionBodyRejected) {
    Parser parser(lex("int main() { extern int puts(char* s); return 0; }"));
    EXPECT_THROW(parser.parse(), ParseError);
}

TEST(ParserExternTest, ExternWithBodyRejected) {
    Parser parser(lex("extern int f(int x) { return x; }"));
    EXPECT_THROW(parser.parse(), ParseError);
}

TEST(ParserExternTest, NamedParamAfterEllipsisRejected) {
    Parser parser(lex("extern int f(char* fmt, ..., int x);"));
    EXPECT_THROW(parser.parse(), ParseError);
}

TEST(ParserExternTest, ExportExternRejected) {
    Parser parser(lex("export extern int puts(char* s);"));
    EXPECT_THROW(parser.parse(), ParseError);
}

// ---------------------------------------------------------------------------
// 语义
// ---------------------------------------------------------------------------

TEST(SemanticExternTest, ExternSymbolsRegistered) {
    const SemanticResult result = analyzeSource(R"nc(
extern int puts(char* s);
extern int printf(char* fmt, ...);
int main() {
    puts("hello");
    printf("%d\n", 42);
    return 0;
}
)nc");
    EXPECT_FALSE(result.hasErrors());
    bool sawPuts = false;
    bool sawPrintf = false;
    for (const auto& symbol : result.globals) {
        if (symbol.name == "puts") {
            sawPuts = true;
            EXPECT_EQ(symbol.kind, SymbolKind::Function);
            EXPECT_TRUE(symbol.isExtern);
            ASSERT_EQ(symbol.paramTypes.len(), 1u);
            EXPECT_EQ(symbol.paramTypes[0], "char*");
        }
        if (symbol.name == "printf") {
            sawPrintf = true;
            EXPECT_TRUE(symbol.isExtern);
        }
    }
    EXPECT_TRUE(sawPuts);
    EXPECT_TRUE(sawPrintf);
}

TEST(SemanticExternTest, NonVariadicCallArityStrict) {
    const SemanticResult result = analyzeSource("extern int abs(int n);\n"
                                                "int main() { return abs(1, 2); }");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:2:24: error: function 'abs' expects 1 argument(s), but got 2");
}

TEST(SemanticExternTest, VariadicCallArityRelaxed) {
    const SemanticResult result = analyzeSource(R"nc(
extern int printf(char* fmt, ...);
int main() {
    printf("no extras");
    printf("%d %d %d", 1, 2, 3);
    return 0;
}
)nc");
    EXPECT_FALSE(result.hasErrors());
}

TEST(SemanticExternTest, VariadicCallTooFewNamedArgs) {
    const SemanticResult result = analyzeSource("extern int printf(char* fmt, ...);\n"
                                                "int main() { return printf(); }");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:2:27: error: function 'printf' expects at least 1 argument(s), "
              "but got 0");
}

TEST(SemanticExternTest, ExternArgumentTypeMismatch) {
    const SemanticResult result = analyzeSource(R"nc(
extern int abs(int n);
extern int puts(char* s);
int main() {
    int a = abs("str");
    puts(42);
    return a;
}
)nc");
    ASSERT_EQ(result.diagnostics.len(), 2u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:5:16: error: cannot convert 'char*' to 'int' in argument 1 of "
              "call to 'abs'");
    EXPECT_EQ(result.diagnostics[1].toString(),
              "test.nc:6:9: error: cannot convert 'int' to 'char*' in argument 1 of "
              "call to 'puts'");
}

TEST(SemanticExternTest, VariadicExtraArgMustBeScalarOrPointer) {
    const SemanticResult result = analyzeSource(R"nc(
struct Point {
    int x;
    int y;
};
extern int printf(char* fmt, ...);
int main() {
    struct Point p = { 1, 2 };
    printf("%p", p);
    return 0;
}
)nc");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:9:11: error: struct value passed as variadic argument 2 of call "
              "to 'printf' ('struct Point')");
}

TEST(SemanticExternTest, ExternThenDefinitionConflict) {
    const SemanticResult result = analyzeSource(R"nc(
extern int puts(char* s);
int puts(char* s) {
    return 0;
}
int main() {
    return puts("x");
}
)nc");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:3:1: error: redefinition of 'puts'");
}

TEST(SemanticExternTest, DefinitionThenExternConflict) {
    const SemanticResult result = analyzeSource(R"nc(
int puts(char* s) {
    return 0;
}
extern int puts(char* s);
int main() {
    return puts("x");
}
)nc");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:5:8: error: redefinition of 'puts'");
}

TEST(SemanticExternTest, DuplicateExternConflict) {
    const SemanticResult result = analyzeSource(R"nc(
extern int puts(char* s);
extern int puts(char* s);
int main() {
    return puts("x");
}
)nc");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:3:8: error: redefinition of 'puts'");
}

TEST(SemanticExternTest, UndeclaredExternalCallStillRejected) {
    const SemanticResult result = analyzeSource("int main() { return not_declared(1); }");
    ASSERT_EQ(result.diagnostics.len(), 1u);
    EXPECT_EQ(result.diagnostics[0].toString(),
              "test.nc:1:33: error: call to undeclared function 'not_declared'");
}

// ---------------------------------------------------------------------------
// IR
// ---------------------------------------------------------------------------

TEST(IrExternTest, LowerCollectsExternDeclarations) {
    const ir::Module module = lowerValidSource(R"nc(
extern int puts(char* s);
extern int printf(char* fmt, ...);
int main() {
    puts("hello");
    printf("%d\n", 42);
    return 0;
}
)nc");
    ASSERT_EQ(module.externs.size(), 2u);
    EXPECT_EQ(module.externs[0].name, "puts");
    EXPECT_FALSE(module.externs[0].isVariadic);
    ASSERT_EQ(module.externs[0].params.size(), 1u);
    EXPECT_EQ(module.externs[0].params[0].type.toString(), "char*");
    EXPECT_EQ(module.externs[1].name, "printf");
    EXPECT_TRUE(module.externs[1].isVariadic);
    // extern 声明不进入 functions（不发射定义）
    ASSERT_EQ(module.functions.size(), 1u);
    EXPECT_EQ(module.functions[0]->name, "main");
}

TEST(IrExternTest, DumpContainsExternLines) {
    const ir::Module module = lowerValidSource(R"nc(
extern int puts(char* s);
extern int printf(char* fmt, ...);
int main() {
    return 0;
}
)nc");
    expectContains(module.dump(), "  extern int puts(char* s);\n");
    expectContains(module.dump(), "  extern int printf(char* fmt, ...);\n");
}

// ---------------------------------------------------------------------------
// NAS 发射
// ---------------------------------------------------------------------------

TEST(CodegenExternTest, NonVariadicExternEmitsFastcall) {
    const std::string assembly = emitAssembly("extern int abs(int n);\n"
                                              "int main() { return abs(-42); }");
    expectContains(assembly, "extern abs\n");
    expectContains(assembly, "callx abs");
    // 非单字面量实参经求值压栈后弹入 R0（fastcall），无 cdecl 标记
    expectNotContains(assembly, ".calling_convention");
}

TEST(CodegenExternTest, VariadicExternEmitsCdecl) {
    const std::string assembly = emitAssembly(R"nc(
extern int printf(char* fmt, ...);
int main() {
    printf("%d %c %s", 1, 2, "x");
    return 0;
}
)nc");
    // varargs：`.calling_convention cdecl` 作用于该 extern 声明后恢复 fastcall
    expectContains(assembly,
                   ".calling_convention cdecl\n"
                   "extern printf\n"
                   ".calling_convention fastcall\n");
    expectContains(assembly, "callx printf");
    // 实参全部压栈，调用者清栈 4*n = 16
    expectContains(assembly, "addi R4, 16");
}

TEST(CodegenExternTest, MixedExternsAssembleCleanly) {
    const std::string assembly = emitAssembly(R"nc(
extern int puts(char* s);
extern int printf(char* fmt, ...);
extern void exit(int code);
int main() {
    printf("n=%d\n", 42);
    puts("done");
    return 0;
}
)nc");
    const AssemblyResult result = Assembler::assemble(assembly);
    EXPECT_TRUE(result.ok) << "line " << result.errorLine << ": " << result.errorMessage
                           << "\n--- assembly ---\n"
                           << assembly;
}

TEST(CodegenExternTest, ExternWithoutCallNotImported) {
    // 声明未调用：不产生导入 entry（不发射 extern 指令）
    const std::string assembly = emitAssembly("extern int puts(char* s);\n"
                                              "int main() { return 0; }");
    expectNotContains(assembly, "extern puts");
}

TEST(CodegenExternTest, ExternDeclaredReturnTypeDrivesCallType) {
    // strlen 声明返回 int：调用点类型 = int，可参与算术
    const std::string assembly = emitAssembly(R"nc(
extern int strlen(char* s);
int main() {
    return strlen("abcd") + 1;
}
)nc");
    const AssemblyResult result = Assembler::assemble(assembly);
    EXPECT_TRUE(result.ok) << "line " << result.errorLine << ": " << result.errorMessage
                           << "\n--- assembly ---\n"
                           << assembly;
}

// ---------------------------------------------------------------------------
// 真宿主 e2e（msvcrt.dll；非 Windows 跳过）
// ---------------------------------------------------------------------------

#ifdef _WIN32

// PRD R3 验收程序：puts 真实输出 "Hello R3"（msvcrt 与测试宿主分属不同 CRT，
// 输出捕获不可靠，断言返回值非负；实际输出经 CLI e2e 人工验证）
TEST(ExternE2ETest, MsvcrtPutsAcceptance) {
    const VmRunOutcome outcome = runOnVm("extern int puts(char* s);\n"
                                         "int main() { puts(\"Hello R3\"); return 0; }");
    ASSERT_TRUE(outcome.ran) << outcome.diagnostics;
    EXPECT_GE(outcome.r0, 0);
}

// 非 varargs 返回值：abs(-42)=42、atoi("-123")=-123、strlen("NanoC")=5，
// 校验和 42-123+5+200=124 经 main 返回值带出
TEST(ExternE2ETest, MsvcrtNonVarargsReturnValues) {
    const VmRunOutcome outcome = runOnVm(R"nc(
extern int abs(int n);
extern int atoi(char* s);
extern int strlen(char* s);
int main() {
    int a = abs(-42);
    int b = atoi("-123");
    int c = strlen("NanoC");
    if (a != 42 || b != -123 || c != 5) {
        return 1;
    }
    return a + b + c + 200;
}
)nc");
    ASSERT_TRUE(outcome.ran) << outcome.diagnostics;
    EXPECT_EQ(outcome.r0, 124);
}

// 统一内存字符串参数边界：puts 输出后返回值仍非负（vmCString 边界保护未误伤）
TEST(ExternE2ETest, MsvcrtPutsAfterComputation) {
    const VmRunOutcome outcome = runOnVm(R"nc(
extern int puts(char* s);
extern int abs(int n);
int main() {
    int guard = abs(-1);
    puts("R3 extern OK");
    return guard + 1;
}
)nc");
    ASSERT_TRUE(outcome.ran) << outcome.diagnostics;
    EXPECT_EQ(outcome.r0, 2);
}

// exit 符号经 msvcrt 解析并在白名单内（包装器可用）；不实际执行——wrap_exit
// 会以调用码终止测试进程，进程级退出码经 CLI e2e 验证（exit(7) → 退出码 7）
TEST(ExternE2ETest, MsvcrtExitResolvesWithoutRunning) {
    ir::Module module = lowerValidSource("extern void exit(int code);\n"
                                         "int main() { return 0; }");
    CodeGenerator codegen;
    const std::string assembly = codegen.generate(module);
    const AssemblyResult result = Assembler::assemble(assembly);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    const std::string nciPath = "extern_exit.nci";
    std::ofstream ofs(nciPath, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(result.image.data()),
              static_cast<std::streamsize>(result.image.size()));
    ofs.close();

    NVirtualMachine vm(8 * 1024 * 1024);
    vm.load(nciPath);
    std::remove(nciPath.c_str());
    // 未调用 exit：加载期解析命中白名单包装器即成功
    EXPECT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
}

// C 后端差分（clang）：同一 extern 程序 VM 后端与 C 后端运行结果一致
TEST(ExternE2ETest, CBackendDiffNonVarargs) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    const std::string source = R"nc(
extern int abs(int n);
extern int atoi(char* s);
extern int strlen(char* s);
extern int puts(char* s);
int main() {
    int a = abs(-42);
    int b = atoi("-123");
    int c = strlen("NanoC");
    if (a != 42 || b != -123 || c != 5) {
        puts("FAIL");
        return 1;
    }
    return a + b + c + 200;
}
)nc";

    const VmRunOutcome vm = runOnVm(source);
    ASSERT_TRUE(vm.ran) << vm.diagnostics;

    const ir::Module module = lowerValidSource(source);
    const CRunOutcome c = compileAndRunC(c_backend::emit(module), "extern_diff");
    ASSERT_TRUE(c.compiled) << c.diagnostics;
    EXPECT_EQ(c.exitCode, vm.r0) << "C exit code vs VM R0";
}

// C 后端 varargs：printf 格式化输出到 stdout 精确断言（VM 侧 printf 待 R7
// 宿主包装器，见文件头说明），退出码与源程序 return 一致
TEST(ExternE2ETest, CBackendPrintfVarargsStdout) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    const std::string source = R"nc(
extern int printf(char* fmt, ...);
extern void exit(int code);
int main() {
    printf("n=%d c=%c s=%s\n", 42, 65, "hi");
    exit(7);
    return 0;
}
)nc";
    const ir::Module module = lowerValidSource(source);
    const CRunOutcome c = compileAndRunC(c_backend::emit(module), "extern_printf", true);
    ASSERT_TRUE(c.compiled) << c.diagnostics;
    EXPECT_EQ(c.stdoutText, "n=42 c=A s=hi\n");
    EXPECT_EQ(c.exitCode, 7);
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// C 后端发射（带签名原型，无平台依赖）
// ---------------------------------------------------------------------------

TEST(CBackendExternTest, SignedPrototypesEmitted) {
    const ir::Module module = lowerValidSource(R"nc(
extern int puts(char* s);
extern int printf(char* fmt, ...);
extern void exit(int code);
int main() {
    puts("hello");
    return 0;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "extern int32_t puts(char* s);\n");
    expectContains(out, "extern int32_t printf(char* fmt, ...);\n");
    expectContains(out, "extern void exit(int32_t code);\n");
    expectNotContains(out, "extern int32_t puts();");
}

TEST(CBackendExternTest, UnresolvedExternalFallbackKept) {
    // 无 extern 声明的未解析外部（#37 兼容路径）仍发射无参原型
    Parser parser(lex("int main() { nc_put(\"abc\"); return 0; }"));
    auto program = parser.parse();
    auto lowered = ir::lower(*program);
    ASSERT_TRUE(lowered.is_ok());
    const std::string out = c_backend::emit(std::move(lowered).unwrap());
    expectContains(out, "extern int32_t nc_put();\n");
}
