#include "nas/instruction.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int
main(int argc, char * argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: nas <input.nas> [output.nci]" << std::endl;
        return 1;
    }

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
