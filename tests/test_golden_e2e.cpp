#include "nas/instruction.hpp"
#include "ncc/codegen.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "nvm/core.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// examples/ 全工具链黄金 e2e（Phase 5）：
//   读入 examples/*.nc 源文件 → Lexer/Parser/CodeGenerator 产 v2.1 汇编
//   → Assembler::assemble 落盘临时 .nci → NVirtualMachine 加载执行
//   → 断言可观测 VM 状态。
//
// VM 没有内建输出（无系统调用），拿不到 stdout，因此不给 examples 加 extern
// printf（宿主互操作后续再做）。断言口径（ncc 生成 enter/leave/ret，main 顶层
// ret 弹出 VM start() 压入的栈底哨兵 = codeSize）：
//   - getRegister(0)：main 返回值
//   - getPC() == getCodeSize()：经哨兵正常终止
//   - getSP() == getStackSize()：栈彻底复原
//   - getBP() == 0：帧链回收
// examples 源文件保持原样；若某 example 使用前端暂不支持的语法（如字符串
// 字面量），记录汇编/解析错误后 GTEST_SKIP（TODO(R1.2): 引用 PRD）。

namespace {

    // 从项目根（或其子目录下的构建工作目录）定位并读入 examples/<name>
    bool readExampleSource(const std::string& name, std::string& out) {
        // 从 CWD 逐级向上查找 examples/<name>（xmake run 的 rundir 在
        // build/<platform>/<arch>/<mode> 下，直接运行时为项目根）
        std::error_code ec;
        for (auto dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path()) {
            std::filesystem::path p = dir / "examples" / name;
            if (std::filesystem::exists(p, ec)) {
                std::ifstream ifs(p, std::ios::binary);
                if (!ifs.is_open()) {
                    return false;
                }
                std::ostringstream ss;
                ss << ifs.rdbuf();
                out = ss.str();
                return true;
            }
            if (dir == dir.parent_path()) {
                break; // 已到根目录
            }
        }
        ADD_FAILURE() << "找不到 examples/" << name
                      << "（CWD=" << std::filesystem::current_path().string() << "）";
        return false;
    }

    // 停机后的 VM 可观测状态快照
    struct VmSnapshot {
        int32_t r0 = 0;
        int32_t pc = 0;
        int32_t sp = 0;
        int32_t bp = 0;
        int32_t stackSize = 0;
        int64_t codeSize = 0;
    };

    // 全链路执行一段 .nc 源码；前端异常/汇编失败时记录并返回 nullopt（调用方跳过）
    std::optional<VmSnapshot> runExample(const std::string& source,
                                         const std::string& nciPath) {
        VmSnapshot snap;
        try {
            Lexer lexer(source);
            std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens);
            auto program = parser.parse();

            CodeGenerator codegen;
            std::string assembly = codegen.generate(*program);

            AssemblyResult result = Assembler::assemble(assembly);
            if (!result.ok) {
                ADD_FAILURE() << "汇编失败 line " << result.errorLine << ": "
                              << result.errorMessage << "\n--- assembly ---\n"
                              << assembly;
                return std::nullopt;
            }

            std::ofstream ofs(nciPath, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(result.image.data()),
                      (std::streamsize)result.image.size());
            ofs.close();

            NVirtualMachine vm(8 * 1024 * 1024);
            vm.load(nciPath);
            vm.start();

            snap.r0 = vm.getRegister(0);
            snap.pc = vm.getPC();
            snap.sp = vm.getSP();
            snap.bp = vm.getBP();
            snap.stackSize = vm.getStackSize();
            snap.codeSize = vm.getCodeSize();
        } catch (const std::exception& e) {
            ADD_FAILURE() << "前端编译异常: " << e.what();
            return std::nullopt;
        }
        std::remove(nciPath.c_str());
        return snap;
    }

    // 每个 example 的公共断言：正常终止 + 栈/帧复原，expectedR0 = main 返回值
    void expectNormalTermination(const VmSnapshot& snap, int32_t expectedR0) {
        EXPECT_EQ(snap.r0, expectedR0);
        EXPECT_EQ(snap.pc, snap.codeSize);  // 停在代码段末尾（哨兵地址）
        EXPECT_EQ(snap.sp, snap.stackSize); // main 的 ret 已消费栈底哨兵
        EXPECT_EQ(snap.bp, 0);              // leave 已恢复 BP
    }

} // namespace

// hello.nc：int a=10, b=20, c=a+b → return 30
TEST(GoldenE2ETest, HelloExample) {
    std::string source;
    ASSERT_TRUE(readExampleSource("hello.nc", source));
    auto snap = runExample(source, "golden_e2e_hello.nci");
    if (!snap) {
        GTEST_SKIP() << "前端暂不支持该语法，跳过（TODO(R1.2): 见 PRD 需求设计文档）";
    }
    expectNormalTermination(*snap, 30);
}

// arithmetic.nc：x=10, y=3 → sum=x+y=13（prod/quot/rem 参与计算但不作返回值）
TEST(GoldenE2ETest, ArithmeticExample) {
    std::string source;
    ASSERT_TRUE(readExampleSource("arithmetic.nc", source));
    auto snap = runExample(source, "golden_e2e_arithmetic.nci");
    if (!snap) {
        GTEST_SKIP() << "前端暂不支持该语法，跳过（TODO(R1.2): 见 PRD 需求设计文档）";
    }
    expectNormalTermination(*snap, 13);
}

// control_flow.nc：x=10>5 → result=1；while i=0..4 累加 → 1+0+1+2+3+4=11
TEST(GoldenE2ETest, ControlFlowExample) {
    std::string source;
    ASSERT_TRUE(readExampleSource("control_flow.nc", source));
    auto snap = runExample(source, "golden_e2e_control_flow.nci");
    if (!snap) {
        GTEST_SKIP() << "前端暂不支持该语法，跳过（TODO(R1.2): 见 PRD 需求设计文档）";
    }
    expectNormalTermination(*snap, 11);
}

// functions.nc：add(10,20)=30，multiply(5,6)=30，add(30,30)=60（fastcall R0-R1 传参）
TEST(GoldenE2ETest, FunctionsExample) {
    std::string source;
    ASSERT_TRUE(readExampleSource("functions.nc", source));
    auto snap = runExample(source, "golden_e2e_functions.nci");
    if (!snap) {
        GTEST_SKIP() << "前端暂不支持该语法，跳过（TODO(R1.2): 见 PRD 需求设计文档）";
    }
    expectNormalTermination(*snap, 60);
}

// loop.nc：while i=1..10 累加 → 55
TEST(GoldenE2ETest, LoopExample) {
    std::string source;
    ASSERT_TRUE(readExampleSource("loop.nc", source));
    auto snap = runExample(source, "golden_e2e_loop.nci");
    if (!snap) {
        GTEST_SKIP() << "前端暂不支持该语法，跳过（TODO(R1.2): 见 PRD 需求设计文档）";
    }
    expectNormalTermination(*snap, 55);
}
