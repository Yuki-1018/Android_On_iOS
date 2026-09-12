#pragma once
#include <filesystem>
#include <functional>
namespace emu {
// Extract a bounded, single-disk ZIP (stored/deflated), into a NEW directory.
// Reject ZIP64, encryption, links, traversal, duplicate files and bad checksums.
void extractImageZip(const std::filesystem::path&, const std::filesystem::path&,
                     const std::function<bool()>& cancelled = {});
}
