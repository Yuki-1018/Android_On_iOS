#include "ZipArchive.hpp"
#include <array>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>
namespace emu {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p+2)) << 16; }
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
struct FD { int fd; ~FD() { if (fd >= 0) ::close(fd); } };
}
void extractImageZip(const std::filesystem::path& source, const std::filesystem::path& root,
                     const std::function<bool()>& cancelled) {
    FD input{::open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
    struct stat st{};
    require(input.fd >= 0 && !fstat(input.fd, &st) && S_ISREG(st.st_mode) && st.st_size >= 22 &&
            uint64_t(st.st_size) < UINT32_MAX, "ZIP must be a regular file smaller than 4 GiB (ZIP64 unsupported)");
    auto read = [&](uint64_t offset, size_t length) {
        require(!cancelled || !cancelled(), "ZIP extraction cancelled");
        require(offset <= uint64_t(st.st_size) && length <= uint64_t(st.st_size) - offset, "Truncated ZIP");
        std::vector<uint8_t> data(length);
        size_t done = 0;
        while (done < length) {
            auto n = pread(input.fd, data.data()+done, length-done, off_t(offset+done));
            if (n < 0 && errno == EINTR) continue;
            require(n > 0, "ZIP read failed"); done += size_t(n);
        }
        return data;
    };
    size_t tailSize = size_t(std::min<int64_t>(st.st_size, 65557));
    auto tail = read(uint64_t(st.st_size)-tailSize, tailSize);
    size_t end = tail.size()-22;
    while (!(u32(tail.data()+end) == 0x06054b50 && end+22+u16(tail.data()+end+20) == tail.size())) {
        require(end != 0, "ZIP directory not found"); --end;
    }
    auto e = tail.data()+end;
    const auto entries = u16(e+10);
    uint64_t position = u32(e+16), centralEnd = position + u32(e+12);
    require(!u16(e+4) && !u16(e+6) && entries && entries != 65535 && entries <= 10000 &&
            entries == u16(e+8) && centralEnd == uint64_t(st.st_size)-tailSize+end, "Unsupported ZIP directory / ZIP64");
    require(std::filesystem::create_directory(root), "ZIP destination must not exist");
    try {
        uint64_t total = 0;
        for (unsigned i = 0; i < entries; ++i) {
            require(position + 46 <= centralEnd, "Invalid ZIP directory entry");
            auto header = read(position, 46); auto p = header.data();
            require(u32(p) == 0x02014b50, "Invalid ZIP directory signature");
            auto flags = u16(p+8), method = u16(p+10), nameSize = u16(p+28);
            uint32_t crc = u32(p+16), packed = u32(p+20), unpacked = u32(p+24), localOffset = u32(p+42);
            uint16_t mode = uint16_t(u32(p+38) >> 16);
            require(!(flags & 0x2041) && (method == 0 || method == 8) && !u16(p+34) &&
                    packed != UINT32_MAX && unpacked != UINT32_MAX && localOffset != UINT32_MAX,
                    "Encrypted, split or ZIP64 images are unsupported");
            require(nameSize && nameSize <= 4096, "Invalid ZIP filename");
            position += 46;
            require(position + nameSize + u16(p+30) + u16(p+32) <= centralEnd, "ZIP entry exceeds directory");
            auto nameBytes = read(position, nameSize);
            std::string name(nameBytes.begin(), nameBytes.end());
            position += nameSize + u16(p+30) + u16(p+32);
            require(name.find('\0') == std::string::npos && name.find('\\') == std::string::npos &&
                    name.find(':') == std::string::npos && name[0] != '/', "Unsafe ZIP path");
            std::filesystem::path relative(name);
            unsigned depth = 0;
            for (const auto& component : relative) {
                require(component != ".." && component != "." && ++depth <= 32, "Unsafe ZIP path depth/traversal");
            }
            bool directory = name.back() == '/';
            require(!(mode & S_IFMT) || (directory ? (mode & S_IFMT) == S_IFDIR : (mode & S_IFMT) == S_IFREG),
                    "ZIP links and special files are forbidden");
            const auto destination = root / relative;
            if (directory) {
                require(unpacked == 0, "ZIP directory contains data");
                std::filesystem::create_directories(destination); continue;
            }
            total += unpacked;
            require(total <= (16ULL << 30), "Expanded ZIP exceeds 16 GiB");
            auto local = read(localOffset, 30);
            require(u32(local.data()) == 0x04034b50 && u16(local.data()+6) == flags &&
                    u16(local.data()+8) == method && u16(local.data()+26) == nameSize, "ZIP header mismatch");
            require(read(uint64_t(localOffset)+30, nameSize) == nameBytes, "ZIP filename mismatch");
            uint64_t offset = uint64_t(localOffset)+30+nameSize+u16(local.data()+28);
            require(offset <= u32(e+16) && packed <= u32(e+16)-offset, "ZIP data overlaps directory");
            std::filesystem::create_directories(destination.parent_path());
            FD output{::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
            require(output.fd >= 0, "ZIP file collision or storage failure");
            z_stream stream{};
            require(method == 0 || inflateInit2(&stream, -MAX_WBITS) == Z_OK, "Cannot initialize ZIP decompressor");
            struct Inflater { z_stream* stream; bool active; ~Inflater() { if (active) inflateEnd(stream); } } inflater{&stream, method == 8};
            std::array<uint8_t, 65536> buffer{};
            uint64_t written = 0; uint32_t checksum = uint32_t(crc32(0, Z_NULL, 0)); int result = Z_OK;
            auto write = [&](const uint8_t* data, size_t n) {
                require(!cancelled || !cancelled(), "ZIP extraction cancelled");
                require(n <= unpacked-written, "ZIP expanded size mismatch");
                checksum = uint32_t(crc32(checksum, data, uInt(n))); written += n;
                while (n) {
                    auto count = ::write(output.fd, data, n);
                    if (count < 0 && errno == EINTR) continue;
                    require(count > 0, "ZIP storage full or write failed"); data += count; n -= size_t(count);
                }
            };
            uint64_t remaining = packed;
            while (remaining) {
                auto data = read(offset, size_t(std::min<uint64_t>(remaining, buffer.size())));
                offset += data.size(); remaining -= data.size();
                if (method == 0) { write(data.data(), data.size()); continue; }
                stream.next_in = data.data(); stream.avail_in = uInt(data.size());
                do {
                    stream.next_out = buffer.data(); stream.avail_out = uInt(buffer.size());
                    result = inflate(&stream, Z_NO_FLUSH);
                    require(result == Z_OK || result == Z_STREAM_END, "Invalid ZIP deflate stream");
                    write(buffer.data(), buffer.size()-stream.avail_out);
                    if (result == Z_STREAM_END) {
                        require(stream.avail_in == 0 && remaining == 0, "Trailing ZIP compressed data"); break;
                    }
                } while (stream.avail_in || stream.avail_out == 0);
            }
            require((method == 0 || result == Z_STREAM_END) && written == unpacked && checksum == crc,
                    "ZIP checksum or size mismatch");
            require(fsync(output.fd) == 0, "Cannot sync ZIP output");
        }
        require(position == centralEnd, "ZIP directory length mismatch");
    } catch (...) { std::filesystem::remove_all(root); throw; }
}
}
