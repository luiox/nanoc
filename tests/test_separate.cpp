#include "nas/instruction.hpp"
#include "nas/linker.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/loader.hpp"
#include "ncc/semantic.hpp"
#include "nvm/core.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// 独立编译 + 链接 e2e（PRD R7 M4 第一步，ncc 侧）：
//   每模块独立编译（Loader::loadStandalone 轻装载 → semantic → ir::lower →
//   CodeGenerator 携导出/导入信息 → Assembler::assemble 产 .nci 目标）→
//   Linker（nas -r 同一实现）链接多目标 → NVirtualMachine 执行断言 R0。
//
// 覆盖：两模块函数调用、跨模块数据（导出全局变量单存储别名）、未导出符号
// 边界（编译期签名级拦截 + 手写 extern 越过时链接/加载期兜底）、三模块链式
// 依赖、增量编译演示（改 main 只重编 main，math.nci 字节不变复用）。
//
// 断言口径与 test_multifile.cpp 一致（main 返回值在 R0、哨兵正常停机）。
//
// 与 CLI 的关系：本期独立编译以 API + 测试驱动（CLI 旗标接线下一轮统一做），
// compileStandalone 即未来 `ncc --separate` 的接线原型。

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

    // 临时工程目录：按测试名隔离，析构时整体删除
    struct TempProject {
        std::filesystem::path dir;

        explicit TempProject(const std::string& name) {
            namespace fs = std::filesystem;
            dir = fs::temp_directory_path() / "nanoc_separate_tests" / name;
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
        }

        ~TempProject() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }

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

    // 单模块独立编译产物：汇编文本 + .nci 目标镜像
    struct StandaloneObject {
        bool ok = false;
        std::string diagnostics;
        std::string assembly;
        std::vector<uint8_t> image;
    };

    // 独立编译管线（未来 CLI --separate 的接线原型）：
    // 轻装载（依赖导出签名注入）→ 语义 → IR → 代码生成（导出行 + 导入引用）
    // → 汇编为 .nci 目标文件
    StandaloneObject compileStandalone(const std::string& entryPath) {
        StandaloneObject object;
        try {
            Loader loader({ entryPath });
            StandaloneResult loaded = loader.loadStandalone(entryPath);
            for (const auto& diagnostic : loaded.diagnostics) {
                object.diagnostics += diagnostic.toString() + "\n";
            }
            if (!loaded.ok) {
                return object;
            }

            SemanticAnalyzer analyzer(entryPath);
            auto analyzed = analyzer.analyze(*loaded.program);
            if (analyzed.is_err()) {
                object.diagnostics += "error: " + analyzed.unwrap_err() + "\n";
                return object;
            }
            for (const auto& diagnostic : analyzed.unwrap().diagnostics) {
                object.diagnostics += diagnostic.toString() + "\n";
            }
            if (analyzed.unwrap().hasErrors()) {
                return object;
            }

            auto lowered = ir::lower(*loaded.program);
            if (lowered.is_err()) {
                object.diagnostics += "error: " + lowered.unwrap_err() + "\n";
                return object;
            }
            ir::Module module = std::move(lowered).unwrap(); // Module 只移动

            // 多文件 mangle 所需的顶层符号链接信息（合成导入声明经 AST 的
            // isImported 携带导入标记，见 LinkageEntry 注释）
            LinkageTable linkage = buildLinkageTable(*loaded.program);

            CodeGenerator codegen;
            object.assembly = codegen.generate(module, &linkage);

            AssemblyResult assembled = Assembler::assemble(object.assembly);
            if (!assembled.ok) {
                object.diagnostics += "assemble failed at line "
                                      + std::to_string(assembled.errorLine) + ": "
                                      + assembled.errorMessage + "\n";
                return object;
            }
            object.image = std::move(assembled.image);
            object.ok = true;
        } catch (const std::exception& e) {
            object.diagnostics += std::string("exception: ") + e.what() + "\n";
        }
        return object;
    }

    // 链接并运行：nas -r 同一链接器实现 + VM 执行，返回停机快照
    VmSnapshot linkAndRun(const std::vector<std::vector<uint8_t>>& images,
                          const std::string& nciPath) {
        LinkResult linked = Linker::linkImages(images);
        if (!linked.ok) {
            ADD_FAILURE() << "link failed: " << linked.errorMessage;
            return {};
        }
        {
            std::ofstream ofs(nciPath, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(linked.image.data()),
                      (std::streamsize)linked.image.size());
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
        return snap;
    }

    // 正常停机断言（口径与 golden e2e 一致）
    void expectNormalTermination(const VmSnapshot& snap, int32_t expectedR0) {
        EXPECT_EQ(snap.r0, expectedR0);
        EXPECT_EQ(snap.pc, snap.codeSize);  // 停在代码段末尾（哨兵地址）
        EXPECT_EQ(snap.sp, snap.stackSize); // main 的 ret 已消费栈底哨兵
        EXPECT_EQ(snap.bp, 0);              // leave 已恢复 BP
    }

    // 链接顺序（历史约定，已不再承载语义）：入口模块放首位。早期 VM 的 CALLX 以
    // addr > 0 判定内部地址（0 同时是旧格式动态导入的哨兵值），落在绝对地址 0 的
    // 跨模块目标会被拒跳，故以"入口对象放最前"惯例规避（C 工具链同款）；#54 根修
    // 后链接器把解析为内部符号的 CALLX 直接改写为 CALL 直调，任意链接顺序均正确，
    // 首位约定仅为产物布局稳定保留

    // math.nc（依赖模块，含私有符号）：测试组之间复用的标准形态。
    // 注意不用带初始化器的全局变量承载语义：依赖模块的全局初始化器留在其
    // 自身产物的缺省 main 桩里，链接产物只从导出 main 进入——初始化不跨模块
    // 执行（PRD R7 一期边界，见 Loader::loadStandalone 注释），私有逻辑用
    // 纯函数承载
    const char* kMathNc = "int adjust(int v) {\n"
                          "    return v * 2;\n"
                          "}\n"
                          "\n"
                          "export int add(int a, int b) {\n"
                          "    return adjust(a + b);\n"
                          "}\n";

} // namespace

// 两模块程序独立编译 + 链接后运行正确（PRD R7 一期验收主路径）：
// math.nc 独立编译为带导出表的 .nci 目标；main.nc 独立编译时经轻装载拿到
// add 的签名（合成 extern 声明），调用点发射 callx + 汇编头 extern 行；
// nas -r 链接把动态导入解析到 math.nci 的导出地址。
TEST(SeparateCompileTest, TwoModuleFunctionCall) {
    TempProject project("two_module");
    const std::string mathPath = project.write("math.nc", kMathNc);
    const std::string mainPath = project.write(
      "main.nc",
      "import math;\n"
      "\n"
      "int main() {\n"
      "    return add(2, 3); // adjust(5) = 10（私有 adjust 在 math.nci 内部解析）\n"
      "}\n");

    StandaloneObject mathObj = compileStandalone(mathPath);
    ASSERT_TRUE(mathObj.ok) << mathObj.diagnostics;
    StandaloneObject mainObj = compileStandalone(mainPath);
    ASSERT_TRUE(mainObj.ok) << mainObj.diagnostics;

    // math.nci：导出 add；私有 adjust 保持模块内部（独立编译是单文件语义，
    // 私有符号本来就在本模块解析，无需 mangle）
    EXPECT_NE(mathObj.assembly.find("\nexport add\n"), std::string::npos);
    EXPECT_NE(mathObj.assembly.find("\nadd:"), std::string::npos);
    EXPECT_NE(mathObj.assembly.find("\nadjust:"), std::string::npos);
    EXPECT_EQ(mathObj.assembly.find("callx"), std::string::npos);

    // main.nci：add 经签名合成可见，调用发射 callx + 汇编头 extern 行；
    // math 的私有符号不在 main 的编译单元（轻装载不合并声明）
    EXPECT_NE(mainObj.assembly.find("\nextern add\n"), std::string::npos);
    EXPECT_NE(mainObj.assembly.find("    callx add\n"), std::string::npos);
    EXPECT_EQ(mainObj.assembly.find("adjust"), std::string::npos);

    // API 级断言：imports 只含依赖的导出签名（add），loadOrder 依赖在前
    {
        Loader loader({ mainPath });
        StandaloneResult loaded = loader.loadStandalone(mainPath);
        ASSERT_TRUE(loaded.ok);
        ASSERT_EQ(loaded.imports.size(), 1u);
        EXPECT_EQ(loaded.imports[0].name, "add");
        EXPECT_TRUE(loaded.imports[0].isFunction);
        EXPECT_EQ(loaded.imports[0].module, "math.nc");
        EXPECT_EQ(loaded.imports[0].returnType, "int");
        ASSERT_EQ(loaded.imports[0].params.size(), 2u);
        // 单模块边界：合成 extern add + main 自身，共 2 条声明
        EXPECT_EQ(loaded.program->declarations.size(), 2u);
        ASSERT_EQ(loaded.loadOrder.size(), 2u);
        EXPECT_EQ(loaded.loadOrder[0], "math.nc");
        EXPECT_EQ(loaded.loadOrder[1], "main.nc");
        // 语义符号摘要：add 以 extern 身份登记（来自依赖签名而非源码 extern 声明）
        SemanticAnalyzer analyzer(mainPath);
        auto analyzed = analyzer.analyze(*loaded.program);
        ASSERT_FALSE(analyzed.is_err());
        bool addExtern = false;
        for (const auto& symbol : analyzed.unwrap().globals) {
            if (symbol.name == "add") {
                addExtern = symbol.isExtern;
            }
        }
        EXPECT_TRUE(addExtern);
    }

    VmSnapshot snap =
      linkAndRun({ mainObj.image, mathObj.image }, project.pathOf("linked.nci"));
    expectNormalTermination(snap, 10);
}

// 跨模块数据（PRD R7：export 全局变量同测）：main 写 counter 后经 math 的
// 导出函数读回同值——证明引用经导入表解析到提供模块的同一存储（若各自落
// 数据段副本，get() 读到的仍是 40 而非 42）。
TEST(SeparateCompileTest, CrossModuleGlobalAliasing) {
    TempProject project("cross_data");
    const std::string dataPath = project.write("data.nc",
                                               "export int counter;\n"
                                               "\n"
                                               "export int get() {\n"
                                               "    return counter;\n"
                                               "}\n"
                                               "\n"
                                               "export void set(int v) {\n"
                                               "    counter = v;\n"
                                               "}\n");
    const std::string mainPath =
      project.write("main.nc",
                    "import data;\n"
                    "\n"
                    "int main() {\n"
                    "    set(40);\n"
                    "    counter = counter + 2; // 经导入表写 data.nci 的 counter\n"
                    "    return get();          // 42：读写同址（单存储别名）\n"
                    "}\n");

    StandaloneObject dataObj = compileStandalone(dataPath);
    ASSERT_TRUE(dataObj.ok) << dataObj.diagnostics;
    StandaloneObject mainObj = compileStandalone(mainPath);
    ASSERT_TRUE(mainObj.ok) << mainObj.diagnostics;

    // data.nci：数据标号导出
    EXPECT_NE(dataObj.assembly.find("\nexport .g_counter\n"), std::string::npos);
    EXPECT_NE(dataObj.assembly.find("\n.g_counter:"), std::string::npos);
    // main.nci：counter 为导入符号——汇编头 extern 行、本模块不落数据段
    EXPECT_NE(mainObj.assembly.find("\nextern .g_counter\n"), std::string::npos);
    EXPECT_EQ(mainObj.assembly.find(".g_counter:"), std::string::npos);
    EXPECT_NE(mainObj.assembly.find("    lea R6, .g_counter\n"), std::string::npos);

    VmSnapshot snap =
      linkAndRun({ mainObj.image, dataObj.image }, project.pathOf("linked.nci"));
    expectNormalTermination(snap, 42);
}

// 未导出符号边界（记录在案，PRD R7 一期语义边界）：
// - 编译期只做签名级检查：math 的私有 adjust 不在依赖签名集内，独立编译
//   main 直接报 undeclared（编译期拦截）。
TEST(SeparateCompileTest, UnexportedSymbolRejectedAtCompileTime) {
    TempProject project("unexported");
    const std::string mathPath = project.write("math.nc", kMathNc);
    const std::string mainPath =
      project.write("main.nc",
                    "import math;\n"
                    "\n"
                    "int main() {\n"
                    "    return adjust(1); // 私有符号：轻装载不注入签名\n"
                    "}\n");

    StandaloneObject mainObj = compileStandalone(mainPath);
    EXPECT_FALSE(mainObj.ok);
    EXPECT_NE(mainObj.diagnostics.find("adjust"), std::string::npos);
    // 依赖模块本身仍可独立编译（私有形态合法）
    StandaloneObject mathObj = compileStandalone(mathPath);
    EXPECT_TRUE(mathObj.ok) << mathObj.diagnostics;
}

// 未导出符号边界（续）：源码手写 extern 声明可越过签名级检查（PRD R3 语义），
// 此时编译/汇编/链接均成功（nas -r 对未内部解析的动态导入保留在导入表），
// 失败在 VM 执行期暴露：CALLX 落到未解析伪地址，报告后停机——链接产物不可
// 静默错链。这是 NCI v2.1 无签名导出表下"编译期不拦截、链接期兜底"的边界。
TEST(SeparateCompileTest, ForcedExternReferenceFailsAtLinkStage) {
    TempProject project("forced_extern");
    const std::string mathPath = project.write("math.nc", kMathNc);
    const std::string mainPath =
      project.write("main.nc",
                    "import math;\n"
                    "\n"
                    "extern int adjust(int v);\n"
                    "\n"
                    "int main() {\n"
                    "    return adjust(1); // 手写 extern 强行引用 math 私有符号\n"
                    "}\n");

    StandaloneObject mathObj = compileStandalone(mathPath);
    ASSERT_TRUE(mathObj.ok) << mathObj.diagnostics;
    StandaloneObject mainObj = compileStandalone(mainPath);
    ASSERT_TRUE(mainObj.ok) << mainObj.diagnostics;

    // nas -r 链接成功：adjust 不在任何模块导出表 → 动态导入保留（宿主解析语义）
    LinkResult linked = Linker::linkImages({ mainObj.image, mathObj.image });
    ASSERT_TRUE(linked.ok) << linked.errorMessage;

    const std::string nciPath = project.pathOf("linked.nci");
    {
        std::ofstream ofs(nciPath, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(linked.image.data()),
                  (std::streamsize)linked.image.size());
    }
    NVirtualMachine vm(8 * 1024 * 1024);
    vm.load(nciPath);
    vm.start(); // CALLX adjust 未解析：报错并停在调用点

    // 停在调用点（pc 到达代码段末尾）但未正常返回：main 帧未撤、R0 保留
    // 调用点已求值的实参（1）——执行未达 return
    EXPECT_EQ(vm.getPC(), vm.getCodeSize());
    EXPECT_EQ(vm.getRegister(0), 1);
    EXPECT_NE(vm.getSP(), vm.getStackSize());
}

// 三模块链式依赖：main → math → helper。math 独立编译时经轻装载注入
// twice 的签名（callx twice），链接时逐模块解析：main 的 add ← math 导出，
// math 的 twice ← helper 导出。
TEST(SeparateCompileTest, ThreeModuleChain) {
    TempProject project("three_chain");
    const std::string helperPath = project.write("helper.nc",
                                                 "export int twice(int v) {\n"
                                                 "    return v + v;\n"
                                                 "}\n");
    const std::string mathPath =
      project.write("math.nc",
                    "import helper;\n"
                    "\n"
                    "export int add3(int a, int b) {\n"
                    "    return twice(a) + b; // 跨模块调用 helper 的导出\n"
                    "}\n");
    const std::string mainPath =
      project.write("main.nc",
                    "import math;\n"
                    "\n"
                    "int main() {\n"
                    "    return add3(2, 3); // twice(2) + 3 = 7\n"
                    "}\n");

    StandaloneObject helperObj = compileStandalone(helperPath);
    ASSERT_TRUE(helperObj.ok) << helperObj.diagnostics;
    StandaloneObject mathObj = compileStandalone(mathPath);
    ASSERT_TRUE(mathObj.ok) << mathObj.diagnostics;
    StandaloneObject mainObj = compileStandalone(mainPath);
    ASSERT_TRUE(mainObj.ok) << mainObj.diagnostics;

    // math.nci：导出 add、导入 twice；main.nci：只依赖 add
    EXPECT_NE(mathObj.assembly.find("\nexport add3\n"), std::string::npos);
    EXPECT_NE(mathObj.assembly.find("\nextern twice\n"), std::string::npos);
    EXPECT_NE(mathObj.assembly.find("    callx twice\n"), std::string::npos);
    EXPECT_EQ(mainObj.assembly.find("twice"), std::string::npos);

    VmSnapshot snap = linkAndRun({ mainObj.image, mathObj.image, helperObj.image },
                                 project.pathOf("linked.nci"));
    expectNormalTermination(snap, 7);
}

// 增量编译演示（PRD R7 验收后半）：改 main.nc 只重编 main 模块，math.nci
// 字节不变直接复用，新旧 main 目标分别与同一 math 目标链接均运行正确。
//
// xmake 增量挂接点（下一轮 toolchain/rule 接线）：每模块一条规则
//   .nc → .nci（ncc --separate），.d 依赖文件（loadOrder 含 import 闭包）
// 交 xmake 追踪——只有依赖变化的模块才重编，目标级缓存即本测试的
// "math.nci 字节不变"。
TEST(SeparateCompileTest, IncrementalRecompileOnlyChangedModule) {
    TempProject project("incremental");
    const std::string mathPath = project.write("math.nc",
                                               "export int add(int a, int b) {\n"
                                               "    return a + b;\n"
                                               "}\n");
    const std::string mainPath = project.write("main.nc",
                                               "import math;\n"
                                               "\n"
                                               "int main() {\n"
                                               "    return add(1, 2);\n"
                                               "}\n");

    // 第一轮：math.nci 与 main.nci(v1)
    StandaloneObject mathObj = compileStandalone(mathPath);
    ASSERT_TRUE(mathObj.ok) << mathObj.diagnostics;
    const std::vector<uint8_t> mathImageV1 = mathObj.image; // 复用基线（字节不变）
    StandaloneObject mainObjV1 = compileStandalone(mainPath);
    ASSERT_TRUE(mainObjV1.ok) << mainObjV1.diagnostics;
    VmSnapshot snapV1 =
      linkAndRun({ mainObjV1.image, mathImageV1 }, project.pathOf("linked_v1.nci"));
    expectNormalTermination(snapV1, 3);

    // 改 main.nc 重编 main 模块；math.nci 不重编（字节不变）
    project.write("main.nc",
                  "import math;\n"
                  "\n"
                  "int main() {\n"
                  "    return add(10, 20);\n"
                  "}\n");
    StandaloneObject mainObjV2 = compileStandalone(mainPath);
    ASSERT_TRUE(mainObjV2.ok) << mainObjV2.diagnostics;

    EXPECT_NE(mainObjV2.assembly, mainObjV1.assembly); // 产物差异只在 main 部分
    VmSnapshot snapV2 =
      linkAndRun({ mainObjV2.image, mathImageV1 }, project.pathOf("linked_v2.nci"));
    expectNormalTermination(snapV2, 30);

    // 复用断言：同一 math 镜像与两版 main 链接均正确——增量构建只需重编
    // 变更模块（mathImageV1 自第一轮起未被触碰）
    StandaloneObject mathRebuilt = compileStandalone(mathPath);
    ASSERT_TRUE(mathRebuilt.ok);
    EXPECT_EQ(mathRebuilt.image, mathImageV1); // 确定性编译：重编亦逐字节一致
}
