#include "nas/instruction.hpp"
#include "ncc/codegen.hpp"
#include "ncc/loader.hpp"
#include "ncc/semantic.hpp"
#include "nvm/core.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// 多文件整体编译 e2e（PRD R2a「文件即模块」）：
//   临时目录写多文件工程 → Loader 递归装载（环形/缺失/幂等）→ SemanticAnalyzer
//   （export 可见性 / main 唯一 / 重复定义）→ CodeGenerator（标号 mangle）
//   → Assembler::assemble → NVirtualMachine 执行 → 断言可观测 VM 状态与诊断。
//
// 断言口径与 test_golden_e2e.cpp 一致（main 返回值在 R0、哨兵正常停机）。

namespace {

    // 停机后的 VM 可观测状态快照
    struct VmSnapshot {
        int32_t r0 = 0;
        int32_t pc = 0;
        int32_t sp = 0;
        int32_t bp = 0;
        int32_t stackSize = 0;
        int64_t codeSize = 0;
    };

    // 一次整体编译的结果：编译成败 + 全部诊断文本 + 汇编产物 + VM 快照
    struct BuildOutcome {
        bool compiled = false;
        std::string diagnostics; // 装载 + 语义诊断（Diagnostic::toString 拼接）
        std::string assembly;
        std::optional<VmSnapshot> snapshot;
    };

    // 临时工程目录：按测试名隔离，析构时整体删除
    struct TempProject {
        std::filesystem::path dir;

        explicit TempProject(const std::string& name) {
            namespace fs = std::filesystem;
            dir = fs::temp_directory_path() / "nanoc_multifile_tests" / name;
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
        }

        ~TempProject() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }

        // 相对路径写入文件（"util/helpers.nc" 自动建子目录），返回绝对路径
        std::string write(const std::string& relativePath, const std::string& content) {
            namespace fs = std::filesystem;
            const fs::path path = dir / relativePath;
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary);
            out << content;
            return path.string();
        }

        std::string pathOf(const std::string& relativePath) const {
            return (dir / relativePath).string();
        }
    };

    // 全链路：装载 → 语义 → 代码生成 → 汇编 → VM 执行（前端异常记录为编译失败）
    BuildOutcome runProject(const std::string& entryPath) {
        BuildOutcome outcome;
        try {
            Loader loader({ entryPath });
            LoadResult loaded = loader.load();
            for (const auto& diagnostic : loaded.diagnostics) {
                outcome.diagnostics += diagnostic.toString() + "\n";
            }
            if (!loaded.ok) {
                return outcome;
            }

            SemanticAnalyzer analyzer(entryPath);
            auto analyzed = analyzer.analyze(*loaded.program);
            if (analyzed.is_err()) {
                outcome.diagnostics += "error: " + analyzed.unwrap_err() + "\n";
                return outcome;
            }
            for (const auto& diagnostic : analyzed.unwrap().diagnostics) {
                outcome.diagnostics += diagnostic.toString() + "\n";
            }
            if (analyzed.unwrap().hasErrors()) {
                return outcome;
            }

            CodeGenerator codegen;
            outcome.assembly = codegen.generate(*loaded.program);

            AssemblyResult result = Assembler::assemble(outcome.assembly);
            if (!result.ok) {
                outcome.diagnostics += "assemble failed at line "
                                       + std::to_string(result.errorLine) + ": "
                                       + result.errorMessage + "\n";
                return outcome;
            }

            const std::string nciPath =
              std::filesystem::path(entryPath).parent_path().string()
              + "/multifile_out.nci";
            {
                std::ofstream ofs(nciPath, std::ios::binary);
                ofs.write(reinterpret_cast<const char*>(result.image.data()),
                          (std::streamsize)result.image.size());
            }

            NVirtualMachine vm(8 * 1024 * 1024);
            vm.load(nciPath);
            vm.start();

            VmSnapshot snap;
            snap.r0 = vm.getRegister(0);
            snap.pc = vm.getPC();
            snap.sp = vm.getSP();
            snap.bp = vm.getBP();
            snap.stackSize = vm.getStackSize();
            snap.codeSize = vm.getCodeSize();
            outcome.snapshot = snap;

            std::remove(nciPath.c_str());
            outcome.compiled = true;
        } catch (const std::exception& e) {
            outcome.diagnostics += std::string("exception: ") + e.what() + "\n";
        }
        return outcome;
    }

    // 正常停机断言（口径与 golden e2e 一致）
    void expectNormalTermination(const VmSnapshot& snap, int32_t expectedR0) {
        EXPECT_EQ(snap.r0, expectedR0);
        EXPECT_EQ(snap.pc, snap.codeSize);  // 停在代码段末尾（哨兵地址）
        EXPECT_EQ(snap.sp, snap.stackSize); // main 的 ret 已消费栈底哨兵
        EXPECT_EQ(snap.bp, 0);              // leave 已恢复 BP
    }

} // namespace

// 4 文件示例工程（main + math + util 子目录 + data）整体编译运行：
// - 跨文件调用导出函数（math/util/data 均有）
// - 跨文件读导出全局变量（data.nc base）
// - 私有符号只在定义文件可见：math 私有 offset 参与运算
// - 跨文件同名私有全局变量（main/data 各有 shadow）经标号 mangle 隔离
TEST(MultiFileTest, FourFileProject) {
    TempProject project("four_file");
    project.write("math.nc",
                  "int offset = 100;\n"
                  "\n"
                  "int adjust(int v) {\n"
                  "    return v + offset;\n"
                  "}\n"
                  "\n"
                  "export int add(int a, int b) {\n"
                  "    return adjust(a + b);\n"
                  "}\n"
                  "\n"
                  "export int mul(int a, int b) {\n"
                  "    return a * b;\n"
                  "}\n");
    project.write("util/helpers.nc",
                  "export int twice(int v) {\n"
                  "    return v + v;\n"
                  "}\n");
    project.write("data.nc",
                  "export int base = 7;\n"
                  "int shadow = 1;\n"
                  "\n"
                  "export int getShadow() {\n"
                  "    return shadow;\n"
                  "}\n");
    project.write("main.nc",
                  "import math;\n"
                  "import \"util/helpers.nc\";\n"
                  "import data;\n"
                  "\n"
                  "int shadow = 2;\n"
                  "\n"
                  "int getLocalShadow() {\n"
                  "    return shadow;\n"
                  "}\n"
                  "\n"
                  "int main() {\n"
                  "    int r = add(1, 2);        // 103（经 math 私有 offset=100）\n"
                  "    r = r + mul(3, 4);        // +12 = 115\n"
                  "    r = r + twice(5);         // +10 = 125\n"
                  "    r = r + base;             // +7 = 132（导出全局变量）\n"
                  "    r = r + getShadow() * 10; // +10 = 142（data 私有 shadow=1）\n"
                  "    r = r + getLocalShadow(); // +2 = 144（main 私有 shadow=2）\n"
                  "    return r;\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    ASSERT_TRUE(outcome.compiled) << outcome.diagnostics;

    // mangle 决策断言：私有符号带文件 stem 前缀；导出符号与 main 保持原名
    EXPECT_NE(outcome.assembly.find(".f_math_offset:"), std::string::npos);
    EXPECT_NE(outcome.assembly.find("call .f_math_adjust"), std::string::npos);
    EXPECT_NE(outcome.assembly.find(".f_data_shadow:"), std::string::npos);
    EXPECT_NE(outcome.assembly.find(".f_main_shadow:"), std::string::npos);
    EXPECT_NE(outcome.assembly.find(".g_base:"), std::string::npos);
    EXPECT_EQ(outcome.assembly.find(".g_shadow"), std::string::npos);
    EXPECT_NE(outcome.assembly.find("\nmain:"), std::string::npos);
    EXPECT_NE(outcome.assembly.find("\nadd:"), std::string::npos);

    ASSERT_TRUE(outcome.snapshot.has_value());
    expectNormalTermination(*outcome.snapshot, 144);
}

// 重复 import 幂等：main 直接重复 import，且 a/b 菱形依赖同一模块，
// 导出函数 add 只登记一次，编译运行正确
TEST(MultiFileTest, RepeatedImportIdempotent) {
    TempProject project("idempotent");
    project.write("math.nc",
                  "export int add(int a, int b) {\n"
                  "    return a + b;\n"
                  "}\n");
    project.write("a.nc",
                  "import math;\n"
                  "export int fa() {\n"
                  "    return add(1, 1);\n"
                  "}\n");
    project.write("b.nc",
                  "import math;\n"
                  "export int fb() {\n"
                  "    return add(2, 2);\n"
                  "}\n");
    project.write("main.nc",
                  "import math;\n"
                  "import math;\n"
                  "import a;\n"
                  "import b;\n"
                  "\n"
                  "int main() {\n"
                  "    return add(fa(), fb()); // add(2, 4) = 6\n"
                  "}\n");

    // 语义符号摘要：add 只登记一次（幂等），fa/fb 各一次
    {
        Loader loader({ project.pathOf("main.nc") });
        LoadResult loaded = loader.load();
        ASSERT_TRUE(loaded.ok);
        SemanticAnalyzer analyzer(project.pathOf("main.nc"));
        auto analyzed = analyzer.analyze(*loaded.program);
        ASSERT_FALSE(analyzed.is_err());
        int addCount = 0;
        for (const auto& symbol : analyzed.unwrap().globals) {
            if (symbol.name == "add") {
                ++addCount;
            }
        }
        EXPECT_EQ(addCount, 1);
    }

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    ASSERT_TRUE(outcome.compiled) << outcome.diagnostics;
    ASSERT_TRUE(outcome.snapshot.has_value());
    expectNormalTermination(*outcome.snapshot, 6);
}

// 环形 import：a -> b -> a，诊断含完整链（b.nc 的 import 语句定位）
TEST(MultiFileTest, CircularImportReportedWithChain) {
    TempProject project("circular");
    project.write("a.nc",
                  "import b;\n"
                  "export int fa() {\n"
                  "    return 1;\n"
                  "}\n");
    project.write("b.nc",
                  "import a;\n"
                  "export int fb() {\n"
                  "    return 2;\n"
                  "}\n");
    project.write("main.nc",
                  "import a;\n"
                  "int main() {\n"
                  "    return fa();\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(outcome.diagnostics.find("circular import"), std::string::npos);
    // 链路完整呈现：入口 -> a -> b -> 回到 a
    EXPECT_NE(outcome.diagnostics.find("a.nc -> b.nc -> a.nc"), std::string::npos);
}

// 找不到文件：诊断含模块名与从哪个文件 import 的（定位行）
TEST(MultiFileTest, MissingModuleReportsImporter) {
    TempProject project("missing");
    project.write("main.nc",
                  "import nope;\n"
                  "int main() {\n"
                  "    return 0;\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(outcome.diagnostics.find("cannot find module 'nope'"), std::string::npos);
    EXPECT_NE(outcome.diagnostics.find("main.nc:1:1"), std::string::npos);
}

// 未导出符号跨文件引用：语义报错并指明定义文件
TEST(MultiFileTest, CrossFilePrivateReferenceRejected) {
    TempProject project("private_ref");
    project.write("math.nc",
                  "int offset = 5;\n"
                  "int secret = 6;\n"
                  "export int g() {\n"
                  "    return offset;\n"
                  "}\n");
    project.write("main.nc",
                  "import math;\n"
                  "int main() {\n"
                  "    return offset + secret; // offset/secret 未导出\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(
      outcome.diagnostics.find("'offset' is defined in 'math.nc' but not exported"),
      std::string::npos);
}

// main 全局唯一：两个文件各自定义 main → redefinition
TEST(MultiFileTest, DuplicateMainAcrossFilesRejected) {
    TempProject project("double_main");
    project.write("main.nc",
                  "import other;\n"
                  "int main() {\n"
                  "    return 1;\n"
                  "}\n");
    project.write("other.nc",
                  "int main() {\n"
                  "    return 2;\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(outcome.diagnostics.find("redefinition of 'main'"), std::string::npos);
}

// 导出名冲突：两个文件导出同名函数 → redefinition
TEST(MultiFileTest, ExportedNameClashAcrossFilesRejected) {
    TempProject project("export_clash");
    project.write("a.nc",
                  "export int add(int x, int y) {\n"
                  "    return x + y;\n"
                  "}\n");
    project.write("b.nc",
                  "export int add(int x, int y) {\n"
                  "    return x - y;\n"
                  "}\n");
    project.write("main.nc",
                  "import a;\n"
                  "import b;\n"
                  "int main() {\n"
                  "    return 0;\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(outcome.diagnostics.find("redefinition of 'add'"), std::string::npos);
}

// 跨文件私有同名函数合法（双 private 不冲突，标号 mangle 隔离）：
// main 只能看到本文件的 pick()，返回值证明调用绑定正确
TEST(MultiFileTest, SameNamePrivateFunctionsCoexist) {
    TempProject project("private_fn");
    project.write("lib.nc",
                  "int pick() {\n"
                  "    return 30;\n"
                  "}\n"
                  "export int fromLib() {\n"
                  "    return pick();\n"
                  "}\n");
    project.write("main.nc",
                  "import lib;\n"
                  "\n"
                  "int pick() {\n"
                  "    return 3;\n"
                  "}\n"
                  "\n"
                  "int main() {\n"
                  "    // main 的 pick()=3 与 lib 私有 pick()=30 各归各\n"
                  "    return pick() * 10 + fromLib(); // 3*10 + 30 = 60\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    ASSERT_TRUE(outcome.compiled) << outcome.diagnostics;
    ASSERT_TRUE(outcome.snapshot.has_value());
    expectNormalTermination(*outcome.snapshot, 60);
}

// import 只允许出现在文件顶部：声明之后的 import 报错（带行号）
TEST(MultiFileTest, ImportMustBeAtTopOfFile) {
    TempProject project("import_late");
    project.write("main.nc",
                  "int x = 1;\n"
                  "import math;\n"
                  "int main() {\n"
                  "    return x;\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(outcome.diagnostics.find("main.nc:2:1"), std::string::npos);
    EXPECT_NE(outcome.diagnostics.find("import is only allowed at the top"),
              std::string::npos);
}

// 函数体内的 import 同样非法（import 不是语句）
TEST(MultiFileTest, ImportInsideFunctionRejected) {
    TempProject project("import_in_fn");
    project.write("main.nc",
                  "int main() {\n"
                  "    import math;\n"
                  "    return 0;\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    EXPECT_FALSE(outcome.compiled);
    EXPECT_NE(outcome.diagnostics.find("main.nc:2:5"), std::string::npos);
}

// export 只修饰顶层函数与全局变量：export struct / export typedef 报错
TEST(MultiFileTest, ExportOnTypeDeclarationRejected) {
    {
        TempProject project("export_struct");
        project.write("main.nc",
                      "export struct Point {\n"
                      "    int x;\n"
                      "    int y;\n"
                      "};\n"
                      "int main() {\n"
                      "    return 0;\n"
                      "}\n");
        BuildOutcome outcome = runProject(project.pathOf("main.nc"));
        EXPECT_FALSE(outcome.compiled);
        EXPECT_NE(outcome.diagnostics.find("'export' can only be applied"),
                  std::string::npos);
    }
    {
        TempProject project("export_typedef");
        project.write("main.nc",
                      "export typedef int MyInt;\n"
                      "int main() {\n"
                      "    return 0;\n"
                      "}\n");
        BuildOutcome outcome = runProject(project.pathOf("main.nc"));
        EXPECT_FALSE(outcome.compiled);
        EXPECT_NE(outcome.diagnostics.find("'export' can only be applied"),
                  std::string::npos);
    }
}

// 跨文件导出全局变量可写：被导入文件读回可见写入（同一份数据段标号）
TEST(MultiFileTest, ExportedGlobalReadWriteAcrossFiles) {
    TempProject project("global_rw");
    project.write("counter.nc",
                  "export int ticks = 10;\n"
                  "\n"
                  "export int readTicks() {\n"
                  "    return ticks;\n"
                  "}\n");
    project.write("main.nc",
                  "import counter;\n"
                  "\n"
                  "int main() {\n"
                  "    ticks = ticks + 5;\n"
                  "    return readTicks(); // 15\n"
                  "}\n");

    BuildOutcome outcome = runProject(project.pathOf("main.nc"));
    ASSERT_TRUE(outcome.compiled) << outcome.diagnostics;
    ASSERT_TRUE(outcome.snapshot.has_value());
    expectNormalTermination(*outcome.snapshot, 15);
}
