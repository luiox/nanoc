#include "instruction.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

struct Label {
    std::string name;
    int32_t addr;
};

struct LabelRef {
    int32_t offset;
    std::string label;
};

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

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ifs.close();

    // First pass: collect labels and generate code
    std::map<std::string, int32_t> labels;
    std::vector<std::unique_ptr<Instruction>> instructions;
    std::vector<LabelRef> labelRefs;
    int32_t pc = 0;

    for (const auto & l : lines) {
        // Skip empty/comments
        if (l.empty() || l[0] == ';' || l[0] == '#')
            continue;

        // Check for label
        if (l.back() == ':') {
            std::string name = l.substr(0, l.size() - 1);
            labels[name] = pc;
            continue;
        }

        // Parse instruction
        auto instr = Assembler::parseLine(l);
        if (instr) {
            instr->emit();
            instructions.push_back(std::move(instr));
            pc += instructions.back()->bytes.size();
        }
    }

    // Write NCI file
    std::ofstream ofs(output, std::ios::binary);
    if (!ofs) {
        std::cerr << "Error: Cannot write " << output << std::endl;
        return 1;
    }

    // Write header (32 bytes)
    const char magic[8] = { 'N', 'a', 'n', 'o', 'C', '\0', 0, 0 };
    ofs.write(magic, 8);

    int32_t headerSize = 32;
    int32_t codeSize = pc;
    int32_t dataSize = 0;
    int32_t importCount = 0;
    int32_t exportCount = 0;
    int32_t entryPoint = 0;

    // Auto-find main label
    if (labels.count("main"))
        entryPoint = labels["main"];

    ofs.write(reinterpret_cast<char *>(&headerSize), 4);
    ofs.write(reinterpret_cast<char *>(&codeSize), 4);
    ofs.write(reinterpret_cast<char *>(&dataSize), 4);
    ofs.write(reinterpret_cast<char *>(&importCount), 4);
    ofs.write(reinterpret_cast<char *>(&exportCount), 4);
    ofs.write(reinterpret_cast<char *>(&entryPoint), 4);

    // Write code
    for (const auto & instr : instructions) {
        ofs.write(reinterpret_cast<const char *>(instr->bytes.data()),
                  instr->bytes.size());
    }

    ofs.close();
    std::cout << "Assembled " << input << " -> " << output << " (" << codeSize
              << " bytes)" << std::endl;

    return 0;
}
