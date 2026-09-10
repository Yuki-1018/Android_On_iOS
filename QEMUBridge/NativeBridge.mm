#include "NativeBridge.h"
#include "ImageKit/SparseImage.hpp"
#include <cstdio>
#include <exception>
#include <stdexcept>

bool AEImportImage(const char *source, const char *destination, uint64_t limit, char *error, size_t capacity) {
    try {
        if (!source || !destination) throw std::invalid_argument("Missing image path");
        emu::copyAndroidImage(source, destination, limit);
        return true;
    } catch (const std::exception& e) {
        if (error && capacity) std::snprintf(error, capacity, "%s", e.what());
        return false;
    }
}
