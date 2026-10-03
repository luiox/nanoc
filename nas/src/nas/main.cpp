#include "nas/instruction.hpp"
#include "nas/linker.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int
    linkMode(int argc, char * argv[])
    {
        std::vector<std::string> inputs;
        std::string output;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-o") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: -o 缺少输出文件名" << std::endl;
                    return 1;
                }
                output = argv[++i];
                continue;
            }
            inputs.push_back(arg);
        }
        if (inputs.empty()) {
            std::cerr << "Usage: nas -r <in1.nci> [in2.nci ...] -o <out.nci>"
                      << std::endl;
            return 1;
        }
        if (output.empty()) {
            std::cerr << "Error: 链接模式需要 -o 指定输出文件" << std::endl;
            return 1;
        }

        LinkResult result = Linker::linkFiles(inputs);
        if (!result.ok) {
            std::cerr << "Error: " << result.errorMessage << std::endl;
            return 1;
        }

        std::ofstream ofs(output, std::ios::binary);
        if (!ofs) {
            std::cerr << "Error: Cannot write " << output << std::endl;
            return 1;
        }
        ofs.write(reinterpret_cast<const char *>(result.image.data()),
                  static_cast<std::streamsize>(result.image.size()));
        ofs.close();

        std::cout << "Linked " << inputs.size() << " objects -> " << output << " ("
                  << result.image.size() << " bytes)" << std::endl;
        return 0;
    }
} // namespace

int
main(int argc, char * argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: nas <input.nas> [output.nci]" << std::endl;
        std::cerr << "       nas -r <in1.nci> [in2.nci ...] -o <out.nci>" << std::endl;
        return 1;
    }

    // 链接模式（PRD R7）：nas -r a.nci b.nci -o out.nci
    if (std::string(argv[1]) == "-r")
        return linkMode(argc, argv);

    std::string input = argv[1];
    std::string output =
      (argc >= 3) ? argv[2] : input.substr(0, input.rfind('.')) + ".nci";

    // Read input file
    std::ifstream ifs(input);
    if (!ifs) {
        std::cerr << "Error: Cannot open " << input << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string source = buffer.str();
    ifs.close();

    // 两遍扫描汇编为 NCI v2.1 完整目标文件
    AssemblyResult result = Assembler::assemble(source);
    if (!result.ok) {
        std::cerr << "Error: " << input << ":" << result.errorLine << ": "
                  << result.errorMessage << std::endl;
        return 1;
    }

    // Write NCI file
    std::ofstream ofs(output, std::ios::binary);
    if (!ofs) {
        std::cerr << "Error: Cannot write " << output << std::endl;
        return 1;
    }
    ofs.write(reinterpret_cast<const char *>(result.image.data()),
              static_cast<std::streamsize>(result.image.size()));
    ofs.close();

    std::cout << "Assembled " << input << " -> " << output << " (" << result.image.size()
              << " bytes)" << std::endl;

    return 0;
}
