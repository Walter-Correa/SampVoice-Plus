/*
 * SampVoice Plus - Shared High-Performance Server Engine
 *
 * Implements all voice routing, dynamic spatial stream ticks, lock-free UDP
 * ingestion/forwarding, legacy client compatibility, and Pawn native operations.
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Effect.hpp"
#include "IServerAdapter.hpp"
#include "PlayerStore.hpp"
#include "SpatialGrid.hpp"
#include "Stream.hpp"
#include "svplus/common/HandleManager.hpp"
#include "svplus/common/LockFreeQueue.hpp"
#include "svplus/common/PacketPool.hpp"
#include "svplus/common/Protocol.hpp"
#include "svplus/common/UdpSocket.hpp"

namespace svplus::server {

struct ServerStartOptions {
    uint16_t udpPort{0};
    bool explicitPort{false};
    std::string bindIp{};
    uint32_t workerThreadCount{4};
    uint32_t updateRateMs{30};
    bool legacyColorFormat{false};
    bool voiceStatus{true};
    bool nearbyList{true};
    bool allowToggleMode{true};
    bool voiceOcclusion{true};
    std::string portOrigin{"default"};
    std::string bindOrigin{"default"};
    std::string threadsOrigin{"default"};
    std::string rateOrigin{"default"};
    std::string legacyColorOrigin{"default"};
    std::string voiceStatusOrigin{"default"};
    std::string nearbyListOrigin{"default"};
    std::string allowToggleModeOrigin{"default"};
    std::string voiceOcclusionOrigin{"default"};
};

class ServerCore {
public:
    explicit ServerCore(IServerAdapter& adapter);
    ~ServerCore();

    ServerCore(const ServerCore&) = delete;
    ServerCore& operator=(const ServerCore&) = delete;

    bool Start(const ServerStartOptions& options);
    bool Start(uint16_t udpPort = 0, uint32_t workerThreadCount = 4, uint32_t updateRateMs = 30);
    void Stop();

    bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }
    uint16_t UdpPort() const noexcept { return udpSocket_.LocalPort(); }

    /**
     * Must be invoked periodically on the host server's main game thread.
     * Drains incoming control packets, sends pending pluginInit packets,
     * rebuilds the O(1) spatial grid, and ticks dynamic streams.
     */
    void Tick();

    // Network events from host adapter
    void HandlePlayerConnectRpc(uint16_t playerId, const uint8_t* rpcBytes, std::size_t rpcSize);
    void HandleControlPacket(uint16_t playerId, const uint8_t* bytes, std::size_t size);
    void HandlePlayerDisconnect(uint16_t playerId);

    // Pawn Native Operations
    void SvDebug(bool enabled);
    void SvInit(uint32_t bitrate);
    uint8_t SvGetVersion(uint16_t playerId) const;
    uint8_t SvGetClientProtocol(uint16_t playerId) const;
    uint32_t SvGetPlusCapabilities(uint16_t playerId) const;
    uint32_t SvGetPlusClientVersion(uint16_t playerId) const;
    bool SvIsPlayerVoiceReady(uint16_t playerId) const;
    bool SvIsPlayerSpeaking(uint16_t playerId) const;
    int32_t SvGetPlayerVoiceStatus(uint16_t playerId) const;
    bool SvIsValidStream(uint32_t streamId) const;
    bool SvIsValidEffect(uint32_t effectId) const;
    bool SvHasMicro(uint16_t playerId) const;
    bool SvStartRecord(uint16_t playerId);
    bool SvStopRecord(uint16_t playerId);

    bool SvAddKey(uint16_t playerId, uint8_t keyId);
    bool SvHasKey(uint16_t playerId, uint8_t keyId) const;
    bool SvRemoveKey(uint16_t playerId, uint8_t keyId);
    void SvRemoveAllKeys(uint16_t playerId);

    static constexpr uint32_t kMaxMuteDurationMs = 86400000u; // 24h cap for timed mutes
    static constexpr std::chrono::milliseconds kSpeakingScanInterval{50};
    static constexpr std::chrono::milliseconds kSpeakingStopTimeout{1000};

    bool SvMutePlayerStatus(uint16_t playerId) const;
    void SvMutePlayerEnable(uint16_t playerId, bool releaseKeys = false, uint32_t timeMs = 0);
    void SvMutePlayerDisable(uint16_t playerId);
    int32_t SvMutePlayerRemaining(uint16_t playerId) const;
    bool SvMutePlayerForListener(uint16_t speakerId, uint16_t listenerId);
    bool SvUnmutePlayerForListener(uint16_t speakerId, uint16_t listenerId);
    bool SvIsPlayerMutedForListener(uint16_t speakerId, uint16_t listenerId) const;
    bool SvEnableListener(uint16_t playerId);
    bool SvDisableListener(uint16_t playerId);
    bool SvCheckListener(uint16_t playerId) const;

    uint32_t SvCreateGStream(uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateSLStreamAtPoint(float distance, float x, float y, float z, uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateSLStreamAtVehicle(float distance, uint16_t vehicleId, uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateSLStreamAtPlayer(float distance, uint16_t playerId, uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateSLStreamAtObject(float distance, uint16_t objectId, uint32_t color, const std::string& name, void* ownerAmx = nullptr);

    uint32_t SvCreateDLStreamAtPoint(float distance, uint32_t maxPlayers, float x, float y, float z, uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateDLStreamAtVehicle(float distance, uint32_t maxPlayers, uint16_t vehicleId, uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateDLStreamAtPlayer(float distance, uint32_t maxPlayers, uint16_t playerId, uint32_t color, const std::string& name, void* ownerAmx = nullptr);
    uint32_t SvCreateDLStreamAtObject(float distance, uint32_t maxPlayers, uint16_t objectId, uint32_t color, const std::string& name, void* ownerAmx = nullptr);

    /// Deletes all streams and effects created by the given AMX script (safe across GMX/unload).
    void CleanupAmxResources(void* ownerAmx);

    void SvUpdatePositionForLPStream(uint32_t streamId, float x, float y, float z);
    void SvUpdateDistanceForLStream(uint32_t streamId, float distance);

    bool SvAttachListenerToStream(uint32_t streamId, uint16_t playerId);
    bool SvAttachListenerToStream(uint32_t streamId, uint16_t playerId, float volume);
    bool SvStreamSetListenerVolume(uint32_t streamId, uint16_t playerId, float volume);
    float SvStreamGetListenerVolume(uint32_t streamId, uint16_t playerId) const;
    int32_t SvGetStreamListenerCount(uint32_t streamId) const;
    int32_t SvGetStreamSpeakerCount(uint32_t streamId) const;
    bool SvHasListenerInStream(uint32_t streamId, uint16_t playerId) const;
    bool SvDetachListenerFromStream(uint32_t streamId, uint16_t playerId);
    void SvDetachAllListenersFromStream(uint32_t streamId);

    bool SvAttachSpeakerToStream(uint32_t streamId, uint16_t playerId);
    bool SvHasSpeakerInStream(uint32_t streamId, uint16_t playerId) const;
    bool SvDetachSpeakerFromStream(uint32_t streamId, uint16_t playerId);
    void SvDetachAllSpeakersFromStream(uint32_t streamId);

    void SvStreamParameterSet(uint32_t streamId, uint32_t parameterId, float value);
    void SvStreamParameterReset(uint32_t streamId, uint32_t parameterId);
    bool SvStreamParameterHas(uint32_t streamId, uint32_t parameterId) const;
    float SvStreamParameterGet(uint32_t streamId, uint32_t parameterId) const;
    void SvStreamParameterSlideFromTo(uint32_t streamId, uint32_t parameterId, float startVal, float endVal, uint32_t timeMs);
    void SvStreamParameterSlideTo(uint32_t streamId, uint32_t parameterId, float endVal, uint32_t timeMs);
    void SvStreamParameterSlide(uint32_t streamId, uint32_t parameterId, float deltaVal, uint32_t timeMs);
    bool SvStreamSetIcon(uint32_t streamId, const std::string& iconName);
    void SvDeleteStream(uint32_t streamId);

    template <typename ParamsT>
    uint32_t CreateEffect(protocol::EffectType type, int32_t priority, const ParamsT& params, void* ownerAmx = nullptr) {
        auto effect = std::make_shared<Effect>(type, priority, params);
        const uint32_t id = effectPool_.Insert(effect);
        if (id != memory::kInvalidHandle) {
            effect->SetHandle(id);
            effect->SetOwnerAmx(ownerAmx);
            if (ownerAmx != nullptr) {
                std::lock_guard lock(ownedMutex_);
                ownedEffects_[id] = ownerAmx;
            }
        }
        return id;
    }

    void SvEffectAttachStream(uint32_t effectId, uint32_t streamId);
    void SvEffectDetachStream(uint32_t effectId, uint32_t streamId);
    void SvEffectDelete(uint32_t effectId);

    [[nodiscard]] std::size_t OwnedStreamCount() const {
        std::lock_guard lock(ownedMutex_);
        return ownedStreams_.size();
    }
    [[nodiscard]] std::size_t OwnedEffectCount() const {
        std::lock_guard lock(ownedMutex_);
        return ownedEffects_.size();
    }

    /// Converts stream color from SA-MP `0xRRGGBBAA` (default) or legacy `0xAARRGGBB` into wire `0xAARRGGBB`.
    [[nodiscard]] static constexpr uint32_t ConvertStreamColor(uint32_t rawColor, bool legacyFormat) noexcept {
        if (rawColor == 0u || legacyFormat) {
            return rawColor;
        }
        return ((rawColor >> 8u) & 0x00FFFFFFu) | ((rawColor & 0xFFu) << 24u);
    }

private:
    struct IncomingControlEvent {
        enum class Kind : uint8_t {
            ControlPacket = 0,
            VoiceMute     = 1,
            VoiceUnmute   = 2
        };

        Kind kind{Kind::ControlPacket};
        uint16_t playerId{protocol::kInvalidPlayerId};
        uint16_t type{0};
        uint8_t keyId{0};
        bool bypassKeyCheck{false};
        bool flag{false};
        bool speaking{false};
        uint32_t timeMs{0};
        uint64_t clientTimestampUs{0};
        uint64_t seq{0};
    };

    class DeferredDeleteSink final : public IDeferredStreamDeleteSink {
    public:
        static constexpr std::chrono::milliseconds kLegacyDeleteDelay{1000};
        static constexpr std::size_t kMaxPendingDeletesPerPlayer{64};

        explicit DeferredDeleteSink(const PlayerStore& store) noexcept
            : store_(store) {}

        void ScheduleOrSendDeleteStream(
            uint16_t playerId,
            uint32_t streamId,
            int64_t lastVoiceForwardNs,
            const ControlPacketBuffer& packetDelete,
            IServerAdapter& adapter
        ) override;

        [[nodiscard]] bool CancelPendingDeleteStream(uint16_t playerId, uint32_t streamId) override;
        void FlushDue(std::chrono::steady_clock::time_point now, IServerAdapter& adapter);
        void ClearPlayer(uint16_t playerId);
        void ClearAll();

    private:
        struct PendingDeleteEntry {
            uint16_t playerId{0};
            uint32_t streamId{0};
            std::chrono::steady_clock::time_point sendAt{};
            ControlPacketBuffer packetDelete{};
        };

        const PlayerStore& store_;
        std::mutex mutex_;
        std::vector<PendingDeleteEntry> pending_;
        std::vector<PendingDeleteEntry> dueScratch_;
        std::array<std::size_t, kMaxPlayers> perPlayerCount_{};
    };

    void IoReaderLoop();
    void WorkerLoop();
    void ProcessVoicePacket(memory::PooledPacket<protocol::kMaxUdpPacketSize> packet);

    void TrackTimedMute(uint16_t playerId);
    void UntrackTimedMute(uint16_t playerId);
    void ProcessExpiredMutes(std::chrono::steady_clock::time_point now);
    void DispatchControlEvent(const IncomingControlEvent& queuedEv);
    void EnqueueSyntheticControlEvent(IncomingControlEvent& ev);
    void ReportControlQueueDropIfDue(std::chrono::steady_clock::time_point now, bool droppedNow);

    std::mutex timedMutesMutex_;
    std::vector<uint16_t> timedMutedPlayers_;
    std::vector<uint16_t> expiredMutesScratch_;

    bool SetListenerEnabled(uint16_t playerId, bool enabled);
    bool AttachListenerInternal(uint32_t streamId, uint16_t playerId, float volume);
    void ClearPairMutesForPlayer(uint16_t playerId) noexcept;
    void ClearAllPairMutes() noexcept;
    void SyncSpeakerStreams(uint16_t playerId);
    uint8_t ComputePlayerVoiceState(uint16_t playerId) const noexcept;
    void SendServerOptions(uint16_t playerId);
    void SendVoiceStateChunk(uint16_t targetPlayerId, const std::vector<protocol::PlusPlayerVoiceStateEntry>& entries);
    void SendVoiceStateSnapshot(uint16_t targetPlayerId);
    void BroadcastVoiceStateEntry(uint16_t subjectId, uint8_t state, uint16_t skipPlayerId = protocol::kInvalidPlayerId);
    void SyncPlayerVoiceStateIfChanged(uint16_t subjectId, uint16_t skipPlayerId = protocol::kInvalidPlayerId);
    uint32_t RegisterStream(std::shared_ptr<Stream> stream, void* ownerAmx);
    void InitialDynamicPopulate(const std::shared_ptr<Stream>& stream);
    uint32_t NextDesiredGen() noexcept;
    void RebuildSpatialGridNow();

    IServerAdapter& adapter_;
    PlayerStore playerStore_;
    DeferredDeleteSink deferredDeleteSink_{playerStore_};
    SpatialGrid spatialGrid_{50.0f};

    memory::HandlePool<Stream> streamPool_;
    memory::HandlePool<Effect> effectPool_;

    std::mutex dynamicStreamsMutex_;
    std::vector<std::shared_ptr<Stream>> dynamicStreams_;

    mutable std::mutex ownedMutex_;
    std::unordered_map<uint32_t, void*> ownedStreams_;
    std::unordered_map<uint32_t, void*> ownedEffects_;

    std::mutex warnIconMutex_;
    std::unordered_set<std::string> warnedUnknownIcons_;

    net::UdpSocket udpSocket_;
    memory::PacketPool<protocol::kMaxUdpPacketSize> packetPool_{8192};
    concurrency::MpmcQueue<memory::PooledPacket<protocol::kMaxUdpPacketSize>> voiceQueue_{8192};
    concurrency::MpmcQueue<uint16_t> pendingInitQueue_{2048};
    concurrency::MpmcQueue<IncomingControlEvent> incomingControlQueue_{4096};
    std::vector<IncomingControlEvent> controlDrainScratch_;
    std::vector<IncomingControlEvent> syntheticControlQueue_;
    std::vector<IncomingControlEvent> syntheticDrainScratch_;
    std::atomic<uint64_t> nextControlEventSeq_{1};

    std::mutex controlQueueDropMutex_;
    std::chrono::steady_clock::time_point lastControlQueueDropLogTime_{};
    uint64_t droppedControlQueueEvents_{0};
    std::atomic<bool> hasDroppedControlQueueEvents_{false};

    std::atomic<bool> running_{false};
    std::atomic<uint32_t> bitrate_{protocol::kDefaultBitrate};

    std::mutex workerWakeMutex_;
    std::condition_variable workerWakeCv_;

    std::thread ioReaderThread_;
    std::vector<std::thread> workerThreads_;

    std::chrono::milliseconds updateRate_{30};
    bool legacyColorFormat_{false};
    bool voiceStatus_{true};
    bool nearbyList_{true};
    bool allowToggleMode_{true};
    bool voiceOcclusion_{false};
    std::array<bool, kMaxPlayers> voiceStateConnected_{};
    std::array<uint8_t, kMaxPlayers> lastSentVoiceState_{};
    std::array<bool, kMaxPlayers> speaking_{};
    std::chrono::steady_clock::time_point lastSpeakingTick_{};

    static constexpr std::size_t kPairMuteRowWords = (kMaxPlayers + 31u) / 32u; // 32 * 32 = 1024 bits per speaker
    using PairMuteRow = std::array<std::atomic<uint32_t>, kPairMuteRowWords>;
    std::unique_ptr<PairMuteRow[]> pairMuteMatrix_;
    std::string effectiveConfigLine_{};
    bool effectiveConfigLogged_{false};
    std::chrono::steady_clock::time_point lastSpatialTick_{};
    bool hasBuiltGrid_{false};
    std::vector<NearbyPlayer> spatialScratch_;
    std::vector<NearbyPlayer> addScratch_;
    std::vector<uint32_t> desiredMarks_;
    uint32_t desiredGen_{0};
    uint64_t nextStreamCreationSeq_{1}; // Main thread only (RegisterStream)
};

} // namespace svplus::server
