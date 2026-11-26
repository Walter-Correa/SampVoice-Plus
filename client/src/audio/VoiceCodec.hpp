#pragma once

#include "svplus/common/Protocol.hpp"

#include <cstdint>
#include <span>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace svplus::client::audio {

class VoiceEncoder {
public:
    VoiceEncoder() = default;
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;

    [[nodiscard]] bool initialize(int bitrate = static_cast<int>(svplus::protocol::kOpusBitrate));
    void shutdown() noexcept;

    [[nodiscard]] bool setBitrate(int bitrate) noexcept;
    [[nodiscard]] int  bitrate() const noexcept;

    [[nodiscard]] int encode(std::span<const std::int16_t> pcm,
                             std::span<std::uint8_t> outPacket) noexcept;

private:
    OpusEncoder* encoder_{nullptr};
};

class VoiceDecoder {
public:
    VoiceDecoder() = default;
    ~VoiceDecoder();
    VoiceDecoder(const VoiceDecoder&) = delete;
    VoiceDecoder& operator=(const VoiceDecoder&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;

    /// Decodes Opus packet (or PLC if packet is empty) into 16-bit PCM mono.
    /// Returns number of decoded samples per channel, or < 0 on error.
    [[nodiscard]] int decode(std::span<const std::uint8_t> packet,
                             std::span<std::int16_t> outPcm,
                             bool decodeFec = false) noexcept;

    void reset() noexcept;

private:
    OpusDecoder* decoder_{nullptr};
};

} // namespace svplus::client::audio
