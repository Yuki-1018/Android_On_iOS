#pragma once
#include <cstdint>
namespace emu {
// Available bytes are the process headroom, not the device's physical RAM.
// Reserve space for the guest's 760 MiB lowmem limit and QEMU/Metal overhead.
inline uint32_t tcgCacheMiB(uint32_t requested, bool increased, uint64_t available, uint64_t physical = UINT64_MAX) {
    // A 3 GiB iPad also needs room for ANGLE/Metal resources and iOS itself.
    if (physical <= (3ULL << 30)) return requested > 128 ? 128 : requested;
    if (!increased) return requested;
    constexpr uint64_t reserve = 1280;
    const uint64_t mib = available >> 20;
    if (mib >= reserve + 512) return 512;
    if (mib >= reserve + 384) return 384;
    return requested;
}
}
