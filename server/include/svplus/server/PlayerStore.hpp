/*
 * SampVoice Plus - Lock-Friendly Per-Player Voice State Store
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "IServerAdapter.hpp"
#include "svplus/common/UdpSocket.hpp"

namespace svplus::server {

struct PlayerVoiceSlot {
    std::atomic<bool> connected{false};
    std::atomic<bool> hasPlugin{false};
    std::atomic<bool> udpIdentified{false};
    std::atomic<bool> microStatus{false};
    std::atomic<bool> recordStatus{false};
    std::atomic<bool> muteStatus{false};
    std::atomic<bool> listenerEnabled{true};
    std::atomic<uint8_t> pluginVersion{0};
    std::atomic<uint8_t> clientProtocol{0}; // 0=none, 11=v3.x, 12=SV+, 4=CyberMor v4 (unsupported)
    std::atomic<bool> v4Warned{false};
    std::atomic<uint32_t> capabilities{0};
    std::atomic<uint32_t> activeKeyCount{0};

    // 256-bit keyset (zero heap allocation, O(1) key add/remove/check)
    std::array<uint64_t, 4> keyBits{};

    uint32_t serverKey{0};
    uint64_t fullSessionKey{0};

    mutable std::shared_mutex dataMutex;
    net::UdpEndpoint udpEndpoint{};
    std::chrono::steady_clock::time_point lastUdpSwitchTime{};
    std::vector<uint32_t> speakerStreams;
    std::vector<uint32_t> listenerStreams;

    // Per-player UDP voice flood limiter (token bucket: 30 pkt/s sustained, burst 10).
    // 100 ms Opus frames arrive at ~10 pkt/s, giving 3x headroom plus jitter burst tolerance.
    static constexpr double kVoiceRateTokensPerSec = 30.0;
    static constexpr double kVoiceRateMaxBurst     = 10.0;
    std::mutex rateMutex;
    double voiceRateTokens{kVoiceRateMaxBurst};
    std::chrono::steady_clock::time_point lastRateRefill{};
    std::chrono::steady_clock::time_point lastDropLogTime{};
    std::chrono::steady_clock::time_point lastNatLogTime{};
    uint64_t droppedVoicePackets{0};

    // Per-player control packet flood limiters (token buckets, isolated from UDP voice mutex):
    // PressKey/ReleaseKey: 20/s burst 20; PlusMicroState: 4/s burst 4; PlusTelemetryPing: 2/s burst 2.
    enum class ControlBucketKind : uint8_t {
        KeyEvent = 0,
        MicroState = 1,
        TelemetryPing = 2
    };
    static constexpr double kKeyRateTokensPerSec   = 20.0;
    static constexpr double kKeyRateMaxBurst       = 20.0;
    static constexpr double kMicroRateTokensPerSec = 4.0;
    static constexpr double kMicroRateMaxBurst     = 4.0;
    static constexpr double kPingRateTokensPerSec  = 2.0;
    static constexpr double kPingRateMaxBurst      = 2.0;
    static constexpr int    kControlFloodLogSec    = 10;

    struct ControlBucketState {
        double tokens{0.0};
        std::chrono::steady_clock::time_point lastRefill{};
    };
    std::mutex controlRateMutex;
    std::array<ControlBucketState, 3> controlBuckets_{{
        {kKeyRateMaxBurst, {}},
        {kMicroRateMaxBurst, {}},
        {kPingRateMaxBurst, {}}
    }};
    std::chrono::steady_clock::time_point lastControlDropLogTime{};
    uint64_t droppedControlPackets{0};
    std::array<uint64_t, 4> pressedKeyBits{};
    std::array<uint32_t, 256> droppedPressKeys{};
    std::array<uint32_t, 256> droppedReleaseKeys{};
    uint64_t droppedPingCount{0};
    uint64_t droppedMicroCount{0};

    bool ConsumeVoiceToken(std::chrono::steady_clock::time_point now, uint64_t& outDroppedToLog) noexcept;
    void ResetControlLimiter() noexcept;
    bool AcceptControlPacket(uint16_t packetType,
                             uint8_t keyId,
                             ControlBucketKind kind,
                             std::chrono::steady_clock::time_point now,
                             std::string& outFloodSummary);
    bool ConsumeControlToken(ControlBucketKind kind,
                             std::chrono::steady_clock::time_point now,
                             uint64_t& outDroppedToLog) noexcept;
    [[nodiscard]] bool TakePressedKey(uint8_t keyId) noexcept;
    [[nodiscard]] std::vector<uint8_t> TakePressedKeysForRemoval(const std::array<uint64_t, 4>& allowedKeyBits);
    void ClearPressedKey(uint8_t keyId) noexcept;
    void ClearAllPressedKeys() noexcept;
    bool ShouldLogNatPortUpdate(std::chrono::steady_clock::time_point now) noexcept;

    bool HasKey(uint8_t keyId) const noexcept {
        return (keyBits[keyId >> 6] & (1ull << (keyId & 63))) != 0;
    }

    bool AddKey(uint8_t keyId) noexcept {
        const uint64_t mask = 1ull << (keyId & 63);
        if (keyBits[keyId >> 6] & mask) {
            return false;
        }
        keyBits[keyId >> 6] |= mask;
        activeKeyCount.fetch_add(1, std::memory_order_release);
        return true;
    }

    bool RemoveKey(uint8_t keyId, bool* outWasPressed = nullptr) noexcept {
        const uint64_t mask = 1ull << (keyId & 63);
        if (!(keyBits[keyId >> 6] & mask)) {
            if (outWasPressed) *outWasPressed = false;
            return false;
        }
        keyBits[keyId >> 6] &= ~mask;
        activeKeyCount.fetch_sub(1, std::memory_order_release);
        const bool wasPressed = TakePressedKey(keyId);
        if (outWasPressed) *outWasPressed = wasPressed;
        return true;
    }

    std::vector<uint8_t> ClearKeys() {
        const auto savedBits = keyBits;
        keyBits.fill(0);
        activeKeyCount.store(0, std::memory_order_release);
        return TakePressedKeysForRemoval(savedBits);
    }

    void Reset();
};

class PlayerStore {
public:
    PlayerStore();
    PlayerStore(const PlayerStore&) = delete;
    PlayerStore& operator=(const PlayerStore&) = delete;

    uint32_t RegisterConnect(
        uint16_t playerId,
        uint32_t ipv4Net,
        uint8_t version,
        uint8_t clientProtocol,
        bool hasMicro,
        uint32_t capabilities
    );

    /// Records an unsupported CyberMor v4.x client; returns true once per connection.
    bool RegisterUnsupportedV4(uint16_t playerId);

    void RegisterDisconnect(uint16_t playerId);

    /**
     * Resolves playerId from incoming UDP (ipv4Net, serverKey).
     * Returns kInvalidPlayerId if not found.
     */
    uint16_t FindPlayerByUdpKey(uint32_t ipv4Net, uint32_t serverKey) const;

    /**
     * Attempts to bind (or NAT-update) the player's UDP endpoint on valid packet.
     * Returns true ONLY the first time this player becomes UDP-identified.
     */
    bool TryIdentifyUdpEndpoint(uint16_t playerId, const net::UdpEndpoint& ep, bool* outPortUpdated = nullptr);

    bool GetUdpEndpoint(uint16_t playerId, net::UdpEndpoint& outEp) const;

    PlayerVoiceSlot* GetSlot(uint16_t playerId) noexcept;
    const PlayerVoiceSlot* GetSlot(uint16_t playerId) const noexcept;

    void AttachSpeakerStream(uint16_t playerId, uint32_t streamId);
    void DetachSpeakerStream(uint16_t playerId, uint32_t streamId);

    void AttachListenerStream(uint16_t playerId, uint32_t streamId);
    void DetachListenerStream(uint16_t playerId, uint32_t streamId);

    const std::array<std::atomic<bool>, kMaxPlayers>& PluginFlags() const noexcept {
        return pluginFlags_;
    }

    const std::array<std::atomic<bool>, kMaxPlayers>& PlusFlags() const noexcept {
        return plusFlags_;
    }

    void ClearAll();

private:
    static uint64_t MakeSessionKey(uint32_t ipv4Net, uint32_t serverKey) noexcept {
        return (static_cast<uint64_t>(ipv4Net) << 32) | static_cast<uint64_t>(serverKey);
    }

    std::array<PlayerVoiceSlot, kMaxPlayers> slots_{};
    std::array<std::atomic<bool>, kMaxPlayers> pluginFlags_{};
    std::array<std::atomic<bool>, kMaxPlayers> plusFlags_{};

    mutable std::shared_mutex keyMapMutex_;
    std::unordered_map<uint64_t, uint16_t> sessionKeyToPlayer_;

    std::mutex rngMutex_;
    std::mt19937 rng_;
};

} // namespace svplus::server
