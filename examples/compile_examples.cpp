#include "../ncc/lexer.hpp"
#include "../ncc/parser.hpp"
#include "../ncc/codegen.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool compileFile(const std::string& inputFile, const std::string& outputFile) {
    std::cout << "=== Compiling " << inputFile << " ===" << std::endl;
    
    // 读取源文件
    std::string source = readFile(inputFile);
    if (source.empty()) {
        // 尝试使用内联代码
        std::cout << "File not found, using inline code..." << std::endl;
        if (inputFile.find("hello") != std::string::npos) {
            source = "int main() { int a = 10; int b = 20; int c = a + b; return c; }";
        } else if (inputFile.find("arithmetic") != std::string::npos) {
            source = "int main() { int x = 10; int y = 3; int sum = x + y; int diff = x - y; int prod = x * y; return sum; }";
        } else if (inputFile.find("control_flow") != std::string::npos) {
            source = "int main() { int x = 10; int result = 0; if (x > 5) { result = 1; } else { result = 0; } return result; }";
        } else if (inputFile.find("functions") != std::string::npos) {
            source = "int add(int a, int b) { return a + b; } int main() { int result = add(10, 20); return result; }";
        } else if (inputFile.find("loop") != std::string::npos) {
            source = "int main() { int sum = 0; int i = 1; while (i <= 10) { sum = sum + i; i = i + 1; } return sum; }";
        } else {
            return false;
        }
    }
    
    std::cout << "Source code:" << std::endl;
    std::cout << source << std::endl;
    
    try {
        // 词法分析
        std::cout << "--- Lexical Analysis ---" << std::endl;
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        std::cout << "Tokens generated: " << tokens.size() << std::endl;
        
        // 语法分析
        std::cout << "--- Parsing ---" << std::endl;
        Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "AST generated successfully" << std::endl;
        
        // 代码生成
        std::cout << "--- Code Generation ---" << std::endl;
        CodeGenerator codegen;
        std::string assembly = codegen.generate(*program);
        
        // 输出汇编代码
        std::cout << "Generated assembly:" << std::endl;
        std::cout << assembly << std::endl;
        
        // 写入输出文件
        std::ofstream outFile(outputFile);
        if (outFile.is_open()) {
            outFile << assembly;
            outFile.close();
            std::cout << "Assembly written to " << outputFile << std::endl;
        }
        
        std::cout << "=== Compilation successful ===" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "NanoC Compiler - Example Programs" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << std::endl;
    
    // 编译示例程序（使用相对于工作目录的路径）
    compileFile("examples/hello.nc", "examples/hello.nas");
    std::cout << std::endl;
    
    compileFile("examples/arithmetic.nc", "examples/arithmetic.nas");
    std::cout << std::endl;
    
    compileFile("examples/control_flow.nc", "examples/control_flow.nas");
    std::cout << std::endl;
    
    compileFile("examples/functions.nc", "examples/functions.nas");
    std::cout << std::endl;
    
    compileFile("examples/loop.nc", "examples/loop.nas");
    std::cout << std::endl;
    
    return 0;
}
