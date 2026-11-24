/*
 * SampVoice Plus - Fast CRC32C (Castagnoli) Checksum
 *
 * Produces bit-identical results to CyberMor/AmyrAhmady VoicePacket::CalcHash()
 * using a 256-entry lookup table for high-throughput UDP packet validation.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "Protocol.hpp"

namespace svplus::crypto {

/**
 * Computes CRC32C (polynomial 0x82F63B78, reflected) over a byte buffer.
 */
uint32_t Crc32c(const void* data, std::size_t length, uint32_t seed = 0) noexcept;

/**
 * Computes the VoicePacketHeader hash (over bytes following the 'hash' field).
 */
inline uint32_t ComputeVoiceHeaderHash(const protocol::VoicePacketHeader& header) noexcept {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&header) + sizeof(header.hash);
    return Crc32c(bytes, sizeof(protocol::VoicePacketHeader) - sizeof(header.hash));
}

/**
 * Verifies that VoicePacketHeader::hash matches the computed CRC32C of the header.
 */
inline bool VerifyVoiceHeaderHash(const protocol::VoicePacketHeader& header) noexcept {
    return header.hash == ComputeVoiceHeaderHash(header);
}

} // namespace svplus::crypto
