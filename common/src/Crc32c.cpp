/*
 * SampVoice Plus - Fast CRC32C (Castagnoli) Checksum Implementation
 */

#include "svplus/common/Crc32c.hpp"

#include <array>

namespace svplus::crypto {

namespace {

constexpr uint32_t kCrc32cPoly = 0x82F63B78u;

constexpr std::array<uint32_t, 256> BuildCrc32cTable() noexcept {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? ((crc >> 1) ^ kCrc32cPoly) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

inline constexpr std::array<uint32_t, 256> kCrc32cTable = BuildCrc32cTable();

} // namespace

uint32_t Crc32c(const void* data, std::size_t length, uint32_t seed) noexcept {
    if (!data && length > 0) {
        return seed;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = ~seed;

    for (std::size_t i = 0; i < length; ++i) {
        crc = kCrc32cTable[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }

    return ~crc;
}

} // namespace svplus::crypto
