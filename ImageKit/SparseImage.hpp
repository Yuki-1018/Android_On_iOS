#pragma once
#include <cstdint>
#include <filesystem>
namespace emu {
struct ImageCopyResult { uint64_t bytes; bool wasSparse; };
// Creates a new output exclusively; removes it on failure. Never modifies source.
// CRC32 chunks and nonzero image checksums are verified. DONT_CARE reads as zero.
ImageCopyResult copyAndroidImage(const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 uint64_t maximumBytes);
}
