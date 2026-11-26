#pragma once

#include "audio/BassLoader.hpp"
#include "audio/VoiceCodec.hpp"
#include "svplus/common/Protocol.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace svplus::client::audio {

enum class StreamKind : std::uint8_t {
    Global = 0,
    LocalAtPoint,
    LocalAtVehicle,
    LocalAtPlayer,
    LocalAtObject,
};

struct StreamEffectSpec {
    std::uint32_t             effectId{0};
    std::uint32_t             effectType{0};
    std::int32_t              priority{0};
    std::vector<std::uint8_t> paramBytes{};
};

struct StreamParameterState {
    float value{0.0f};
    bool  sliding{false};
    float endValue{0.0f};
    float ratePerMs{0.0f};
    std::chrono::steady_clock::time_point endTime{};

    void set(float v) noexcept {
        value = v;
        sliding = false;
    }
    void slide(float startVal, float endVal, std::uint32_t timeMs) noexcept;
    [[nodiscard]] float current() noexcept;
};

struct BufferedVoicePacket {
    std::uint32_t seq{0};
    std::uint16_t length{0};
    std::chrono::steady_clock::time_point arrivalTime{};
    std::array<std::uint8_t, protocol::kMaxVoicePayloadBytes> payload{};
};

/// Lightweight bounded jitter buffer handling out-of-order reorder, FEC recovery, and PLC.
class SpeakerJitterBuffer {
public:
    static constexpr std::size_t kMaxBufferedFrames = 8u;
    static constexpr auto kMaxReorderWait = std::chrono::milliseconds(35);

    enum class ActionKind : std::uint8_t {
        DecodeNormal = 0,
        DecodeFecAndThenNormal,
        ConcealPlc,
    };

    struct OutputStep {
        ActionKind                   kind{ActionKind::DecodeNormal};
        std::span<const std::uint8_t> payload{};
    };

    void reset() noexcept;

    /// Enqueues an incoming encoded frame and yields ordered decoding actions immediately or on tick.
    template <typename EmitFn>
    void pushAndDrain(std::uint32_t seq,
                      std::span<const std::uint8_t> payload,
                      std::chrono::steady_clock::time_point now,
                      EmitFn&& emit);

    /// Drains frames whose reorder wait window expired (triggers FEC or PLC for missing seq).
    template <typename EmitFn>
    void drainTimedOut(std::chrono::steady_clock::time_point now, EmitFn&& emit);

    [[nodiscard]] std::uint32_t nextExpectedSeq() const noexcept { return nextExpectedSeq_; }
    [[nodiscard]] std::size_t   bufferedCount() const noexcept { return count_; }

private:
    static constexpr std::int32_t seqDiff(std::uint32_t a, std::uint32_t b) noexcept {
        return static_cast<std::int32_t>(a - b);
    }

    void insertSorted(std::uint32_t seq,
                      std::span<const std::uint8_t> payload,
                      std::chrono::steady_clock::time_point now) noexcept;
    void popFront() noexcept;

    template <typename EmitFn>
    void drainContiguous(EmitFn&& emit);

    bool          hasExpectedSeq_{false};
    std::uint32_t nextExpectedSeq_{0};
    std::size_t   count_{0};
    std::array<BufferedVoicePacket, kMaxBufferedFrames> buffer_{};
};

struct SpeakerChannel {
    static constexpr std::uint8_t  kMinPrebufferFrames     = 1u;
    static constexpr std::uint8_t  kDefaultPrebufferFrames = 2u;
    static constexpr std::uint8_t  kMaxPrebufferFrames     = 4u;
    static constexpr std::uint32_t kStableReduceFrames     = 50u; // 50 received frames (~5s speech) without underrun
    static constexpr auto          kActiveSpeechGrace      = std::chrono::milliseconds(150);

    std::uint16_t       playerId{0};
    HSTREAM             bassStream{0};
    VoiceDecoder        decoder{};
    SpeakerJitterBuffer jitter{};
    std::vector<HFX>    effects{};
    std::chrono::steady_clock::time_point lastPacketTime{};

    // Adaptive prebuffer / underrun cushion state (R2-4 + R3-4)
    bool          prebuffering{true};
    std::uint8_t  prebufferedFrames{0};
    std::uint8_t  targetPrebufferFrames{kDefaultPrebufferFrames};
    std::uint32_t stableFrames{0};

    // Persistent DSP state across packets (eliminates inter-frame high-pass / AGC clicks)
    float hpPrevX{0.0f};
    float hpPrevY{0.0f};
    float env{0.0f};

    // Smoothed software pan state (BASS_ATTRIB_PAN)
    float lastAppliedPan{0.0f};
    bool  hasAppliedPan{false};

    void beginPrebuffer(bool resetStable = true) noexcept {
        prebuffering      = true;
        prebufferedFrames = 0;
        if (resetStable) {
            stableFrames = 0;
        }
    }

    /// Returns true when the channel has queued enough prebuffer frames to start playback.
    /// Counts received/decoded frames across utterances to shrink inflated cushions in normal speech.
    [[nodiscard]] bool onDecodedFrameQueued() noexcept {
        if (prebuffering) {
            ++prebufferedFrames;
            if (prebufferedFrames >= targetPrebufferFrames) {
                prebuffering = false;
                return true;
            }
            return false;
        }

        if (targetPrebufferFrames > kMinPrebufferFrames) {
            ++stableFrames;
            if (stableFrames >= kStableReduceFrames) {
                stableFrames = 0;
                --targetPrebufferFrames;
            }
        }
        return false;
    }

    /// Distinguishes true mid-speech underruns (recent packet or pending jitter seq) from natural end-of-speech stalls.
    /// Returns true if BASS channel should pause and re-prebuffer.
    [[nodiscard]] bool onPlaybackPoll(bool stalled,
                                      std::chrono::steady_clock::time_point now) noexcept {
        if (prebuffering || !stalled) {
            return false;
        }

        const bool recentPacket =
            lastPacketTime.time_since_epoch().count() != 0 &&
            (now - lastPacketTime) <= kActiveSpeechGrace;
        const bool awaitingSeq = jitter.bufferedCount() > 0u;

        if (recentPacket || awaitingSeq) {
            // Real mid-speech underrun: widen prebuffer cushion up to max and reset stability count
            if (targetPrebufferFrames < kMaxPrebufferFrames) {
                ++targetPrebufferFrames;
            }
            beginPrebuffer(true);
        } else {
            // Natural end of utterance: re-arm prebuffer cleanly without penalizing target depth or stable progress
            beginPrebuffer(false);
        }
        return true;
    }
};

class AudioStream {
public:
    AudioStream(std::uint32_t id,
                StreamKind kind,
                std::uint32_t targetOrZero,
                float distance,
                protocol::Vec3f position,
                std::uint32_t color,
                std::string name);
    ~AudioStream();

    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    [[nodiscard]] std::uint32_t      id() const noexcept { return id_; }
    [[nodiscard]] StreamKind         kind() const noexcept { return kind_; }
    [[nodiscard]] std::uint32_t      target() const noexcept { return target_; }
    [[nodiscard]] float              distance() const noexcept;
    [[nodiscard]] protocol::Vec3f    position() const noexcept;
    [[nodiscard]] std::uint32_t      color() const noexcept { return color_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] protocol::StreamIconId iconId() const noexcept;

    void setDistance(float d) noexcept;
    void setPosition(protocol::Vec3f pos) noexcept;
    void setIconId(protocol::StreamIconId iconId) noexcept;
    void clearChannels() noexcept;

    void setParameter(std::uint32_t paramId, float value) noexcept;
    void slideParameter(std::uint32_t paramId, float startVal, float endVal, std::uint32_t timeMs) noexcept;

    void addOrUpdateEffect(const StreamEffectSpec& spec);
    void removeEffect(std::uint32_t effectId);

    void pushVoiceFrame(std::uint16_t speakerId,
                        std::uint32_t seq,
                        std::span<const std::uint8_t> opusPayload,
                        float masterVolume,
                        bool soundEnabled,
                        bool playerMuted,
                        bool applyFilter,
                        bool applyBalancer);

    void resetSpeaker(std::uint16_t speakerId) noexcept;

    using SpeakerMutedFn = bool (*)(std::uint16_t speakerId, void* ctx) noexcept;

    /// Called periodically on the audio thread to drain jitter timeouts, update software spatial volume/pan, params, and cull idle channels.
    void tick(const float resolvedPos[3],
              bool posValid,
              const float camPos[3],
              const float camFront[3],
              const float camTop[3],
              bool hasCamera,
              const float localPedPos[3],
              bool hasLocalPed,
              float masterVolume,
              bool soundEnabled,
              bool applyFilter,
              bool applyBalancer,
              SpeakerMutedFn isSpeakerMuted,
              void* mutedCtx);

    /// Thread-safe combined tick + active speaker collection under a single streamMutex_ lock.
    void tickAndCollectSpeakers(const float resolvedPos[3],
                                bool posValid,
                                const float camPos[3],
                                const float camFront[3],
                                const float camTop[3],
                                bool hasCamera,
                                const float localPedPos[3],
                                bool hasLocalPed,
                                float masterVolume,
                                bool soundEnabled,
                                bool applyFilter,
                                bool applyBalancer,
                                SpeakerMutedFn isSpeakerMuted,
                                void* mutedCtx,
                                std::vector<std::uint16_t>& outSpeakers,
                                protocol::StreamIconId& outIconId);

    struct SpatialAttenuation {
        float gain{1.0f};
        float pan{0.0f};
    };

    [[nodiscard]] static SpatialAttenuation computeSpatialAttenuation(
        StreamKind kind,
        float streamDistance,
        const float sourcePos[3],
        bool sourcePosValid,
        const float camPos[3],
        const float camFront[3],
        const float camTop[3],
        bool hasCamera,
        const float localPedPos[3] = nullptr,
        bool hasLocalPed = false) noexcept;

    void collectActiveSpeakers(std::vector<std::uint16_t>& outSpeakers) const;
    [[nodiscard]] std::size_t channelCount() const noexcept;

private:
    void tickLocked(const float resolvedPos[3],
                    bool posValid,
                    const float camPos[3],
                    const float camFront[3],
                    const float camTop[3],
                    bool hasCamera,
                    const float localPedPos[3],
                    bool hasLocalPed,
                    float masterVolume,
                    bool soundEnabled,
                    bool applyFilter,
                    bool applyBalancer,
                    SpeakerMutedFn isSpeakerMuted,
                    void* mutedCtx);
    void collectActiveSpeakersLocked(std::vector<std::uint16_t>& outSpeakers) const;
    SpeakerChannel* ensureChannel(std::uint16_t speakerId);
    void decodeAndSubmitStep(SpeakerChannel& ch,
                             const SpeakerJitterBuffer::OutputStep& step,
                             bool applyFilter,
                             bool applyBalancer) noexcept;
    void applyParametersToChannel(SpeakerChannel& ch, float masterVolume, bool soundEnabled);
    void applyEffectToChannel(SpeakerChannel& ch, const StreamEffectSpec& spec);
    void rebuildChannelEffects(SpeakerChannel& ch);

    mutable std::mutex streamMutex_{};
    std::uint32_t      id_{0};
    StreamKind         kind_{StreamKind::Global};
    std::uint32_t      target_{0};
    float              distance_{0.0f};
    protocol::Vec3f    position_{};
    std::uint32_t          color_{0xFFFFFFFFu};
    protocol::StreamIconId iconId_{protocol::StreamIconId::Speaker};
    std::string            name_{};
    SpatialAttenuation cachedSpatial_{};

    std::unordered_map<std::uint32_t, StreamParameterState> params_{};
    std::unordered_map<std::uint32_t, StreamEffectSpec>     effectSpecs_{};
    std::unordered_map<std::uint16_t, std::unique_ptr<SpeakerChannel>> channels_{};
    std::unordered_map<std::uint16_t, std::chrono::steady_clock::time_point> activeSpeakersLastSeen_{};
    std::array<std::int16_t, 5760> decodeBuf_{};
};

// -----------------------------------------------------------------------------
// SpeakerJitterBuffer Inline Template Implementation (header-only for unit tests)
// -----------------------------------------------------------------------------

inline void SpeakerJitterBuffer::reset() noexcept {
    hasExpectedSeq_  = false;
    nextExpectedSeq_ = 0;
    count_           = 0;
}

inline void SpeakerJitterBuffer::popFront() noexcept {
    if (count_ == 0) {
        return;
    }
    for (std::size_t i = 1; i < count_; ++i) {
        buffer_[i - 1] = buffer_[i];
    }
    --count_;
}

inline void SpeakerJitterBuffer::insertSorted(std::uint32_t seq,
                                              std::span<const std::uint8_t> payload,
                                              std::chrono::steady_clock::time_point now) noexcept {
    if (payload.empty() || payload.size() > protocol::kMaxVoicePayloadBytes) {
        return;
    }
    for (std::size_t i = 0; i < count_; ++i) {
        if (buffer_[i].seq == seq) {
            return; // Duplicate
        }
    }
    if (count_ == kMaxBufferedFrames) {
        // Buffer full: drop newest beyond window or make room if earlier than tail
        if (seqDiff(seq, buffer_[count_ - 1].seq) >= 0) {
            return;
        }
        --count_;
    }

    std::size_t pos = count_;
    while (pos > 0 && seqDiff(seq, buffer_[pos - 1].seq) < 0) {
        buffer_[pos] = buffer_[pos - 1];
        --pos;
    }
    buffer_[pos].seq         = seq;
    buffer_[pos].length      = static_cast<std::uint16_t>(payload.size());
    buffer_[pos].arrivalTime = now;
    std::memcpy(buffer_[pos].payload.data(), payload.data(), payload.size());
    ++count_;
}

template <typename EmitFn>
void SpeakerJitterBuffer::drainContiguous(EmitFn&& emit) {
    while (count_ > 0 && buffer_[0].seq == nextExpectedSeq_) {
        emit(OutputStep{
            ActionKind::DecodeNormal,
            std::span<const std::uint8_t>(buffer_[0].payload.data(), buffer_[0].length),
        });
        popFront();
        ++nextExpectedSeq_;
    }
}

template <typename EmitFn>
void SpeakerJitterBuffer::pushAndDrain(std::uint32_t seq,
                                       std::span<const std::uint8_t> payload,
                                       std::chrono::steady_clock::time_point now,
                                       EmitFn&& emit) {
    if (payload.empty()) {
        return;
    }
    if (!hasExpectedSeq_) {
        hasExpectedSeq_  = true;
        nextExpectedSeq_ = seq + 1;
        emit(OutputStep{ActionKind::DecodeNormal, payload});
        return;
    }

    const std::int32_t delta = seqDiff(seq, nextExpectedSeq_);
    if (delta < 0) {
        return; // Late/stale duplicate
    }
    if (delta == 0) {
        emit(OutputStep{ActionKind::DecodeNormal, payload});
        ++nextExpectedSeq_;
        drainContiguous(emit);
        return;
    }
    if (delta > 16) {
        // Large discontinuity (e.g. talkspurt reset) -> resynchronize cleanly
        count_           = 0;
        nextExpectedSeq_ = seq + 1;
        emit(OutputStep{ActionKind::DecodeNormal, payload});
        return;
    }

    insertSorted(seq, payload, now);
}

template <typename EmitFn>
void SpeakerJitterBuffer::drainTimedOut(std::chrono::steady_clock::time_point now, EmitFn&& emit) {
    if (!hasExpectedSeq_) {
        return;
    }
    while (count_ > 0) {
        if (buffer_[0].seq == nextExpectedSeq_) {
            drainContiguous(emit);
            continue;
        }
        if ((now - buffer_[0].arrivalTime) < kMaxReorderWait) {
            break;
        }

        const std::uint32_t gap = static_cast<std::uint32_t>(seqDiff(buffer_[0].seq, nextExpectedSeq_));
        if (gap > 2u) {
            // Multiple packets dropped beyond FEC/PLC window: conceal 1 frame then advance cleanly
            emit(OutputStep{ActionKind::ConcealPlc, {}});
            nextExpectedSeq_ = buffer_[0].seq;
        } else {
            // Conceal up to 2 lost frames; if gap == 1 try in-band FEC from buffer_[0] first
            for (std::uint32_t i = 0; i < gap; ++i) {
                if (i == gap - 1u) {
                    emit(OutputStep{
                        ActionKind::DecodeFecAndThenNormal,
                        std::span<const std::uint8_t>(buffer_[0].payload.data(), buffer_[0].length),
                    });
                } else {
                    emit(OutputStep{ActionKind::ConcealPlc, {}});
                }
            }
            nextExpectedSeq_ = buffer_[0].seq;
        }
        drainContiguous(emit);
    }
}

} // namespace svplus::client::audio
