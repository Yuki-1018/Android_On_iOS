#include "RSAPublicKey.hpp"
#include <stdexcept>
namespace emu::adb {
std::array<uint8_t,524> androidPublicKey(std::span<const uint8_t> modulus, uint32_t exponent) {
    if (modulus.size()!=256 || !(modulus.front()&0x80) || !(modulus.back()&1) || exponent!=65537)
        throw std::invalid_argument("ADB requires a 2048-bit RSA key with exponent 65537");
    std::array<uint32_t,64> n{}, rr{};
    for (size_t i=0;i<256;++i) n[i/4] |= uint32_t(modulus[255-i]) << ((i%4)*8);
    uint32_t inverse=1;
    for (int i=0;i<5;++i) inverse*=2-n[0]*inverse;
    rr[0]=1;
    // Repeated modular doubling computes R^2 mod n without a big-number library.
    for (int bit=0;bit<4096;++bit) {
        uint32_t carry=0;
        for (size_t i=0;i<64;++i) { uint32_t next=rr[i]>>31; rr[i]=(rr[i]<<1)|carry; carry=next; }
        bool ge=carry!=0;
        if (!ge) {
            ge=true;
            for (size_t i=64;i-->0;) { if (rr[i]!=n[i]) { ge=rr[i]>n[i]; break; } }
        }
        if (ge) {
            uint64_t borrow=0;
            for (size_t i=0;i<64;++i) { uint64_t sub=uint64_t(n[i])+borrow; uint32_t before=rr[i]; rr[i]=uint32_t(uint64_t(before)-sub); borrow=uint64_t(before)<sub; }
        }
    }
    std::array<uint8_t,524> out{};
    auto put=[&](size_t at,uint32_t value) { for (int i=0;i<4;++i) out[at+i]=uint8_t(value>>(i*8)); };
    put(0,64); put(4,0-inverse);
    for (size_t i=0;i<64;++i) { put(8+i*4,n[i]); put(264+i*4,rr[i]); }
    put(520,exponent); return out;
}
}
