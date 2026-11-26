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
#include <utility>
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
    void setDeafened(bool deafened, const char* origin = "menu") noexcept;
    [[nodiscard]] bool isDeafened() const noexcept { return deafened_.load(std::memory_order_acquire); }

    struct SpatialLosProbe {
        bool (*getCameraBasis)(float pos[3], float front[3], float top[3], void* ctx) noexcept = nullptr;
        bool (*getLocalPedPosition)(float pos[3], void* ctx) noexcept = nullptr;
        bool (*resolveStreamPos)(std::uint32_t id, StreamKind kind, std::uint32_t target, protocol::Vec3f staticPos, float outPos[3], void* ctx) noexcept = nullptr;
        bool (*isLocalInVehicle)(std::uint16_t vehicleId, void* ctx) noexcept = nullptr;
        const void* (*resolveObjectEntity)(std::uint16_t objectId, void* ctx) noexcept = nullptr;
        bool (*checkLineOfSightClear)(const float origin[3], const float target[3], const void* ignoreEntity, void* ctx) noexcept = nullptr;
        void* ctx{nullptr};
    };

    /// Called strictly on the render/game thread to capture camera & entity world positions safely.
    void updateSpatialFromMainThread(
        bool voiceOcclusionEnabled = false,
        const SpatialLosProbe* envProbe = nullptr,
        std::chrono::steady_clock::time_point nowOverride = {});

    /// Returns last published occlusion flag for a positional stream (used for verification).
    [[nodiscard]] bool isStreamOccludedSnapshot(std::uint32_t streamId) const noexcept;

    /// Called periodically on the dedicated audio thread to advance jitter buffers, software spatial state, & active speakers.
    void tick(const config::ClientConfig& cfg, bool voiceOcclusionEnabled = false);

    [[nodiscard]] std::shared_ptr<const std::vector<ActiveSpeakerInfo>> activeSpeakersSnapshot() const;

    struct MetricsSnapshot {
        std::size_t   streamCount{0};
        std::size_t   activeSpeakerCount{0};
        std::uint64_t packetsReceived{0};
        std::uint64_t packetsDropped{0};
        std::uint64_t packetsIgnored{0};
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
        bool  hasLocalPed{false};
        float localPedPos[3]{};
        std::vector<std::pair<std::uint32_t, CachedPos>> streamPositions{};
    };

    struct TargetQuery {
        std::uint32_t                id{0};
        StreamKind                   kind{StreamKind::Global};
        std::uint32_t                target{0};
        float                        distance{0.0f};
        bool                         hasActiveSpeakers{false};
        protocol::Vec3f              staticPos{};
        std::shared_ptr<AudioStream> stream{};
    };

    struct OcclusionTrackState {
        bool occluded{false};
        bool pendingCandidate{false};
        std::uint8_t sameCount{0};
        std::chrono::steady_clock::time_point lastCheckTime{};
    };

    struct OrderedStreamEntry {
        std::uint64_t                order{0};
        std::uint32_t                id{0};
        std::shared_ptr<AudioStream> stream{};
    };

    struct MuteCacheEntry {
        std::uint32_t nameGen{0};
        std::uint32_t muteGen{0};
        std::uint32_t volumeGen{0};
        std::uint32_t serverGen{0};
        bool          muted{false};
        float         volumeFactor{1.0f};
    };

    const MuteCacheEntry& speakerStateCached(std::uint16_t speakerId, const config::ClientConfig& cfg) noexcept;
    [[nodiscard]] bool    isSpeakerMutedCached(std::uint16_t speakerId, const config::ClientConfig& cfg) noexcept;
    [[nodiscard]] float   speakerVolumeCached(std::uint16_t speakerId, const config::ClientConfig& cfg) noexcept;
    void publishSpeakerSnapshotIfChanged(const std::vector<ActiveSpeakerInfo>& speakers);

    mutable std::mutex mutex_{};
    std::unordered_map<std::uint32_t, std::shared_ptr<AudioStream>> streams_{};
    std::vector<OrderedStreamEntry>                                 orderedStreams_{};
    std::uint64_t                                                   nextCreateSeq_{1};
    ActiveSpeakerQueue speakerQueue_{};
    std::array<MuteCacheEntry, samp::kMaxPlayerSlots> muteCache_{};

    std::atomic<std::uint64_t> packetsReceived_{0};
    std::atomic<std::uint64_t> packetsDropped_{0};
    std::atomic<std::uint64_t> packetsIgnored_{0};
    std::atomic<std::size_t>   streamCountAtomic_{0};
    std::atomic<std::size_t>   activeSpeakerCountAtomic_{0};

    mutable std::mutex speakerSnapshotMutex_{};
    std::shared_ptr<const std::vector<ActiveSpeakerInfo>> publishedSpeakers_{
        std::make_shared<const std::vector<ActiveSpeakerInfo>>()};

    mutable std::mutex spatialMutex_{};
    std::array<std::shared_ptr<SpatialSnapshot>, 3> spatialBuffers_{};
    std::shared_ptr<const SpatialSnapshot>          publishedSpatial_{};
    std::atomic<bool>                               listenerDisabled_{false};
    std::atomic<bool>                               deafened_{false};

    // Main-thread reusable scratch buffers (only accessed by updateSpatialFromMainThread)
    std::vector<std::shared_ptr<AudioStream>>          spatialStreamsScratch_{};
    std::vector<TargetQuery>                           spatialQueriesScratch_{};
    std::unordered_map<std::uint32_t, OcclusionTrackState> occlusionStates_{};
    std::size_t                                        occlusionCursor_{0};

    // Audio-thread reusable scratch buffers (only accessed by tick)
    std::vector<OrderedStreamEntry>                    tickStreamsScratch_{};
    std::vector<ActiveSpeakerInfo>                     activeNowScratch_{};
    std::vector<std::size_t>                           activeStreamCountsScratch_{};
    std::array<std::int32_t, samp::kMaxPlayerSlots>    speakerIdxByIdScratch_{};
    std::unordered_map<std::uint16_t, std::size_t>     speakerIdxOverflowScratch_{};
    std::vector<std::uint16_t>                         streamSpeakersScratch_{};
    std::vector<ActiveSpeakerInfo>                     prevPublishedScratch_{};
    std::vector<ActiveSpeakerInfo>                     nextPublishedScratch_{};

    mutable std::mutex         serverMutex_{};
    std::string                currentServerEndpoint_{};
    std::atomic<std::uint32_t> serverGen_{1};
};

} // namespace svplus::client::audio
