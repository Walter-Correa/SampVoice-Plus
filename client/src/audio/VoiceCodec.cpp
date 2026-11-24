#include "audio/VoiceCodec.hpp"

#include <opus.h>

namespace svplus::client::audio {

VoiceEncoder::~VoiceEncoder() {
    shutdown();
}

bool VoiceEncoder::initialize(int bitrate) {
    shutdown();
    int err = OPUS_OK;
    encoder_ = opus_encoder_create(
        static_cast<opus_int32>(svplus::protocol::kFrequency),
        1,
        OPUS_APPLICATION_VOIP,
        &err);
    if (!encoder_ || err != OPUS_OK) {
        encoder_ = nullptr;
        return false;
    }

    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(encoder_, OPUS_SET_DTX(0));
    return true;
}

void VoiceEncoder::shutdown() noexcept {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
}

int VoiceEncoder::encode(std::span<const std::int16_t> pcm,
                         std::span<std::uint8_t> outPacket) noexcept {
    if (!encoder_ || pcm.empty() || outPacket.empty()) {
        return -1;
    }
    return opus_encode(
        encoder_,
        pcm.data(),
        static_cast<int>(pcm.size()),
        outPacket.data(),
        static_cast<opus_int32>(outPacket.size()));
}

VoiceDecoder::~VoiceDecoder() {
    shutdown();
}

bool VoiceDecoder::initialize() {
    shutdown();
    int err = OPUS_OK;
    decoder_ = opus_decoder_create(
        static_cast<opus_int32>(svplus::protocol::kFrequency),
        1,
        &err);
    if (!decoder_ || err != OPUS_OK) {
        decoder_ = nullptr;
        return false;
    }
    return true;
}

void VoiceDecoder::shutdown() noexcept {
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}

int VoiceDecoder::decode(std::span<const std::uint8_t> packet,
                         std::span<std::int16_t> outPcm,
                         bool decodeFec) noexcept {
    if (!decoder_ || outPcm.empty()) {
        return -1;
    }
    if (packet.empty()) {
        // Packet Loss Concealment (20ms default frame when packet missing)
        return opus_decode(decoder_, nullptr, 0, outPcm.data(), static_cast<int>(outPcm.size()), 0);
    }
    return opus_decode(
        decoder_,
        packet.data(),
        static_cast<opus_int32>(packet.size()),
        outPcm.data(),
        static_cast<int>(outPcm.size()),
        decodeFec ? 1 : 0);
}

void VoiceDecoder::reset() noexcept {
    if (decoder_) {
        opus_decoder_ctl(decoder_, OPUS_RESET_STATE);
    }
}

} // namespace svplus::client::audio
