#include <iostream>

int main(int argc, char* argv[]) {
    (void)argv;
    if (argc == 1) {
        std::cout << "Usage: ncc <input file> [options]\n";
    }
    std::cout << "Hello!\n";
    return 0;
}
