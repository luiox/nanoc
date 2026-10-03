#include "support/diff_harness.hpp"

#include "nas/instruction.hpp"
#include "ncc/c_backend.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"
#include "nvm/core.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

// R13 差分 harness 实现。设计口径见 diff_harness.hpp 文件头注释：
// - VM 侧进程内（复用 test_golden_e2e.cpp 的 codegen→nas→nvm 链路与
//   正常停机判定），C 侧进程外真编译（emit → 落盘 → C 编译器 → 运行）。
// - 编译器探测与 #47（test_c_backend.cpp）一致：
//   NANOC_C_COMPILER > PATH clang > gcc；探测失败 → 矩阵记 skip。
// - 矩阵断言口径：退出码统一低 8 位归一化（rawExit & 0xFF）后比较。
namespace nanoc_diff {

    namespace {

        // -----------------------------------------------------------------------
        // 前端管线（lex → parse → semantic 门禁 → IR lower），错误进 diagnostics
        // -----------------------------------------------------------------------

        bool
        lowerToIr(const std::string& source, ir::Module& out, std::string& diagnostics) {
            try {
                Lexer lexer(source);
                std::vector<Token> tokens = lexer.tokenize();

                Parser parser(tokens);
                auto program = parser.parse();

                SemanticAnalyzer analyzer("diff.nc");
                auto analyzed = analyzer.analyze(*program);
                if (analyzed.is_err()) {
                    diagnostics =
                      "semantic analyze returned Err: " + analyzed.unwrap_err();
                    return false;
                }
                const SemanticResult& semantic = analyzed.unwrap();
                if (semantic.hasErrors()) {
                    std::ostringstream buffer;
                    for (const auto& diag : semantic.diagnostics) {
                        if (diag.severity == DiagnosticSeverity::Error) {
                            buffer << diag.toString() << "\n";
                        }
                    }
                    diagnostics = "semantic errors:\n" + buffer.str();
                    return false;
                }

                auto lowered = ir::lower(*program);
                if (lowered.is_err()) {
                    diagnostics = "lower() returned Err: " + lowered.unwrap_err();
                    return false;
                }
                out = std::move(lowered).unwrap();
                return true;
            } catch (const std::exception& e) {
                diagnostics = std::string("frontend exception: ") + e.what();
                return false;
            }
        }

        // -----------------------------------------------------------------------
        // C 编译器探测与进程外驱动（与 test_c_backend.cpp #47 同策略）
        // -----------------------------------------------------------------------

        bool toolAvailable(const std::string& command) {
            const std::string logPath = "diffmx_probe.log";
            const int rc =
              std::system((command + " --version > " + logPath + " 2>&1").c_str());
            std::remove(logPath.c_str());
            return rc == 0;
        }

        const std::string& cCompilerCommandImpl() {
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

        // 落盘 → 编译 → 运行；任何一步失败 diagnostics 带上下文
        CResult compileAndRun(const std::string& cSource, const std::string& tempBase) {
            CResult result;
            const std::string cPath = tempBase + ".c";
            const std::string exePath = tempBase + ".exe";
            const std::string logPath = tempBase + ".log";

            {
                std::ofstream out(cPath, std::ios::binary);
                out << cSource;
            }
            const std::string compileCommand = cCompilerCommand() + " -O0 " + cPath
                                               + " -o " + exePath + " > " + logPath
                                               + " 2>&1";
            const int compileRc = std::system(compileCommand.c_str());
            if (compileRc != 0) {
                std::ifstream log(logPath, std::ios::binary);
                std::ostringstream buffer;
                buffer << log.rdbuf();
                result.diagnostics = "compile command: " + compileCommand
                                     + "\ncompiler log:\n" + buffer.str()
                                     + "\n--- generated C ---\n" + cSource;
                std::remove(cPath.c_str());
                std::remove(logPath.c_str());
                return result;
            }

            result.exitCode = std::system(exePath.c_str());
            result.ok = true;

            std::remove(cPath.c_str());
            std::remove(exePath.c_str());
            std::remove(logPath.c_str());
            return result;
        }

        // -----------------------------------------------------------------------
        // 内置后端
        // -----------------------------------------------------------------------

        // VM 后端（进程内）：恒可用；与 golden e2e 同一正常停机判定
        class VmBackend : public IDiffBackend {
        public:
            std::string name() const override { return "vm"; }

            bool probe() override {
                return true; // 进程内链路，无外部依赖
            }

            BackendOutput execute(const std::string& nccSource) override {
                BackendOutput output;
                const VmResult result = runOnVm(nccSource);
                if (!result.ok) {
                    output.diagnostics = result.diagnostics;
                    return output;
                }
                output.executed = true;
                output.rawExit = static_cast<int>(result.r0);
                output.exitCode8 = result.r0 & 0xFF; // 负值按补码截断（见头文件映射口径）
                return output;
            }
        };

        // C 后端（进程外真编译）：无编译器时 probe() 为 false → 矩阵整列 skip
        class CDiffBackend : public IDiffBackend {
        public:
            std::string name() const override { return "c"; }

            bool probe() override { return cCompilerAvailable(); }

            BackendOutput execute(const std::string& nccSource) override {
                BackendOutput output;
                const CResult result = runOnC(nccSource, "diffmx_c_run");
                if (!result.ok) {
                    output.diagnostics = result.diagnostics;
                    return output;
                }
                output.executed = true;
                output.rawExit = result.exitCode;
                output.exitCode8 = result.exitCode & 0xFF;
                return output;
            }
        };

        std::vector<std::unique_ptr<IDiffBackend>>& backendStorage() {
            static std::vector<std::unique_ptr<IDiffBackend>> storage;
            return storage;
        }

        // -----------------------------------------------------------------------
        // 渲染辅助
        // -----------------------------------------------------------------------

        std::string statusText(const MatrixCell& cell) {
            switch (cell.status) {
            case CellStatus::Pass:
                return "pass(" + std::to_string(cell.exitCode8) + ")";
            case CellStatus::Skip:
                return "SKIP";
            case CellStatus::Fail:
                return "FAIL";
            }
            return "?";
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // 单后端执行器
    // ---------------------------------------------------------------------------

    VmResult runOnVm(const std::string& nccSource) {
        VmResult result;
        const std::string nciPath = "diffmx_vm.nci";
        try {
            Lexer lexer(nccSource);
            std::vector<Token> tokens = lexer.tokenize();

            Parser parser(tokens);
            auto program = parser.parse();

            CodeGenerator codegen;
            const std::string assembly = codegen.generate(*program);

            const AssemblyResult assembled = Assembler::assemble(assembly);
            if (!assembled.ok) {
                result.diagnostics = "assemble failed line "
                                     + std::to_string(assembled.errorLine) + ": "
                                     + assembled.errorMessage;
                return result;
            }

            {
                std::ofstream ofs(nciPath, std::ios::binary);
                ofs.write(reinterpret_cast<const char*>(assembled.image.data()),
                          static_cast<std::streamsize>(assembled.image.size()));
            }

            NVirtualMachine vm(8 * 1024 * 1024);
            vm.load(nciPath);
            vm.start();

            // 正常停机判定（同 test_golden_e2e.cpp）：main 顶层 ret 弹出 start()
            // 压入的栈底哨兵 = codeSize；栈彻底复原；帧链回收
            const int32_t pc = vm.getPC();
            const int32_t sp = vm.getSP();
            const int32_t bp = vm.getBP();
            const int64_t codeSize = vm.getCodeSize();
            const int32_t stackSize = vm.getStackSize();
            if (pc != codeSize || sp != stackSize || bp != 0) {
                result.diagnostics = "VM abnormal halt: pc=" + std::to_string(pc)
                                     + " (expected " + std::to_string(codeSize)
                                     + "), sp=" + std::to_string(sp) + " (expected "
                                     + std::to_string(stackSize)
                                     + "), bp=" + std::to_string(bp) + " (expected 0)";
                return result;
            }
            result.r0 = vm.getRegister(0);
            result.ok = true;
        } catch (const std::exception& e) {
            result.diagnostics = std::string("frontend/VM exception: ") + e.what();
            return result;
        }
        std::remove(nciPath.c_str());
        return result;
    }

    CResult runOnC(const std::string& nccSource, const std::string& tempBase) {
        CResult result;
        if (!cCompilerAvailable()) {
            result.diagnostics = "no C compiler available (probe NANOC_C_COMPILER, "
                                 "clang, gcc)";
            return result;
        }
        ir::Module module;
        std::string diagnostics;
        if (!lowerToIr(nccSource, module, diagnostics)) {
            result.diagnostics = diagnostics;
            return result;
        }
        return compileAndRun(c_backend::emit(module), tempBase);
    }

    const std::string& cCompilerCommand() { return cCompilerCommandImpl(); }

    bool cCompilerAvailable() { return !cCompilerCommandImpl().empty(); }

    // ---------------------------------------------------------------------------
    // 注册表
    // ---------------------------------------------------------------------------

    void registerBackend(std::unique_ptr<IDiffBackend> backend) {
        backendStorage().push_back(std::move(backend));
    }

    const std::vector<IDiffBackend*>& backends() {
        static std::vector<IDiffBackend*> registry;
        static bool indexed = false;
        if (!indexed) {
            for (const auto& backend : backendStorage()) {
                registry.push_back(backend.get());
            }
            indexed = true;
        }
        return registry;
    }

    void registerBuiltinBackends() {
        if (!backendStorage().empty()) {
            return; // 幂等：矩阵跑两遍（各测试读缓存）不重复注册
        }
        // R6 接入点：未来 LLVM 后端在此追加一次 registerBackend(...) 即自动扩列
        registerBackend(std::make_unique<VmBackend>());
        registerBackend(std::make_unique<CDiffBackend>());
    }

    // ---------------------------------------------------------------------------
    // 矩阵驱动
    // ---------------------------------------------------------------------------

    std::vector<MatrixCell> runRow(const DiffProgram& program) {
        std::vector<MatrixCell> cells;
        for (IDiffBackend* backend : backends()) {
            MatrixCell cell;
            cell.program = program.name;
            cell.backend = backend->name();
            if (!backend->probe()) {
                cell.status = CellStatus::Skip;
                cell.detail = "backend unavailable (probe failed)";
            } else {
                const BackendOutput output = backend->execute(program.source);
                if (!output.executed) {
                    cell.status = CellStatus::Fail;
                    cell.detail = output.diagnostics;
                } else {
                    cell.status = CellStatus::Pass;
                    cell.rawExit = output.rawExit;
                    cell.exitCode8 = output.exitCode8;
                }
            }
            cells.push_back(cell);
        }
        return cells;
    }

    std::string rowMismatch(const std::vector<MatrixCell>& cells) {
        const MatrixCell* reference = nullptr;
        for (const auto& cell : cells) {
            if (cell.status == CellStatus::Pass) {
                reference = &cell; // 注册序第一个 Pass 的后端（内置注册序即 vm）为基准
                break;
            }
        }
        if (reference == nullptr) {
            return "";
        }
        std::string mismatch;
        for (const auto& cell : cells) {
            if (&cell == reference || cell.status != CellStatus::Pass) {
                continue;
            }
            if (cell.exitCode8 != reference->exitCode8) {
                mismatch += cell.backend + " exit8=" + std::to_string(cell.exitCode8)
                            + " (raw " + std::to_string(cell.rawExit)
                            + ") != " + reference->backend
                            + " exit8=" + std::to_string(reference->exitCode8) + " (raw "
                            + std::to_string(reference->rawExit) + "); ";
            }
        }
        return mismatch;
    }

    std::string renderMatrix(const std::vector<MatrixCell>& cells) {
        // 行宽自适应：程序名列取最宽值；后端列对齐到最宽状态文本
        std::size_t programWidth = strlen("program");
        std::vector<std::string> programs;
        for (const auto& cell : cells) {
            if (std::find(programs.begin(), programs.end(), cell.program)
                == programs.end()) {
                programs.push_back(cell.program);
                programWidth = std::max(programWidth, cell.program.size());
            }
        }
        std::size_t statusWidth = 0;
        for (const auto& cell : cells) {
            statusWidth = std::max(statusWidth, statusText(cell).size());
        }

        std::ostringstream out;
        out << "diff matrix (program x backend -> pass(exit8)/SKIP/FAIL):\n";
        out << "  " << "program" << std::string(programWidth - 7, ' ');
        // 每后端一列，按首见序
        std::vector<std::string> backendNames;
        for (const auto& cell : cells) {
            if (std::find(backendNames.begin(), backendNames.end(), cell.backend)
                == backendNames.end()) {
                backendNames.push_back(cell.backend);
            }
        }
        for (const auto& backend : backendNames) {
            const std::size_t width = std::max(backend.size(), statusWidth);
            out << "  " << backend << std::string(width - backend.size(), ' ');
        }
        out << "\n";
        for (const auto& program : programs) {
            out << "  " << program << std::string(programWidth - program.size(), ' ');
            for (const auto& backend : backendNames) {
                const auto match =
                  std::find_if(cells.begin(), cells.end(), [&](const MatrixCell& cell) {
                      return cell.program == program && cell.backend == backend;
                  });
                if (match == cells.end()) {
                    continue;
                }
                out << "  " << statusText(*match)
                    << std::string(statusWidth - statusText(*match).size(), ' ');
            }
            out << "\n";
        }
        out << "legend: pass(exit8) = 双后端可比观测为退出码低 8 位; SKIP = 后端不可用"
               "（如无 C 编译器）; FAIL = 执行/编译失败\n";
        return out.str();
    }

    bool readExampleSource(const std::string& name, std::string& out) {
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
        return false;
    }

} // namespace nanoc_diff
