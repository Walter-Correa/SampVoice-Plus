/*
 * SampVoice Plus - Unified Voice Stream & Parameter Engine
 *
 * Uses safe 32-bit opaque stream handles, compact listener vectors for O(L)
 * packet fan-out (instead of O(1000) player pool scans), and O(1) spatial grid queries.
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ControlBuilder.hpp"
#include "IServerAdapter.hpp"
#include "SpatialGrid.hpp"
#include "svplus/common/Protocol.hpp"

namespace svplus::server {

class IDeferredStreamDeleteSink {
public:
    virtual ~IDeferredStreamDeleteSink() = default;

    virtual void ScheduleOrSendDeleteStream(
        uint16_t playerId,
        uint32_t streamId,
        int64_t lastVoiceForwardNs,
        const ControlPacketBuffer& packetDelete,
        IServerAdapter& adapter,
        bool initialOutdated = false
    ) = 0;

    [[nodiscard]] virtual bool CancelPendingDeleteStream(
        uint16_t playerId,
        uint32_t streamId,
        bool* outOutdated = nullptr
    ) = 0;

    virtual void MarkPendingDeletesOutdated(uint32_t streamId) {
        (void)streamId;
    }

    virtual void ScheduleOrRecreateStream(uint16_t playerId, uint32_t streamId, int64_t lastVoiceForwardNs) {
        (void)playerId;
        (void)streamId;
        (void)lastVoiceForwardNs;
    }

    [[nodiscard]] virtual bool CancelPendingRecreate(uint16_t playerId, uint32_t streamId) {
        (void)playerId;
        (void)streamId;
        return false;
    }

    virtual void OnStreamListenerAttached(uint16_t playerId, uint32_t streamId) {
        (void)playerId;
        (void)streamId;
    }

    virtual void OnStreamListenerDetached(uint16_t playerId, uint32_t streamId) {
        (void)playerId;
        (void)streamId;
    }
};

class StreamParameter {
public:
    using Clock = std::chrono::steady_clock;

    StreamParameter(uint32_t streamId, uint32_t parameterId, float initValue);

    float Get() const noexcept;
    void Set(float value) noexcept;
    void SlideFromTo(float startValue, float endValue, uint32_t timeMs) noexcept;
    void SlideTo(float endValue, uint32_t timeMs) noexcept;
    void Slide(float deltaValue, uint32_t timeMs) noexcept;
    [[nodiscard]] bool IsSlidingAt(Clock::time_point now) const noexcept {
        return sliding_ && now < endTime_;
    }

    ControlPacketBuffer BuildApplyPacket(float volumeScale = 1.0f) const;
    ControlPacketBuffer BuildApplyPacketAt(Clock::time_point now, float volumeScale = 1.0f) const;

    static bool IsValidParameter(uint32_t parameterId) noexcept;
    static float DefaultValue(uint32_t parameterId) noexcept;

private:

    uint32_t streamId_{0};
    uint32_t parameterId_{0};
    float value_{0.0f};
    float endValue_{0.0f};
    float ratio_{0.0f};
    Clock::time_point endTime_{};
    bool sliding_{false};
};

class Stream {
public:
    enum class Mode : uint8_t {
        Global       = 0,
        StaticLocal  = 1,
        DynamicLocal = 2
    };

    static std::shared_ptr<Stream> CreateGlobal(uint32_t color, const std::string& name);

    static std::shared_ptr<Stream> CreateStaticLocal(
        EntityKind kind, float distance, uint32_t targetId,
        protocol::Vec3f position, uint32_t color, const std::string& name
    );

    static std::shared_ptr<Stream> CreateDynamicLocal(
        EntityKind kind, float distance, uint32_t maxPlayers, uint32_t targetId,
        protocol::Vec3f position, uint32_t color, const std::string& name,
        int32_t worldId = -1, int32_t interiorId = -1
    );

    void SetHandle(uint32_t handle);
    uint32_t GetHandle() const noexcept { return handle_; }
    void SetCreationSeq(uint64_t seq) noexcept { creationSeq_ = seq; }
    uint64_t GetCreationSeq() const noexcept { return creationSeq_; }
    void SetOwnerAmx(void* amx) noexcept { ownerAmx_ = amx; }
    void* GetOwnerAmx() const noexcept { return ownerAmx_; }
    void SetRawColor(uint32_t rawColor) noexcept { rawColor_ = rawColor; }
    uint32_t GetRawColor() const noexcept { return rawColor_; }
    uint32_t GetColor() const noexcept { return color_; }
    const std::string& GetName() const noexcept { return name_; }

    Mode GetMode() const noexcept { return mode_; }
    EntityKind GetEntityKind() const noexcept { return entityKind_; }
    int32_t GetStreamType() const noexcept;
    float GetDistance() const noexcept { return distance_; }
    protocol::Vec3f GetPosition() const noexcept { return position_; }
    uint32_t GetTargetId() const noexcept { return targetId_; }
    uint32_t GetMaxPlayers() const noexcept { return maxPlayers_; }
    bool IsDynamic() const noexcept { return mode_ == Mode::DynamicLocal; }
    bool IsLocal() const noexcept { return mode_ != Mode::Global; }
    bool IsPointStream() const noexcept { return IsLocal() && entityKind_ == EntityKind::Point; }

    bool SetWorldAndInterior(int32_t worldId, int32_t interiorId) noexcept;
    int32_t GetWorldId() const noexcept { return worldId_; }
    int32_t GetInteriorId() const noexcept { return interiorId_; }

    static constexpr float kMinListenerVolume = 0.0f;
    static constexpr float kMaxListenerVolume = 4.0f;

    void SetPriority(uint32_t priority) noexcept { priority_ = priority; }
    [[nodiscard]] uint32_t GetPriority() const noexcept { return priority_; }
    void SetPriorityDuckPct(uint32_t duckPct) noexcept { priorityDuckPct_ = (duckPct <= 100u) ? duckPct : 100u; }
    [[nodiscard]] uint32_t GetPriorityDuckPct() const noexcept { return priorityDuckPct_; }

    // Listener management (COW atomic snapshot for lock-free audio fan-out)
    bool AttachListener(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendCreatePacket = true,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr,
        float volume = 1.0f,
        const std::array<uint32_t, kMaxPlayers>* bestActivePrios = nullptr,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    );
    bool AttachDynamicListenerIfInRange(
        uint16_t playerId,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr,
        const std::array<uint32_t, kMaxPlayers>* bestActivePrios = nullptr,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    );
    bool DetachListener(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendDeletePacket = true,
        bool blockIfDynamic = true,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );
    void UnblockListener(uint16_t playerId) noexcept;
    bool HasListener(uint16_t playerId) const noexcept;
    bool SetListenerVolumeFactor(uint16_t playerId, float volume, IServerAdapter& adapter, float listenerDuckFactor = 1.0f);
    [[nodiscard]] float GetListenerVolumeFactor(uint16_t playerId) const noexcept;
    void ClearListenerVolumeFactor(uint16_t playerId) noexcept;
    bool SetListenerPriorityDucked(
        uint16_t playerId,
        bool ducked,
        float listenerDuckFactor,
        IServerAdapter& adapter,
        float startFactorOverride = -1.0f
    );
    [[nodiscard]] bool IsListenerPriorityDucked(uint16_t playerId) const noexcept {
        return playerId < kMaxPlayers && listenerPriorityDucked_[playerId];
    }
    void SetListenerVoicePaused(uint16_t playerId, bool paused) noexcept {
        if (playerId < kMaxPlayers) {
            voicePausedMask_[playerId].store(paused, std::memory_order_release);
        }
    }
    [[nodiscard]] bool IsListenerVoicePaused(uint16_t playerId) const noexcept {
        return playerId < kMaxPlayers && voicePausedMask_[playerId].load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t GetListenerCount() const noexcept;
    std::vector<uint16_t> DetachAllListeners(
        IServerAdapter& adapter,
        bool sendDeletePacket = true,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );
    std::shared_ptr<const std::vector<uint16_t>> GetListenersSnapshot() const noexcept;

    void RecordVoiceForwarded(int64_t steadyTimeNs) noexcept;
    [[nodiscard]] int64_t LastVoiceForwardedNs() const noexcept {
        return lastVoiceForwardNs_.load(std::memory_order_acquire);
    }

    // Speaker management (O(1) lock-free check on worker hot path)
    bool AttachSpeaker(uint16_t playerId) noexcept;
    bool DetachSpeaker(uint16_t playerId) noexcept;
    bool HasSpeaker(uint16_t playerId) const noexcept;
    [[nodiscard]] std::size_t GetSpeakerCount() const noexcept { return speakerList_.size(); }
    [[nodiscard]] const std::vector<uint16_t>& SpeakersRef() const noexcept { return speakerList_; }
    [[nodiscard]] std::vector<uint16_t> GetSpeakersCopy() const { return speakerList_; }
    std::vector<uint16_t> DetachAllSpeakers();

    // Spatial updates
    bool UpdatePosition(float x, float y, float z, IServerAdapter& adapter);
    bool UpdateDistance(
        float distance,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr
    );

    // Dynamic stream spatial tick (O(1) spatial grid query + legacy maxplayers semantics)
    void TickDynamic(
        const SpatialGrid& grid,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>& playerHasPlugin,
        const std::array<std::atomic<bool>, kMaxPlayers>& playerIsPlus,
        std::vector<NearbyPlayer>& scratchBuffer,
        std::vector<uint32_t>& desiredMarks,
        uint32_t desiredGen,
        std::vector<NearbyPlayer>& addScratch,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr,
        const std::array<uint32_t, kMaxPlayers>* bestActivePrios = nullptr,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    );

    // Icon & metadata customization
    bool UpdateName(
        const std::string& name,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
        const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions,
        IDeferredStreamDeleteSink* deferredDeletes,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    );
    bool UpdateColor(
        uint32_t wireColor,
        uint32_t rawColor,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
        const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions,
        IDeferredStreamDeleteSink* deferredDeletes,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    );
    void SendRecreateToListener(
        uint16_t playerId,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr,
        float listenerDuckFactor = 1.0f
    ) const;

    void SetIcon(
        protocol::StreamIconId iconId,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>& plusFlags
    );
    protocol::StreamIconId GetIcon() const noexcept { return iconId_; }

    // Parameter management
    bool SetParameter(uint32_t parameterId, float value, IServerAdapter& adapter, const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr);
    bool ResetParameter(uint32_t parameterId, IServerAdapter& adapter, const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr);
    bool HasParameter(uint32_t parameterId) const;
    float GetParameter(uint32_t parameterId) const;
    bool SlideParameterFromTo(uint32_t parameterId, float startVal, float endVal, uint32_t timeMs, IServerAdapter& adapter, const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr);
    bool SlideParameterTo(uint32_t parameterId, float endVal, uint32_t timeMs, IServerAdapter& adapter, const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr);
    bool SlideParameter(uint32_t parameterId, float deltaVal, uint32_t timeMs, IServerAdapter& adapter, const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr);

    // Effect tracking
    void RegisterEffectPacket(uint32_t effectId, ControlPacketBuffer createPacket, IServerAdapter& adapter);
    void UpdateEffectPacket(
        uint32_t effectId,
        ControlPacketBuffer createPacket,
        const ControlPacketBuffer& deletePacket,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
        const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions
    );
    void UnregisterEffectPacket(uint32_t effectId, ControlPacketBuffer deletePacket, IServerAdapter& adapter);
    std::vector<uint32_t> ClearEffects();

    // Broadcast control packet to all attached listeners
    void BroadcastControlPacket(const ControlPacketBuffer& packet, IServerAdapter& adapter) const;

private:
    Stream();

    void SetListenerVolumeFactorLocked(uint16_t playerId, float factor) noexcept;
    void SetListenerPriorityDuckedLocked(uint16_t playerId, bool ducked) noexcept;
    void BroadcastParameterUpdate(
        const StreamParameter& param,
        uint32_t parameterId,
        IServerAdapter& adapter,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    ) const;

    bool AttachListenerInternal(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendCreatePacket,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr,
        const std::array<uint32_t, kMaxPlayers>* bestActivePrios = nullptr,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    );
    bool DetachListenerInternal(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendDeletePacket,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );

    ControlPacketBuffer BuildIconPacket() const;
    ControlPacketBuffer BuildInfoPacket() const;
    void RebuildCreatePackets();
    void SendAttachStatePackets(uint16_t playerId, bool isPlus, IServerAdapter& adapter, float listenerDuckFactor = 1.0f) const;
    void SendReattachSyncPackets(uint16_t playerId, bool isPlus, IServerAdapter& adapter, float listenerDuckFactor = 1.0f) const;
    void NotifyInfoChanged(
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
        const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions,
        IDeferredStreamDeleteSink* deferredDeletes,
        const std::array<float, kMaxPlayers>* bestActiveDuckFactors = nullptr
    ) const;
    void PublishListenerSnapshotLocked();

    uint32_t handle_{0};
    uint64_t creationSeq_{0};
    void* ownerAmx_{nullptr};
    uint32_t priority_{0};
    uint32_t priorityDuckPct_{50};
    Mode mode_{Mode::Global};
    EntityKind entityKind_{EntityKind::Point};
    float distance_{0.0f};
    uint32_t maxPlayers_{0};
    uint32_t targetId_{0};
    protocol::Vec3f position_{0.0f, 0.0f, 0.0f};
    int32_t worldId_{-1};
    int32_t interiorId_{-1};
    uint32_t rawColor_{0};
    uint32_t color_{0};
    protocol::StreamIconId iconId_{protocol::StreamIconId::Speaker};
    std::string name_;

    ControlPacketBuffer packetCreate_;
    ControlPacketBuffer packetCreateLegacy_;
    ControlPacketBuffer packetDelete_;
    ControlPacketBuffer packetUpdateDistance_;
    ControlPacketBuffer packetUpdateDistanceLegacy_;
    ControlPacketBuffer packetUpdatePosition_;

    std::array<std::atomic<bool>, kMaxPlayers> listenerMask_{};
    std::array<bool, kMaxPlayers> blockedListeners_{};
    std::array<float, kMaxPlayers> listenerVolumeFactors_{};
    std::size_t nonDefaultVolumeCount_{0};
    std::array<bool, kMaxPlayers> listenerPriorityDucked_{};
    std::array<float, kMaxPlayers> listenerAppliedDuckFactor_{};
    std::size_t priorityDuckedCount_{0};
    std::array<std::atomic<bool>, kMaxPlayers> voicePausedMask_{};
    mutable std::shared_mutex listenerMutex_;
    std::vector<uint16_t> listenerList_;
    std::shared_ptr<const std::vector<uint16_t>> listenerSnapshot_{std::make_shared<const std::vector<uint16_t>>()};

    std::array<std::atomic<bool>, kMaxPlayers> speakerMask_{};
    std::vector<uint16_t> speakerList_;
    std::atomic<int64_t> lastVoiceForwardNs_{0};

    std::unordered_map<uint32_t, StreamParameter> parameters_;
    std::unordered_map<uint32_t, ControlPacketBuffer> effects_;
};

} // namespace svplus::server
