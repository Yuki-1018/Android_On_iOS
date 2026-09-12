#pragma once
#include "Protocol.hpp"
#include <functional>
#include <chrono>
#include <filesystem>
namespace emu::adb {
struct Transport {
    std::function<bool()> connected, cancelled;
    std::function<size_t(std::span<uint8_t>)> read;
    std::function<size_t(std::span<const uint8_t>)> write;
    std::function<std::vector<uint8_t>(std::span<const uint8_t>)> signToken;
    std::function<std::vector<uint8_t>()> publicKey;
};
// One serial owner. Bounded classic ADB transport and one stream at a time.
class Client {
    Transport io_;
    uint32_t next_ = 1, local_ = 0, remote_ = 0, limit_ = maxPayload;
    bool closed_ = true;
    std::vector<uint8_t> buffered_;
    std::chrono::seconds timeout_{120};
    using Deadline = std::chrono::steady_clock::time_point;
    void exactRead(std::span<uint8_t>, Deadline);
    void exactWrite(std::span<const uint8_t>, Deadline);
    Packet receive(Deadline);
    void send(const Packet&, Deadline);
    void open(const std::string&);
    void write(std::span<const uint8_t>);
    std::vector<uint8_t> read();
    void close();
    void acceptData(const Packet&);
public:
    explicit Client(Transport transport) : io_(std::move(transport)) {}
    void connect();
    std::string shell(const std::string&, size_t outputLimit = 1024 * 1024,
                      std::chrono::seconds timeout = std::chrono::seconds(120));
    void push(const std::filesystem::path& source, const std::string& destination,
              const std::function<void(uint64_t,uint64_t)>& progress = {});
};
}
