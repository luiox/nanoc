#include "support/diff_harness.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>
#include <vector>

// R13 三后端差分测试框架·矩阵化（PRD：把散落的差分断言收敛成可扩展矩阵）。
//
// 矩阵 = 程序集 × 后端：
// - 程序集：examples/ 全部 5 例（expectedR0 与 test_golden_e2e.cpp 锚点一致）
//   + 8 个特性用例（struct/指针/数组/字符串/控制流/递归/全局/综合，其中 3 个
//   从 test_c_backend.cpp 的手写差分用例提炼为 .nc 常量）。
// - 后端：vm（进程内 codegen→nas→nvm）与 c（进程外 emit→真编译→运行）。
//   未来 LLVM 后端见 diff_harness.hpp 的 R6 接入点注释。
// - 断言：同一程序在两个后端的可观测退出码一致。映射口径（详见 harness 头）：
//   VM main 返回值 = R0（int32_t）；C 侧 `int32_t main(void)` 返回值经进程退
//   出码。Windows 保 32 位、POSIX 只留低 8 位 → 统一按 & 0xFF 比较；255 截断
//   边界：R0=256 与 R0=0 同余不可区分，故矩阵程序 main 返回值约定落在
//   [0, 255]（本文件全部程序满足），负值按补码截断（-1 → 255）。
// - skip 策略（PRD R13）：无 C 编译器时 C 整列记 SKIP（探针见
//   cCompilerCommand()：NANOC_C_COMPILER > PATH clang > gcc），可执行后端
//   不足 2 个的行不判失败，整体 GTEST_SKIP。
// - expectedR0 锚点：已知程序的 VM 原始 R0 另行断言，防止两后端"一致地错"。
// - TODO(R3 后续)：extern/宿主调用（puts 等）场景本期不入矩阵；R3 合并后
//   下一轮把 puts 场景加进矩阵（比较口径扩展为 {exitCode8, stdOut}，
//   见 diff_harness.hpp 尾部 TODO）。

namespace {

    // -----------------------------------------------------------------------
    // 程序集（11+ 程序）：examples 5 例 + 特性用例 8 个
    // -----------------------------------------------------------------------

    nanoc_diff::DiffProgram exampleProgram(const std::string& file, int32_t expectedR0) {
        nanoc_diff::DiffProgram program;
        program.name = "examples/" + file;
        program.slug = "examples_" + file.substr(0, file.find('.'));
        if (!nanoc_diff::readExampleSource(file, program.source)) {
            ADD_FAILURE() << "找不到 examples/" << file << "（矩阵行将以 FAIL 呈现诊断）";
        }
        program.expectedR0 = expectedR0;
        return program;
    }

    // expectedR0 锚点均手工核算并控制在 [0, 255]，保证退出码映射单射
    std::vector<nanoc_diff::DiffProgram> buildPrograms() {
        std::vector<nanoc_diff::DiffProgram> programs;

        // examples/ 全部 5 例（锚点 = test_golden_e2e.cpp 的期望 R0）
        programs.push_back(exampleProgram("hello.nc", 30));
        programs.push_back(exampleProgram("arithmetic.nc", 13));
        programs.push_back(exampleProgram("control_flow.nc", 11));
        programs.push_back(exampleProgram("functions.nc", 60));
        programs.push_back(exampleProgram("loop.nc", 55));

        // 特性：控制流 + 短路副作用 + 相互递归（提炼自 CBackendDiffTest）
        programs.push_back({ "feature/control_flow_short_circuit_recursion",
                             "feature_control_flow_short_circuit_recursion",
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
                             14 });

        // 特性：struct 值语义（拷贝/按值传参/按值返回，源对象不被修改）
        programs.push_back({ "feature/struct_value_semantics",
                             "feature_struct_value_semantics",
                             R"nc(struct Box {
    int w;
    int h;
};

int area(struct Box b) {
    return b.w * b.h;
}

struct Box grow(struct Box b, int dw, int dh) {
    b.w = b.w + dw;
    b.h = b.h + dh;
    return b;
}

int main() {
    struct Box a = { 2, 3 };
    struct Box b = a; // 值拷贝：改 b 不影响 a
    b.w = 10;
    struct Box c = grow(a, 1, 1); // 按值传参：a 不变，返回值整体拷出
    // area(a)=6, area(b)=30, area(c)=12
    return area(a) + area(b) / 10 + area(c);
}
)nc",
                             21 });

        // 特性：指针（swap/NULL/指针步进回读）
        programs.push_back({ "feature/pointer_swap_walk",
                             "feature_pointer_swap_walk",
                             R"nc(void swap(int* a, int* b) {
    int t = *a;
    *a = *b;
    *b = t;
}

int main() {
    int x = 1;
    int y = 2;
    swap(&x, &y);
    int ok = (x == 2) && (y == 1);
    int* p = &x;
    p = p + 1;
    int* q = p - 1;
    int back = *q; // 步进后回读：q 重新指向 x
    int* n = NULL;
    if (n != NULL) {
        return 200;
    }
    // x=2, y=1, ok=1, back=2
    return x * 100 + y * 10 + ok * 5 + back;
}
)nc",
                             217 });

        // 特性：数组（一维 + 按一维展开模拟 2x2 矩阵）
        programs.push_back({ "feature/array_flat_matrix",
                             "feature_array_flat_matrix",
                             R"nc(int sumRange(int* xs, int n) {
    int s = 0;
    int i = 0;
    while (i < n) {
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

int main() {
    int a[8];
    int i = 0;
    while (i < 8) {
        a[i] = i + 1;
        i = i + 1;
    }
    int total = sumRange(a, 8); // 36
    a[3] = a[3] * 10;           // 4 -> 40
    total = total + a[3] - 4;   // 72
    int m[4];                   // 2x2 矩阵按一维展开：m[r*2+c] = r*10+c
    int r = 0;
    while (r < 2) {
        int c = 0;
        while (c < 2) {
            m[r * 2 + c] = r * 10 + c;
            c = c + 1;
        }
        r = r + 1;
    }
    total = total + m[0] + m[1] + m[2] + m[3]; // +22 -> 94
    return total;
}
)nc",
                             94 });

        // 特性：字符串/字符（字符串字面量作不透明指针传递 + 去重池 + char 运算；
        // 不 deref 字符串内容——VM 侧字面量无字节级读，见 semantic.hpp 已知限制）
        programs.push_back({ "feature/string_char_pool",
                             "feature_string_char_pool",
                             R"nc(int takeStr(char* s) {
    return 7;
}

int tag(char c) {
    if (c == 'A') {
        return 1;
    }
    return 0;
}

int main() {
    int acc = tag('A') * 10 + tag('B'); // 10
    acc = acc + takeStr("alpha");       // 同字面量两次入参（去重池单落）→ 17
    acc = acc + takeStr("alpha");       // 24
    acc = acc + takeStr("beta");        // 31
    char buf[4];
    buf[0] = 'x';
    buf[1] = 'y';
    acc = acc + (buf[0] == 'x');  // +1
    acc = acc + (buf[1] == 'z');  // +0
    char* msg = "alpha";
    acc = acc + takeStr(msg) - 7; // 变量传递同样命中池 → +0
    return acc;
}
)nc",
                             32 });

        // 特性：递归（相互递归 + 双递归 fib）
        programs.push_back({ "feature/mutual_recursion_fib",
                             "feature_mutual_recursion_fib",
                             R"nc(int isEven(int n) {
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

int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    // isEven(10)=1, isOdd(7)=1, fib(12)=144
    return isEven(10) + (isOdd(7) * 2) + fib(12);
}
)nc",
                             147 });

        // 特性：struct/指针/数组综合（提炼自 CBackendDiffTest）
        programs.push_back({ "feature/struct_pointer_array",
                             "feature_struct_pointer_array",
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
                             196 });

        // 特性：全局变量/typedef/字符串池/字符（提炼自 CBackendDiffTest）
        programs.push_back({ "feature/globals_chars_strings",
                             "feature_globals_chars_strings",
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
                             140 });

        return programs;
    }

    const std::vector<nanoc_diff::DiffProgram>& matrixPrograms() {
        static const std::vector<nanoc_diff::DiffProgram> programs = buildPrograms();
        return programs;
    }

    // 矩阵全量执行（进程内缓存：每个程序×后端只真正跑一次，各测试读缓存）
    const std::vector<nanoc_diff::MatrixCell>& matrixCells() {
        static std::vector<nanoc_diff::MatrixCell> cells;
        if (cells.empty()) {
            nanoc_diff::registerBuiltinBackends();
            for (const auto& program : matrixPrograms()) {
                const std::vector<nanoc_diff::MatrixCell> row =
                  nanoc_diff::runRow(program);
                cells.insert(cells.end(), row.begin(), row.end());
            }
        }
        return cells;
    }

    const nanoc_diff::DiffProgram* findProgram(const std::string& name) {
        const auto& programs = matrixPrograms();
        const auto match = std::find_if(
          programs.begin(),
          programs.end(),
          [&](const nanoc_diff::DiffProgram& program) { return program.name == name; });
        return match == programs.end() ? nullptr : &*match;
    }

    std::vector<nanoc_diff::MatrixCell> cellsForProgram(const std::string& name) {
        std::vector<nanoc_diff::MatrixCell> row;
        for (const auto& cell : matrixCells()) {
            if (cell.program == name) {
                row.push_back(cell);
            }
        }
        return row;
    }

    int countStatus(const std::vector<nanoc_diff::MatrixCell>& row,
                    nanoc_diff::CellStatus status) {
        int count = 0;
        for (const auto& cell : row) {
            if (cell.status == status) {
                ++count;
            }
        }
        return count;
    }

    // 单程序行的断言：无 FAIL 单元 → 有锚点则校验 VM 原始 R0 → 可比后端 ≥2
    // 时按低 8 位口径断言跨后端一致（不足 2 个则按 PRD R13 skip 策略跳过）
    void assertProgram(const std::string& name) {
        const nanoc_diff::DiffProgram* program = findProgram(name);
        ASSERT_NE(program, nullptr) << "matrix row not registered: " << name;

        const std::vector<nanoc_diff::MatrixCell> row = cellsForProgram(name);
        ASSERT_FALSE(row.empty()) << "matrix cells missing: " << name;

        for (const auto& cell : row) {
            EXPECT_NE(cell.status, nanoc_diff::CellStatus::Fail)
              << "[" << name << " / " << cell.backend << "] " << cell.detail;
        }

        if (program->expectedR0) {
            for (const auto& cell : row) {
                if (cell.backend == "vm" && cell.status == nanoc_diff::CellStatus::Pass) {
                    EXPECT_EQ(cell.rawExit, static_cast<int>(*program->expectedR0))
                      << name << ": VM R0 anchor（防两后端一致地错）";
                }
            }
        }

        const int executed = countStatus(row, nanoc_diff::CellStatus::Pass);
        if (executed < 2) {
            GTEST_SKIP() << name << ": 可执行后端 " << executed
                         << " 个（<2，通常为无 C 编译器），按 PRD R13 skip 策略不判失败";
        }

        const std::string mismatch = nanoc_diff::rowMismatch(row);
        EXPECT_TRUE(mismatch.empty()) << "[" << name << "] 后端不一致: " << mismatch;
    }

} // namespace

// ---------------------------------------------------------------------------
// harness 本体小断言（映射口径与注册表）
// ---------------------------------------------------------------------------

TEST(DiffHarnessTest, ExitCode8BitNormalization) {
    // 矩阵比较口径的 255 截断边界（文档化于 diff_harness.hpp）：
    // - [0, 255] 区间内映射单射（矩阵程序集约定值域）；
    // - 256 与 0 同余不可区分（模 256 截断的固有边界）；
    // - 负值按补码截断，两侧后端口径一致。
    EXPECT_EQ(0 & 0xFF, 0);
    EXPECT_EQ(255 & 0xFF, 255);
    EXPECT_EQ(256 & 0xFF, 0);
    EXPECT_EQ(-1 & 0xFF, 255);
}

TEST(DiffHarnessTest, BuiltinBackendsRegistered) {
    nanoc_diff::registerBuiltinBackends();
    const auto& registered = nanoc_diff::backends();
    ASSERT_GE(registered.size(), 2U);
    EXPECT_EQ(registered[0]->name(), "vm");
    EXPECT_EQ(registered[1]->name(), "c");
    EXPECT_TRUE(registered[0]->probe()); // vm 进程内链路恒可用
    // c 后端 probe 结果随环境（有/无 C 编译器）变化，此处只断言可调用
    registered[1]->probe();
}

// ---------------------------------------------------------------------------
// 矩阵行：examples 全部 5 例
// ---------------------------------------------------------------------------

TEST(DiffMatrixTest, Examples_Hello) { assertProgram("examples/hello.nc"); }

TEST(DiffMatrixTest, Examples_Arithmetic) { assertProgram("examples/arithmetic.nc"); }

TEST(DiffMatrixTest, Examples_ControlFlow) { assertProgram("examples/control_flow.nc"); }

TEST(DiffMatrixTest, Examples_Functions) { assertProgram("examples/functions.nc"); }

TEST(DiffMatrixTest, Examples_Loop) { assertProgram("examples/loop.nc"); }

// ---------------------------------------------------------------------------
// 矩阵行：特性用例 8 个
// ---------------------------------------------------------------------------

TEST(DiffMatrixTest, Feature_ControlFlowShortCircuitRecursion) {
    assertProgram("feature/control_flow_short_circuit_recursion");
}

TEST(DiffMatrixTest, Feature_StructValueSemantics) {
    assertProgram("feature/struct_value_semantics");
}

TEST(DiffMatrixTest, Feature_PointerSwapWalk) {
    assertProgram("feature/pointer_swap_walk");
}

TEST(DiffMatrixTest, Feature_ArrayFlatMatrix) {
    assertProgram("feature/array_flat_matrix");
}

TEST(DiffMatrixTest, Feature_StringCharPool) {
    assertProgram("feature/string_char_pool");
}

TEST(DiffMatrixTest, Feature_MutualRecursionFib) {
    assertProgram("feature/mutual_recursion_fib");
}

TEST(DiffMatrixTest, Feature_StructPointerArray) {
    assertProgram("feature/struct_pointer_array");
}

TEST(DiffMatrixTest, Feature_GlobalsCharsStrings) {
    assertProgram("feature/globals_chars_strings");
}

// ---------------------------------------------------------------------------
// 汇总：可读矩阵输出到测试日志（spdlog + gtest message 各一份）+ 全局一致
// ---------------------------------------------------------------------------

TEST(DiffMatrixTest, MatrixSummary) {
    const std::vector<nanoc_diff::MatrixCell>& cells = matrixCells();
    const std::string table = nanoc_diff::renderMatrix(cells);

    spdlog::info("R13 diff matrix:\n{}", table);
    SUCCEED() << "\n" << table;

    // 汇总口径：无 FAIL；可比行全部一致；至少一行真正可比（CI windows-latest
    // 带 clang，应全跑而非全 skip；全 skip 只应出现在无编译器的裸环境）
    int failCount = 0;
    int passCount = 0;
    for (const auto& cell : cells) {
        if (cell.status == nanoc_diff::CellStatus::Fail) {
            ++failCount;
        } else if (cell.status == nanoc_diff::CellStatus::Pass) {
            ++passCount;
        }
    }
    for (const auto& program : matrixPrograms()) {
        const std::vector<nanoc_diff::MatrixCell> row = cellsForProgram(program.name);
        const std::string mismatch = nanoc_diff::rowMismatch(row);
        EXPECT_TRUE(mismatch.empty()) << "[" << program.name << "] " << mismatch;
    }
    EXPECT_EQ(failCount, 0) << "矩阵存在执行/编译失败的后端单元";
    if (passCount == 0) {
        GTEST_SKIP() << "无任何后端产出可观测结果（无 C 编译器且 VM 行缺失？）";
    }
}
