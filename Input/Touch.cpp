#include "Touch.hpp"
#include <algorithm>
#include <cmath>

namespace emu {
std::optional<Point> guestPoint(Point p, double vw, double vh, int gw, int gh, Rotation rotation, bool clamp) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(vw) || !std::isfinite(vh) ||
        vw <= 0 || vh <= 0 || gw <= 0 || gh <= 0) return std::nullopt;
    const bool sideways = rotation == Rotation::left || rotation == Rotation::right;
    const double width = sideways ? gh : gw, height = sideways ? gw : gh;
    const double scale = std::min(vw / width, vh / height);
    double x = (p.x - (vw - width * scale) / 2) / (width * scale);
    double y = (p.y - (vh - height * scale) / 2) / (height * scale);
    if (!clamp && (x < 0 || y < 0 || x > 1 || y > 1)) return std::nullopt;
    x = std::clamp(x, 0.0, 1.0); y = std::clamp(y, 0.0, 1.0);
    Point result{x, y};
    switch (rotation) {
        case Rotation::upright: break;
        case Rotation::right: result = {y, 1 - x}; break;
        case Rotation::inverted: result = {1 - x, 1 - y}; break;
        case Rotation::left: result = {1 - y, x}; break;
    }
    return Point{result.x * (gw - 1), result.y * (gh - 1)};
}
bool TouchInput::touch(uint64_t identity, TouchPhase phase, int32_t x, int32_t y) noexcept {
    size_t index = slots_.size();
    size_t active = 0;
    for (size_t i = 0; i < slots_.size(); ++i) {
        active += slots_[i].active;
        if (slots_[i].active && slots_[i].identity == identity) index = i;
    }
    const bool down = phase == TouchPhase::down;
    const bool end = phase == TouchPhase::up || phase == TouchPhase::cancel;
    if (down) {
        if (index != slots_.size()) return false;
        for (size_t i = 0; i < slots_.size(); ++i) if (!slots_[i].active) { index = i; break; }
    }
    if (index == slots_.size()) return false;
    std::array<InputEvent, 7> packet{};
    size_t n = 0;
    if (down && active == 0) packet[n++] = {1, 330, 1}; // BTN_TOUCH first per Linux MT documentation.
    if (end && active == 1) packet[n++] = {1, 330, 0};
    packet[n++] = {3, 47, static_cast<int32_t>(index)}; // ABS_MT_SLOT
    // Keep IDs positive and avoid collisions across wrap-around.
    int32_t tracking = static_cast<int32_t>(tracking_ & 0x7fffffffU);
    if (down) {
        bool collision;
        do {
            collision = false;
            for (const auto& slot : slots_) if (slot.active && slot.tracking == tracking) collision = true;
            if (collision) tracking = static_cast<int32_t>((uint32_t(tracking) + 1) & 0x7fffffffU);
        } while (collision);
        packet[n++] = {3, 57, tracking};
    }
    if (end) packet[n++] = {3, 57, -1};
    else {
        packet[n++] = {3, 53, x}; packet[n++] = {3, 54, y};
    }
    packet[n++] = {0, 0, 0};
    if (!events_.push(std::span(packet.data(), n))) return false;
    if (down) { slots_[index] = {true, identity, tracking}; tracking_ = uint32_t(tracking) + 1; }
    if (end) slots_[index].active = false;
    return true;
}
bool TouchInput::key(HardwareKey key, int32_t value) noexcept {
    if (value < 0 || value > 2) return false;
    const InputEvent packet[] = {{1, static_cast<uint16_t>(key), value}, {0, 0, 0}};
    return events_.push(packet);
}
bool TouchInput::cancelAll() noexcept {
    std::array<InputEvent, 22> packet{};
    size_t n = 1;
    packet[0] = {1, 330, 0};
    for (size_t i = 0; i < slots_.size(); ++i) if (slots_[i].active) {
        packet[n++] = {3, 47, static_cast<int32_t>(i)};
        packet[n++] = {3, 57, -1};
    }
    if (n == 1) return true;
    packet[n++] = {0, 0, 0};
    if (!events_.push(std::span(packet.data(), n))) return false;
    for (auto& slot : slots_) slot.active = false;
    return true;
}
}
