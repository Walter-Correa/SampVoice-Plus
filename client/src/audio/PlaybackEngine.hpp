#pragma once

#include "audio/ActiveSpeakerQueue.hpp"
#include "audio/AudioStream.hpp"
#include "config/ClientConfig.hpp"
#include "samp/SampBridge.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace svplus::client::audio {

class PlaybackEngine {
public:
    static PlaybackEngine& instance() noexcept;

    void createStream(std::uint32_t streamId,
                      StreamKind kind,
                      std::uint32_t target,
                      float distance,
                      protocol::Vec3f position,
                      std::uint32_t color,
                      std::string name);
    void deleteStream(std::uint32_t streamId);
    void updateStreamDistance(std::uint32_t streamId, float distance);
    void updateStreamPosition(std::uint32_t streamId, protocol::Vec3f position);
    void setStreamIcon(std::uint32_t streamId, protocol::StreamIconId iconId);
    void setListenerDisabled(bool disabled);
    void setStreamParameter(std::uint32_t streamId, std::uint32_t paramId, float value);
    void slideStreamParameter(std::uint32_t streamId,
                              std::uint32_t paramId,
                              float startVal,
                              float endVal,
                              std::uint32_t timeMs);
    void createEffect(std::uint32_t streamId, const StreamEffectSpec& spec);
    void deleteEffect(std::uint32_t streamId, std::uint32_t effectId);

    void onVoicePacket(std::uint32_t streamId,
                       std::uint16_t speakerId,
                       std::uint32_t seq,
                       std::span<const std::uint8_t> opusPayload,
                       const config::ClientConfig& cfg);
    void onSpeakerEnd(std::uint32_t streamId, std::uint16_t speakerId);

    void resetAll();
    void setCurrentServerEndpoint(std::string_view endpoint);

    /// Called strictly on the render/game thread to capture camera & entity world positions safely.
    void updateSpatialFromMainThread();

    /// Called periodically on the dedicated audio thread to advance jitter buffers, software spatial state, & active speakers.
    void tick(const config::ClientConfig& cfg);

    [[nodiscard]] std::shared_ptr<const std::vector<ActiveSpeakerInfo>> activeSpeakersSnapshot() const;

    struct MetricsSnapshot {
        std::size_t   streamCount{0};
        std::size_t   activeSpeakerCount{0};
        std::uint64_t packetsReceived{0};
        std::uint64_t packetsDropped{0};
    };
    [[nodiscard]] MetricsSnapshot metricsSnapshot() const noexcept;
    [[nodiscard]] std::size_t     totalChannelCount() const noexcept;
    [[nodiscard]] bool            hasStream(std::uint32_t streamId) const noexcept;

private:
    PlaybackEngine() = default;
    ~PlaybackEngine() = default;
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    struct CachedPos {
        bool  valid{false};
        float pos[3]{};
    };

    struct SpatialSnapshot {
        bool  hasCamera{false};
        float camPos[3]{};
        float camFront[3]{0.0f, 1.0f, 0.0f};
        float camTop[3]{0.0f, 0.0f, 1.0f};
        std::unordered_map<std::uint32_t, CachedPos> streamPositions{};
    };

    struct MuteCacheEntry {
        std::uint32_t nameGen{0};
        std::uint32_t muteGen{0};
        std::uint32_t serverGen{0};
        bool          muted{false};
    };

    [[nodiscard]] bool isSpeakerMutedCached(std::uint16_t speakerId, const config::ClientConfig& cfg) noexcept;
    void publishSpeakerSnapshot(std::vector<ActiveSpeakerInfo> speakers);

    mutable std::mutex mutex_{};
    std::unordered_map<std::uint32_t, std::unique_ptr<AudioStream>> streams_{};
    std::unordered_map<std::uint32_t, std::uint64_t>                streamCreateOrder_{};
    std::uint64_t                                                   nextCreateSeq_{1};
    ActiveSpeakerQueue speakerQueue_{};
    std::array<MuteCacheEntry, samp::kMaxPlayerSlots> muteCache_{};

    std::atomic<std::uint64_t> packetsReceived_{0};
    std::atomic<std::uint64_t> packetsDropped_{0};
    std::atomic<std::size_t>   streamCountAtomic_{0};
    std::atomic<std::size_t>   activeSpeakerCountAtomic_{0};

    mutable std::mutex speakerSnapshotMutex_{};
    std::shared_ptr<const std::vector<ActiveSpeakerInfo>> publishedSpeakers_{
        std::make_shared<const std::vector<ActiveSpeakerInfo>>()};

    mutable std::mutex spatialMutex_{};
    SpatialSnapshot    spatialSnapshot_{};
    std::atomic<bool>  listenerDisabled_{false};

    mutable std::mutex         serverMutex_{};
    std::string                currentServerEndpoint_{};
    std::atomic<std::uint32_t> serverGen_{1};
};

} // namespace svplus::client::audio
