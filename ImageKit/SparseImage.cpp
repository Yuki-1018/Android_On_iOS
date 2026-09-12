#include "SparseImage.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace emu {
namespace {
struct FD {
    int value;
    explicit FD(int v): value(v) { if (v < 0) throw std::runtime_error(std::strerror(errno)); }
    ~FD() { close(value); }
    FD(const FD&) = delete;
    FD& operator=(const FD&) = delete;
};
[[noreturn]] void invalid(const char* why) { throw std::runtime_error(why); }
void readAll(int fd, void* memory, size_t length) {
    auto* p = static_cast<uint8_t*>(memory);
    while (length) {
        auto n = read(fd, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) invalid("Truncated image or read failure");
        p += n; length -= static_cast<size_t>(n);
    }
}
void writeAll(int fd, const void* memory, size_t length) {
    const auto* p = static_cast<const uint8_t*>(memory);
    while (length) {
        auto n = write(fd, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) invalid("Image storage write failed");
        p += n; length -= static_cast<size_t>(n);
    }
}
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
constexpr auto crcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0);
        table[i] = c;
    }
    return table;
}
constexpr auto table = crcTable();
uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return crc;
}
// Advance the CRC through zero bytes in O(log n), without reading or writing
// sparse holes. Linear transformation over GF(2), including the current state.
uint32_t crcZeros(uint32_t crc, uint64_t bytes) {
    auto apply = [](const std::array<uint32_t, 32>& matrix, uint32_t value) {
        uint32_t result = 0;
        for (unsigned bit = 0; value; ++bit, value >>= 1) {
            if (value & 1) result ^= matrix[bit];
        }
        return result;
    };
    std::array<uint32_t, 32> matrix{};
    for (unsigned bit = 0; bit < 32; ++bit) {
        const uint32_t v = uint32_t{1} << bit;
        matrix[bit] = table[v & 255] ^ (v >> 8);
    }
    while (bytes) {
        if (bytes & 1) crc = apply(matrix, crc);
        bytes >>= 1;
        if (!bytes) break;
        auto squared = matrix;
        for (unsigned bit = 0; bit < 32; ++bit) squared[bit] = apply(matrix, matrix[bit]);
        matrix = squared;
    }
    return crc;
}
}
ImageCopyResult copyAndroidImage(const std::filesystem::path& source,
                                 const std::filesystem::path& destination, uint64_t maximumBytes) {
    FD input(open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat st{};
    if (fstat(input.value, &st) || !S_ISREG(st.st_mode) || st.st_size < 4) invalid("Image must be a regular nonempty file");
    if (static_cast<uint64_t>(st.st_size) > maximumBytes) invalid("Input image exceeds size limit");
    FD output(open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    try {
        std::array<uint8_t, 65536> buffer{};
        std::array<uint8_t, 28> header{};
        readAll(input.value, header.data(), 4);
        bool sparse = u32(header.data()) == 0xed26ff3a;
        uint64_t size = 0;
        if (!sparse) {
            size = static_cast<uint64_t>(st.st_size);
            writeAll(output.value, header.data(), 4);
            uint64_t remaining = size - 4;
            while (remaining) {
                auto n = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
                readAll(input.value, buffer.data(), n); writeAll(output.value, buffer.data(), n); remaining -= n;
            }
        } else {
            readAll(input.value, header.data() + 4, 24);
            const uint16_t fileHeader = u16(header.data() + 8), chunkHeader = u16(header.data() + 10);
            const uint32_t blockSize = u32(header.data() + 12), blocks = u32(header.data() + 16);
            const uint32_t chunks = u32(header.data() + 20), expectedCRC = u32(header.data() + 24);
            if (u16(header.data() + 4) != 1 || fileHeader < 28 || chunkHeader < 12 ||
                blockSize == 0 || blockSize % 4 || blocks == 0 || chunks == 0) invalid("Invalid sparse header");
            size = uint64_t(blockSize) * blocks;
            if (size > maximumBytes || size > INT64_MAX) invalid("Expanded sparse image exceeds size limit");
            readAll(input.value, buffer.data(), fileHeader - 28);
            uint64_t producedBlocks = 0;
            uint32_t crc = 0xffffffffU;
            for (uint32_t i = 0; i < chunks; ++i) {
                std::array<uint8_t, 12> chunk{};
                readAll(input.value, chunk.data(), chunk.size());
                const auto type = u16(chunk.data());
                const auto count = u32(chunk.data() + 4), total = u32(chunk.data() + 8);
                if (total < chunkHeader || count > blocks - producedBlocks) invalid("Sparse chunk exceeds declared image");
                readAll(input.value, buffer.data(), chunkHeader - 12);
                uint64_t length = uint64_t(count) * blockSize;
                const auto payload = total - chunkHeader;
                if (type == 0xcac4) {
                    if (count != 0 || payload != 4) invalid("Invalid CRC32 chunk");
                    readAll(input.value, buffer.data(), 4);
                    if (u32(buffer.data()) != (crc ^ 0xffffffffU)) invalid("Sparse CRC32 mismatch");
                    continue;
                }
                if (type == 0xcac1) {
                    if (length != payload) invalid("Invalid RAW chunk length");
                } else if (type == 0xcac2) {
                    if (payload != 4) invalid("Invalid FILL chunk length");
                    readAll(input.value, buffer.data(), 4);
                    for (size_t j = 4; j < buffer.size(); ++j) buffer[j] = buffer[j % 4];
                } else if (type == 0xcac3) {
                    if (payload != 0) invalid("Invalid DONT_CARE chunk length");
                    crc = crcZeros(crc, length);
                    if (lseek(output.value, static_cast<off_t>(length), SEEK_CUR) < 0) invalid("Cannot create sparse output hole");
                    producedBlocks += count;
                    continue;
                } else invalid("Unsupported sparse chunk type");
                while (length) {
                    const auto n = static_cast<size_t>(std::min<uint64_t>(length, buffer.size()));
                    if (type == 0xcac1) readAll(input.value, buffer.data(), n);
                    crc = crcUpdate(crc, buffer.data(), n);
                    writeAll(output.value, buffer.data(), n);
                    length -= n;
                }
                producedBlocks += count;
            }
            if (producedBlocks != blocks) invalid("Sparse block count mismatch");
            if (expectedCRC && expectedCRC != (crc ^ 0xffffffffU)) invalid("Sparse image checksum mismatch");
            if (ftruncate(output.value, static_cast<off_t>(size))) invalid("Cannot set raw image length");
        }
        uint8_t extra;
        ssize_t trailing;
        do { trailing = read(input.value, &extra, 1); } while (trailing < 0 && errno == EINTR);
        if (trailing != 0) invalid("Image has trailing data or changed during import");
        if (fsync(output.value)) invalid("Cannot sync imported image");
        return {size, sparse};
    } catch (...) { unlink(destination.c_str()); throw; }
}
}
