#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>

// IR 路径发射断言（R1.4 后端迁移）：CodeGenerator 消费 ir::Module。
// 覆盖：显式 lower→generate 与兼容入口 generate(Program&) 的产物一致性、
// 语句级赋值的死结果消除、宿主外部调用经 IR 路径、链接信息 side-table
// （IR 缺口绕过，见 codegen.hpp LinkageEntry 注释）与 IR dump 可读性。

namespace {

    // 显式 IR 路径：lex → parse → lower → generate（单文件模式，无 linkage）
    std::string compileViaIr(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();
        auto lowered = ir::lower(*program);
        if (lowered.is_err()) {
            throw std::runtime_error(lowered.unwrap_err());
        }
        ir::Module module = std::move(lowered).unwrap(); // Module 只移动
        CodeGenerator codegen;
        return codegen.generate(module);
    }

    // 兼容入口：AST 直入（内部同样经 lower → IR 发射）
    std::string compileViaShim(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();
        CodeGenerator codegen;
        return codegen.generate(*program);
    }

    ir::Module lowerModule(const std::string& source) {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto program = parser.parse();
        auto lowered = ir::lower(*program);
        if (lowered.is_err()) {
            throw std::runtime_error(lowered.unwrap_err());
        }
        return std::move(lowered).unwrap();
    }

    // 统计子串出现次数
    std::size_t countOf(const std::string& text, const std::string& needle) {
        std::size_t count = 0;
        for (std::size_t pos = text.find(needle); pos != std::string::npos;
             pos = text.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }

} // namespace

// 显式 IR 路径与兼容入口（AST → lower → IR）产物逐字节一致：
// 发射逻辑只有一份，两条入口汇聚于同一 IR 消费实现
TEST(IrCodegenTest, ExplicitPathMatchesCompatibilityShim) {
    const std::string source =
      "struct Point { int x; int y; };\n"
      "struct Point make(int x, int y) { struct Point p = {x, y}; return p; }\n"
      "int main() {\n"
      "    struct Point a = make(1, 2);\n"
      "    int i = 0;\n"
      "    int s = 0;\n"
      "    while (i < 3) { s = s + a.x; i = i + 1; }\n"
      "    if (s > 0 && a.y != 0 || !0) { s = s * 2; }\n"
      "    return s - a.y;\n"
      "}";
    EXPECT_EQ(compileViaIr(source), compileViaShim(source));
}

// 语句级赋值经 IrStoreStmt 直接发射：不再物化死赋值结果（省去 push/pop 对）。
// 这是迁移后唯一一类系统性文本差异（行为等价，见 PR 回归矩阵）
TEST(IrCodegenTest, StoreStmtOmitsDeadResultPair) {
    std::string assembly = compileViaIr("int main() { int x; x = 5; return x; }");

    // 值求值 → 立即存回，中间无 push/pop 死对（emitStoreVar 的取址序列在存回前）
    const std::string compact = "lmm R0, 5\n    push R0\n    pop R0\n    mov R6, R5\n";
    EXPECT_NE(assembly.find(compact), std::string::npos);
    // 旧形态（存回后再 push 结果）不得出现
    EXPECT_EQ(countOf(assembly, "store [R6], R0\n    push R0\n    pop R0\n"),
              (std::size_t)0);
}

// 表达式语境的赋值保留结果语义（push 一次，供外层消费）
TEST(IrCodegenTest, AssignExprKeepsResultValue) {
    std::string assembly =
      compileViaIr("int main() { int x; int y; return y = (x = 7); }");

    // 内层 x = 7 与外层 y = ... 各 push 一次结果
    EXPECT_GE(countOf(assembly, "store [R6], R0\n    push R0\n"), (std::size_t)2);
}

// 宿主外部调用经 IR 路径：未定义被调 → extern + callx（与迁移前一致）
TEST(IrCodegenTest, ExternHostCallThroughIrPath) {
    std::string assembly = compileViaIr("int main() { printf(10); return 0; }");

    EXPECT_TRUE(assembly.find("extern printf") != std::string::npos);
    EXPECT_TRUE(assembly.find("callx printf") != std::string::npos);
    EXPECT_TRUE(assembly.find("addi R4") == std::string::npos); // 单参无需清栈
}

// sret 结构体返回经 IR 类型注记判定：调用者 R7 传接收槽、被调者溢出 R7、
// 返回时逐字拷贝（IrCallExpr 的返回类型来自 lower 的函数签名登记）
TEST(IrCodegenTest, StructReturnSretThroughIrTypes) {
    std::string assembly = compileViaIr(
      "struct Point { int x; int y; };\n"
      "struct Point make(int x, int y) { struct Point p = {x, y}; return p; }\n"
      "int main() { struct Point a = make(1, 2); return a.x + a.y; }");

    EXPECT_TRUE(assembly.find("mov R7, R5") != std::string::npos);
    EXPECT_TRUE(assembly.find("subi R7,") != std::string::npos);
    EXPECT_TRUE(assembly.find("store [R6], R7") != std::string::npos);
    EXPECT_TRUE(assembly.find("load R2, [R6]") != std::string::npos);
}

// ---- 链接信息 side-table（IR 缺口绕过）----

// buildLinkageTable 按声明序收集，与 ir::lower 的 functions/globals 导出顺序
// 位置对齐（文件/导出标记来自 AST，IR 未建模）
TEST(IrCodegenTest, LinkageTableAlignsWithDeclarationOrder) {
    Program program(1, 1);

    auto helper = std::make_unique<FuncDeclaration>("int", "helper", 1, 1);
    helper->sourceFile = "lib.nc";
    helper->isExported = false;
    program.declarations.push_back(std::move(helper));

    auto counter = std::make_unique<VarDeclaration>("int", "counter", 2, 1);
    counter->sourceFile = "main.nc";
    counter->isExported = true;
    program.declarations.push_back(std::move(counter));

    auto exportedFn = std::make_unique<FuncDeclaration>("int", "api", 3, 1);
    exportedFn->sourceFile = "lib.nc";
    exportedFn->isExported = true;
    program.declarations.push_back(std::move(exportedFn));

    auto entry = std::make_unique<VarDeclaration>("int", "shadow", 4, 1);
    entry->sourceFile = "main.nc";
    entry->isExported = false;
    program.declarations.push_back(std::move(entry));

    LinkageTable table = buildLinkageTable(program);
    ASSERT_EQ(table.functions.size(), (std::size_t)2);
    ASSERT_EQ(table.globals.size(), (std::size_t)2);

    EXPECT_EQ(table.functions[0].name, "helper");
    EXPECT_EQ(table.functions[0].file, "lib.nc");
    EXPECT_FALSE(table.functions[0].isExported);
    EXPECT_EQ(table.functions[1].name, "api");
    EXPECT_TRUE(table.functions[1].isExported);

    EXPECT_EQ(table.globals[0].name, "counter");
    EXPECT_TRUE(table.globals[0].isExported);
    EXPECT_EQ(table.globals[1].name, "shadow");
    EXPECT_FALSE(table.globals[1].isExported);
}

// linkage 与 IR 模块不对齐属于内部契约破坏：generate 防御性抛异常
TEST(IrCodegenTest, MisalignedLinkageTableRejected) {
    ir::Module module = lowerModule("int main() { return 0; }");
    ASSERT_EQ(module.functions.size(), (std::size_t)1);

    LinkageTable tooMany;
    tooMany.functions.resize(2);
    tooMany.globals.resize(0);
    CodeGenerator codegen;
    EXPECT_THROW(codegen.generate(module, &tooMany), std::runtime_error);
}

// ---- IR dump 人工可读性 sanity ----

// dump 覆盖 struct/typedef/全局/函数/局部/表达式注记，缩进分层可人工检视
TEST(IrCodegenTest, DumpIsHumanReadable) {
    ir::Module module = lowerModule("struct Point { int x; int y; };\n"
                                    "typedef struct Point PointT;\n"
                                    "int base = 3;\n"
                                    "int main() {\n"
                                    "    PointT p;\n"
                                    "    p.x = base + 1;\n"
                                    "    int v = p.x * 2;\n"
                                    "    if (v > 0) { v = v - 1; }\n"
                                    "    return v;\n"
                                    "}");

    const std::string text = module.dump();
    EXPECT_TRUE(text.find("module") == (std::size_t)0);
    EXPECT_TRUE(text.find("struct Point {") != std::string::npos);
    EXPECT_TRUE(text.find("field int x") != std::string::npos);
    EXPECT_TRUE(text.find("typedef PointT = struct Point") != std::string::npos);
    EXPECT_TRUE(text.find("global int base = (const int 3)") != std::string::npos);
    EXPECT_TRUE(text.find("func int main() {") != std::string::npos);
    EXPECT_TRUE(text.find("let struct Point p") != std::string::npos);
    EXPECT_TRUE(text.find("store (member int (var struct Point p) .x)")
                != std::string::npos);
    EXPECT_TRUE(text.find("(add int (var int base) (const int 1))") != std::string::npos);
    EXPECT_TRUE(text.find("if ") != std::string::npos);
    EXPECT_TRUE(text.find("return (var int v)") != std::string::npos);
    EXPECT_TRUE(text.find("  }") != std::string::npos); // 块收尾缩进
}
