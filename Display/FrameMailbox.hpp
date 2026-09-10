#pragma once
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace emu {
struct DirtyRect { uint32_t x, y, width, height; };
struct FrameStats { uint64_t submitted = 0, consumed = 0, replaced = 0, bytesCopied = 0; };
// Persistent BGRA backing store coalesces dirty regions across dropped presentations.
// Producer never overwrites a texture in flight: consumer copies under the lock.
class FrameMailbox {
    uint32_t width_, height_;
    std::vector<uint8_t> pixels_;
    mutable std::mutex mutex_;
    bool pending_ = false;
    DirtyRect dirty_{};
    FrameStats stats_{};
public:
    FrameMailbox(uint32_t width, uint32_t height);
    void update(std::span<const uint8_t> source, size_t stride, DirtyRect rect);
    // Consumer callback executes synchronously with stable pixels. Do not reenter.
    bool consume(void (*upload)(void*, const uint8_t*, size_t, DirtyRect), void* context);
    FrameStats stats() const;
};
}
