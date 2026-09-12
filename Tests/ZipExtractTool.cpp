#include "ImageKit/ZipArchive.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    unsigned polls = 0;
    try { emu::extractImageZip(argv[1], argv[2], [&] { return argc == 4 && ++polls > 7; }); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
