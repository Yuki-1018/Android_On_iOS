#include "Protocol.hpp"
#include <algorithm>
#include <stdexcept>
namespace emu::adb {
namespace {
void put(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (i * 8))); }
uint32_t get(std::span<const uint8_t> b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at+1]) << 8 | uint32_t(b[at+2]) << 16 | uint32_t(b[at+3]) << 24;
}
uint32_t checksum(std::span<const uint8_t> bytes) { uint32_t result = 0; for (auto b : bytes) result += b; return result; }
}
std::vector<uint8_t> encode(const Packet& p) {
    if (p.payload.size() > maxPayload) throw std::invalid_argument("ADB payload exceeds API 22 limit");
    std::vector<uint8_t> out; out.reserve(24 + p.payload.size());
    put(out, p.command); put(out, p.arg0); put(out, p.arg1); put(out, uint32_t(p.payload.size()));
    put(out, checksum(p.payload)); put(out, p.command ^ 0xffffffffU);
    out.insert(out.end(), p.payload.begin(), p.payload.end()); return out;
}
Packet decode(std::span<const uint8_t> bytes) {
    if (bytes.size() < 24) throw std::invalid_argument("Truncated ADB header");
    const auto length = get(bytes, 12), cmd = get(bytes, 0);
    if (length > maxPayload || bytes.size() != 24 + length || (cmd ^ get(bytes, 20)) != 0xffffffffU)
        throw std::invalid_argument("Invalid ADB header");
    auto payload = bytes.subspan(24);
    if (checksum(payload) != get(bytes, 16)) throw std::invalid_argument("ADB checksum mismatch");
    return {cmd, get(bytes, 4), get(bytes, 8), {payload.begin(), payload.end()}};
}
std::string shellQuote(const std::string& value) {
    if (value.find('\0') != std::string::npos) throw std::invalid_argument("NUL in shell argument");
    std::string out = "'";
    for (char c : value) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}
std::string installCommand(const std::string& name, bool replace) {
    if (name.empty() || name.size() > 100 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    }) || name == "." || name == "..") throw std::invalid_argument("Invalid staged APK basename");
    return std::string("pm install ") + (replace ? "-r " : "") + shellQuote("/data/local/tmp/" + name);
}
}
