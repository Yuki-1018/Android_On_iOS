#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace emu {
// Exactly one producer and one consumer. No reset while either is active.
// All N entries are usable; monotonically increasing unsigned indices wrap safely.
template<class T, size_t N> class SPSCRing {
    static_assert(N > 0 && (N & (N - 1)) == 0);
    static_assert(N <= SIZE_MAX / 2);
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<size_t>::is_always_lock_free);
    std::array<T, N> entries_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
public:
    bool push(std::span<const T> values) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        if (values.size() > N - (h - t)) return false;
        for (size_t i = 0; i < values.size(); ++i) entries_[(h + i) & (N - 1)] = values[i];
        head_.store(h + values.size(), std::memory_order_release);
        return true;
    }
    size_t pop(std::span<T> output) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        const auto count = output.size() < h - t ? output.size() : h - t;
        for (size_t i = 0; i < count; ++i) output[i] = entries_[(t + i) & (N - 1)];
        tail_.store(t + count, std::memory_order_release);
        return count;
    }
};
}
