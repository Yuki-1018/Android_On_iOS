#pragma once
#include <array>
#include <cstdint>
namespace emu {
// USB HID keyboard usages -> Linux evdev physical keys (Generic.kl handles
// Android mapping and the guest IME/layout handles text). Never inject text
// as a sequence of guessed US-layout key presses.
constexpr uint16_t linuxKeyForHID(uint16_t usage) {
    constexpr std::array<uint16_t, 26> letters = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    if (usage >= 4 && usage <= 29) return letters[usage - 4];
    if (usage >= 30 && usage <= 38) return usage - 28;
    if (usage >= 58 && usage <= 67) return usage + 1;
    if (usage >= 224 && usage <= 231) {
        constexpr std::array<uint16_t, 8> modifiers = {29,42,56,125,97,54,100,126};
        return modifiers[usage - 224];
    }
    switch (usage) {
        case 39: return 11; case 40: return 28; case 41: return 1;
        case 42: return 14; case 43: return 15; case 44: return 57;
        case 45: return 12; case 46: return 13; case 47: return 26;
        case 48: return 27; case 49: return 43; case 50: return 43;
        case 51: return 39; case 52: return 40; case 53: return 41;
        case 54: return 51; case 55: return 52; case 56: return 53;
        case 57: return 58; case 68: return 87; case 69: return 88;
        case 70: return 99; case 71: return 70; case 72: return 119;
        case 73: return 110; case 74: return 102; case 75: return 104;
        case 76: return 111; case 77: return 107; case 78: return 109;
        case 79: return 106; case 80: return 105; case 81: return 108;
        case 82: return 103; case 83: return 69; case 84: return 98;
        case 85: return 55; case 86: return 74; case 87: return 78;
        case 88: return 96; case 89: return 79; case 90: return 80;
        case 91: return 81; case 92: return 75; case 93: return 76;
        case 94: return 77; case 95: return 71; case 96: return 72;
        case 97: return 73; case 98: return 82; case 99: return 83;
        case 100: return 86; case 101: return 127;
        default: return 0;
    }
}
}
