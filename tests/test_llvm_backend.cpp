#include "support/diff_harness.hpp"

#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/llvm_backend.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <string>

// LLVM 后端测试（PRD R6 / M3）：
// - .ll 黄金片段断言：代表性 ir::Module → LLVM 文本 IR 片段（类型映射
//   int→i32 / char→i8、opaque ptr、struct gep 与布局注释、短路块分裂 phi、
//   extern declare 与 varargs、字符串池 \XX 转义、常量/延迟全局初始化、
//   未解析外部 varargs declare）
// - 真编译运行验收（探测到 llc + 链接器时）：emit → llc → lld-link/clang →
//   运行 → 退出码与 VM 后端（codegen → nas → nvm）一致；覆盖控制流/短路、
//   struct 值语义、指针/数组、char、递归、全局初始化；extern puts 场景 VM
//   无宿主调用，单独按锚点断言
// - 无 LLVM 工具链时 e2e 组 GTEST_SKIP（探测：NANOC_LLVM_DIR > PATH llc >
//   本机 SDK 路径，见 llvm_backend.hpp）
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

    // 跳过语义门禁的降级（未解析外部负形场景专用，#37 兼容路径）
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

    // 断言生成文本包含片段（黄金片段式；失败时输出完整生成文本）
    void expectContains(const std::string& text, const std::string& fragment) {
        EXPECT_NE(text.find(fragment), std::string::npos)
          << "--- missing fragment: " << fragment << "\n--- generated LLVM IR ---\n"
          << text;
    }

    void expectNotContains(const std::string& text, const std::string& fragment) {
        EXPECT_EQ(text.find(fragment), std::string::npos)
          << "--- unexpected fragment: " << fragment << "\n--- generated LLVM IR ---\n"
          << text;
    }

    // LLVM 工具链可用（llc + 链接器）；不可用时跳过真编译组
    bool llvmReady() {
        const llvm_backend::Toolchain& tc = llvm_backend::toolchain();
        return !tc.llc.empty() && !tc.linker.empty();
    }

    // NanoC 源码 → LLVM 后端 → llc → lld-link/clang → 运行 → 退出码
    bool runOnLlvm(const std::string& source, int& exitCode, std::string& diagnostics) {
        const std::string exePath = "llvm_be_run.exe";
        std::remove(exePath.c_str()); // 清理上次残留，防误跑旧 exe
        const std::string llText = llvm_backend::emit(lowerValidSource(source));
        if (!llvm_backend::compileToExe(llText, exePath, diagnostics)) {
            return false;
        }
        exitCode = std::system(exePath.c_str());
        std::remove(exePath.c_str());
        return true;
    }

    // 差分断言：LLVM 后端退出码 == VM 后端 R0（低 8 位归一化口径）
    void expectLlvmAgreesWithVm(const std::string& source,
                                int32_t expectedR0,
                                const std::string& label) {
        const nanoc_diff::VmResult vm = nanoc_diff::runOnVm(source);
        ASSERT_TRUE(vm.ok) << label << ": VM side: " << vm.diagnostics;
        ASSERT_EQ(vm.r0, expectedR0) << label << ": VM R0 anchor（防两侧一致地错）";

        if (!llvmReady()) {
            GTEST_SKIP() << label << ": 无 LLVM 工具链（llc/链接器），跳过真编译";
        }
        int exitCode = 0;
        std::string diagnostics;
        ASSERT_TRUE(runOnLlvm(source, exitCode, diagnostics)) << label << ": LLVM side:\n"
                                                              << diagnostics;
        EXPECT_EQ(exitCode & 0xFF, expectedR0 & 0xFF)
          << label << ": LLVM exit code vs VM R0";
    }

} // namespace

// ---------------------------------------------------------------------------
// 工具链探测
// ---------------------------------------------------------------------------

TEST(LlvmToolchainTest, ProbeReport) {
    const llvm_backend::Toolchain& tc = llvm_backend::toolchain();
    spdlog::info("LLVM toolchain: llc='{}', linker='{}', source='{}'",
                 tc.llc,
                 tc.linker,
                 tc.source);
    if (tc.llc.empty()) {
        SUCCEED() << "无 llc（探测 NANOC_LLVM_DIR > PATH > SDK 路径），"
                     "真编译组将由各自用例 skip";
        return;
    }
    EXPECT_FALSE(tc.source.empty()); // 有工具必有来源描述（日志/报错用）
    SUCCEED() << "using llc: " << tc.llc << " (" << tc.source
              << "), linker: " << tc.linker;
}

// ---------------------------------------------------------------------------
// .ll 黄金式断言
// ---------------------------------------------------------------------------

TEST(LlvmBackendTest, EmptyMainGolden) {
    const ir::Module module = lowerValidSource("int main() { return 0; }");
    const std::string out = llvm_backend::emit(module);
    EXPECT_EQ(out,
              "; Generated by ncc LLVM backend (NanoC PRD R6). Do not edit.\n"
              "; Type mapping: int -> i32, char -> i8 (signed, C char "
              "semantics),\n"
              "; T* -> ptr (opaque pointers, LLVM 15+), struct -> %struct.Tag.\n"
              "; No target triple: llc applies its host default.\n"
              "\n"
              "; func: int main()\n"
              "define i32 @main() {\n"
              "entry:\n"
              "    ret i32 0\n"
              "}\n");
}

TEST(LlvmBackendTest, TypeMapping) {
    const ir::Module module = lowerValidSource(R"nc(
char next(char c) {
    char r = c;
    return r;
}

int main() {
    char c = 'A';
    int arr[3];
    arr[0] = c;
    int* p = &arr[0];
    return next(c) - 'A' + arr[0] + *p;
}
)nc");
    const std::string out = llvm_backend::emit(module);
    // int → i32、char → i8（含参数与返回值）；char 常量按有符号提升后直写
    expectContains(out, "define i8 @next(i8 %c)");
    expectContains(out, "%c.addr = alloca i8");
    expectContains(out, "store i8 65, ptr %c");
    expectContains(out, "%arr = alloca [3 x i32]");
    expectContains(out, "%p = alloca ptr");
    // char 读值即 sext 提升（C 整型提升口径）
    expectContains(out, "sext i8 %t");
    expectContains(out, "getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 0");
    expectContains(out, "call i8 @next(i8 %t");
    // opaque pointer 风格：不出现 typed pointer 拼写
    expectNotContains(out, "i32*");
    expectNotContains(out, "i8**");
}

TEST(LlvmBackendTest, StructGEPAndLayoutComment) {
    const ir::Module module = lowerValidSource(R"nc(
struct Point {
    int x;
    int y;
};

int main() {
    struct Point p = { 3, 4 };
    p.y = p.x + 1;
    struct Point* q = &p;
    q->x = q->y * 2;
    return p.x + p.y;
}
)nc");
    const std::string out = llvm_backend::emit(module);
    expectContains(out, "%struct.Point = type { i32, i32 }");
    // 全 int 成员：自然布局与 NanoC 4 字节无填充规则一致，注释钉死偏移
    expectContains(out,
                   "; layout: member offsets match the NanoC rule (0, 4); sizeof = 8");
    expectContains(out, "%p = alloca %struct.Point");
    expectContains(out, "getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 1");
    // p->x 经指针 load 后 gep（与 (*p).x 等价）
    expectContains(out, "getelementptr inbounds %struct.Point, ptr %t");
}

TEST(LlvmBackendTest, StructLayoutNoteWhenNotReproducible) {
    // 指针成员（自然布局 8 字节 vs NanoC 全成员 4 字节）使布局不可复现
    // （与 C 后端 StructLayoutNoteWhenNotReproducible 同口径；char 后随
    // int 的场景两侧偏移恰好一致，不触发 note）
    const ir::Module module = lowerValidSource(R"nc(
struct Node {
    int value;
    struct Node* next;
};

int main() {
    struct Node a;
    struct Node b;
    a.value = 1;
    a.next = &b;
    b.value = 2;
    b.next = NULL;
    return a.value + a.next->value;
}
)nc");
    const std::string out = llvm_backend::emit(module);
    expectContains(out, "%struct.Node = type { i32, ptr }");
    expectContains(out,
                   "; layout note: natural LLVM layout diverges from the "
                   "NanoC 4-byte");
    expectNotContains(out, "; layout: member offsets match");
    // 指针成员 gep 与解引用链（a.next->value）
    expectContains(out, "getelementptr inbounds %struct.Node, ptr %t");
}

TEST(LlvmBackendTest, ShortCircuitBlockSplit) {
    const ir::Module module = lowerValidSource(R"nc(
int g_calls = 0;

int side(int v) {
    g_calls = g_calls + 1;
    return v;
}

int main() {
    int a = 0 && side(1);
    int b = 1 || side(1);
    int c = 2 && side(0);
    return g_calls + a + b + c;
}
)nc");
    const std::string out = llvm_backend::emit(module);
    // && / || 块分裂 + phi 归并（i1），值语境 zext 到 i32
    expectContains(out, "br i1 %t0, label %land.rhs.0, label %land.end.1");
    expectContains(out, "%t3 = phi i1 [ false, %entry ], [ %t2, %land.rhs.0 ]");
    expectContains(out, "%t8 = phi i1 [ true, %land.end.1 ], [ %t7, %lor.rhs.2 ]");
    expectContains(out, "zext i1 %t3 to i32");
}

TEST(LlvmBackendTest, ExternDeclareAndVarargs) {
    const ir::Module module = lowerValidSource(R"nc(
extern int puts(char* s);
extern int printf(char* fmt, ...);

int main() {
    puts("ok");
    return 0;
}
)nc");
    const std::string out = llvm_backend::emit(module);
    expectContains(out, "declare i32 @puts(ptr)");
    expectContains(out, "declare i32 @printf(ptr, ...)");
    expectContains(out, "call i32 @puts(ptr %t");
}

TEST(LlvmBackendTest, UnresolvedExternVarargsDeclare) {
    // 跳过语义门禁的降级输入（#37 兼容路径）：llc 不接受调用点隐式声明，
    // 按零固定参数 varargs declare 发射，返回类型兜底 i32
    const ir::Module module = lowerUngated(R"nc(
int main() {
    return mystery(7);
}
)nc");
    const std::string out = llvm_backend::emit(module);
    expectContains(out, "; unresolved externals");
    expectContains(out, "declare i32 @mystery(...)");
    expectContains(out, "call i32 @mystery(i32 7)");
}

TEST(LlvmBackendTest, StringPoolAndEscapeEncoding) {
    const ir::Module module = lowerValidSource(R"nc(
int main() {
    char* a = "hello";
    char* b = "hello";
    char* c = "tab\there";
    return (a == b) + (a != c);
}
)nc");
    const std::string out = llvm_backend::emit(module);
    // private constant + \00 终止；同文去重；C 转义解码后按 LLVM \XX 重编码
    expectContains(out,
                   "@NcStr0 = private unnamed_addr constant [6 x i8] c\"hello\\00\"");
    expectContains(
      out,
      "@NcStr1 = private unnamed_addr constant [9 x i8] c\"tab\\09here\\00\"");
    // 字符串引用点：gep 首元素（char* 类型贯通）
    expectContains(out, "getelementptr inbounds [6 x i8], ptr @NcStr0, i32 0, i32 0");
    // 指针相等比较：icmp eq/ne ptr
    expectContains(out, "icmp eq ptr");
    expectContains(out, "icmp ne ptr");
}

TEST(LlvmBackendTest, GlobalsConstAndDeferredInit) {
    const ir::Module module = lowerValidSource(R"nc(
int g_base = 5;
int g_dbl = g_base * 2;
int g_zero;
char g_letter = 'Z';
int g_arr[4];

int main() {
    g_arr[0] = g_dbl;
    return g_base + g_dbl + g_zero + (g_letter == 'Z') + g_arr[0];
}
)nc");
    const std::string out = llvm_backend::emit(module);
    // 常量初始化器直写 global
    expectContains(out, "@g_base = global i32 5");
    expectContains(out, "@g_letter = global i8 90");
    expectContains(out, "@g_arr = global [4 x i32] zeroinitializer");
    expectContains(out, "@g_zero = global i32 0");
    // 非常量初始化器：零值落地 + main 入口前注入（对齐 VM 语义）
    expectContains(out, "@g_dbl = global i32 0");
    expectContains(out, "%t0 = load i32, ptr @g_base");
    expectContains(out, "%t1 = mul i32 %t0, 2");
    expectContains(out, "store i32 %t1, ptr @g_dbl");
}

// ---------------------------------------------------------------------------
// 真编译运行验收（无 LLVM 工具链时 skip；锚点防 VM/LLVM 一致地错）
// ---------------------------------------------------------------------------

TEST(LlvmE2ETest, ControlFlowShortCircuit) {
    expectLlvmAgreesWithVm(R"nc(
int g_calls = 0;

int side(int v) {
    g_calls = g_calls + 1;
    return v;
}

int main() {
    int a = 0 && side(1);
    int b = 1 || side(1);
    int c = 2 && side(0);
    if (a != 0 || b != 1 || c != 0) {
        return 99;
    }
    return g_calls + a + b + c;
}
)nc",
                           2,
                           "control_flow_short_circuit");
}

TEST(LlvmE2ETest, StructPointerArray) {
    expectLlvmAgreesWithVm(R"nc(
struct Point {
    int x;
    int y;
};

int g_count = 0;

int side(int v) {
    g_count = g_count + v;
    return v;
}

int main() {
    struct Point p = { 3, 4 };
    int sum = p.x + p.y;
    int* sp = &p.x;
    *sp = *sp + 10;
    int arr[4];
    int i = 0;
    while (i < 4) {
        arr[i] = i * 2;
        i = i + 1;
    }
    int t1 = 0 && side(1);
    int t2 = sum > 5 || side(1);
    for (int j = 0; j < 3; j = j + 1) {
        sum = sum + arr[j];
    }
    if (t1 != 0 || g_count != 0 || t2 != 1) {
        return 1;
    }
    return sum + p.x;
}
)nc",
                           26,
                           "struct_pointer_array");
}

TEST(LlvmE2ETest, CharSemantics) {
    expectLlvmAgreesWithVm(R"nc(
char g_letter = 'A';

int classify(char c) {
    if (c == 'A') {
        return 1;
    }
    if (c < 'A') {
        return 2;
    }
    return 3;
}

int main() {
    char buf[8];
    buf[0] = 'H';
    buf[1] = 'I';
    if (classify(g_letter) != 1 || classify('!') != 2 || classify('z') != 3) {
        return 11;
    }
    int acc = buf[0] - 'A' + buf[1] - 'A';
    return acc + (g_letter == 'A');
}
)nc",
                           16,
                           "char_semantics");
}

TEST(LlvmE2ETest, GlobalsAndStructInitList) {
    expectLlvmAgreesWithVm(R"nc(
struct Pair {
    int a;
    int b;
};

int g_base = 60;
int g_zero;
struct Pair g_pair = { 7, 8 };
char g_letter = 'A';
int g_dbl = g_base * 2;

int main() {
    struct Pair local = { 1, 2 };
    g_zero = g_base + g_pair.a + g_pair.b;
    char buf[4];
    buf[0] = 'x';
    int acc = g_zero + local.a + local.b;
    acc = acc + (g_letter == 'A') + (buf[0] == 'x');
    acc = acc + g_dbl;
    return g_zero + acc - g_base;
}
)nc",
                           215,
                           "globals_struct_init_list");
}

TEST(LlvmE2ETest, Recursion) {
    expectLlvmAgreesWithVm(R"nc(
int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    return fib(10);
}
)nc",
                           55,
                           "recursion_fib");
}

TEST(LlvmE2ETest, LoopBreakContinue) {
    expectLlvmAgreesWithVm(R"nc(
int main() {
    int i = 0;
    int sum = 0;
    while (1) {
        i = i + 1;
        if (i > 10) {
            break;
        }
        if (i % 2 == 0) {
            continue;
        }
        sum = sum + i;
    }
    return sum * 3 + i;
}
)nc",
                           86,
                           "loop_break_continue");
}

TEST(LlvmE2ETest, ExternPutsSmoke) {
    // extern/宿主调用：VM 无宿主函数表，单独按锚点断言（同
    // test_c_backend 的 ExternCallSmoke 口径）
    if (!llvmReady()) {
        GTEST_SKIP() << "无 LLVM 工具链（llc/链接器），跳过真编译";
    }
    int exitCode = 0;
    std::string diagnostics;
    ASSERT_TRUE(runOnLlvm(R"nc(
extern int puts(char* s);

int useString(char* s) {
    return 7;
}

int main() {
    puts("llvm-diff-ok");
    char* msg = "nano";
    return useString(msg) + 7;
}
)nc",
                          exitCode,
                          diagnostics))
      << "LLVM side:\n"
      << diagnostics;
    EXPECT_EQ(exitCode & 0xFF, 14) << "extern puts smoke: exit code";
}
