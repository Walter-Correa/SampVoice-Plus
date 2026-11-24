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
        IServerAdapter& adapter
    ) = 0;

    [[nodiscard]] virtual bool CancelPendingDeleteStream(uint16_t playerId, uint32_t streamId) = 0;
};

class StreamParameter {
public:
    StreamParameter(uint32_t streamId, uint32_t parameterId, float initValue);

    float Get() const noexcept;
    void Set(float value) noexcept;
    void SlideFromTo(float startValue, float endValue, uint32_t timeMs) noexcept;
    void SlideTo(float endValue, uint32_t timeMs) noexcept;
    void Slide(float deltaValue, uint32_t timeMs) noexcept;

    ControlPacketBuffer BuildApplyPacket() const;

    static bool IsValidParameter(uint32_t parameterId) noexcept;
    static float DefaultValue(uint32_t parameterId) noexcept;

private:
    using Clock = std::chrono::steady_clock;

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
        protocol::Vec3f position, uint32_t color, const std::string& name
    );

    void SetHandle(uint32_t handle);
    uint32_t GetHandle() const noexcept { return handle_; }
    void SetCreationSeq(uint64_t seq) noexcept { creationSeq_ = seq; }
    uint64_t GetCreationSeq() const noexcept { return creationSeq_; }
    void SetOwnerAmx(void* amx) noexcept { ownerAmx_ = amx; }
    void* GetOwnerAmx() const noexcept { return ownerAmx_; }
    uint32_t GetColor() const noexcept { return color_; }
    const std::string& GetName() const noexcept { return name_; }

    Mode GetMode() const noexcept { return mode_; }
    EntityKind GetEntityKind() const noexcept { return entityKind_; }
    bool IsDynamic() const noexcept { return mode_ == Mode::DynamicLocal; }
    bool IsLocal() const noexcept { return mode_ != Mode::Global; }
    bool IsPointStream() const noexcept { return IsLocal() && entityKind_ == EntityKind::Point; }

    // Listener management (COW atomic snapshot for lock-free audio fan-out)
    bool AttachListener(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendCreatePacket = true,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );
    bool AttachDynamicListenerIfInRange(
        uint16_t playerId,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
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
    std::vector<uint16_t> DetachAllSpeakers();

    // Spatial updates
    void UpdatePosition(float x, float y, float z, IServerAdapter& adapter);
    void UpdateDistance(float distance, IServerAdapter& adapter);

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
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );

    // Icon customization (Plus v12 clients only)
    void SetIcon(
        protocol::StreamIconId iconId,
        IServerAdapter& adapter,
        const std::array<std::atomic<bool>, kMaxPlayers>& plusFlags
    );
    protocol::StreamIconId GetIcon() const noexcept { return iconId_; }

    // Parameter management
    void SetParameter(uint32_t parameterId, float value, IServerAdapter& adapter);
    void ResetParameter(uint32_t parameterId, IServerAdapter& adapter);
    bool HasParameter(uint32_t parameterId) const;
    float GetParameter(uint32_t parameterId) const;
    void SlideParameterFromTo(uint32_t parameterId, float startVal, float endVal, uint32_t timeMs, IServerAdapter& adapter);
    void SlideParameterTo(uint32_t parameterId, float endVal, uint32_t timeMs, IServerAdapter& adapter);
    void SlideParameter(uint32_t parameterId, float deltaVal, uint32_t timeMs, IServerAdapter& adapter);

    // Effect tracking
    void RegisterEffectPacket(uint32_t effectId, ControlPacketBuffer createPacket, IServerAdapter& adapter);
    void UnregisterEffectPacket(uint32_t effectId, ControlPacketBuffer deletePacket, IServerAdapter& adapter);
    std::vector<uint32_t> ClearEffects();

    // Broadcast control packet to all attached listeners
    void BroadcastControlPacket(const ControlPacketBuffer& packet, IServerAdapter& adapter) const;

private:
    Stream() = default;

    bool AttachListenerInternal(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendCreatePacket,
        const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags = nullptr,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );
    bool DetachListenerInternal(
        uint16_t playerId,
        IServerAdapter& adapter,
        bool sendDeletePacket,
        IDeferredStreamDeleteSink* deferredDeletes = nullptr
    );

    ControlPacketBuffer BuildIconPacket() const;
    void PublishListenerSnapshotLocked();

    uint32_t handle_{0};
    uint64_t creationSeq_{0};
    void* ownerAmx_{nullptr};
    Mode mode_{Mode::Global};
    EntityKind entityKind_{EntityKind::Point};
    float distance_{0.0f};
    uint32_t maxPlayers_{0};
    uint32_t targetId_{0};
    protocol::Vec3f position_{0.0f, 0.0f, 0.0f};
    uint32_t color_{0};
    protocol::StreamIconId iconId_{protocol::StreamIconId::Speaker};
    std::string name_;

    ControlPacketBuffer packetCreate_;
    ControlPacketBuffer packetDelete_;
    ControlPacketBuffer packetUpdateDistance_;
    ControlPacketBuffer packetUpdatePosition_;

    std::array<std::atomic<bool>, kMaxPlayers> listenerMask_{};
    std::array<bool, kMaxPlayers> blockedListeners_{};
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
