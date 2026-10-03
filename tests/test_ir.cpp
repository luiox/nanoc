#include "ncc/ir.hpp"

#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// IR 层数据模型 + 降级器测试（PRD R1.3）：
// - dump 文本黄金断言：缩进分层、表达式带类型注记，整模块逐字节比对
// - 覆盖当前全部语言结构：标量/指针/数组/struct/typedef/字符串/控制流/调用
// - IrType 单元断言：toString 与同一性（长度不参与）
// - 健壮性：未过语义的非法输入不崩溃（Error 类型抑制级联，负例诊断归语义层）
namespace {

    // 完整管线：lex → parse → semantic（必须零错误）→ lower
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

    // 跳过语义门禁的降级（健壮性用例专用）
    std::string lowerUngatedDump(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        auto lowered = ir::lower(*program);
        if (lowered.is_err()) {
            ADD_FAILURE() << "lower() returned Err: " << lowered.unwrap_err();
            return "";
        }
        return std::move(lowered).unwrap().dump();
    }

    // 读取 examples/ 下的示例程序：xmake run 的工作目录可能是项目根，也可能是
    // build 输出目录，故从当前目录逐级向上探测；最后再按本源文件位置兜底
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
// IrType
// ---------------------------------------------------------------------------

TEST(IRTypeTest, ToString) {
    EXPECT_EQ(ir::IrType::Int.toString(), "int");
    EXPECT_EQ(ir::IrType::Char.toString(), "char");
    EXPECT_EQ(ir::IrType::Void.toString(), "void");
    EXPECT_EQ(ir::IrType::Null.toString(), "null");
    EXPECT_EQ(ir::IrType::Error.toString(), "<error>");
    EXPECT_EQ(ir::IrType::pointerTo(ir::IrType::Int).toString(), "int*");
    EXPECT_EQ(ir::IrType::pointerTo(ir::IrType::Char).toString(), "char*");
    EXPECT_EQ(ir::IrType::arrayOf(ir::IrType::Int, 10).toString(), "int[10]");
    EXPECT_EQ(ir::IrType::arrayOf(ir::IrType::Int, 0).toString(), "int[]");
    EXPECT_EQ(ir::IrType::structOf("Point").toString(), "struct Point");
    EXPECT_EQ(ir::IrType::pointerTo(ir::IrType::structOf("Node")).toString(),
              "struct Node*");
    EXPECT_EQ(ir::IrType::arrayOf(ir::IrType::structOf("Point"), 4).toString(),
              "struct Point[4]");
}

TEST(IRTypeTest, Identity) {
    // 深度同一性：指针/数组携带元素类型同一性
    EXPECT_EQ(ir::IrType::pointerTo(ir::IrType::Int),
              ir::IrType::pointerTo(ir::IrType::Int));
    EXPECT_NE(ir::IrType::pointerTo(ir::IrType::Int),
              ir::IrType::pointerTo(ir::IrType::Char));
    EXPECT_NE(ir::IrType::structOf("A"), ir::IrType::structOf("B"));
    EXPECT_EQ(ir::IrType::structOf("A"), ir::IrType::structOf("A"));

    // 数组长度不参与同一性（与 semantic 策略一致）
    EXPECT_EQ(ir::IrType::arrayOf(ir::IrType::Int, 10),
              ir::IrType::arrayOf(ir::IrType::Int, 5));
    EXPECT_NE(ir::IrType::arrayOf(ir::IrType::Int, 10),
              ir::IrType::arrayOf(ir::IrType::Char, 10));

    // 数组与 pointer-to-element 不同一（退化只发生在使用点）
    EXPECT_NE(ir::IrType::arrayOf(ir::IrType::Int, 10),
              ir::IrType::pointerTo(ir::IrType::Int));
}

// ---------------------------------------------------------------------------
// dump 黄金断言
// ---------------------------------------------------------------------------

TEST(IRLowerTest, EmptyMain) {
    ir::Module module = lowerValidSource("int main() { return 0; }");
    EXPECT_EQ(module.dump(), R"nc(module
  func int main() {
    return (const int 0)
  }
)nc");
}

TEST(IRLowerTest, ArithmeticAndStore) {
    ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 1 + 2 * 3;
    x = x - 4;
    return x;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int main() {
    locals: int x
    let int x = (add int (const int 1) (mul int (const int 2) (const int 3)))
    store (var int x) = (sub int (var int x) (const int 4))
    return (var int x)
  }
)nc");
}

TEST(IRLowerTest, ControlFlow) {
    ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 10;
    if (x > 5) {
        x = 1;
    } else {
        x = 0;
    }
    while (x < 5) {
        x = x + 1;
    }
    for (int i = 0; i < 3; i = i + 1) {
        x = x + i;
    }
    return x;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int main() {
    locals: int x, int i
    let int x = (const int 10)
    if (gt int (var int x) (const int 5)) {
      store (var int x) = (const int 1)
    } else {
      store (var int x) = (const int 0)
    }
    while (lt int (var int x) (const int 5)) {
      store (var int x) = (add int (var int x) (const int 1))
    }
    for (let int i = (const int 0); (lt int (var int i) (const int 3)); (assign int (var int i) (add int (var int i) (const int 1)))) {
      store (var int x) = (add int (var int x) (var int i))
    }
    return (var int x)
  }
)nc");
}

TEST(IRLowerTest, ElseIfChain) {
    ir::Module module = lowerValidSource(R"nc(
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
    return classify(0 - 5);
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int classify(int v) {
    if (lt int (var int v) (const int 0)) {
      return (sub int (const int 0) (const int 1))
    } else if (eq int (var int v) (const int 0)) {
      return (const int 0)
    } else {
      return (const int 1)
    }
  }
  func int main() {
    return (call int classify (sub int (const int 0) (const int 5)))
  }
)nc");
}

TEST(IRLowerTest, FunctionsAndCalls) {
    ir::Module module = lowerValidSource(R"nc(
int add(int a, int b) {
    return a + b;
}
int main() {
    int r = add(1, 2);
    add(r, 3);
    return r;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int add(int a, int b) {
    return (add int (var int a) (var int b))
  }
  func int main() {
    locals: int r
    let int r = (call int add (const int 1) (const int 2))
    eval (call int add (var int r) (const int 3))
    return (var int r)
  }
)nc");
}

TEST(IRLowerTest, Pointers) {
    ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 42;
    int* p = &x;
    *p = 7;
    int y = *p + 1;
    int* q = NULL;
    p = p + 1;
    return y;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int main() {
    locals: int x, int* p, int y, int* q
    let int x = (const int 42)
    let int* p = (addrof int* (var int x))
    store (deref int (var int* p)) = (const int 7)
    let int y = (add int (deref int (var int* p)) (const int 1))
    let int* q = (null)
    store (var int* p) = (add int* (var int* p) (const int 1))
    return (var int y)
  }
)nc");
}

TEST(IRLowerTest, Arrays) {
    ir::Module module = lowerValidSource(R"nc(
int sum(int* xs, int n) {
    int s = 0;
    for (int i = 0; i < n; i = i + 1) {
        s = s + xs[i];
    }
    return s;
}
int main() {
    int a[10];
    a[0] = 1;
    a[1] = 2;
    return sum(a, 2);
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int sum(int* xs, int n) {
    locals: int s, int i
    let int s = (const int 0)
    for (let int i = (const int 0); (lt int (var int i) (var int n)); (assign int (var int i) (add int (var int i) (const int 1)))) {
      store (var int s) = (add int (var int s) (index int (var int* xs) (var int i)))
    }
    return (var int s)
  }
  func int main() {
    locals: int[10] a
    let int[10] a
    store (index int (var int[10] a) (const int 0)) = (const int 1)
    store (index int (var int[10] a) (const int 1)) = (const int 2)
    return (call int sum (var int[10] a) (const int 2))
  }
)nc");
}

TEST(IRLowerTest, StructsAndTypedefs) {
    ir::Module module = lowerValidSource(R"nc(
struct Point {
    int x;
    int y;
};
typedef struct Point PointT;
typedef int MyInt;
struct Node {
    struct Node* next;
    int value;
};
int main() {
    struct Point p = { 1, 2 };
    PointT t;
    t.x = p.x + 3;
    MyInt m = t.y;
    struct Node n;
    n.next = NULL;
    n.value = 5;
    struct Node* np = &n;
    return np->value + (*np).value;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  struct Point {
    field int x
    field int y
  }
  struct Node {
    field struct Node* next
    field int value
  }
  typedef PointT = struct Point
  typedef MyInt = int
  func int main() {
    locals: struct Point p, struct Point t, int m, struct Node n, struct Node* np
    let struct Point p = (initlist struct Point (const int 1) (const int 2))
    let struct Point t
    store (member int (var struct Point t) .x) = (add int (member int (var struct Point p) .x) (const int 3))
    let int m = (member int (var struct Point t) .y)
    let struct Node n
    store (member struct Node* (var struct Node n) .next) = (null)
    store (member int (var struct Node n) .value) = (const int 5)
    let struct Node* np = (addrof struct Node* (var struct Node n))
    return (add int (member int (var struct Node* np) ->value) (member int (deref struct Node (var struct Node* np)) .value))
  }
)nc");
}

TEST(IRLowerTest, StringsAndChars) {
    ir::Module module = lowerValidSource(R"nc(
int print(char* s) {
    return 0;
}
int main() {
    char c = 'A';
    char z = 'x';
    char buf[16];
    buf[0] = 'x';
    print("hello");
    return c;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int print(char* s) {
    return (const int 0)
  }
  func int main() {
    locals: char c, char z, char[16] buf
    let char c = (const char 'A')
    let char z = (const char 'x')
    let char[16] buf
    store (index char (var char[16] buf) (const int 0)) = (const char 'x')
    eval (call int print (string char* "hello"))
    return (var char c)
  }
)nc");
}

TEST(IRLowerTest, LogicalUnaryAssignChain) {
    ir::Module module = lowerValidSource(R"nc(
int main() {
    int a = 1;
    int b = 0;
    int c = a && b || !a;
    int d = -a;
    a = b = 3;
    int e = c == 1 != 0;
    int f = a % 2 / 1;
    return d + e + f;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int main() {
    locals: int a, int b, int c, int d, int e, int f
    let int a = (const int 1)
    let int b = (const int 0)
    let int c = (logor int (logand int (var int a) (var int b)) (unary int ! (var int a)))
    let int d = (unary int - (var int a))
    store (var int a) = (assign int (var int b) (const int 3))
    let int e = (ne int (eq int (var int c) (const int 1)) (const int 0))
    let int f = (div int (mod int (var int a) (const int 2)) (const int 1))
    return (add int (add int (var int d) (var int e)) (var int f))
  }
)nc");
}

TEST(IRLowerTest, Globals) {
    ir::Module module = lowerValidSource(R"nc(
int g_count = 5;
int g_zero;
struct Pair {
    int a;
    int b;
};
struct Pair g_pair = { 7, 8 };
int main() {
    g_zero = g_count + 1;
    return g_zero;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  struct Pair {
    field int a
    field int b
  }
  global int g_count = (const int 5)
  global int g_zero
  global struct Pair g_pair = (initlist struct Pair (const int 7) (const int 8))
  func int main() {
    store (var int g_zero) = (add int (var int g_count) (const int 1))
    return (var int g_zero)
  }
)nc");
}

TEST(IRLowerTest, ScopingAndShadowing) {
    ir::Module module = lowerValidSource(R"nc(
int main() {
    int x = 1;
    {
        int x = 2;
        x = x + 1;
    }
    if (x > 0) int y = 3;
    while (x > 0) x = x - 1;
    return x;
}
)nc");
    EXPECT_EQ(module.dump(), R"nc(module
  func int main() {
    locals: int x, int x, int y
    let int x = (const int 1)
    {
      let int x = (const int 2)
      store (var int x) = (add int (var int x) (const int 1))
    }
    if (gt int (var int x) (const int 0)) {
      let int y = (const int 3)
    }
    while (gt int (var int x) (const int 0)) {
      store (var int x) = (sub int (var int x) (const int 1))
    }
    return (var int x)
  }
)nc");
}

// ---------------------------------------------------------------------------
// examples/ 冒烟：全部示例过 semantic 后可降级，dump 结构完整
// ---------------------------------------------------------------------------

TEST(IRLowerTest, ExamplesSmoke) {
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
        ir::Module module = lowerValidSource(source);
        const std::string dump = module.dump();
        EXPECT_FALSE(dump.empty()) << name;
        EXPECT_EQ(dump.substr(0, 7), "module\n") << name;
    }
}

// ---------------------------------------------------------------------------
// 健壮性：lower 的输入契约是"已通过语义分析"，但不得因非法输入崩溃。
// 类型层面错误（语义层已报）降级为 <error> 抑制级联；负例诊断归语义层，此处
// 只断言不崩溃且 dump 确定性。
// ---------------------------------------------------------------------------

TEST(IRLowerTest, Robustness_UndeclaredWithoutSemanticGate) {
    const std::string dump = lowerUngatedDump(R"nc(
int main() {
    x = undefined + 1;
    return y;
}
)nc");
    EXPECT_EQ(dump, R"nc(module
  func int main() {
    store (var <error> x) = (add <error> (var <error> undefined) (const int 1))
    return (var <error> y)
  }
)nc");
}
