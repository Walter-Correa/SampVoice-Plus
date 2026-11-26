#pragma once

#include "audio/BassLoader.hpp"
#include "audio/VoiceCodec.hpp"
#include "config/ClientConfig.hpp"
#include "svplus/common/Protocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace svplus::client::audio {

struct CaptureDeviceInfo {
    int         deviceIndex{0};
    std::string name{};
    bool        isDefault{false};
};

class CaptureEngine {
public:
    using EncodedFrameCallback = std::function<void(std::span<const std::uint8_t> opusPacket, std::uint32_t seq)>;
    using KeyStateCallback     = std::function<void(std::uint8_t keyId, bool pressed)>;

    static CaptureEngine& instance() noexcept;

    /// Must be called on the dedicated audio thread.
    [[nodiscard]] bool initialize(const config::ClientConfig& cfg);
    /// Must be called on the dedicated audio thread during teardown.
    void shutdown() noexcept;

    void setCallbacks(EncodedFrameCallback onFrame, KeyStateCallback onKeyState);

    void addActivationKey(std::uint8_t vkCode);
    void removeActivationKey(std::uint8_t vkCode);
    void clearActivationKeys();

    void setServerRecordActive(bool active) noexcept;
    void setServerMuted(bool muted) noexcept;
    void latchPressedKeysForMute() noexcept;
    void requestBitrate(std::uint32_t rawBitrate, bool fromServer = true) noexcept;

    /// Thread-safe UI requests executed on the dedicated audio thread in tick().
    void requestRefreshDevices() noexcept;
    void requestSelectDevice(std::string_view name);

    /// Controls local microphone loopback self-monitor (R56-7). Safe to call from UI thread.
    void setMicMonitorEnabled(bool enabled, const char* reason = "user");
    [[nodiscard]] bool isMicMonitorEnabled() const noexcept {
        return micMonitorDesired_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::vector<CaptureDeviceInfo> devicesSnapshot() const;

    /// Executed periodically on the dedicated audio thread.
    void tick(const config::ClientConfig& cfg,
              bool inputBlocked,
              const char* inputBlockReason = nullptr,
              bool allowToggleMode = true);

    /// Evaluates activation key state transitions using an explicit key-state probe.
    void evaluateActivationKeys(
        const config::ClientConfig& cfg,
        bool inputBlocked,
        const char* inputBlockReason,
        bool allowToggleMode,
        bool hasDeviceNow,
        bool (*isKeyPhysicallyDown)(std::uint8_t vk, void* ctx) noexcept,
        void* keyProbeCtx,
        std::chrono::steady_clock::time_point now,
        bool& outAnyKeyHeld
    );

    [[nodiscard]] bool          hasMicrophone() const noexcept { return hasDevice_.load(std::memory_order_acquire); }
    [[nodiscard]] bool          isTransmitting() const noexcept { return transmitting_.load(std::memory_order_acquire); }
    [[nodiscard]] bool          isServerMuted() const noexcept { return serverMuted_.load(std::memory_order_acquire); }
    [[nodiscard]] bool          hasKeyConflict() const noexcept { return hasKeyConflict_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint8_t  conflictingKey() const noexcept { return conflictingKey_.load(std::memory_order_acquire); }
    [[nodiscard]] bool          hasActivationKey(std::uint8_t vkCode) const noexcept;
    [[nodiscard]] float         currentInputLevel() const noexcept { return inputLevel_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint32_t activeBitrate() const noexcept { return activeBitrate_.load(std::memory_order_acquire); }

private:
    CaptureEngine() = default;
    ~CaptureEngine() = default;
    CaptureEngine(const CaptureEngine&) = delete;
    CaptureEngine& operator=(const CaptureEngine&) = delete;

    static constexpr std::size_t kFrameSamples = protocol::kLegacyFrameSamples; // Fixed 100ms (4800 samples)
    static constexpr std::size_t kRingCapacity = kFrameSamples * 4u;
    static constexpr std::size_t kMinTailSamples = 480u; // 10ms minimum speech tail to pad on key release

    struct PcmRingBuffer {
        std::array<std::int16_t, kRingCapacity> buf{};
        std::size_t head{0};
        std::size_t tail{0};
        std::size_t count{0};

        void clear() noexcept;
        void push(const std::int16_t* samples, std::size_t n) noexcept;
        bool popFrame(std::int16_t* outFrame) noexcept;
    };

    void refreshDevicesOnAudioThread();
    bool selectDeviceOnAudioThread(std::string_view name);
    bool openCaptureStream(int deviceIndex, std::string_view deviceName = {});
    void closeCaptureStream() noexcept;
    void processMicDsp(std::span<std::int16_t> pcm, float gain, bool suppressNoise) noexcept;

    void startMicMonitorOnAudioThread();
    void stopMicMonitorOnAudioThread(const char* reason) noexcept;

    void flushPaddedTail(const EncodedFrameCallback& frameCb);

    mutable std::mutex mutex_{};
    std::vector<CaptureDeviceInfo> devices_{};
    int     selectedDeviceIndex_{-1};
    std::string selectedDeviceName_{};
    bool    activeDevicePresent_{false};
    HRECORD recordHandle_{0};
    std::chrono::steady_clock::time_point lastDeviceScan_{};
    std::chrono::steady_clock::time_point lastReconnectAttempt_{};
    bool    reconnectLogged_{false};
    int     lastBassError_{0};
    std::string lastFailedDeviceName_{};
    int     consecutiveOpenFailures_{0};
    bool    openFailureErrLogged_{false};

    std::atomic<bool> pendingRefresh_{false};
    std::mutex        pendingDeviceMutex_{};
    bool              hasPendingDeviceSelect_{false};
    std::string       pendingDeviceName_{};

    VoiceEncoder  encoder_{};
    std::uint32_t sequenceCounter_{0};

    struct KeyBinding {
        std::uint8_t vk{0};
        bool         pressed{false};
        bool         rawDownLast{false};
        bool         physDownLast{false};
        bool         toggleLatched{false};
        bool         conflictWarned{false};
        bool         muteLatched{false};
    };
    std::vector<KeyBinding> activationKeys_{};

    void pollActivationKeys(
        const config::ClientConfig& cfg,
        bool inputBlocked,
        const char* inputBlockReason,
        bool allowToggleMode,
        std::chrono::steady_clock::time_point now,
        bool& outAnyKeyHeld
    );

    void recordKeyTransitionLog(std::uint8_t vk,
                                bool pressed,
                                const char* reason,
                                std::chrono::steady_clock::time_point now);
    void flushKeyTransitionBurst(std::chrono::steady_clock::time_point now, bool force);

    std::deque<std::chrono::steady_clock::time_point> keyTransitionTimes_{};
    bool        keyBurstCoalescing_{false};
    std::size_t coalescedKeyTransitions_{0};
    std::string coalescedKeySummary_{};
    std::chrono::steady_clock::time_point lastKeyBurstLog_{};

    PcmRingBuffer                                         txRing_{};
    PcmRingBuffer                                         monitorRing_{};
    std::array<std::int16_t, kFrameSamples>               encodeScratch_{};
    std::array<std::int16_t, kFrameSamples>               monitorDecodeBuf_{};
    std::vector<std::int16_t>                             captureScratch_{};
    std::vector<std::uint8_t>                             opusOutBuf_{};
    bool                                                  wasTransmitting_{false};

    VoiceEncoder  monitorEncoder_{};
    VoiceDecoder  monitorDecoder_{};
    HSTREAM       monitorBassStream_{0};
    std::uint32_t appliedMonitorBitrate_{protocol::kDefaultBitrate};
    std::atomic<bool> micMonitorDesired_{false};
    std::mutex        monitorReasonMutex_{};
    std::string       monitorStopReason_{};
    bool              micMonitorActive_{false};

    // Persistent high-pass & noise gate filter state
    float hpPrevX_{0.0f};
    float hpPrevY_{0.0f};
    float gateEnv_{0.0f};
    float limiterEnv_{0.0f};

    std::atomic<bool>          hasDevice_{false};
    std::atomic<bool>          transmitting_{false};
    std::atomic<bool>          serverRecordActive_{false};
    std::atomic<bool>          serverMuted_{false};
    std::atomic<bool>          hasKeyConflict_{false};
    std::atomic<std::uint8_t>  conflictingKey_{0};
    std::atomic<float>         inputLevel_{0.0f};
    std::atomic<std::uint64_t> pendingBitratePacked_{static_cast<std::uint64_t>(protocol::kDefaultBitrate)};
    std::uint64_t              appliedBitratePacked_{static_cast<std::uint64_t>(protocol::kDefaultBitrate)};
    std::atomic<std::uint32_t> activeBitrate_{protocol::kDefaultBitrate};

    EncodedFrameCallback onFrame_{};
    KeyStateCallback     onKeyState_{};
};

} // namespace svplus::client::audio
