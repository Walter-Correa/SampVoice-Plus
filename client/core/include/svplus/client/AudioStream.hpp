#pragma once

#include "svplus/client/Platform.hpp"
#include "svplus/client/VoiceCodec.hpp"
#include "svplus/common/Protocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace svplus::client::audio {

// Voice occlusion tuning constants (named for easy tuning after playtesting)
inline constexpr float kOcclusionCutoffHz          = 1000.0f;
inline constexpr float kOcclusionClearCutoffHz     = 22000.0f;
inline constexpr float kOcclusionVolumeGain        = 0.5f;   // -6 dB when occluded
inline constexpr float kOcclusionTransitionMs      = 150.0f; // smooth fade-in/out window
inline constexpr float kOcclusionSampleRateHz      = static_cast<float>(protocol::kFrequency);
inline constexpr float kOcclusionPedHeadOffsetZ    = 0.7f;
inline constexpr float kOcclusionVehicleOffsetZ    = 0.5f;
inline constexpr float kOcclusionSourceMarginM     = 0.5f;
inline constexpr float kOcclusionCascadeScale      = 1.55377397f; // -3 dB at kOcclusionCutoffHz for 2 cascaded 1st-order LPFs
inline constexpr auto  kOcclusionCheckInterval     = std::chrono::milliseconds(100);
inline constexpr auto  kOcclusionStaleAfter        = std::chrono::milliseconds(500);
inline constexpr std::size_t kMaxOcclusionChecksPerFrame = 8u;
inline constexpr std::uint8_t kOcclusionHysteresisCount  = 2u;

// Active-speaker HUD/overhead hold vs BASS channel/decoder idle free timeouts
inline constexpr auto kSpeakerDisplayHold     = std::chrono::milliseconds(1000);
inline constexpr auto kSpeakerChannelIdleFree = std::chrono::milliseconds(5000);

// Backlog catch-up & burst flush tuning constants
inline constexpr float         kQueueCatchUpHighMs      = 300.0f;  // >300 ms queued -> speed up playback +6%
inline constexpr float         kQueueCatchUpLowMs       = 150.0f;  // <=150 ms queued -> return to normal 1.0x rate
inline constexpr float         kQueueCatchUpRate        = 1.06f;   // +6% playback rate multiplier
inline constexpr std::uint32_t kQueueCatchUpSlideMs     = 80u;     // Smooth frequency slide duration (ms)
inline constexpr float         kQueueFlushThresholdMs   = 800.0f;  // >800 ms queued -> restart BASS stream with fresh tail
inline constexpr float         kQueueFlushKeepMs        = 150.0f;  // Re-pushed PCM tail window (~150 ms)
inline constexpr std::size_t   kQueueFlushKeepSamples   = (protocol::kFrequency / 1000u) * 150u; // 7200 samples @ 48 kHz
inline constexpr std::size_t   kQueueFlushFadeSamples   = (protocol::kFrequency / 1000u) * 5u;   // 240 samples (5 ms) fade-in
inline constexpr auto          kQueueFlushCoalesceWindow = std::chrono::milliseconds(20);
inline constexpr float         kQueueFlushCoalesceCapMs = 200.0f;
inline constexpr auto          kQueueCatchUpLogInterval = std::chrono::seconds(60);

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
    [[nodiscard]] std::uint64_t takeDroppedFrames() noexcept {
        const std::uint64_t dropped = droppedFrames_;
        droppedFrames_ = 0;
        return dropped;
    }

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
    std::uint64_t droppedFrames_{0};
    std::array<BufferedVoicePacket, kMaxBufferedFrames> buffer_{};
};

struct SpeakerChannel {
    static constexpr std::uint8_t  kMinPrebufferFrames     = 1u;
    static constexpr std::uint8_t  kDefaultPrebufferFrames = 2u;
    static constexpr std::uint8_t  kMaxPrebufferFrames     = 4u;
    static constexpr std::uint32_t kStableReduceFrames     = 50u; // 50 received frames (~5s speech) without underrun
    static constexpr std::uint32_t kHighQueueReduceFrames  = 10u; // Faster cushion shrink when BASS queue >300 ms
    static constexpr auto          kActiveSpeechGrace      = std::chrono::milliseconds(150);

    std::uint16_t       playerId{0};
    HSTREAM             bassStream{0};
    VoiceDecoder        decoder{};
    SpeakerJitterBuffer jitter{};
    std::vector<HFX>    effects{};
    std::chrono::steady_clock::time_point lastPacketTime{};

    // Adaptive prebuffer / underrun cushion state
    bool          prebuffering{true};
    std::uint8_t  prebufferedFrames{0};
    std::uint8_t  targetPrebufferFrames{kDefaultPrebufferFrames};
    std::uint32_t stableFrames{0};

    // Persistent DSP state across packets (eliminates inter-frame high-pass / AGC clicks)
    float hpPrevX{0.0f};
    float hpPrevY{0.0f};
    float env{0.0f};

    // Per-channel wall-occlusion low-pass filter & gain transition state
    float occLpState1{0.0f};
    float occLpState2{0.0f};
    float occFilterProgress{0.0f}; // 0.0 = clear/bypassed, 1.0 = muffled (~1000 Hz)
    bool  occLpInitialized{false};
    bool  snapOcclusionOnNextDecode{true};
    float occGainFactor{1.0f};     // Smoothly glides between 1.0 (clear) and kOcclusionVolumeGain (0.5)
    std::chrono::steady_clock::time_point lastOccTickTime{};

    // Smoothed software pan state (BASS_ATTRIB_PAN)
    float lastAppliedPan{0.0f};
    bool  hasAppliedPan{false};

    // Per-player client volume multiplier (0.0 .. 2.0, default 1.0)
    float playerVolume{1.0f};

    // Backlog catch-up & burst flush state
    bool                      catchUpActive{false};
    float                     lastAppliedFreq{static_cast<float>(protocol::kFrequency)};
    bool                      hasAppliedFreq{false};
    std::vector<std::int16_t> recentPcm{};
    std::vector<std::int16_t> flushTempPcm{};
    std::chrono::steady_clock::time_point lastCatchUpLogTime{};
    std::chrono::steady_clock::time_point lastFlushTime{};

    // Per-utterance diagnostic RX telemetry (updated strictly on audio thread under streamMutex_)
    struct UtteranceTelemetry {
        std::uint32_t pkts{0};
        std::uint32_t dec{0};
        std::uint32_t decErr{0};
        std::uint32_t plc{0};
        std::uint32_t stalls{0};
        std::uint32_t flushes{0};
        std::uint16_t peakAbs{0};
        double        sumSq{0.0};
        std::uint64_t sampleCount{0};
        bool          played{false};
        float         lastAppliedVol{0.0f};
        float         lastAppliedFreq{static_cast<float>(protocol::kFrequency)};
        bool          hasAppliedFreq{false};
        bool          startedFresh{false};
        bool          hasActivity{false};
    };
    UtteranceTelemetry telemetry{};

    void appendRecentPcm(std::span<const std::int16_t> pcm) {
        if (pcm.empty()) {
            return;
        }
        if (pcm.size() >= kQueueFlushKeepSamples) {
            const auto tail = pcm.subspan(pcm.size() - kQueueFlushKeepSamples);
            recentPcm.assign(tail.begin(), tail.end());
            return;
        }
        recentPcm.insert(recentPcm.end(), pcm.begin(), pcm.end());
        if (recentPcm.size() > kQueueFlushKeepSamples) {
            const std::size_t excess = recentPcm.size() - kQueueFlushKeepSamples;
            recentPcm.erase(recentPcm.begin(), recentPcm.begin() + static_cast<std::ptrdiff_t>(excess));
        }
    }

    void beginPrebuffer(bool resetStable = true) noexcept {
        prebuffering      = true;
        prebufferedFrames = 0;
        if (resetStable) {
            stableFrames = 0;
        }
    }

    /// Returns true when the channel has queued enough prebuffer frames to start playback.
    /// Counts received/decoded frames across utterances to shrink inflated cushions in normal speech.
    [[nodiscard]] bool onDecodedFrameQueued(bool highQueue = false) noexcept {
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
            const std::uint32_t reduceThreshold = highQueue ? kHighQueueReduceFrames : kStableReduceFrames;
            if (stableFrames >= reduceThreshold) {
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
            ++telemetry.stalls;
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
                std::string name,
                const IBassLibrary* bass = nullptr);
    ~AudioStream();

    void setBassLibrary(const IBassLibrary* bass) noexcept {
        bass_          = bass;
        cachedBassApi_ = bass ? &bass->api() : nullptr;
    }

    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    [[nodiscard]] std::uint32_t      id() const noexcept { return id_; }
    [[nodiscard]] StreamKind         kind() const noexcept { return kind_; }
    [[nodiscard]] std::uint32_t      target() const noexcept { return target_; }
    [[nodiscard]] float              distance() const noexcept;
    [[nodiscard]] protocol::Vec3f    position() const noexcept;
    [[nodiscard]] std::uint32_t      color() const;
    [[nodiscard]] std::string        name() const;
    [[nodiscard]] protocol::StreamIconId iconId() const noexcept;
    [[nodiscard]] bool                   hasActiveSpeakersNow() const noexcept {
        return hasActiveSpeakers_.load(std::memory_order_acquire);
    }

    void setDistance(float d) noexcept;
    void setPosition(protocol::Vec3f pos) noexcept;
    void setInfo(std::uint32_t color, std::string_view name);
    void setIconId(protocol::StreamIconId iconId) noexcept;
    void setLatestOccluded(bool occluded) noexcept {
        latestOccluded_.store(kind_ != StreamKind::Global && occluded, std::memory_order_release);
    }
    [[nodiscard]] bool isLatestOccluded() const noexcept {
        return (kind_ != StreamKind::Global) && latestOccluded_.load(std::memory_order_acquire);
    }
    void setDropCounter(std::atomic<std::uint64_t>* counter) noexcept {
        dropCounter_.store(counter, std::memory_order_release);
    }
    void clearChannels() noexcept;

    void setParameter(std::uint32_t paramId, float value) noexcept;
    void slideParameter(std::uint32_t paramId, float startVal, float endVal, std::uint32_t timeMs) noexcept;

    void addOrUpdateEffect(const StreamEffectSpec& spec);
    void removeEffect(std::uint32_t effectId);

    void pushVoiceFrame(std::uint16_t speakerId,
                        std::uint32_t seq,
                        std::span<const std::uint8_t> opusPayload,
                        float masterVolume,
                        float playerVolume,
                        bool soundEnabled,
                        bool playerMuted,
                        bool applyFilter,
                        bool applyBalancer);

    void resetSpeaker(std::uint16_t speakerId) noexcept;

    using SpeakerMutedFn  = bool  (*)(std::uint16_t speakerId, void* ctx) noexcept;
    using SpeakerVolumeFn = float (*)(std::uint16_t speakerId, void* ctx) noexcept;

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
              void* mutedCtx,
              SpeakerVolumeFn getSpeakerVolume = nullptr);

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
                                protocol::StreamIconId& outIconId,
                                std::uint32_t& outColor,
                                std::string& outName,
                                SpeakerVolumeFn getSpeakerVolume = nullptr);

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

    struct SpeakerTelemetrySnapshot {
        std::uint32_t pkts{0};
        std::uint32_t dec{0};
        std::uint32_t decErr{0};
        std::uint32_t plc{0};
        std::uint32_t stalls{0};
        std::uint32_t flushes{0};
        std::uint16_t peakAbs{0};
        double        sumSq{0.0};
        std::uint64_t sampleCount{0};
        bool          played{false};
        float         lastAppliedVol{0.0f};
        float         lastAppliedFreq{static_cast<float>(protocol::kFrequency)};
        bool          hasAppliedFreq{false};
        bool          startedFresh{false};
        bool          hasActivity{false};
    };
    bool takeSpeakerTelemetry(std::uint16_t speakerId,
                              SpeakerTelemetrySnapshot& outStats,
                              std::string* outStreamName = nullptr) noexcept;

    void collectActiveSpeakers(std::vector<std::uint16_t>& outSpeakers) const;
    [[nodiscard]] std::size_t channelCount() const noexcept;
    [[nodiscard]] std::size_t effectCount() const noexcept;

private:
    void warnInvalidInputLocked(const char* field, const char* detail) noexcept;
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
                    void* mutedCtx,
                    SpeakerVolumeFn getSpeakerVolume);
    void collectActiveSpeakersLocked(std::vector<std::uint16_t>& outSpeakers) const;
    void freeChannelResourcesLocked(SpeakerChannel& ch) noexcept;
    void releaseChannelLocked(std::uint16_t speakerId, bool saveTelemetry) noexcept;
    SpeakerChannel* ensureChannel(std::uint16_t speakerId);
    void decodeAndSubmitStep(SpeakerChannel& ch,
                             const SpeakerJitterBuffer::OutputStep& step,
                             const BassApi& bass,
                             bool applyFilter,
                             bool applyBalancer,
                             bool targetOccluded,
                             std::chrono::steady_clock::time_point now) noexcept;
    void applyParametersToChannel(SpeakerChannel& ch, const BassApi& bass, float masterVolume, bool soundEnabled);
    void applyEffectToChannel(SpeakerChannel& ch, const StreamEffectSpec& spec);
    void rebuildChannelEffects(SpeakerChannel& ch);
    void flushJitterDrops(SpeakerChannel& ch) noexcept;
    void manageChannelBacklog(SpeakerChannel& ch, const BassApi& bass, std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] const BassApi& bassApi() const noexcept {
        return cachedBassApi_ ? *cachedBassApi_ : kEmptyBass_;
    }

    static constexpr BassApi kEmptyBass_{};
    const IBassLibrary* bass_{nullptr};
    const BassApi*      cachedBassApi_{nullptr};
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
    std::atomic<bool>  latestOccluded_{false};
    std::atomic<bool>  hasActiveSpeakers_{false};
    std::atomic<bool>  hasTelemetry_{false};
    std::atomic<std::atomic<std::uint64_t>*> dropCounter_{nullptr};

    std::unordered_map<std::uint32_t, StreamParameterState> params_{};
    std::unordered_map<std::uint32_t, StreamEffectSpec>     effectSpecs_{};
    std::unordered_map<std::uint16_t, std::unique_ptr<SpeakerChannel>> channels_{};
    std::unordered_map<std::uint16_t, SpeakerChannel::UtteranceTelemetry> pendingTelemetry_{};
    std::unordered_map<std::uint16_t, std::chrono::steady_clock::time_point> activeSpeakersLastSeen_{};
    std::array<std::int16_t, 5760> decodeBuf_{};
    std::chrono::steady_clock::time_point lastInvalidInputWarn_{};
    bool          hasInvalidInputWarned_{false};
    std::uint32_t suppressedInvalidInputWarns_{0};
};

/// Persistent per-speaker speech clarity DSP + optional smooth wall-occlusion low-pass filter.
void processChannelAudioDsp(SpeakerChannel& ch,
                            std::span<std::int16_t> pcm,
                            bool applyFilter,
                            bool applyBalancer,
                            bool targetOccluded = false) noexcept;

/// Smoothly advances per-channel occlusion volume multiplier between 1.0 and kOcclusionVolumeGain.
[[nodiscard]] float advanceChannelOcclusionGain(SpeakerChannel& ch,
                                                bool targetOccluded,
                                                std::chrono::steady_clock::time_point now) noexcept;

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
        ++droppedFrames_;
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
