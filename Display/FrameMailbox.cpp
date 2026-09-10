#include "FrameMailbox.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace emu {
FrameMailbox::FrameMailbox(uint32_t w, uint32_t h): width_(w), height_(h) {
    if (w == 0 || h == 0 || w > 4096 || h > 4096) throw std::invalid_argument("Invalid framebuffer dimensions");
    pixels_.resize(size_t(w) * h * 4);
}
void FrameMailbox::update(std::span<const uint8_t> source, size_t stride, DirtyRect r) {
    if (r.width == 0 || r.height == 0) return;
    if (r.x >= width_ || r.y >= height_ || r.width > width_ - r.x || r.height > height_ - r.y ||
        stride < size_t(width_) * 4 || stride > SIZE_MAX / height_ || source.size() < stride * height_)
        throw std::invalid_argument("Framebuffer dirty rectangle out of bounds");
    std::lock_guard lock(mutex_);
    for (uint32_t y = r.y; y < r.y + r.height; ++y)
        std::memcpy(pixels_.data() + (size_t(y) * width_ + r.x) * 4, source.data() + size_t(y) * stride + r.x * 4, size_t(r.width) * 4);
    stats_.bytesCopied += uint64_t(r.width) * r.height * 4;
    ++stats_.submitted;
    if (pending_) {
        ++stats_.replaced;
        auto x = std::min(r.x, dirty_.x), y = std::min(r.y, dirty_.y);
        dirty_ = {x, y, std::max(r.x + r.width, dirty_.x + dirty_.width) - x,
                        std::max(r.y + r.height, dirty_.y + dirty_.height) - y};
    } else dirty_ = r;
    pending_ = true;
}
bool FrameMailbox::consume(void (*upload)(void*, const uint8_t*, size_t, DirtyRect), void* context) {
    if (!upload) return false;
    std::lock_guard lock(mutex_);
    if (!pending_) return false;
    upload(context, pixels_.data(), size_t(width_) * 4, dirty_);
    pending_ = false; ++stats_.consumed; return true;
}
FrameStats FrameMailbox::stats() const { std::lock_guard lock(mutex_); return stats_; }
}
