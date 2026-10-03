#include "nas/instruction.hpp"
#include "ncc/c_backend.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"
#include "nvm/core.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// C 后端测试（PRD R4 / R13 差分思想第一次落地）：
// - dump 黄金式断言：代表性 ir::Module → C 文本片段（类型映射、布局断言、
//   控制流、短路、指针/成员左值、字符串池、extern 原型、函数前置原型）
// - 真编译运行验收：emit → 临时 .c → clang/gcc 编译 → 运行 → 断言退出码与
//   VM 后端（codegen → nas → nvm）一致；覆盖控制流/短路副作用、struct 值
//   语义（传参/返回/赋值）、指针、数组、全局变量、字符与字符串
// - 环境无 C 编译器时 GTEST_SKIP（可用 NANOC_C_COMPILER 指定编译器命令）
namespace {

    // 完整前端管线：lex → parse → semantic（必须零错误）→ lower
    ir::Module lowerValidSource(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        SemanticAnalyzer analyzer("test.nc");
        auto analyzed = analyzer.analyze(*program);
        if (analyzed.is_err()) {
            ADD_FAILURE() << "analyze() returned Err: " << analyzed.unwrap_err();
            return ir::Module{};
        }
        if (analyzed.unwrap().hasErrors()) {
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

    // 跳过语义门禁的降级（extern 负形场景专用：IR 尚无 extern 声明节点，
    // 未定义函数调用点类型为 Error，语义层会报；C 后端按未解析外部处理）
    ir::Module lowerUngated(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        auto lowered = ir::lower(*program);
        if (lowered.is_err()) {
            ADD_FAILURE() << "lower() returned Err: " << lowered.unwrap_err();
            return ir::Module{};
        }
        return std::move(lowered).unwrap();
    }

    // 断言生成文本包含片段（黄金片段式断言；失败时输出完整生成文本）
    void expectContains(const std::string& text, const std::string& fragment) {
        EXPECT_NE(text.find(fragment), std::string::npos) << "--- generated C ---\n"
                                                          << text;
    }

    void expectNotContains(const std::string& text, const std::string& fragment) {
        EXPECT_EQ(text.find(fragment), std::string::npos) << "--- generated C ---\n"
                                                          << text;
    }

    // -----------------------------------------------------------------------
    // C 编译器探测与运行
    // -----------------------------------------------------------------------

    bool toolAvailable(const std::string& command) {
        const std::string logPath = "ncc_cbe_probe.log";
        const int rc =
          std::system((command + " --version > " + logPath + " 2>&1").c_str());
        std::remove(logPath.c_str());
        return rc == 0;
    }

    // 编译器命令（进程内缓存）：环境变量 NANOC_C_COMPILER 优先，其后 clang/gcc
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

    // emit 文本落盘 → C 编译器编译 → 运行 → 退出码（Windows cmd 经 system()
    // 透传程序退出码）；编译失败时 diagnostics 携带编译日志与生成文本
    struct CRunOutcome {
        bool compiled = false;
        int exitCode = 0;
        std::string diagnostics;
    };

    CRunOutcome compileAndRunC(const std::string& cSource, const std::string& base) {
        CRunOutcome outcome;
        const std::string cPath = base + ".c";
        const std::string exePath = base + ".exe";
        const std::string logPath = base + ".log";

        {
            std::ofstream out(cPath, std::ios::binary);
            out << cSource;
        }
        const std::string compileCommand = cCompilerCommand() + " -O0 " + cPath + " -o "
                                           + exePath + " > " + logPath + " 2>&1";
        const int compileRc = std::system(compileCommand.c_str());
        if (compileRc != 0) {
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
        outcome.exitCode = std::system(exePath.c_str());

        std::remove(cPath.c_str());
        std::remove(exePath.c_str());
        std::remove(logPath.c_str());
        return outcome;
    }

    // -----------------------------------------------------------------------
    // VM 后端执行（与 tests/test_golden_e2e.cpp 同一链路：codegen → nas → nvm）
    // -----------------------------------------------------------------------

    bool runOnVm(const std::string& source, int32_t& r0, std::string& diagnostics) {
        const std::string nciPath = "cbe_diff.nci";
        try {
            Lexer lexer(source);
            std::vector<Token> tokens = lexer.tokenize();

            Parser parser(tokens);
            auto program = parser.parse();

            CodeGenerator codegen;
            const std::string assembly = codegen.generate(*program);

            const AssemblyResult result = Assembler::assemble(assembly);
            if (!result.ok) {
                diagnostics = "assemble failed line " + std::to_string(result.errorLine)
                              + ": " + result.errorMessage;
                return false;
            }

            std::ofstream ofs(nciPath, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(result.image.data()),
                      static_cast<std::streamsize>(result.image.size()));
            ofs.close();

            NVirtualMachine vm(8 * 1024 * 1024);
            vm.load(nciPath);
            vm.start();
            r0 = vm.getRegister(0);
        } catch (const std::exception& e) {
            diagnostics = std::string("frontend/VM exception: ") + e.what();
            return false;
        }
        std::remove(nciPath.c_str());
        return true;
    }

    // NanoC 源码 → C 后端 → 编译运行；失败时 diagnostics 携带生成文本
    bool runOnCBackend(const std::string& source,
                       const std::string& base,
                       int& exitCode,
                       std::string& diagnostics) {
        const ir::Module module = lowerValidSource(source);
        const std::string cSource = c_backend::emit(module);
        const CRunOutcome outcome = compileAndRunC(cSource, base);
        if (!outcome.compiled) {
            diagnostics = outcome.diagnostics;
            return false;
        }
        exitCode = outcome.exitCode;
        return true;
    }

    // 差分断言：同一 NanoC 程序在 VM 后端与 C 后端的可观测结果一致
    void expectBackendsAgree(const std::string& source,
                             const std::string& base,
                             const std::string& label) {
        int32_t vmR0 = 0;
        std::string vmDiag;
        ASSERT_TRUE(runOnVm(source, vmR0, vmDiag)) << label << ": VM side: " << vmDiag;

        int cExit = 0;
        std::string cDiag;
        ASSERT_TRUE(runOnCBackend(source, base, cExit, cDiag)) << label << ": C side:\n"
                                                               << cDiag;

        EXPECT_EQ(cExit, vmR0) << label << ": C exit code vs VM R0";
    }

    // 读取 examples/ 下的示例程序（工作目录探测，同 test_ir.cpp 策略）
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
        ADD_FAILURE() << "example not found: " << name;
        return "";
    }

} // namespace

// ---------------------------------------------------------------------------
// dump 黄金式断言
// ---------------------------------------------------------------------------

TEST(CBackendTest, EmptyMainGolden) {
    const ir::Module module = lowerValidSource("int main() { return 0; }");
    const std::string out = c_backend::emit(module);
    EXPECT_EQ(out,
              "// Generated by ncc C backend (NanoC PRD R4). Do not edit.\n"
              "// Traceability: each `// struct:` / `// global:` / `// func:`\n"
              "// comment echoes the source ir::Module entity (NanoC type\n"
              "// spelling, same as `ir::Module::dump()`).\n"
              "#include <stdint.h>\n"
              "#include <stddef.h>\n"
              "\n"
              "int32_t main(void);\n"
              "\n"
              "// func: int main()\n"
              "int32_t main(void) {\n"
              "    return 0;\n"
              "}\n");
}

TEST(CBackendTest, TypeMapping) {
    const ir::Module module = lowerValidSource(R"nc(
struct Point {
    int x;
    int y;
};
int main() {
    int x = 42;
    char c = 'A';
    int* p = &x;
    struct Point pt = { 1, 2 };
    pt.x = *p + c;
    p = NULL;
    return x;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "#include <stdint.h>\n#include <stddef.h>\n");
    expectContains(out, "int32_t x = 42;\n");
    expectContains(out, "char c = 'A';\n");
    expectContains(out, "int32_t* p = &x;\n");
    expectContains(out, "struct Point pt = { 1, 2 };\n");
    expectContains(out, "pt.x = *p + c;\n");
    expectContains(out, "p = NULL;\n");
    expectNotContains(out, "int8_t");
    expectNotContains(out, "#include <string.h>");
}

TEST(CBackendTest, StructLayoutAssertsForReproducibleLayout) {
    const ir::Module module = lowerValidSource(R"nc(
struct Point {
    int x;
    int y;
};
struct Triple {
    int a;
    int b;
    int c;
};
int main() {
    struct Point p = { 1, 2 };
    return p.x + p.y;
}
)nc");
    const std::string out = c_backend::emit(module);
    // 全 int 成员：NanoC 4 字节对齐无填充布局在 C 侧逐一钉死
    expectContains(
      out,
      "struct Point {\n"
      "    int32_t x;\n"
      "    int32_t y;\n"
      "};\n"
      "_Static_assert(offsetof(struct Point, x) == 0, \"NanoC struct layout\");\n"
      "_Static_assert(offsetof(struct Point, y) == 4, \"NanoC struct layout\");\n"
      "_Static_assert(sizeof(struct Point) == 8, \"NanoC struct layout\");\n");
    expectContains(
      out,
      "_Static_assert(offsetof(struct Triple, c) == 8, \"NanoC struct layout\");\n");
}

TEST(CBackendTest, StructLayoutNoteWhenNotReproducible) {
    const ir::Module module = lowerValidSource(R"nc(
struct Node {
    struct Node* next;
    int value;
};
int main() {
    struct Node n;
    n.next = NULL;
    n.value = 5;
    return n.value;
}
)nc");
    const std::string out = c_backend::emit(module);
    // 指针成员 8 字节：mandated 映射下无法复现 VM 字节布局 → 布局注释而非断言
    expectContains(out, "struct Node* next;\n    int32_t value;\n};\n");
    expectContains(out, "// layout note");
    expectNotContains(out, "_Static_assert(offsetof(struct Node");
}

TEST(CBackendTest, ControlFlowStructured) {
    const ir::Module module = lowerValidSource(R"nc(
int classify(int v) {
    if (v < 0) {
        return 0 - 1;
    } else if (v == 0) {
        return 0;
    } else {
        return 1;
    }
}
int main() {
    int x = 10;
    if (x > 5) x = 1; else x = 0;
    while (x < 5) x = x + 1;
    for (int i = 0; i < 3; i = i + 1) {
        x = x + i;
    }
    return classify(x);
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out,
                   "int32_t classify(int32_t v) {\n"
                   "    if (v < 0) {\n"
                   "        return 0 - 1;\n"
                   "    } else if (v == 0) {\n"
                   "        return 0;\n"
                   "    } else {\n"
                   "        return 1;\n"
                   "    }\n"
                   "}\n");
    expectContains(out,
                   "    if (x > 5) {\n"
                   "        x = 1;\n"
                   "    } else {\n"
                   "        x = 0;\n"
                   "    }\n");
    expectContains(out,
                   "    while (x < 5) {\n"
                   "        x = x + 1;\n"
                   "    }\n");
    expectContains(out,
                   "    for (int32_t i = 0; i < 3; i = (i + 1)) {\n"
                   "        x = x + i;\n"
                   "    }\n");
    // 单语句分支也加大括号（块语句结构化；C17 中分支体内声明必须成块）
    expectNotContains(out, "if (x > 5) x = 1;");
}

TEST(CBackendTest, ShortCircuitMappedNatively) {
    const ir::Module module = lowerValidSource(R"nc(
int main() {
    int a = 1;
    int b = 0;
    int c = a && b || !a;
    int d = -a;
    a = b = 3;
    return c + d + a;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "int32_t c = (a && b) || (!a);\n");
    expectContains(out, "int32_t d = -a;\n");
    expectContains(out, "a = b = 3;\n");
}

TEST(CBackendTest, PointerAndLvalueForms) {
    const ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 42;
    int* p = &x;
    *p = 7;
    int y = *p + 1;
    int* q = NULL;
    p = p + 1;
    int z = *(p - 1);
    return y + z;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "*p = 7;\n");
    expectContains(out, "int32_t y = *p + 1;\n");
    expectContains(out, "int32_t* q = NULL;\n");
    expectContains(out, "p = p + 1;\n");
    expectContains(out, "int32_t z = *(p - 1);\n");
}

TEST(CBackendTest, MemberAccessAndStructPointers) {
    const ir::Module module = lowerValidSource(R"nc(
struct Node {
    struct Node* next;
    int value;
};
int main() {
    struct Node n;
    struct Node m;
    n.value = 5;
    m.value = 6;
    n.next = &m;
    struct Node* np = &n;
    np->value = 1;
    int a = (*np).value;
    int b = np->next->value;
    return a + b + n.value;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "n.next = &m;\n");
    expectContains(out, "struct Node* np = &n;\n");
    expectContains(out, "np->value = 1;\n");
    expectContains(out, "int32_t a = (*np).value;\n");
    expectContains(out, "int32_t b = np->next->value;\n");
}

TEST(CBackendTest, GlobalsInitListAndStringPool) {
    const ir::Module module = lowerValidSource(R"nc(
struct Pair {
    int a;
    int b;
};
int g_count = 5;
int g_zero;
struct Pair g_pair = { 7, 8 };
int use(char* s) {
    return 0;
}
int main() {
    g_zero = g_count;
    use("hello");
    use("hello");
    return g_zero;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "int32_t g_count = 5;\n");
    expectContains(out, "int32_t g_zero;\n");
    expectContains(out, "struct Pair g_pair = { 7, 8 };\n");
    // 字符串去重池：同一字面量只落池一次，引用点复用池名
    expectContains(out, "static const char NcStr0[] = \"hello\";\n");
    expectContains(out, "use(NcStr0);\n");
    expectNotContains(out, "NcStr1");
    expectContains(out, "int32_t use(char* s) {\n");
}

TEST(CBackendTest, FunctionPrototypesForMutualRecursion) {
    const ir::Module module = lowerValidSource(R"nc(
int isEven(int n) {
    if (n == 0) {
        return 1;
    }
    return isOdd(n - 1);
}
int isOdd(int n) {
    if (n == 0) {
        return 0;
    }
    return isEven(n - 1);
}
int main() {
    return isEven(4);
}
)nc");
    const std::string out = c_backend::emit(module);
    // 前置原型：任意声明序调用（相互递归）在 C 侧同样可用
    expectContains(out,
                   "int32_t isEven(int32_t n);\n"
                   "int32_t isOdd(int32_t n);\n"
                   "int32_t main(void);\n");
    expectContains(out, "return isOdd(n - 1);\n");
}

TEST(CBackendTest, ExternPrototypesForUnresolvedCalls) {
    // IR 尚无 extern 声明（R3 未落地）：未定义函数的调用点按未解析外部处理，
    // 发射无参 C 原型（本用例按 R3 前的 IR 现状跳过语义门禁降级）
    const ir::Module module = lowerUngated(R"nc(
int main() {
    nc_put("abc");
    return nc_add(3, 4);
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "extern int32_t nc_put();\n");
    expectContains(out, "extern int32_t nc_add();\n");
    expectContains(out, "nc_put(NcStr0);\n");
    expectContains(out, "return nc_add(3, 4);\n");
}

TEST(CBackendTest, TypedefsEmitted) {
    const ir::Module module = lowerValidSource(R"nc(
struct Point {
    int x;
    int y;
};
typedef struct Point PointT;
typedef int MyInt;
int main() {
    PointT p = { 1, 2 };
    MyInt m = p.x;
    return m + p.y;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out, "typedef struct Point PointT;\n");
    expectContains(out, "typedef int32_t MyInt;\n");
}

TEST(CBackendTest, SingleStatementBodiesGetBraces) {
    const ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 1;
    if (x > 0) int y = 3;
    while (x > 0) x = x - 1;
    for (int i = 0; i < 2; i = i + 1) int z = i;
    return x;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out,
                   "    if (x > 0) {\n"
                   "        int32_t y = 3;\n"
                   "    }\n");
    expectContains(out,
                   "    while (x > 0) {\n"
                   "        x = x - 1;\n"
                   "    }\n");
    expectContains(out,
                   "    for (int32_t i = 0; i < 2; i = (i + 1)) {\n"
                   "        int32_t z = i;\n"
                   "    }\n");
}

TEST(CBackendTest, NestedBlockStatements) {
    const ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 1;
    {
        int x = 2;
        x = x + 1;
    }
    return x;
}
)nc");
    const std::string out = c_backend::emit(module);
    expectContains(out,
                   "    {\n"
                   "        int32_t x = 2;\n"
                   "        x = x + 1;\n"
                   "    }\n");
}

// ---------------------------------------------------------------------------
// 真编译运行验收（R13 差分：C 后端 vs VM 后端，退出码 == R0）
// ---------------------------------------------------------------------------

TEST(CBackendDiffTest, ControlFlowShortCircuitAndRecursion) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    expectBackendsAgree(
      R"nc(int g_calls = 0;

int side(int v) {
    g_calls = g_calls + 1;
    return v;
}

int isEven(int n) {
    if (n == 0) {
        return 1;
    }
    return isOdd(n - 1);
}

int isOdd(int n) {
    if (n == 0) {
        return 0;
    }
    return isEven(n - 1);
}

int main() {
    int acc = 0;
    int i = 0;
    while (i < 5) {
        if (i % 2 == 0) {
            acc = acc + i * 2;
        } else {
            acc = acc - 1;
        }
        i = i + 1;
    }
    for (int j = 0; j < 3; j = j + 1) {
        acc = acc + j;
    }
    // 短路：被跳过的一侧不产生副作用（g_calls 不变），命中的一侧 +1
    int before = g_calls;
    int t1 = 0 && side(1);
    int afterZeroAnd = g_calls;
    int t2 = 1 || side(1);
    int afterTrueOr = g_calls;
    int t3 = 1 && side(1);
    int afterTrueAnd = g_calls;
    if (afterZeroAnd != before || afterTrueOr != before
        || afterTrueAnd != before + 1) {
        return 200;
    }
    if (isEven(10) != 1 || isOdd(7) != 1) {
        return 201;
    }
    if (acc != 13) {
        return 202;
    }
    return acc + g_calls;
}
)nc",
      "cbe_diff_ctrl",
      "control flow / short-circuit / mutual recursion");
}

TEST(CBackendDiffTest, StructPointersAndArrays) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    expectBackendsAgree(
      R"nc(struct Point {
    int x;
    int y;
};

struct Node {
    struct Node* next;
    int value;
};

struct Point addPoints(struct Point a, struct Point b) {
    struct Point r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    return r;
}

void swap(int* a, int* b) {
    int t = *a;
    *a = *b;
    *b = t;
}

int sumArray(int* xs, int n) {
    int s = 0;
    int i = 0;
    while (i < n) {
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

int main() {
    struct Point p = { 3, 4 };
    struct Point q = { 10, 20 };
    struct Point r = addPoints(p, q);
    int total = r.x + r.y;

    int a = 5;
    int b = 9;
    swap(&a, &b);
    if (a != 9 || b != 5) {
        return 210;
    }

    int arr[5];
    int i = 0;
    while (i < 5) {
        arr[i] = i * i;
        i = i + 1;
    }
    total = total + sumArray(arr, 5);

    struct Node n1;
    struct Node n2;
    n1.value = 100;
    n2.value = 11;
    n1.next = &n2;
    n2.next = NULL;
    struct Node* it = &n1;
    while (it != NULL) {
        total = total + it->value;
        it = it->next;
    }

    struct Point copy = p;
    total = total + copy.y;

    struct Point* pp = &r;
    pp->x = pp->x + 1;
    total = total + r.x;

    return total;
}
)nc",
      "cbe_diff_struct",
      "struct value semantics / pointers / arrays");
}

TEST(CBackendDiffTest, GlobalsCharsAndStrings) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    expectBackendsAgree(
      R"nc(struct Pair {
    int a;
    int b;
};

typedef struct Pair PairT;

int g_base = 100;
int g_zero;
struct Pair g_pair = { 7, 8 };
char g_letter = 'A';

int useString(char* s) {
    return 3;
}

int main() {
    PairT local = { 1, 2 };
    g_zero = g_base + g_pair.a + g_pair.b;
    char buf[8];
    buf[0] = 'x';
    buf[1] = 'y';
    int acc = g_zero + local.a + local.b;
    acc = acc + (g_letter == 'A');
    acc = acc + buf[0] - 'x';
    acc = acc + useString("hello world");
    char* msg = "nano";
    acc = acc + useString(msg);
    return g_zero + acc - 100;
}
)nc",
      "cbe_diff_globals",
      "globals / chars / string pool / typedef");
}

TEST(CBackendDiffTest, ExamplesSuite) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    const std::vector<std::string> examples = { "hello.nc",
                                                "arithmetic.nc",
                                                "control_flow.nc",
                                                "functions.nc",
                                                "loop.nc" };
    for (const auto& name : examples) {
        const std::string source = readExample(name);
        if (source.empty()) {
            continue;
        }
        expectBackendsAgree(source, "cbe_diff_example_" + name, name);
    }
}

TEST(CBackendDiffTest, ExternCallSmoke) {
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler (clang/gcc) available on PATH";
    }
    // put 型场景（PRD R4 验收口径）：未解析外部调用发射无参原型，与宿主侧
    // 追加的定义链接运行；副作用经返回值带出：nc_put 计数 +1（丢弃），nc_add
    // 计数 +10 并返回累计值 11
    const ir::Module module = lowerUngated(R"nc(
int main() {
    nc_put("abc");
    return nc_add(3, 4);
}
)nc");
    std::string cSource = c_backend::emit(module);
    cSource += "\n"
               "// appended host-side definitions for the extern smoke test\n"
               "static int32_t ncCounter = 0;\n"
               "int32_t nc_put() {\n"
               "    ncCounter = ncCounter + 1;\n"
               "    return ncCounter;\n"
               "}\n"
               "int32_t nc_add() {\n"
               "    ncCounter = ncCounter + 10;\n"
               "    return ncCounter;\n"
               "}\n";
    const CRunOutcome outcome = compileAndRunC(cSource, "cbe_diff_extern");
    ASSERT_TRUE(outcome.compiled) << outcome.diagnostics;
    EXPECT_EQ(outcome.exitCode, 11);
}
