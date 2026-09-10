#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace emu::adb {
constexpr uint32_t command(char a, char b, char c, char d) {
    return uint32_t(a) | uint32_t(b) << 8 | uint32_t(c) << 16 | uint32_t(d) << 24;
}
struct Packet { uint32_t command, arg0, arg1; std::vector<uint8_t> payload; };
std::vector<uint8_t> encode(const Packet& packet);
Packet decode(std::span<const uint8_t> bytes);
std::string shellQuote(const std::string& value);
std::string installCommand(const std::string& sandboxName, bool replace);
// Android 5.1 uses the 24-byte classic ADB header and 4096-byte max payload.
constexpr size_t maxPayload = 4096;
}
