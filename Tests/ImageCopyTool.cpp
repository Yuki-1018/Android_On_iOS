#include "ImageKit/SparseImage.hpp"
#include <exception>
#include <cstdio>
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        auto result = emu::copyAndroidImage(argv[1], argv[2], 64ULL << 20);
        return result.bytes == (64ULL << 20) && result.wasSparse ? 0 : 3;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
