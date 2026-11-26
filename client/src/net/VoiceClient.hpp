#pragma once

#include "config/ClientConfig.hpp"
#include "svplus/common/LockFreeQueue.hpp"
#include "svplus/common/Protocol.hpp"
#include "svplus/common/UdpSocket.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace svplus::client::net {

struct LocalSpeakerStreamTag {
    std::uint32_t streamId{0};
    std::uint32_t color{0};
    std::string   name{};
    std::uint8_t  kind{0}; // 0 = global, 1 = positional (SL/DL)

    bool operator==(const LocalSpeakerStreamTag& other) const noexcept = default;
};

enum class VoiceDiagnosticState : std::uint8_t {
    Idle = 0,               // Not connected to SA-MP server
    Connecting,             // SA-MP connected; voice handshake in progress
    NoVoiceServer,          // Server did not respond to voice handshake (no plugin?)
    UdpBlocked,             // UDP keep-alive timed out (UDP port firewalled/NAT?)
    ConnectedMicDisabled,   // Voice connected, microphone disabled in settings
    ConnectedNoMic,         // Voice connected, no microphone enabled/available
    ConnectedMuted,         // Voice connected, muted by server
    ListenerDisabled,       // Voice connected, incoming listening disabled by server
    ConnectedOk             // Voice connected and ready
};

class VoiceClient {
public:
    static VoiceClient& instance() noexcept;

    void setServerEndpointInfo(std::string_view host, std::uint32_t ipv4Net, std::uint16_t rakPort, bool microSentInHandshake);
    void handleControlPacket(std::uint16_t packetId, const std::uint8_t* data, std::size_t size);

    /// Thread-safe: called on the dedicated audio thread.
    void sendEncodedVoiceFrame(std::span<const std::uint8_t> opusPacket, std::uint32_t seq);
    void sendKeyEvent(std::uint8_t vkCode, bool pressed);

    /// Executed strictly on the dedicated audio thread (owns udpSocket_ start/stop/TX + RX queue drain).
    void tick(const config::ClientConfig& cfg, bool sampConnected);
    /// Must be called from the dedicated audio thread (or after audio thread exits).
    void shutdown() noexcept;

    [[nodiscard]] bool                 isVoiceConnected() const noexcept { return pluginInitialized_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 isServerPlus() const noexcept { return serverIsPlus_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 isVoiceStatusEnabled() const noexcept { return voiceStatusEnabled_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 isNearbyListEnabled() const noexcept { return nearbyListEnabled_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 isAllowToggleMode() const noexcept { return allowToggleMode_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 isVoiceOcclusionEnabled() const noexcept { return voiceOcclusion_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 isListenerDisabled() const noexcept { return listenerDisabled_.load(std::memory_order_acquire); }
    [[nodiscard]] bool                 acceptsVoiceLatency() const noexcept { return serverAcceptsVoiceLatency_.load(std::memory_order_acquire); }
    bool                               applyPlusServerOptionsPayload(const std::uint8_t* data, std::size_t size) noexcept;
    void                               clearServerOptionsAndVoiceStates() noexcept;
    [[nodiscard]] protocol::PlayerVoiceState playerVoiceState(std::uint16_t playerId) const noexcept {
        if (!voiceStatusEnabled_.load(std::memory_order_acquire) || playerId >= playerVoiceStates_.size()) {
            return protocol::PlayerVoiceState::Ok;
        }
        const std::uint8_t raw = playerVoiceStates_[playerId].load(std::memory_order_acquire);
        if (raw == static_cast<std::uint8_t>(protocol::PlayerVoiceState::NoMicro)) {
            return protocol::PlayerVoiceState::NoMicro;
        }
        if (raw == static_cast<std::uint8_t>(protocol::PlayerVoiceState::CannotHear)) {
            return protocol::PlayerVoiceState::CannotHear;
        }
        return protocol::PlayerVoiceState::Ok;
    }
    [[nodiscard]] float                rttMs() const noexcept { return rttMs_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint16_t        serverUdpPort() const noexcept { return serverUdpPort_.load(std::memory_order_acquire); }
    [[nodiscard]] VoiceDiagnosticState diagnosticState() const noexcept { return diagnosticState_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t        txVoicePackets() const noexcept { return txVoicePackets_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t        rxVoicePackets() const noexcept { return rxVoicePackets_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t        rxQueueDropped() const noexcept { return rxQueueDropped_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::shared_ptr<const std::vector<LocalSpeakerStreamTag>> localSpeakerStreamsSnapshot() const;

private:
    VoiceClient() = default;
    ~VoiceClient() = default;
    VoiceClient(const VoiceClient&) = delete;
    VoiceClient& operator=(const VoiceClient&) = delete;

    struct QueuedVoicePacket {
        std::uint32_t streamId{0};
        std::uint16_t speakerId{0};
        std::uint32_t seq{0};
        std::uint16_t length{0};
        std::array<std::uint8_t, protocol::kMaxVoicePayloadBytes> payload{};
    };

    void startUdpLoop();
    void stopUdpLoop() noexcept;
    void resetSessionOnAudioThread();
    void clearLocalSpeakerStreams();
    void udpRxThreadFunc();
    void sendKeepAlive() noexcept;

    mutable std::mutex         endpointMutex_{};
    std::string                serverHost_{};
    std::uint32_t              serverIpv4Net_{0};
    std::uint16_t              rakPort_{0};

    // Pending UDP connect published on game thread -> applied cleanly on audio thread
    std::atomic<bool>          hasPendingConnect_{false};
    svplus::net::UdpEndpoint   pendingEndpoint_{};
    std::uint32_t              pendingServerKey_{0};

    // Owned exclusively by the dedicated audio thread (zero cross-thread socket open/close/send races)
    svplus::net::UdpEndpoint   serverEndpoint_{};
    std::atomic<std::uint64_t> expectedSourcePacked_{0};
    svplus::net::UdpSocket     udpSocket_{};
    std::thread                rxThread_{};
    std::atomic<bool>          rxRunning_{false};

    std::atomic<std::uint32_t>        serverKey_{0};
    std::atomic<std::uint16_t>        serverUdpPort_{0};
    std::atomic<bool>                 pluginInitialized_{false};
    std::atomic<bool>                 serverIsPlus_{false};
    std::atomic<bool>                 voiceStatusEnabled_{false};
    std::atomic<bool>                 nearbyListEnabled_{false};
    std::atomic<bool>                 allowToggleMode_{true};
    std::atomic<bool>                 voiceOcclusion_{false};
    std::atomic<bool>                 serverAcceptsVoiceLatency_{false};
    std::atomic<bool>                 listenerDisabled_{false};
    std::array<std::atomic<std::uint8_t>, 1004> playerVoiceStates_{};
    std::atomic<bool>                 handshakeMicroSent_{true};
    std::atomic<float>                rttMs_{-1.0f};
    std::atomic<VoiceDiagnosticState> diagnosticState_{VoiceDiagnosticState::Idle};
    std::atomic<std::uint64_t>        txVoicePackets_{0};
    std::atomic<std::uint64_t>        rxVoicePackets_{0};
    std::atomic<std::uint64_t>        rxQueueDropped_{0};

    bool                                  serverMicroSynced_{false};
    bool                                  serverKnownMicro_{false};
    bool                                  wasSampConnected_{false};
    std::chrono::steady_clock::time_point sampConnectTime_{};
    std::chrono::steady_clock::time_point serverInfoTime_{};
    std::uint32_t                         keepAliveBackoffMs_{250};
    std::chrono::steady_clock::time_point lastKeepAlive_{};
    std::chrono::steady_clock::time_point lastPingSent_{};

    std::atomic<std::int64_t>             lastUdpRxMs_{0};
    std::atomic<std::int64_t>             lastUdpSocketErrMs_{0};
    bool                                  udpHealthyOnce_{false};
    bool                                  initialUdpUnreachableErrLogged_{false};
    std::chrono::steady_clock::time_point udpDropStartTime_{};
    bool                                  midSessionUdpDropErrLogged_{false};

    mutable std::mutex                                              localSpeakerStreamsMutex_{};
    std::shared_ptr<const std::vector<LocalSpeakerStreamTag>>       localSpeakerStreams_{
        std::make_shared<const std::vector<LocalSpeakerStreamTag>>()};

    svplus::concurrency::SpscQueue<QueuedVoicePacket> rxAudioQueue_{256};
};

} // namespace svplus::client::net
