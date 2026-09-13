#ifndef ANDROIDEMU_UTF8_HPP
#define ANDROIDEMU_UTF8_HPP
#include <cstddef>
#include <cstdint>
#include <string>
namespace emu {
// Log snapshots may begin/end inside a character or contain binary guest bytes.
inline std::string logUTF8(const uint8_t *bytes, size_t size) {
    std::string text;
    text.reserve(size);
    for (size_t i = 0; i < size;) {
        uint8_t lead = bytes[i];
        size_t length = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
                        lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        bool valid = length && length <= size - i;
        for (size_t j = 1; valid && j < length; ++j)
            valid = (bytes[i+j] & 0xc0) == 0x80;
        if (valid && length >= 3) {
            uint8_t second = bytes[i+1];
            valid = !(lead == 0xe0 && second < 0xa0) && !(lead == 0xed && second >= 0xa0) &&
                    !(lead == 0xf0 && second < 0x90) && !(lead == 0xf4 && second >= 0x90);
        }
        if (valid) { text.append(reinterpret_cast<const char *>(bytes+i), length); i += length; }
        else { text.append("\xef\xbf\xbd"); ++i; }
    }
    return text;
}
}
#endif
