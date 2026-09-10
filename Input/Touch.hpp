#pragma once
#include "Core/SPSCRing.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace emu {
struct InputEvent { uint16_t type, code; int32_t value; };
enum class TouchPhase { down, move, up, cancel };
enum class Rotation { upright, right, inverted, left };
struct Point { double x, y; };
std::optional<Point> guestPoint(Point point, double viewWidth, double viewHeight,
                                int guestWidth, int guestHeight, Rotation rotation, bool clamp);
// Codes are Linux input-event codes, not Android KeyEvent values.
// Requires Generic.kl: KEY_HOMEPAGE (172) maps to Android HOME;
// KEY_HOME (102) maps to MOVE_HOME. The guest device must not select qwerty2.kl.
enum class HardwareKey : uint16_t { back = 158, home = 172, recents = 580, power = 116,
                                    volumeUp = 115, volumeDown = 114, menu = 139, search = 217 };
class TouchInput {
    struct Slot { bool active = false; uint64_t identity = 0; int32_t tracking = -1; };
    std::array<Slot, 10> slots_{};
    uint32_t tracking_ = 0;
    SPSCRing<InputEvent, 4096> events_;
public:
    // Producer is the UIKit thread. State commits only if the complete SYN frame fits.
    bool touch(uint64_t identity, TouchPhase phase, int32_t x, int32_t y) noexcept;
    bool key(HardwareKey key, int32_t value) noexcept;
    bool cancelAll() noexcept;
    size_t read(std::span<InputEvent> output) noexcept { return events_.pop(output); }
};
}
