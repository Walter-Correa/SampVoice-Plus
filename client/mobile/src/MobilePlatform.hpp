#pragma once

#include "svplus_mobile.h"

#include "svplus/client/CoreConfig.hpp"
#include "svplus/client/OutgoingControlQueue.hpp"
#include "svplus/client/Platform.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace svplus::mobile {

class MobileBassLibrary final : public svplus::client::IBassLibrary {
public:
    using ProcLookupFn = void* (*)(void* user, const char* symbolName);

    void setResolver(ProcLookupFn procLookup, void* user) noexcept {
        procLookup_ = procLookup;
        user_       = user;
    }

    [[nodiscard]] bool initialize() noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] bool pollReady() noexcept override;

    [[nodiscard]] bool isLoaded() const noexcept override { return loaded_.load(std::memory_order_acquire); }
    [[nodiscard]] bool isAudioReady() const noexcept override { return ready_.load(std::memory_order_acquire); }
    [[nodiscard]] const svplus::client::audio::BassApi& api() const noexcept override { return api_; }

private:
    static HFX WINAPI channelSetFxThunk(DWORD handle, DWORD type, int priority) noexcept;

    template <typename Fn>
    [[nodiscard]] bool resolveSymbol(const char* name, Fn& out) noexcept;

    [[nodiscard]] void* lookupRawSymbol(const char* name) const noexcept;

    static std::atomic<MobileBassLibrary*> activeInstance_;

    ProcLookupFn                         procLookup_{nullptr};
    void*                                user_{nullptr};
#if !defined(_WIN32)
    void*                                dlHandle_{nullptr};
#endif
    std::atomic<bool>                    loaded_{false};
    std::atomic<bool>                    ready_{false};
    svplus::client::audio::BassApi       api_{};
    HFX(WINAPI*                          rawChannelSetFX_)(DWORD, DWORD, int){nullptr};
    std::atomic<std::uint32_t>           refusedFxLogMask_{0};
};

struct IncomingControlMessage {
    std::uint16_t             packetId{0};
    std::vector<std::uint8_t> payload{};
    std::size_t               accountedBytes{0};
};

class MobileIncomingControlQueue final {
public:
    static constexpr std::size_t kMaxPackets = 8192u;
    static constexpr std::size_t kMaxBytes   = 4u * 1024u * 1024u;

    void enqueue(std::uint16_t packetId, const std::uint8_t* data, std::size_t size);
    void drain(std::vector<IncomingControlMessage>& out);
    void clear() noexcept;

private:
    std::mutex                           mutex_{};
    std::deque<IncomingControlMessage>   queue_{};
    std::size_t                          queuedBytes_{0};
    std::chrono::steady_clock::time_point lastDropWarnTime_{};
    std::uint64_t                        suppressedDrops_{0};
};

class MobileControlTransport final : public svplus::client::IControlTransport {
public:
    using SendControlFn = void (*)(void* user, const uint8_t* bytes, uint32_t size);

    [[nodiscard]] bool send(std::uint16_t packetId, const void* data, std::size_t size) override;
    void clearOutgoing() noexcept override;

    void drainToHost(SendControlFn sendFn, void* user);
    void resetOnMainThread() noexcept;

private:
    svplus::client::OutgoingControlQueue               queue_{512};
    std::vector<svplus::client::OutgoingControlMessage> drainScratch_{};
};

class MobileKeyInput final : public svplus::client::IKeyInput {
public:
    void setKeyDown(std::uint8_t vkCode, bool down) noexcept {
        keys_[vkCode].store(down, std::memory_order_release);
    }

    void reset() noexcept {
        for (auto& k : keys_) {
            k.store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] bool isKeyDown(std::uint8_t keyCode) const noexcept override {
        return keys_[keyCode].load(std::memory_order_acquire);
    }

private:
    std::array<std::atomic<bool>, 256> keys_{};
};

class MobileGameState final : public svplus::client::IGameState {
public:
    void configure(const svp_host_t& host) noexcept {
        host_ = host;
    }

    void reset() noexcept;

    void setFrameActive(bool active) noexcept {
        inFrame_.store(active, std::memory_order_release);
    }

    bool setPlayerName(std::uint16_t playerId, std::string_view rawName);
    bool clearPlayerName(std::uint16_t playerId);
    void flushPendingNamesOnFrame();

    [[nodiscard]] bool getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept override;
    [[nodiscard]] bool getLocalPedPosition(float pos[3]) const noexcept override;
    [[nodiscard]] bool getPlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept override;
    [[nodiscard]] bool getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept override;
    [[nodiscard]] bool getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept override;
    [[nodiscard]] bool isLocalPlayerInVehicle(std::uint16_t vehicleId) const noexcept override;

    [[nodiscard]] std::uint32_t nameCacheGeneration() const noexcept override {
        return nameGen_.load(std::memory_order_acquire);
    }
    void copyCachedPlayerName(std::uint16_t playerId, std::string& out) const override;

    [[nodiscard]] bool hasOcclusion() const noexcept override {
        return host_.is_line_of_sight_clear != nullptr;
    }
    [[nodiscard]] const void* getObjectEntity(std::uint16_t objectId) const noexcept override;
    [[nodiscard]] bool isLineOfSightClear(
        const float origin[3],
        const float target[3],
        const void* ignoreEntity) const noexcept override;

    void decodeLegacyText(std::string_view raw, std::string& out) const override;

private:
    enum class PendingNameOp : std::uint8_t {
        None = 0,
        Set,
        Clear
    };

    struct PendingNameSlot {
        PendingNameOp op{PendingNameOp::None};
        std::string   raw{};
    };

    struct FramePendingNameItem {
        std::uint16_t playerId{0};
        PendingNameOp op{PendingNameOp::None};
        std::string   raw{};
        std::string   decoded{};
    };

    svp_host_t host_{};
    std::atomic<bool> inFrame_{false};
    mutable bool hasLastValidCamUp_{false};
    mutable std::array<float, 3> lastValidCamUp_{0.0f, 1.0f, 0.0f};

    mutable std::mutex pendingMutex_{};
    std::array<PendingNameSlot, svplus::client::kMaxPlayerSlots> pendingNames_{};
    std::vector<std::uint16_t> dirtyPlayerIds_{};
    std::atomic<bool> hasPendingNames_{false};
    std::vector<FramePendingNameItem> framePendingScratch_{};

    mutable std::mutex nameMutex_{};
    std::array<std::string, svplus::client::kMaxPlayerSlots> cachedNames_{};
    std::atomic<std::uint32_t> nameGen_{1};
};

} // namespace svplus::mobile
