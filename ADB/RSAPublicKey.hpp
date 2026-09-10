#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace emu::adb {
// Android's mincrypt RSAPublicKey wire format, not ASN.1. Modulus is 256-byte BE.
std::array<uint8_t,524> androidPublicKey(std::span<const uint8_t> modulus, uint32_t exponent);
}
