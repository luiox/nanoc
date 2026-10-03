#include "ncc/c_backend.hpp"
#include "ncc/codegen.hpp"
#include "ncc/ir.hpp"
#include "ncc/lexer.hpp"
#include "ncc/llvm_backend.hpp"
#include "ncc/loader.hpp"
#include "ncc/parser.hpp"
#include "ncc/semantic.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <libca/opt/opt.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

    constexpr const char* NCC_VERSION = "0.1.0";

    ca::opt::Command make_command() {
        ca::opt::Command root;
        root.name = "ncc";
        root.help = "NanoC compiler: compile .nc sources to VM assembly (.nas) or C";
        root.usage = "ncc [options] <file.nc>...";

        ca::opt::Arg files;
        files.name = "files";
        files.kind = ca::opt::OptKind::Positional;
        files.metavar = "<file.nc>...";
        files.help = "input .nc sources, compiled as one unit in command-line order";

        ca::opt::Arg output;
        output.name = "output";
        output.aliases = { "-o" };
        output.metavar = "<file>";
        output.help = "output path (default: <first input with .nas extension>)";

        ca::opt::Arg emit;
        emit.name = "emit";
        emit.kind = ca::opt::OptKind::String;
        emit.default_value = "asm";
        emit.metavar = "<kind>";
        emit.help = "output kind: asm|c|llvm|obj|exe (llvm = textual LLVM IR; "
                    "obj/exe need the LLVM toolchain, see NANOC_LLVM_DIR)";

        ca::opt::Arg mmd;
        mmd.name = "MMD";
        mmd.kind = ca::opt::OptKind::Flag;
        mmd.aliases = { "-MMD" };
        mmd.help = "write a dependency file next to the output (path via -MF)";

        ca::opt::Arg mf;
        mf.name = "MF";
        mf.kind = ca::opt::OptKind::String;
        mf.aliases = { "-MF" };
        mf.metavar = "<file>";
        mf.help = "dependency file path (use with -MMD)";

        ca::opt::Arg dumpIr;
        dumpIr.name = "dump-ir";
        dumpIr.kind = ca::opt::OptKind::Flag;
        dumpIr.help = "dump the lowered IR module to stderr for debugging";

        ca::opt::Arg version;
        version.name = "version";
        version.kind = ca::opt::OptKind::Flag;
        version.help = "print ncc version and exit";

        root.args = { files, output, emit, mmd, mf, dumpIr, version };
        return root;
    }

    void print_text(std::ostream& os, const std::string& text) {
        os << text;
        if (text.empty() || text.back() != '\n') {
            os << '\n';
        }
    }

    std::string replace_extension(std::string path, const std::string& ext) {
        const std::size_t sep = path.find_last_of("/\\");
        const std::size_t start = (sep == std::string::npos) ? 0 : sep + 1;
        const std::size_t dot = path.find_last_of('.');
        if (dot != std::string::npos && dot > start) {
            path = path.substr(0, dot);
        }
        return path + ext;
    }

    bool write_file(const std::string& path, const std::string& content) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            std::cerr << "ncc: error: cannot open output file '" << path << "'\n";
            return false;
        }
        out << content;
        if (!out) {
            std::cerr << "ncc: error: failed to write '" << path << "'\n";
            return false;
        }
        return true;
    }

    std::string make_depfile(const std::string& out,
                             const std::vector<std::string>& inputs) {
        // make 把反斜杠当转义字符，depfile 内路径统一写成正斜杠
        auto normalize = [](std::string path) {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        };
        std::string dep = normalize(out) + ":";
        for (const std::string& in : inputs) {
            dep += " " + normalize(in);
        }
        dep += "\n";
        return dep;
    }

} // namespace

int main(int argc, char* argv[]) {
    const ca::opt::Command command = make_command();

    ca::opt::Parser parser(command);
    auto parsed = parser.parse(argc, argv);
    if (parsed.is_err()) {
        const auto err = parsed.unwrap_err();
        if (err.category == ca::opt::ParseErrorCategory::HelpRequested) {
            print_text(std::cout, err.message);
            return 0;
        }
        std::cerr << "ncc: " << err.message << "\n";
        return 1;
    }

    const auto options = parsed.unwrap();
    if (options.has("version")) {
        std::cout << "ncc " << NCC_VERSION << "\n";
        return 0;
    }

    const std::vector<std::string>& inputs = options.positionals();
    if (inputs.empty()) {
        print_text(std::cerr, ca::opt::help_text(command));
        return 1;
    }

    const std::string emit_kind = options.get("emit");
    if (emit_kind != "asm" && emit_kind != "c" && emit_kind != "llvm"
        && emit_kind != "obj" && emit_kind != "exe") {
        std::cerr << "ncc: error: --emit=" << emit_kind
                  << " is not supported (available: asm, c, llvm, obj, exe)\n";
        return 1;
    }

    // 多文件装载（PRD R2a 文件即模块）：每个输入都是一个装载根（import
    // 依赖递归并入，跨根重复 import 幂等），合并为一个编译单元。
    // 装载诊断（找不到文件/环形 import/解析错误）与语义诊断同格式输出
    Loader loader(inputs);
    LoadResult loaded = loader.load();
    for (const auto& diagnostic : loaded.diagnostics) {
        print_text(std::cerr, diagnostic.toString());
    }
    if (!loaded.ok) {
        return 1;
    }

    std::string assembly;
    try {
        // 语义分析（PRD R1.1/R1.2 + R2a 可见性）：先收集全部诊断，有 error
        // 则输出并退出非 0，无诊断才进入代码生成
        SemanticAnalyzer analyzer(inputs.front());
        auto analyzed = analyzer.analyze(*loaded.program);
        if (analyzed.is_err()) {
            std::cerr << "ncc: error: " << analyzed.unwrap_err() << "\n";
            return 1;
        }
        bool hasErrors = false;
        for (const auto& diagnostic : analyzed.unwrap().diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Error) {
                hasErrors = true;
            }
            print_text(std::cerr, diagnostic.toString());
        }
        if (hasErrors) {
            return 1;
        }

        // 语义通过 → ir::lower → IR 代码生成（PRD R1.3-ii/R1.4 后端管线）。
        // lower 输入契约 = 已通过语义分析；契约级失败按内部错误处理
        auto lowered = ir::lower(*loaded.program);
        if (lowered.is_err()) {
            std::cerr << "ncc: error: " << lowered.unwrap_err() << "\n";
            return 1;
        }
        ir::Module module = std::move(lowered).unwrap(); // Module 只移动

        if (options.has("dump-ir")) {
            print_text(std::cerr, module.dump());
        }

        // --emit=c（PRD R4）：IR → 可读 C（差分测试 oracle 与 xmake rule 一期
        // 产物）。错误处理与 --emit=asm 一致：发射异常按编译错误退出非 0
        if (emit_kind == "c") {
            std::string cSource;
            try {
                cSource = c_backend::emit(module);
            } catch (const std::exception& e) {
                std::cerr << "ncc: error: " << e.what() << "\n";
                return 1;
            }

            std::string c_out_path = options.get("output");
            if (c_out_path.empty()) {
                c_out_path = replace_extension(inputs.front(), ".c");
            }
            if (!write_file(c_out_path, cSource)) {
                return 1;
            }

            if (options.has("MMD")) {
                std::string dep_path = options.get("MF");
                if (dep_path.empty()) {
                    dep_path = replace_extension(c_out_path, ".d");
                }
                if (!write_file(dep_path, make_depfile(c_out_path, loaded.loadOrder))) {
                    return 1;
                }
            }
            return 0;
        }

        // --emit=llvm|obj|exe（PRD R6）：IR → LLVM 文本 IR；obj/exe 走
        // llc（+ lld-link/clang 链接）。工具链探测与报错口径见
        // llvm_backend.hpp；错误处理与 --emit=c/asm 一致：编译错误退出非 0
        if (emit_kind == "llvm" || emit_kind == "obj" || emit_kind == "exe") {
            std::string llSource;
            try {
                llSource = llvm_backend::emit(module);
            } catch (const std::exception& e) {
                std::cerr << "ncc: error: " << e.what() << "\n";
                return 1;
            }

            const char* outExt =
              emit_kind == "llvm" ? ".ll" : (emit_kind == "obj" ? ".obj" : ".exe");
            std::string out_path = options.get("output");
            if (out_path.empty()) {
                out_path = replace_extension(inputs.front(), outExt);
            }

            if (emit_kind == "llvm") {
                if (!write_file(out_path, llSource)) {
                    return 1;
                }
            } else {
                std::string diagnostics;
                const bool ok =
                  emit_kind == "obj"
                    ? llvm_backend::compileToObj(llSource, out_path, diagnostics)
                    : llvm_backend::compileToExe(llSource, out_path, diagnostics);
                if (!ok) {
                    std::cerr << "ncc: error: " << diagnostics << "\n";
                    return 1;
                }
            }

            if (options.has("MMD")) {
                std::string dep_path = options.get("MF");
                if (dep_path.empty()) {
                    dep_path = replace_extension(out_path, ".d");
                }
                if (!write_file(dep_path, make_depfile(out_path, loaded.loadOrder))) {
                    return 1;
                }
            }
            return 0;
        }

        // 多文件 mangle 所需的顶层符号链接信息（IR 未建模 sourceFile/export，
        // 以位置对齐 side-table 绕过，见 LinkageEntry 注释）
        LinkageTable linkage = buildLinkageTable(*loaded.program);

        CodeGenerator codegen;
        assembly = codegen.generate(module, &linkage);
    } catch (const std::exception& e) {
        std::cerr << "ncc: error: " << e.what() << "\n";
        return 1;
    }

    std::string out_path = options.get("output");
    if (out_path.empty()) {
        out_path = replace_extension(inputs.front(), ".nas");
    }
    if (!write_file(out_path, assembly)) {
        return 1;
    }

    if (options.has("MMD")) {
        std::string dep_path = options.get("MF");
        if (dep_path.empty()) {
            dep_path = replace_extension(out_path, ".d");
        }
        // 依赖文件包含全部 import 闭包（装载顺序），增量构建据此追踪
        if (!write_file(dep_path, make_depfile(out_path, loaded.loadOrder))) {
            return 1;
        }
    }

    return 0;
}
