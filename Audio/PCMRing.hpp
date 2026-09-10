#pragma once
#include "Core/SPSCRing.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
namespace emu {
struct StereoSample { int16_t left, right; };
class PCMRing {
    SPSCRing<StereoSample, 4096> samples_;
public:
    std::atomic<uint64_t> underruns{0}, overruns{0};
    bool write(std::span<const StereoSample> frames) noexcept {
        if (samples_.push(frames)) return true;
        overruns.fetch_add(1, std::memory_order_relaxed); return false;
    }
    void render(std::span<StereoSample> frames) noexcept {
        const auto count = samples_.pop(frames);
        if (count != frames.size()) {
            std::fill(frames.begin() + count, frames.end(), StereoSample{});
            underruns.fetch_add(1, std::memory_order_relaxed);
        }
    }
};
}
