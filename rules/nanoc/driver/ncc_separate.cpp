// ncc-separate：独立编译驱动（PRD R7 独立编译语义的 C 后端 CLI 接线，PRD R8
// toolchain("nanoc") 的按文件编译器）。
//
// 为什么存在：R7 的轻装载 Loader::loadStandalone（#54）只以 API 交付，ncc 主
// CLI 的 `ncc <file> --emit=c` 仍走整体装载 load()——import 闭包的**定义**会被
// 并入产物（实测两模块各自 --emit=c 后 C 链接报 LNK2005/LNK1169 重复定义）。
// #54 注明 CLI 旗标接线"下一轮统一做"；在本文件（SDK 的 rules/ 区，非 ncc/**）
// 提供独立驱动先行接线，ncc 主 CLI 未来加 --separate 后本驱动可退役。
//
// 编译模型（PRD R7 二期 / R8 toolchain 形态）：
//     ncc-separate <entry.nc> --emit=c -o <out.c>
//     = loadStandalone 轻装载（本模块声明 + 依赖导出签名的合成 extern 声明）
//       → 语义分析 → ir::lower → c_backend::emit
//     产出的 .c 对依赖模块只含 extern 原型（PRD R3 发射路径），函数体只有本
//     模块的——每个 .nc 独立产出自洽 .c，跨模块符号由 C 链接器按符号名解析。
//
// 与 ncc 的关系：复用 ncc 的公开组件（loader/semantic/ir/c_backend 均为
// ncc/src 头与编译单元），管线与 tests/test_separate.cpp 的 compileStandalone
// 原型一致（那里走汇编/.nci 路径，这里走 --emit=c 路径）。
//
// 用法：
//     ncc-separate <entry.nc> [-o <file>] [-MMD [-MF <file>]] [--dump-ir]
// 依赖文件（-MMD）含 import 闭包全部文件（装载顺序），供需要闭包级增量的
// 调用方使用；xmake toolchain 形态的增量口径见 rules/nanoc/toolchain.lua。

#include "ncc/c_backend.hpp"
#include "ncc/ir.hpp"
#include "ncc/loader.hpp"
#include "ncc/semantic.hpp"

#include <fstream>
#include <iostream>
#include <libca/opt/opt.hpp>
#include <string>
#include <vector>

namespace {

    constexpr const char* NCC_SEPARATE_VERSION = "0.1.0";

    ca::opt::Command make_command() {
        ca::opt::Command root;
        root.name = "ncc-separate";
        root.help = "NanoC standalone compiler: compile one .nc module to C "
                    "(extern prototypes for imported symbols)";
        root.usage = "ncc-separate [options] <entry.nc>";

        ca::opt::Arg files;
        files.name = "files";
        files.kind = ca::opt::OptKind::Positional;
        files.metavar = "<entry.nc>";
        files.help = "entry .nc module, compiled standalone (import closure "
                     "contributes signatures only, not definitions)";

        ca::opt::Arg output;
        output.name = "output";
        output.aliases = { "-o" };
        output.metavar = "<file>";
        output.help = "output path (default: <entry with .c extension>)";

        ca::opt::Arg emit;
        emit.name = "emit";
        emit.kind = ca::opt::OptKind::String;
        emit.default_value = "c";
        emit.metavar = "<kind>";
        emit.help = "output kind: c (only c is available in this driver)";

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
        version.help = "print ncc-separate version and exit";

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
            std::cerr << "ncc-separate: error: cannot open output file '" << path
                      << "'\n";
            return false;
        }
        out << content;
        if (!out) {
            std::cerr << "ncc-separate: error: failed to write '" << path << "'\n";
            return false;
        }
        return true;
    }

    std::string make_depfile(const std::string& out,
                             const std::vector<std::string>& inputs) {
        // make 把反斜杠当转义字符，depfile 内路径统一写成正斜杠
        std::string dep;
        for (const char c : out) {
            dep += (c == '\\') ? '/' : c;
        }
        dep += ":";
        for (const std::string& in : inputs) {
            dep += " ";
            for (const char c : in) {
                dep += (c == '\\') ? '/' : c;
            }
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
        std::cerr << "ncc-separate: " << err.message << "\n";
        return 1;
    }

    const auto options = parsed.unwrap();
    if (options.has("version")) {
        std::cout << "ncc-separate " << NCC_SEPARATE_VERSION << "\n";
        return 0;
    }

    const std::vector<std::string>& inputs = options.positionals();
    if (inputs.size() != 1) {
        // 独立编译一次只产一个模块（多入口是整体编译语义，用 ncc 主 CLI）
        print_text(std::cerr, ca::opt::help_text(command));
        return 1;
    }
    const std::string& entry = inputs.front();

    const std::string emit_kind = options.get("emit");
    if (emit_kind != "c") {
        std::cerr << "ncc-separate: error: --emit=" << emit_kind
                  << " is not supported (available: c)\n";
        return 1;
    }

    // 独立编译管线（PRD R7，与 tests/test_separate.cpp 的 compileStandalone
    // 同构；C 后端无汇编/mangle 环节，不需要 LinkageTable/CodeGenerator）：
    // 轻装载 → 语义 → IR → C 发射。错误口径与 ncc 主 CLI 一致：全部诊断
    // 先收集输出，有 error 退出非 0。
    try {
        Loader loader({ entry });
        StandaloneResult loaded = loader.loadStandalone(entry);
        for (const auto& diagnostic : loaded.diagnostics) {
            print_text(std::cerr, diagnostic.toString());
        }
        if (!loaded.ok) {
            return 1;
        }

        SemanticAnalyzer analyzer(entry);
        auto analyzed = analyzer.analyze(*loaded.program);
        if (analyzed.is_err()) {
            std::cerr << "ncc-separate: error: " << analyzed.unwrap_err() << "\n";
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

        auto lowered = ir::lower(*loaded.program);
        if (lowered.is_err()) {
            std::cerr << "ncc-separate: error: " << lowered.unwrap_err() << "\n";
            return 1;
        }
        ir::Module module = std::move(lowered).unwrap(); // Module 只移动

        if (options.has("dump-ir")) {
            print_text(std::cerr, module.dump());
        }

        // C 发射（PRD R4 同一后端）：依赖模块符号经合成 extern 声明发射为
        // 原型；发射异常按编译错误处理，退出非 0
        std::string cSource;
        try {
            cSource = c_backend::emit(module);
        } catch (const std::exception& e) {
            std::cerr << "ncc-separate: error: " << e.what() << "\n";
            return 1;
        }

        std::string c_out_path = options.get("output");
        if (c_out_path.empty()) {
            c_out_path = replace_extension(entry, ".c");
        }
        if (!write_file(c_out_path, cSource)) {
            return 1;
        }

        if (options.has("MMD")) {
            std::string dep_path = options.get("MF");
            if (dep_path.empty()) {
                dep_path = replace_extension(c_out_path, ".d");
            }
            // 依赖文件含 import 闭包全部文件（装载顺序），口径与 ncc -MMD 一致
            if (!write_file(dep_path, make_depfile(c_out_path, loaded.loadOrder))) {
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "ncc-separate: error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
