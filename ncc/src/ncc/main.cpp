#include "ncc/codegen.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <libca/opt/opt.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

    constexpr const char* NCC_VERSION = "0.1.0";

    ca::opt::Command make_command() {
        ca::opt::Command root;
        root.name = "ncc";
        root.help = "NanoC compiler: compile .nc sources to VM assembly (.nas)";
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
        emit.help = "output kind: asm; c/llvm/obj/exe reserved";

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

        ca::opt::Arg version;
        version.name = "version";
        version.kind = ca::opt::OptKind::Flag;
        version.help = "print ncc version and exit";

        root.args = { files, output, emit, mmd, mf, version };
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

    bool read_sources(const std::vector<std::string>& paths, std::string& source) {
        for (const std::string& path : paths) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                std::cerr << "ncc: error: cannot open input file '" << path << "'\n";
                return false;
            }
            std::ostringstream buf;
            buf << in.rdbuf();
            source += buf.str();
            source += '\n';
        }
        return true;
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
    if (emit_kind != "asm") {
        std::cerr << "ncc: error: --emit=" << emit_kind
                  << " is not supported yet (available: asm)\n";
        return 1;
    }

    std::string source;
    if (!read_sources(inputs, source)) {
        return 1;
    }

    std::string assembly;
    try {
        Lexer lexer(source);
        const std::vector<Token> tokens = lexer.tokenize();
        Parser program_parser(tokens);
        std::unique_ptr<Program> program = program_parser.parse();
        CodeGenerator codegen;
        assembly = codegen.generate(*program);
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
        if (!write_file(dep_path, make_depfile(out_path, inputs))) {
            return 1;
        }
    }

    return 0;
}
