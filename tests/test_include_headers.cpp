// include C 头文件·声明子集（PRD R9 / M7）测试：
// - 预处理（经 Loader 驱动）：#include 引号形式（相对当前文件、嵌套子目录）、
//   include guard（#ifndef/#define/#endif）与 #pragma once 识别、重复/循环
//   include 幂等跳过、对象宏（值为字面量/常量表达式，登记时按当前常量表
//   单层展开）、enum 按常量展开（语言本体不新增 enum 语法）、固定宽度类型
//   预置表（int32_t/uint8_t 等 → int）
// - 负例（识别后报错而非误编译）：函数宏、条件编译 #if/#ifdef/#ifndef（guard
//   形态除外）、<...> 角形式、找不到头文件、不支持 pragma；#define 覆盖语言
//   关键字（NULL）被静默忽略
// - Parser 头文件模式：函数原型（无函数体）、(void) 空参表、无名形参、数组
//   形参退化、const/volatile/unsigned 等修饰符链（语义忽略）、float/double
//   报不支持；语言本体（.nc）不接受无函数体原型（回归边界）
// - 语义：头文件原型与同名定义合并（C 原型语义）、签名不兼容报 conflicting
//   types、未被定义覆盖的原型以 isPrototype 进符号摘要
// - e2e：.nc + .h 全链路（Loader → semantic → IR → codegen → nas → nvm 断言
//   R0）；未解析原型走 extern 路径（msvcrt abs 宿主解析）；C 后端 include 同
//   一头文件真编译差分（头文件本身单独过 C 编译器，证明是合法 C）
#include "nas/instruction.hpp"
#include "ncc/c_backend.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/loader.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"
#include "nvm/core.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

    // -----------------------------------------------------------------------
    // 临时工程目录（与 test_separate.cpp 同策略：按测试名隔离，析构即删）
    // -----------------------------------------------------------------------

    struct TempProject {
        std::filesystem::path dir;

        explicit TempProject(const std::string& name) {
            namespace fs = std::filesystem;
            dir = fs::temp_directory_path() / "nanoc_include_tests" / name;
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
        }

        ~TempProject() { std::filesystem::remove_all(dir); }

        // 相对路径写入文件，返回绝对路径
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

    // -----------------------------------------------------------------------
    // 装载 + 语义辅助
    // -----------------------------------------------------------------------

    struct LoadOutcome {
        bool ok = false;
        std::string diagnostics;
        std::unique_ptr<Program> program;
        std::vector<std::string> loadOrder;
    };

    LoadOutcome loadEntry(const std::string& entryPath) {
        LoadOutcome outcome;
        Loader loader({ entryPath });
        LoadResult loaded = loader.load();
        for (const auto& diagnostic : loaded.diagnostics) {
            outcome.diagnostics += diagnostic.toString() + "\n";
        }
        outcome.ok = loaded.ok;
        outcome.loadOrder = loaded.loadOrder;
        if (loaded.ok) {
            outcome.program = std::move(loaded.program);
        }
        return outcome;
    }

    // 装载 + 语义门禁（无 error 即通过；返回全局符号摘要供断言）
    SemanticResult analyzeLoaded(const LoadOutcome& loaded, const std::string& entry) {
        SemanticAnalyzer analyzer(entry);
        auto analyzed = analyzer.analyze(*loaded.program);
        if (analyzed.is_err()) {
            ADD_FAILURE() << "analyze() returned Err: " << analyzed.unwrap_err();
            return SemanticResult{};
        }
        return std::move(analyzed).unwrap();
    }

    // -----------------------------------------------------------------------
    // VM 全链路（Loader → semantic → IR → codegen → nas → nvm，断言正常停机）
    // -----------------------------------------------------------------------

    struct VmRun {
        bool ran = false;
        int32_t r0 = 0;
        std::string diagnostics;
    };

    VmRun runOnVm(const LoadOutcome& loaded, const std::string& entry) {
        VmRun run;
        const std::string nciPath =
          (std::filesystem::temp_directory_path() / "include_e2e.nci").string();
        try {
            SemanticAnalyzer analyzer(entry);
            auto analyzed = analyzer.analyze(*loaded.program);
            if (analyzed.is_err()) {
                run.diagnostics = "analyze Err: " + analyzed.unwrap_err();
                return run;
            }
            for (const auto& diagnostic : analyzed.unwrap().diagnostics) {
                if (diagnostic.severity == DiagnosticSeverity::Error) {
                    run.diagnostics = "semantic error: " + diagnostic.toString();
                    return run;
                }
            }

            auto lowered = ir::lower(*loaded.program);
            if (lowered.is_err()) {
                run.diagnostics = "lower Err: " + lowered.unwrap_err();
                return run;
            }
            ir::Module module = std::move(lowered).unwrap();

            LinkageTable linkage = buildLinkageTable(*loaded.program);
            CodeGenerator codegen;
            const std::string assembly = codegen.generate(module, &linkage);

            const AssemblyResult assembled = Assembler::assemble(assembly);
            if (!assembled.ok) {
                run.diagnostics = "assemble failed line "
                                  + std::to_string(assembled.errorLine) + ": "
                                  + assembled.errorMessage;
                return run;
            }

            {
                std::ofstream ofs(nciPath, std::ios::binary);
                ofs.write(reinterpret_cast<const char*>(assembled.image.data()),
                          static_cast<std::streamsize>(assembled.image.size()));
            }

            NVirtualMachine vm(8 * 1024 * 1024);
            vm.load(nciPath);
            vm.start();

            // 正常停机判定（同 golden e2e 口径）：栈/帧彻底复原
            if (vm.getPC() != vm.getCodeSize() || vm.getSP() != vm.getStackSize()
                || vm.getBP() != 0) {
                run.diagnostics = "VM abnormal halt";
                return run;
            }
            run.r0 = vm.getRegister(0);
            run.ran = true;
        } catch (const std::exception& e) {
            run.diagnostics = std::string("frontend/VM exception: ") + e.what();
        }
        std::remove(nciPath.c_str());
        return run;
    }

    // -----------------------------------------------------------------------
    // C 编译器探测（与 test_extern.cpp / diff_harness 同策略）
    // -----------------------------------------------------------------------

    bool toolAvailable(const std::string& command) {
        const std::string logPath = "include_probe.log";
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

    struct CRun {
        bool compiled = false;
        int exitCode = 0;
        std::string diagnostics;
    };

    // C 源码落盘 → 编译 → 运行（退出码原始值，调用方按低 8 位归一化比较）
    CRun compileAndRunC(const std::string& cSource, const std::string& base) {
        CRun outcome;
        const std::string cPath = base + ".c";
        const std::string exePath = base + ".exe";
        const std::string logPath = base + ".log";

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

        outcome.exitCode = std::system(exePath.c_str());
        outcome.compiled = true;

        std::remove(cPath.c_str());
        std::remove(exePath.c_str());
        std::remove(logPath.c_str());
        return outcome;
    }

    // -----------------------------------------------------------------------
    // e2e 共用头文件（合法 C：可被 C 编译器单独编译）
    // -----------------------------------------------------------------------

    const char* kMyLibH = "#ifndef MYLIB_H\n"
                          "#define MYLIB_H\n"
                          "\n"
                          "struct Point {\n"
                          "    int x;\n"
                          "    int y;\n"
                          "};\n"
                          "\n"
                          "typedef struct Point PointT;\n"
                          "\n"
                          "enum Color {\n"
                          "    COLOR_RED,\n"
                          "    COLOR_GREEN = 10,\n"
                          "    COLOR_BLUE\n"
                          "};\n"
                          "\n"
                          "#define MAX_SCALE 40\n"
                          "#define BASE (MAX_SCALE + 2)\n"
                          "\n"
                          "int area(struct Point p);\n"
                          "int scale(int v, int f);\n"
                          "unsigned mix(unsigned n, const int k);\n"
                          "int widen(int v);\n"
                          "int sum(int arr[], int n);\n"
                          "int noop(void);\n"
                          "int named(int, int);\n"
                          "\n"
                          "#include \"sub/inner.h\"\n"
                          "\n"
                          "#endif // MYLIB_H\n";

    const char* kInnerH = "#pragma once\n"
                          "int inner(void);\n";

    // 原型的全部实现（与头文件签名兼容；原型+定义合并）
    const char* kImplNc = "export int area(struct Point p) {\n"
                          "    return p.x * p.y;\n"
                          "}\n"
                          "export int scale(int v, int f) {\n"
                          "    return v * f;\n"
                          "}\n"
                          "export int mix(int n, int k) {\n"
                          "    return n + k;\n"
                          "}\n"
                          "export int widen(int v) {\n"
                          "    return v + 1;\n"
                          "}\n"
                          "export int sum(int* arr, int n) {\n"
                          "    int s = 0;\n"
                          "    for (int i = 0; i < n; i = i + 1) {\n"
                          "        s = s + arr[i];\n"
                          "    }\n"
                          "    return s;\n"
                          "}\n"
                          "export int noop() {\n"
                          "    return 7;\n"
                          "}\n"
                          "export int named(int a, int b) {\n"
                          "    return a - b;\n"
                          "}\n"
                          "export int inner() {\n"
                          "    return 3;\n"
                          "}\n";

    // 12 + 10 + 3 + 10 + 6 + 7 + 6 + 3 + 42 + 11 - 10 + 0 = 100
    const char* kMainNc =
      "import \"impl.nc\";\n"
      "#include \"mylib.h\"\n"
      "\n"
      "int main() {\n"
      "    struct Point p;\n"
      "    p.x = 3;\n"
      "    p.y = 4;\n"
      "    PointT q;\n"
      "    q.x = 2;\n"
      "    q.y = 5;\n"
      "    int values[3];\n"
      "    values[0] = 1;\n"
      "    values[1] = 2;\n"
      "    values[2] = 3;\n"
      "    int total = area(p) + scale(q.x, q.y) + mix(1, 2) + widen(9)\n"
      "              + sum(values, 3) + noop() + named(10, 4) + inner()\n"
      "              + BASE + COLOR_BLUE - COLOR_GREEN + COLOR_RED;\n"
      "    return total;\n"
      "}\n";

    std::string writeMyLibProject(TempProject& project) {
        project.write("mylib.h", kMyLibH);
        project.write("sub/inner.h", kInnerH);
        project.write("impl.nc", kImplNc);
        return project.write("main.nc", kMainNc);
    }

} // namespace

// ---------------------------------------------------------------------------
// 预处理正例：guard / pragma once / 幂等 / 宏 / enum / 固定宽度类型
// ---------------------------------------------------------------------------

TEST(LoaderIncludeTest, GuardAndPragmaOnceAccepted) {
    TempProject project("guard_pragma");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"guarded.h\"\n"
                                               "#include \"once.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return guarded() + once();\n"
                                               "}\n");
    project.write("guarded.h",
                  "#ifndef GUARDED_H\n"
                  "#define GUARDED_H\n"
                  "int guarded(void);\n"
                  "#endif // GUARDED_H\n");
    project.write("once.h",
                  "#pragma once\n"
                  "int once(void);\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    SemanticResult semantic = analyzeLoaded(loaded, mainPath);
    ASSERT_FALSE(semantic.hasErrors());

    // 两个原型都进符号表（isPrototype 标记，摘要收尾补发）
    bool sawGuarded = false;
    bool sawOnce = false;
    for (const auto& symbol : semantic.globals) {
        if (symbol.name == "guarded") {
            sawGuarded = true;
            EXPECT_TRUE(symbol.isPrototype);
        }
        if (symbol.name == "once") {
            sawOnce = true;
            EXPECT_TRUE(symbol.isPrototype);
        }
    }
    EXPECT_TRUE(sawGuarded);
    EXPECT_TRUE(sawOnce);

    // 头文件进依赖清单（-MMD 增量追踪；显示路径 = 头文件所在目录相对形式）
    const bool guardedInOrder = std::any_of(
      loaded.loadOrder.begin(),
      loaded.loadOrder.end(),
      [](const std::string& path) {
          return path.size() >= 9 && path.substr(path.size() - 9) == "guarded.h";
      });
    const bool onceInOrder =
      std::any_of(loaded.loadOrder.begin(),
                  loaded.loadOrder.end(),
                  [](const std::string& path) {
                      return path.size() >= 6 && path.substr(path.size() - 6) == "once.h";
                  });
    EXPECT_TRUE(guardedInOrder);
    EXPECT_TRUE(onceInOrder);
}

TEST(LoaderIncludeTest, RepeatedIncludeIdempotent) {
    TempProject project("repeat_include");
    // 同一头文件被 .nc 两次 include 且经头文件间接第三次 include：声明只拼
    // 接一次（struct/typedef 重复登记会报 redefinition，故能装载即幂等）
    const std::string mainPath = project.write("main.nc",
                                               "#include \"pt.h\"\n"
                                               "#include \"pt.h\"\n"
                                               "#include \"wrapper.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    struct Pt p;\n"
                                               "    p.x = 20;\n"
                                               "    return p.x;\n"
                                               "}\n");
    project.write("pt.h",
                  "#ifndef PT_H\n"
                  "#define PT_H\n"
                  "struct Pt {\n"
                  "    int x;\n"
                  "};\n"
                  "typedef struct Pt PtT;\n"
                  "#endif\n");
    project.write("wrapper.h", "#include \"pt.h\"\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    SemanticResult semantic = analyzeLoaded(loaded, mainPath);
    EXPECT_FALSE(semantic.hasErrors()) << [&] {
        std::string text;
        for (const auto& d : semantic.diagnostics) {
            text += d.toString() + "\n";
        }
        return text;
    }();
}

TEST(LoaderIncludeTest, CircularIncludeIdempotent) {
    TempProject project("circular_include");
    // a.h ↔ b.h 循环 include：幂等跳过不报错（与 C guard 语义一致）
    const std::string mainPath = project.write("main.nc",
                                               "#include \"a.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return from_a() + from_b();\n"
                                               "}\n");
    project.write("a.h",
                  "#ifndef A_H\n"
                  "#define A_H\n"
                  "#include \"b.h\"\n"
                  "int from_a(void);\n"
                  "#endif\n");
    project.write("b.h",
                  "#ifndef B_H\n"
                  "#define B_H\n"
                  "#include \"a.h\"\n"
                  "int from_b(void);\n"
                  "#endif\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    SemanticResult semantic = analyzeLoaded(loaded, mainPath);
    EXPECT_FALSE(semantic.hasErrors());
}

TEST(LoaderIncludeTest, ObjectMacroAndEnumConstants) {
    TempProject project("macro_enum");
    const std::string mainPath =
      project.write("main.nc",
                    "#include \"cfg.h\"\n"
                    "\n"
                    "int main() {\n"
                    "    int buf[BUF_SIZE];\n"
                    "    buf[0] = MODE_B - MODE_A;\n"
                    "    return buf[0] + DOUBLE_OF_TWO + MODE_B;\n"
                    "}\n");
    project.write("cfg.h",
                  "#ifndef CFG_H\n"
                  "#define CFG_H\n"
                  "#define BUF_SIZE 4\n"
                  "#define DOUBLE_OF_TWO (1 + 1)\n"
                  "enum Mode { MODE_A, MODE_B = 5 };\n"
                  "typedef enum Mode ModeT;\n"
                  "#endif\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;

    VmRun run = runOnVm(loaded, mainPath);
    ASSERT_TRUE(run.ran) << run.diagnostics;
    // MODE_B - MODE_A = 5；+ DOUBLE_OF_TWO(2) + MODE_B(5) = 12
    EXPECT_EQ(run.r0, 12);
}

TEST(LoaderIncludeTest, HeaderUsesFixedWidthAndQualifiers) {
    TempProject project("fixed_width");
    const std::string mainPath = project.write("main.nc",
                                               "import \"impl.nc\";\n"
                                               "#include \"cw.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return narrow(300) + hold(5);\n"
                                               "}\n");
    project.write("cw.h",
                  "#ifndef CW_H\n"
                  "#define CW_H\n"
                  "int32_t narrow(int16_t v);\n"
                  "const unsigned hold(const volatile unsigned n);\n"
                  "#endif\n");
    // 实现定义在 .nc（与原型合并：固定宽度/修饰符折叠后签名兼容）
    project.write("impl.nc",
                  "export int narrow(int v) {\n"
                  "    return v - 297;\n"
                  "}\n"
                  "export int hold(int n) {\n"
                  "    return n;\n"
                  "}\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;

    VmRun run = runOnVm(loaded, mainPath);
    ASSERT_TRUE(run.ran) << run.diagnostics;
    EXPECT_EQ(run.r0, 8); // (300-297) + 5
}

// ---------------------------------------------------------------------------
// 预处理负例：识别后报"不支持"，不误编译
// ---------------------------------------------------------------------------

TEST(LoaderIncludeNegativeTest, FunctionMacroRejected) {
    TempProject project("neg_fn_macro");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"bad.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return 0;\n"
                                               "}\n");
    project.write("bad.h",
                  "#ifndef BAD_H\n"
                  "#define BAD_H\n"
                  "#define ADD(a, b) ((a) + (b))\n"
                  "#endif\n");

    LoadOutcome loaded = loadEntry(mainPath);
    EXPECT_FALSE(loaded.ok);
    EXPECT_NE(loaded.diagnostics.find("function-like macros are not supported"),
              std::string::npos)
      << loaded.diagnostics;
}

TEST(LoaderIncludeNegativeTest, ConditionalCompilationRejected) {
    // guard 形态（#ifndef/#define/#endif）之外的条件编译一律报"不支持"
    const char* fixtures[] = {
        "#if 1\nint f1(void);\n#endif\n",
        "#ifdef X\nint f2(void);\n#endif\n",
        "#ifndef Y\nint f3(void);\n#endif\n",
        "#else\nint f4(void);\n#endif\n",
    };
    for (const char* content : fixtures) {
        TempProject project("neg_cond");
        const std::string mainPath =
          project.write("main.nc", "#include \"c.h\"\nint main() { return 0; }\n");
        project.write("c.h", content);

        LoadOutcome loaded = loadEntry(mainPath);
        EXPECT_FALSE(loaded.ok) << content;
        EXPECT_NE(loaded.diagnostics.find("conditional compilation is not supported"),
                  std::string::npos)
          << content << ":\n"
          << loaded.diagnostics;
    }
}

TEST(LoaderIncludeNegativeTest, MissingHeaderRejected) {
    TempProject project("neg_missing");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"nope.h\"\n"
                                               "int main() {\n"
                                               "    return 0;\n"
                                               "}\n");

    LoadOutcome loaded = loadEntry(mainPath);
    EXPECT_FALSE(loaded.ok);
    EXPECT_NE(loaded.diagnostics.find("cannot find header 'nope.h'"), std::string::npos)
      << loaded.diagnostics;
}

TEST(LoaderIncludeNegativeTest, AngleIncludeRejected) {
    TempProject project("neg_angle");
    const std::string mainPath = project.write("main.nc",
                                               "#include <stdio.h>\n"
                                               "int main() {\n"
                                               "    return 0;\n"
                                               "}\n");

    LoadOutcome loaded = loadEntry(mainPath);
    EXPECT_FALSE(loaded.ok);
    EXPECT_NE(loaded.diagnostics.find("only quoted #include"), std::string::npos)
      << loaded.diagnostics;
}

TEST(LoaderIncludeNegativeTest, UnsupportedPragmaRejected) {
    TempProject project("neg_pragma");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"packed.h\"\n"
                                               "int main() {\n"
                                               "    return 0;\n"
                                               "}\n");
    project.write("packed.h", "#pragma pack(1)\nint f(void);\n");

    LoadOutcome loaded = loadEntry(mainPath);
    EXPECT_FALSE(loaded.ok);
    EXPECT_NE(loaded.diagnostics.find("unsupported pragma"), std::string::npos)
      << loaded.diagnostics;
}

TEST(LoaderIncludeNegativeTest, DefineOnLanguageKeywordIgnored) {
    // 决策记录：#define 宏名与 NanoC 关键字冲突（NULL）时忽略该行——语言
    // 关键字不可被文本替换，静默跳过比报错更接近 C 的无害重定义
    TempProject project("neg_kw_define");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"nulldef.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    int* p = NULL;\n"
                                               "    return 3;\n"
                                               "}\n");
    project.write("nulldef.h",
                  "#ifndef NULLDEF_H\n"
                  "#define NULLDEF_H\n"
                  "#define NULL ((void*)0)\n"
                  "int unused(void);\n"
                  "#endif\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    VmRun run = runOnVm(loaded, mainPath);
    ASSERT_TRUE(run.ran) << run.diagnostics;
    EXPECT_EQ(run.r0, 3); // NULL 仍是语言关键字（Null 字面量），赋指针合法
}

// ---------------------------------------------------------------------------
// Parser 头文件模式（直接驱动）
// ---------------------------------------------------------------------------

std::unique_ptr<Program> parseHeaderSnippet(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize(), "snippet.h");
    parser.setHeaderMode(true);
    return parser.parse();
}

TEST(ParserHeaderModeTest, PrototypeParsesWithoutBody) {
    auto program = parseHeaderSnippet("int add(int a, int b);\n");
    ASSERT_EQ(program->declarations.size(), 1u);
    const auto* func =
      static_cast<const FuncDeclaration*>(program->declarations[0].get());
    EXPECT_TRUE(func->isPrototype);
    EXPECT_EQ(func->name, "add");
    EXPECT_EQ(func->body, nullptr);
    ASSERT_EQ(func->parameters.size(), 2u);
}

TEST(ParserHeaderModeTest, PrototypeVariants) {
    // (void) 空参表 / 无名形参 / 数组形参退化 / varargs / 指针返回
    auto program = parseHeaderSnippet("int f(void);\n"
                                      "int g(int, int);\n"
                                      "int h(int arr[], int n);\n"
                                      "char* getenv(char* name);\n"
                                      "int printf(char* fmt, ...);\n");
    ASSERT_EQ(program->declarations.size(), 5u);

    const auto* g = static_cast<const FuncDeclaration*>(program->declarations[1].get());
    EXPECT_EQ(g->parameters.size(), 2u);
    EXPECT_EQ(g->parameters[0]->name, "");

    const auto* h = static_cast<const FuncDeclaration*>(program->declarations[2].get());
    ASSERT_EQ(h->parameters.size(), 2u);
    EXPECT_EQ(h->parameters[0]->pointerDepth, 1); // int arr[] → int*

    const auto* getenvFn =
      static_cast<const FuncDeclaration*>(program->declarations[3].get());
    EXPECT_EQ(getenvFn->returnPointerDepth, 1);

    const auto* printfFn =
      static_cast<const FuncDeclaration*>(program->declarations[4].get());
    EXPECT_TRUE(printfFn->isVariadic);
}

TEST(ParserHeaderModeTest, QualifierChainsSemanticallyIgnored) {
    auto program = parseHeaderSnippet("const unsigned long counter = 3;\n"
                                      "static inline int fast(const int v);\n");
    ASSERT_EQ(program->declarations.size(), 2u);

    const auto* counter =
      static_cast<const VarDeclaration*>(program->declarations[0].get());
    EXPECT_EQ(counter->type, "int"); // 修饰符折叠为基础类型
    EXPECT_EQ(counter->name, "counter");

    const auto* fast =
      static_cast<const FuncDeclaration*>(program->declarations[1].get());
    EXPECT_TRUE(fast->isPrototype);
}

TEST(ParserHeaderModeTest, FloatingPointRejected) {
    Lexer lexer("float ratio(void);\n");
    Parser parser(lexer.tokenize(), "fp.h");
    parser.setHeaderMode(true);
    EXPECT_THROW(parser.parse(), ParseError);
}

TEST(ParserHeaderModeTest, LanguageProperStillRejectsPrototypes) {
    // 回归边界：.nc（非头文件模式）不接受无函数体原型（语言本体不变）
    Lexer lexer("int add(int a, int b);\n");
    Parser parser(lexer.tokenize(), "plain.nc");
    EXPECT_THROW(parser.parse(), ParseError);
}

// ---------------------------------------------------------------------------
// 语义：原型 + 定义合并（C 原型语义）
// ---------------------------------------------------------------------------

TEST(SemanticPrototypeTest, PrototypeMergedWithDefinition) {
    TempProject project("proto_def_merge");
    const std::string mainPath = writeMyLibProject(project);

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    SemanticResult semantic = analyzeLoaded(loaded, mainPath);
    ASSERT_FALSE(semantic.hasErrors());

    // area 在头文件有原型（impl.nc 定义）：合并为一条符号，definedIn =
    // 定义所在文件，不再带原型标记
    int areaEntries = 0;
    for (const auto& symbol : semantic.globals) {
        if (symbol.name == "area") {
            ++areaEntries;
            EXPECT_EQ(symbol.definedIn, "impl.nc");
            EXPECT_FALSE(symbol.isPrototype);
            EXPECT_TRUE(symbol.isExported);
        }
    }
    EXPECT_EQ(areaEntries, 1);
}

TEST(SemanticPrototypeTest, UnresolvedPrototypeStaysPrototype) {
    TempProject project("proto_only");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"only.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return helper(2);\n"
                                               "}\n");
    project.write("only.h",
                  "#ifndef ONLY_H\n"
                  "#define ONLY_H\n"
                  "int helper(int v);\n"
                  "#endif\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    SemanticResult semantic = analyzeLoaded(loaded, mainPath);
    ASSERT_FALSE(semantic.hasErrors());

    bool sawHelper = false;
    for (const auto& symbol : semantic.globals) {
        if (symbol.name == "helper") {
            sawHelper = true;
            EXPECT_TRUE(symbol.isPrototype);
            // 头文件声明 definedIn 置空 = 全单元可见（C 翻译单元口径）
            EXPECT_TRUE(symbol.definedIn.empty());
        }
    }
    EXPECT_TRUE(sawHelper);
}

TEST(SemanticPrototypeTest, ConflictingPrototypeRejected) {
    TempProject project("proto_conflict");
    const std::string mainPath = project.write("main.nc",
                                               "import \"impl.nc\";\n"
                                               "#include \"conf.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return 0;\n"
                                               "}\n");
    project.write("conf.h", "int pick(int v);\n");
    project.write("impl.nc",
                  "export int pick(int* v) {\n" // 签名不兼容
                  "    return 1;\n"
                  "}\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;
    SemanticResult semantic = analyzeLoaded(loaded, mainPath);
    EXPECT_TRUE(semantic.hasErrors());
    bool sawConflict = false;
    for (const auto& diagnostic : semantic.diagnostics) {
        if (diagnostic.message.find("conflicting types for 'pick'")
            != std::string::npos) {
            sawConflict = true;
        }
    }
    EXPECT_TRUE(sawConflict);
}

// ---------------------------------------------------------------------------
// e2e：.nc + .h 全链路（VM 断言 R0；未解析原型走宿主；C 后端真编译差分）
// ---------------------------------------------------------------------------

TEST(IncludeE2ETest, HeaderDeclarationsDriveCallAndMemberAccess) {
    TempProject project("e2e_full");
    const std::string mainPath = writeMyLibProject(project);

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;

    VmRun run = runOnVm(loaded, mainPath);
    ASSERT_TRUE(run.ran) << run.diagnostics;
    // area(3,4)=12 + scale(2,5)=10 + mix(1,2)=3 + widen(9)=10 + sum=6
    // + noop=7 + named(10,4)=6 + inner=3 + BASE=42 + BLUE(11) - GREEN(10)
    // + RED(0) = 100
    EXPECT_EQ(run.r0, 100);
}

#ifdef _WIN32

TEST(IncludeE2ETest, UnresolvedPrototypeResolvedByHostLibrary) {
    // extern 路径（PRD R3 汇合点）：头文件原型无单元内定义 → callx →
    // msvcrt 宿主解析
    TempProject project("e2e_host");
    const std::string mainPath = project.write("main.nc",
                                               "#include \"math.h\"\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return abs(0 - 7);\n"
                                               "}\n");
    project.write("math.h",
                  "#ifndef MATH_H\n"
                  "#define MATH_H\n"
                  "int abs(int x);\n"
                  "#endif\n");

    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;

    const std::string nciPath =
      (std::filesystem::temp_directory_path() / "include_host.nci").string();
    try {
        SemanticAnalyzer analyzer(mainPath);
        auto analyzed = analyzer.analyze(*loaded.program);
        ASSERT_FALSE(analyzed.is_err());
        ASSERT_FALSE(analyzed.unwrap().hasErrors());

        auto lowered = ir::lower(*loaded.program);
        ASSERT_FALSE(lowered.is_err());
        ir::Module module = std::move(lowered).unwrap();
        LinkageTable linkage = buildLinkageTable(*loaded.program);
        CodeGenerator codegen;
        const std::string assembly = codegen.generate(module, &linkage);

        // 未解析原型以 extern 指令落汇编头（宿主动态导入）
        EXPECT_NE(assembly.find("\nextern abs\n"), std::string::npos);

        const AssemblyResult assembled = Assembler::assemble(assembly);
        ASSERT_TRUE(assembled.ok) << assembled.errorMessage;
        {
            std::ofstream ofs(nciPath, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(assembled.image.data()),
                      static_cast<std::streamsize>(assembled.image.size()));
        }
        NVirtualMachine vm(8 * 1024 * 1024);
        vm.load(nciPath);
        ASSERT_TRUE(vm.loadHostLibrary("msvcrt.dll"));
        vm.start();
        EXPECT_EQ(vm.getRegister(0), 7);
    } catch (const std::exception& e) {
        ADD_FAILURE() << "exception: " << e.what();
    }
    std::remove(nciPath.c_str());
}

#endif // _WIN32

TEST(IncludeE2ETest, CBackendDifferentialWithSameHeader) {
    // C 后端差分：同一 .nc + .h，emit → C 编译器真编译 → 运行，退出码与
    // VM R0 低 8 位一致。头文件是合法 C：先单独过 C 编译器（两边共用）
    if (cCompilerCommand().empty()) {
        GTEST_SKIP() << "no C compiler available (probe NANOC_C_COMPILER, clang, gcc)";
    }

    TempProject project("e2e_diff");
    const std::string mainPath = writeMyLibProject(project);

    // 1) 头文件本身是合法 C：驱动 .c 只 include 并空跑 main
    const CRun headerCheck =
      compileAndRunC("#include \"mylib.h\"\nint main(void) { return 0; }\n",
                     project.pathOf("driver"));
    EXPECT_TRUE(headerCheck.compiled) << headerCheck.diagnostics;

    // 2) 同一编译单元走 C 后端真编译
    LoadOutcome loaded = loadEntry(mainPath);
    ASSERT_TRUE(loaded.ok) << loaded.diagnostics;

    VmRun vm = runOnVm(loaded, mainPath);
    ASSERT_TRUE(vm.ran) << vm.diagnostics;

    SemanticAnalyzer analyzer(mainPath);
    auto analyzed = analyzer.analyze(*loaded.program);
    ASSERT_FALSE(analyzed.is_err());
    ASSERT_FALSE(analyzed.unwrap().hasErrors());
    auto lowered = ir::lower(*loaded.program);
    ASSERT_FALSE(lowered.is_err());
    ir::Module module = std::move(lowered).unwrap();
    const std::string cSource = c_backend::emit(module);

    const CRun cRun = compileAndRunC(cSource, "include_diff_c");
    ASSERT_TRUE(cRun.compiled) << cRun.diagnostics;
    EXPECT_EQ(cRun.exitCode & 0xFF, vm.r0 & 0xFF);
}
