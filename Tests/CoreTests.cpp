#include "ImageKit/SparseImage.hpp"
#include "Input/Touch.hpp"
#include "Input/Keyboard.hpp"
#include "Audio/PCMRing.hpp"
#include "Display/FrameMailbox.hpp"
#include "ADB/Protocol.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
int checks = 0;
void check(bool condition) { ++checks; if (!condition) throw std::runtime_error("Check " + std::to_string(checks) + " failed"); }
template<class F> void rejects(F fn) { bool threw = false; try { fn(); } catch (const std::exception&) { threw = true; } check(threw); }
void put(std::vector<uint8_t>& b, uint32_t v, int n = 4) { for (int i = 0; i < n; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void save(const std::filesystem::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
std::vector<uint8_t> sparse() {
    std::vector<uint8_t> b;
    put(b, 0xed26ff3a); put(b, 1, 2); put(b, 0, 2); put(b, 28, 2); put(b, 12, 2);
    put(b, 4); put(b, 3); put(b, 3); put(b, 0);
    put(b, 0xcac1, 2); put(b, 0, 2); put(b, 1); put(b, 16); put(b, 0x04030201);
    put(b, 0xcac2, 2); put(b, 0, 2); put(b, 1); put(b, 16); put(b, 0x08070605);
    put(b, 0xcac3, 2); put(b, 0, 2); put(b, 1); put(b, 12);
    return b;
}
void imageTests(const std::filesystem::path& dir) {
    auto src = dir / "source", out = dir / "out";
    auto b = sparse(); save(src, b);
    auto result = emu::copyAndroidImage(src, out, 1024);
    check(result.wasSparse && result.bytes == 12 && std::filesystem::file_size(out) == 12);
    std::ifstream f(out, std::ios::binary); std::vector<uint8_t> raw(std::istreambuf_iterator<char>(f), {});
    check(raw == std::vector<uint8_t>({1,2,3,4,5,6,7,8,0,0,0,0}));
    rejects([&] { emu::copyAndroidImage(src, out, 1024); });
    check(std::filesystem::file_size(out) == 12);
    std::filesystem::remove(out);
    for (size_t length = 0; length < b.size(); ++length) {
        save(src, {b.begin(), b.begin() + length});
        rejects([&] { emu::copyAndroidImage(src, out, 1024); });
        check(!std::filesystem::exists(out));
    }
    save(src, b); rejects([&] { emu::copyAndroidImage(src, out, 8); });
    b.push_back(1); save(src,b); rejects([&] { emu::copyAndroidImage(src,out,1024); }); b.pop_back();
    b[24] = 1; save(src,b); rejects([&] { emu::copyAndroidImage(src,out,1024); });
    b = sparse(); b[28] = 0xff; save(src,b); rejects([&] { emu::copyAndroidImage(src,out,1024); });
    b = sparse(); b[32] = 4; save(src,b); rejects([&] { emu::copyAndroidImage(src,out,1024); });
    save(src, raw); result = emu::copyAndroidImage(src,out,1024); check(!result.wasSparse && result.bytes == 12);
    std::filesystem::remove(out);
    std::filesystem::create_symlink(src, dir / "link");
    rejects([&] { emu::copyAndroidImage(dir / "link", out, 1024); });
    std::filesystem::create_symlink(src, out);
    rejects([&] { emu::copyAndroidImage(src, out, 1024); });
}
void ringTests() {
    emu::SPSCRing<uint32_t, 16> ring;
    std::atomic<bool> correct{true};
    constexpr uint32_t count = 500000;
    std::thread producer([&] {
        for (uint32_t i = 0; i < count; ++i) while (!ring.push(std::span(&i, 1))) std::this_thread::yield();
    });
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t n;
        while (!ring.pop(std::span(&n, 1))) std::this_thread::yield();
        if (n != i) correct.store(false);
    }
    producer.join(); check(correct.load());
    emu::PCMRing audio;
    std::array<emu::StereoSample, 2> frames{{{123, -123},{32767, -32768}}};
    check(audio.write(frames));
    std::array<emu::StereoSample, 4> rendered{}; audio.render(rendered);
    check(rendered[0].left == 123 && rendered[1].right == -32768 && rendered[2].left == 0);
    check(audio.underruns == 1);
}
void touchTests() {
    auto p = emu::guestPoint({100, 100}, 200, 200, 100, 200, emu::Rotation::upright, false);
    check(p && p->x == 49.5 && p->y == 99.5);
    check(!emu::guestPoint({0, 100}, 200, 200, 100, 200, emu::Rotation::upright, false));
    p = emu::guestPoint({0, 0}, 200, 100, 100, 200, emu::Rotation::right, true);
    check(p && p->x == 0 && p->y == 199);
    // Effective viewport dimensions after safe-area layout: SE, notched
    // portrait/landscape, iPad full screen and narrow Stage Manager windows.
    for (const auto size : {emu::Point{375, 667}, emu::Point{393, 759},
                           emu::Point{759, 372}, emu::Point{1024, 1322}, emu::Point{460, 700}}) {
        auto center = emu::guestPoint({size.x / 2, size.y / 2}, size.x, size.y, 360, 780, emu::Rotation::upright, false);
        check(center && std::abs(center->x - 179.5) < 0.001 && std::abs(center->y - 389.5) < 0.001);
        auto outside = emu::guestPoint({-1, -1}, size.x, size.y, 360, 780, emu::Rotation::upright, false);
        check(!outside);
    }
    emu::TouchInput input;
    for (uint64_t i = 0; i < 10; ++i) check(input.touch(i, emu::TouchPhase::down, int32_t(i), 20));
    check(!input.touch(10, emu::TouchPhase::down, 0, 0));
    check(!input.touch(0, emu::TouchPhase::down, 0, 0));
    std::array<emu::InputEvent, 128> events{};
    auto n = input.read(events); check(n == 51 && events[0].code == 330);
    check(input.touch(0, emu::TouchPhase::move, 0, 20));
    check(input.read(events) == 0); // No queue traffic for subpixel/stationary movement.
    check(input.touch(0, emu::TouchPhase::move, 1, 20));
    check(input.read(events) == 4);
    check(input.cancelAll()); n = input.read(events); check(n == 22 && events[0].code == 330 && events[0].value == 0);
    check(!input.touch(0, emu::TouchPhase::move, 1, 1));
    check(input.key(emu::HardwareKey::back, 1)); check(input.key(emu::HardwareKey::back, 0));
    n = input.read(events); check(n == 4 && events[0].code == 158 && events[2].value == 0);
    // Queue overflow must not commit a touch that the guest has never seen.
    while (input.key(emu::HardwareKey::home, 1)) {}
    check(!input.touch(42, emu::TouchPhase::down, 0, 0));
    while (input.read(events)) {}
    check(input.touch(42, emu::TouchPhase::down, 0, 0));
}
void frameTests() {
    emu::FrameMailbox mailbox(2,2);
    std::array<uint8_t,16> a{}; a[0] = 1;
    mailbox.update(a,8,{0,0,1,1}); a[12] = 2;
    mailbox.update(a,8,{1,1,1,1});
    bool called = false;
    check(mailbox.consume([](void* ctx, const uint8_t* pixels, size_t stride, emu::DirtyRect r) {
        *static_cast<bool*>(ctx) = true; check(pixels[0] == 1 && pixels[12] == 2 && stride == 8 && r.width == 2 && r.height == 2);
    }, &called));
    check(called && mailbox.stats().replaced == 1 && mailbox.stats().bytesCopied == 8);
    check(!mailbox.consume([](void*, const uint8_t*, size_t, emu::DirtyRect) {}, nullptr));
    rejects([&] { mailbox.update(a,8,{UINT32_MAX,0,1,1}); });
    rejects([&] { mailbox.update(a,SIZE_MAX,{0,0,1,1}); });
}
void adbTests() {
    emu::adb::Packet packet{emu::adb::command('W','R','T','E'),1,2,{0,1,255}};
    auto bytes = emu::adb::encode(packet); auto decoded = emu::adb::decode(bytes);
    check(decoded.payload == packet.payload && decoded.arg1 == 2);
    bytes.back() = 1; rejects([&] { emu::adb::decode(bytes); });
    bytes.resize(23); rejects([&] { emu::adb::decode(bytes); });
    check(emu::adb::shellQuote("a'b") == "'a'\\''b'");
    check(emu::adb::installCommand("test.apk", true) == "pm install -r '/data/local/tmp/test.apk'");
    rejects([] { emu::adb::installCommand("../a.apk",false); });
    rejects([] { emu::adb::shellQuote(std::string("a\0b",3)); });
}
}
int main() {
    static_assert(emu::linuxKeyForHID(4) == 30);
    static_assert(emu::linuxKeyForHID(29) == 44);
    static_assert(emu::linuxKeyForHID(74) == 102);
    static_assert(emu::linuxKeyForHID(224) == 29 && emu::linuxKeyForHID(228) == 97);
    static_assert(emu::linuxKeyForHID(231) == 126);
    static_assert(emu::linuxKeyForHID(0xffff) == 0);
    char path[] = "/tmp/androidemu-tests-XXXXXX";
    auto* tmp = mkdtemp(path);
    if (!tmp) return 2;
    const std::filesystem::path dir(tmp);
    try {
        imageTests(dir); ringTests(); touchTests(); frameTests(); adbTests();
        std::filesystem::remove_all(dir);
        std::cout << checks << " checks passed\n"; return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove_all(dir); std::cerr << e.what() << '\n'; return 1;
    }
}
