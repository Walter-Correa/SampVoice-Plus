#pragma once

#include "samp/OutgoingControlQueue.hpp"
#include "samp/VersionDetector.hpp"
#include "svplus/common/Protocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace svplus::client::samp {

inline constexpr std::size_t kMaxPlayerSlots = 1004u;

class SampBridge {
public:
    using ControlPacketCallback = std::function<void(std::uint16_t packetId, const std::uint8_t* data, std::size_t size)>;
    using ConnectStateCallback  = std::function<void(std::string_view host, std::uint32_t ipv4Net, std::uint16_t port, bool microSent)>;

    static SampBridge& instance() noexcept;

    void setSelfModule(void* hModule) noexcept { selfModule_ = hModule; }
    void setCallbacks(ControlPacketCallback onControl, ConnectStateCallback onConnectPrepared);
    void setMicrophoneAvailable(bool available) noexcept { microAvailable_.store(available, std::memory_order_release); }

    /// Must be called strictly on the render/game thread (D3D9 Present).
    void tick();
    void shutdown() noexcept;

    [[nodiscard]] SampVersion version() const noexcept { return layout_.version; }
    [[nodiscard]] const char* versionName() const noexcept { return layout_.name; }
    [[nodiscard]] bool        isConnected() const noexcept;
    [[nodiscard]] bool        isLocalPlayerSpawned() const noexcept;
    [[nodiscard]] bool        isUnsupportedSamp() const noexcept { return unsupportedDetected_; }
    [[nodiscard]] bool        hasConflictingClient() const noexcept { return conflictingClient_.load(std::memory_order_acquire); }

    /// Thread-safe static helper to check loaded .asi modules for legacy SampVoice or duplicate Plus conflicts.
    [[nodiscard]] static bool detectConflictingClientModule(
        void*         selfModule,
        std::wstring* outConflictPath = nullptr) noexcept;

    // Safe to call from game/render thread only:
    [[nodiscard]] bool        isChatInputOpen() const noexcept;
    [[nodiscard]] bool        isDialogOpen() const noexcept;
    [[nodiscard]] bool        isScoreboardOpen() const noexcept;
    [[nodiscard]] bool        isGameMenuOpen() const noexcept;
    [[nodiscard]] bool        isTextInputBlockingKeys() const noexcept;
    void                      setMenuCursorMode(bool open) noexcept;

    /// Thread-safe: enqueues a control packet to be sent strictly on the game thread via RakClient.
    bool sendControlPacket(std::uint16_t packetId, const void* payload, std::uint16_t payloadSize);
    void clearOutgoingControlQueue() noexcept;

    // Game-thread spatial & entity queries:
    [[nodiscard]] bool          getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept;
    [[nodiscard]] bool          getCameraWorldPosition(float pos[3]) const noexcept;
    [[nodiscard]] bool          getLocalPlayerPedPosition(float pos[3]) const noexcept;
    [[nodiscard]] bool          getRemotePlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept;
    [[nodiscard]] bool          getRemotePlayerHeadBonePosition(std::uint16_t playerId, float pos[3]) const noexcept;
    [[nodiscard]] bool          getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept;
    [[nodiscard]] bool          getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept;
    [[nodiscard]] bool          worldToScreen(const float worldPos[3], float screenOut[2], float* outDist = nullptr) const noexcept;

    [[nodiscard]] std::uint16_t getLocalPlayerId() const noexcept;
    [[nodiscard]] bool          isRemotePlayerConnected(std::uint16_t playerId) const noexcept;
    [[nodiscard]] std::uint32_t getPlayerColorArgb(std::uint16_t playerId) const noexcept;
    [[nodiscard]] std::string   getPlayerName(std::uint16_t playerId) const;
    [[nodiscard]] std::string   getLocalPlayerName() const;
    [[nodiscard]] std::string   getServerHostAddress() const;
    [[nodiscard]] std::string   getServerHostname() const;
    [[nodiscard]] std::string   getServerEndpointKey() const;
    [[nodiscard]] int           getServerPort() const noexcept;

    /// Thread-safe snapshot lookup for background audio thread (populated every ~250 ms on render thread).
    [[nodiscard]] std::string   getCachedPlayerName(std::uint16_t playerId) const;
    [[nodiscard]] std::uint32_t nameCacheGeneration() const noexcept {
        return nameCacheGen_.load(std::memory_order_acquire);
    }

private:
    SampBridge() = default;
    ~SampBridge() = default;
    SampBridge(const SampBridge&) = delete;
    SampBridge& operator=(const SampBridge&) = delete;

    class ProxyRakClient;
    friend class ProxyRakClient;

    struct SavedCursorState {
        bool  valid{false};
        int   sampCursorMode{0};
        bool  winCursorShowing{false};
        int   winShowCursorCount{0};
        void* winCursorHandle{nullptr};
        bool  hasClipRect{false};
        long  clipLeft{0};
        long  clipTop{0};
        long  clipRight{0};
        long  clipBottom{0};
    };

    void detectVersionIfNeeded() noexcept;
    void checkConflictWindow() noexcept;
    void tryHookRakClient();
    void onProxyDestroyed(const ProxyRakClient* proxy) noexcept;
    void flushOutgoingControlPacketsOnMainThread() noexcept;
    void updateSnapshotsOnMainThread();
    [[nodiscard]] void* currentNetGamePtr() const noexcept;
    [[nodiscard]] const void* resolvePlayerGtaPed(std::uint16_t playerId) const noexcept;
    [[nodiscard]] bool  isVtableInSamp(const void* objPtr) const noexcept;

    void*            selfModule_{nullptr};
    std::uintptr_t   sampBase_{0};
    std::size_t      sampSize_{0};
    SampLayout       layout_{};
    bool             unsupportedDetected_{false};
    bool             rakDtorVerified_{false};
    std::atomic<bool> conflictingClient_{false};
    bool             conflictWindowExpired_{false};
    std::chrono::steady_clock::time_point firstTickTime_{};
    std::chrono::steady_clock::time_point lastConflictCheckTime_{};
    std::chrono::steady_clock::time_point lastNameRefreshTime_{};

    // Keep prior wrappers alive in shadow RakPeer blocks for process lifetime (zero UAF on exit)
    std::vector<ProxyRakClient*> proxies_{};
    ProxyRakClient* activeProxy_{nullptr};
    bool            rakHooked_{false};
    bool            rakTornDown_{false};
    bool            prevConnected_{false};
    bool            prevSpawned_{false};
    mutable bool    boneFallbackLogged_{false};
    std::atomic<bool> microAvailable_{true};
    bool            cursorOpenedByMenu_{false};
    SavedCursorState savedCursorState_{};

    mutable std::mutex                             cacheMutex_{};
    std::array<std::string, kMaxPlayerSlots>       cachedRawPlayerNames_{};
    std::array<std::string, kMaxPlayerSlots>       cachedPlayerNames_{};
    std::atomic<std::uint32_t>                     nameCacheGen_{1};
    std::atomic<std::uint32_t>                     connectedIpv4Net_{0};
    std::atomic<std::uint16_t>                     connectedRakPort_{0};

    OutgoingControlQueue                outgoingControlQueue_{512};
    std::vector<OutgoingControlMessage> outgoingDrainScratch_{};

    ControlPacketCallback onControl_{};
    ConnectStateCallback  onConnectPrepared_{};
};

} // namespace svplus::client::samp
