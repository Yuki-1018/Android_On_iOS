#include "Client.hpp"
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <thread>
#include <array>
namespace emu::adb {
namespace {
constexpr auto CNXN = command('C','N','X','N'), AUTH = command('A','U','T','H');
constexpr auto OPEN = command('O','P','E','N'), OKAY = command('O','K','A','Y');
constexpr auto WRTE = command('W','R','T','E'), CLSE = command('C','L','S','E');
auto deadline(std::chrono::seconds timeout = std::chrono::seconds(120)) { return std::chrono::steady_clock::now() + timeout; }
void checkDeadline(const Transport& io, std::chrono::steady_clock::time_point end) {
    if (io.cancelled()) throw std::runtime_error("ADB operation cancelled");
    if (!io.connected()) throw std::runtime_error("Android ADB transport disconnected");
    if (std::chrono::steady_clock::now() >= end) throw std::runtime_error("Android ADB response timed out");
}
uint32_t le32(std::span<const uint8_t> b, size_t offset) {
    return uint32_t(b[offset]) | uint32_t(b[offset+1]) << 8 | uint32_t(b[offset+2]) << 16 | uint32_t(b[offset+3]) << 24;
}
void put32(std::vector<uint8_t>& b, uint32_t value) { for (int i=0;i<4;++i) b.push_back(uint8_t(value >> (i*8))); }
std::vector<uint8_t> text(const std::string& value) { auto b = std::vector<uint8_t>(value.begin(), value.end()); b.push_back(0); return b; }
}
void Client::exactRead(std::span<uint8_t> bytes, Deadline end) {
    while (!bytes.empty()) {
        checkDeadline(io_, end); size_t n = io_.read(bytes);
        if (n > bytes.size()) throw std::runtime_error("Invalid ADB read count");
        bytes = bytes.subspan(n); if (!n) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
void Client::exactWrite(std::span<const uint8_t> bytes, Deadline end) {
    while (!bytes.empty()) {
        checkDeadline(io_, end); size_t n = io_.write(bytes);
        if (n > bytes.size()) throw std::runtime_error("Invalid ADB write count");
        bytes = bytes.subspan(n); if (!n) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
Packet Client::receive(Deadline end) {
    std::vector<uint8_t> bytes(24); exactRead(bytes, end);
    uint32_t n = le32(bytes,12);
    if (n > maxPayload) throw std::runtime_error("Oversized ADB packet");
    bytes.resize(24+n); exactRead(std::span(bytes).subspan(24),end); return decode(bytes);
}
void Client::send(const Packet& packet, Deadline end) { auto bytes = encode(packet); exactWrite(bytes,end); }
void Client::connect() {
    auto end = deadline();
    send({CNXN,0x01000000,maxPayload,text("host::androidemu")},end);
    bool signedOnce = false;
    for (unsigned messages=0;messages<16;++messages) {
        Packet p = receive(end);
        if (p.command == CNXN) {
            if (p.arg0 < 0x01000000 || p.arg1 < 256) throw std::runtime_error("Unsupported Android ADB version");
            limit_ = std::min<uint32_t>(maxPayload,p.arg1); return;
        }
        if (p.command != AUTH || p.arg0 != 1 || p.payload.size() != 20) throw std::runtime_error("Invalid ADB handshake");
        auto response = signedOnce ? io_.publicKey() : io_.signToken(p.payload);
        if (response.empty() || response.size() > maxPayload) throw std::runtime_error("Invalid ADB authorization response");
        send({AUTH,signedOnce ? 3U : 2U,0,std::move(response)},end); signedOnce = true;
    }
    throw std::runtime_error("Android rejected ADB authorization");
}
void Client::open(const std::string& service) {
    if (service.size() >= limit_ || service.find('\0') != std::string::npos) throw std::invalid_argument("Invalid ADB service");
    local_ = next_++; if (!local_) local_ = next_++;
    remote_ = 0; buffered_.clear(); closed_ = false;
    auto end = deadline(timeout_); send({OPEN,local_,0,text(service)},end);
    while (true) {
        auto p=receive(end);
        if (p.arg1 != local_ && p.command == CLSE) continue;
        if (p.arg1 != local_ || p.command != OKAY || !p.arg0) { closed_=true; throw std::runtime_error("Android refused ADB service"); }
        remote_=p.arg0; return;
    }
}
void Client::acceptData(const Packet& p) {
    if (p.arg0 != remote_ || p.arg1 != local_) throw std::runtime_error("ADB stream ID mismatch");
    if (p.payload.size() > 65536-buffered_.size()) throw std::runtime_error("ADB stream backpressure limit");
    buffered_.insert(buffered_.end(),p.payload.begin(),p.payload.end());
    send({OKAY,local_,remote_,{}},deadline());
}
void Client::write(std::span<const uint8_t> data) {
    while (!data.empty()) {
        auto end=deadline(timeout_); auto part=data.first(std::min<size_t>(data.size(),limit_));
        send({WRTE,local_,remote_,{part.begin(),part.end()}},end);
        while (true) {
            auto p=receive(end);
            if (p.command==CLSE && p.arg1!=local_) continue;
            if (p.command==WRTE) { acceptData(p); continue; }
            if (p.command==OKAY && p.arg0==remote_ && p.arg1==local_) break;
            throw std::runtime_error("ADB stream closed during write");
        }
        data=data.subspan(part.size());
    }
}
std::vector<uint8_t> Client::read() {
    if (!buffered_.empty()) { auto b=std::move(buffered_); buffered_.clear(); return b; }
    if (closed_) return {};
    auto end=deadline(timeout_);
    while (true) {
        auto p=receive(end);
        if (p.command==CLSE && p.arg1!=local_) continue;
        if (p.command==WRTE) { acceptData(p); auto b=std::move(buffered_); buffered_.clear(); return b; }
        if (p.command==CLSE && p.arg1==local_) { close(); return {}; }
        throw std::runtime_error("Unexpected ADB stream packet");
    }
}
void Client::close() { if (!closed_) { closed_=true; send({CLSE,local_,remote_,{}},deadline()); } }
std::string Client::shell(const std::string& commandText, size_t limit, std::chrono::seconds timeout) {
    timeout_ = timeout;
    open("shell:"+commandText); std::string output;
    try {
        while (!closed_) {
            auto data=read();
            if (data.size()>limit-output.size()) throw std::runtime_error("ADB command output limit exceeded");
            output.append(data.begin(),data.end());
        }
    } catch (...) { try { close(); } catch (...) {} throw; }
    return output;
}
void Client::push(const std::filesystem::path& source, const std::string& destination,
                  const std::function<void(uint64_t,uint64_t)>& progress) {
    timeout_ = std::chrono::seconds(120);
    if (destination.empty() || destination.size()>1024 || destination.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid sync destination");
    struct File { int fd; ~File() { if (fd >= 0) ::close(fd); } } file{::open(source.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};
    struct stat info{};
    if (file.fd < 0 || fstat(file.fd,&info) || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        uint64_t(info.st_size)>512ULL*1024*1024) throw std::invalid_argument("APK must be a regular non-symlink file <=512 MiB");
    uint64_t size=uint64_t(info.st_size);
    open("sync:");
    try {
        auto path=destination+",33188"; std::vector<uint8_t> request;
        put32(request,command('S','E','N','D')); put32(request,uint32_t(path.size())); request.insert(request.end(),path.begin(),path.end()); write(request);
        // Fit each sync DATA header and data into one classic ADB packet.
        // 4096 data bytes used to require two stop-and-wait round trips.
        std::array<uint8_t,4088> block; uint64_t sent=0;
        while (sent<size) {
            size_t count=std::min<uint64_t>(block.size(),size-sent);
            size_t received=0;
            while (received<count) {
                ssize_t n=::read(file.fd,block.data()+received,count-received);
                if (n<0 && errno==EINTR) continue;
                if (n<=0) throw std::runtime_error("APK changed or could not be read during transfer");
                received+=size_t(n);
            }
            request.clear(); put32(request,command('D','A','T','A')); put32(request,uint32_t(count));
            request.insert(request.end(),block.begin(),block.begin()+count); write(request);
            sent+=count; if (progress) progress(sent,size);
        }
        request.clear(); put32(request,command('D','O','N','E')); put32(request,0); write(request);
        std::vector<uint8_t> reply;
        while (reply.size()<8) {
            auto chunk=read(); if (chunk.empty()) throw std::runtime_error("Truncated sync response");
            reply.insert(reply.end(),chunk.begin(),chunk.end());
        }
        uint32_t length=le32(reply,4);
        if (length>4096) throw std::runtime_error("Oversized sync failure");
        while (reply.size()<8+length) {
            auto chunk=read(); if (chunk.empty()) throw std::runtime_error("Truncated sync failure");
            reply.insert(reply.end(),chunk.begin(),chunk.end());
        }
        if (le32(reply,0)!=OKAY || length) throw std::runtime_error("Android sync failed: "+std::string(reply.begin()+8,reply.begin()+8+length));
        close();
    } catch (...) { try { close(); } catch (...) {} throw; }
}
}
