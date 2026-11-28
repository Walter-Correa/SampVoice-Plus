#pragma once

#include "svplus/common/LockFreeQueue.hpp"
#include "svplus/common/Protocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace svplus::client {

struct OutgoingControlMessage {
    using Clock = std::chrono::steady_clock;

    std::array<std::uint8_t, protocol::kMaxUdpPacketSize> bytes{};
    int                                                   size{0};
    std::uint16_t                                         packetId{0};
    std::uint8_t                                          keyId{0};
    Clock::time_point                                     enqueuedAt{};
};

/// Thread-safe queue between background threads (audio/capture) and the game thread.
/// Enforces at most one pending PlusTelemetryPing, stamps ping timestamps at actual TX time,
/// keeps only the newest PlusVoiceLatency report per drain, and cancels mutually-nullifying
/// PressKey -> ReleaseKey pairs after a long game-thread stall.
class OutgoingControlQueue {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::chrono::milliseconds kStaleKeyStallThreshold{3000};

    explicit OutgoingControlQueue(std::size_t capacity = 512)
        : queue_(capacity) {}

    bool enqueue(
        std::uint16_t packetId,
        const void* payload,
        std::uint16_t payloadSize,
        Clock::time_point now = Clock::now()
    ) {
        const std::size_t totalBytes = 1u + sizeof(protocol::ControlPacketHeader) + payloadSize;
        if (totalBytes > protocol::kMaxUdpPacketSize) {
            return false;
        }

        const bool isPing =
            (packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::PlusTelemetryPing));
        if (isPing && pingPending_.exchange(true, std::memory_order_acq_rel)) {
            return false;
        }

        OutgoingControlMessage msg;
        msg.size = static_cast<int>(totalBytes);
        msg.packetId = packetId;
        msg.enqueuedAt = now;
        msg.bytes[0] = protocol::kRakNetPacketIdControl;

        const protocol::ControlPacketHeader hdr{packetId, payloadSize};
        std::memcpy(msg.bytes.data() + 1, &hdr, sizeof(hdr));
        if (payload && payloadSize > 0) {
            std::memcpy(msg.bytes.data() + 1 + sizeof(hdr), payload, payloadSize);
            if ((packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::PressKey) ||
                 packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::ReleaseKey)) &&
                payloadSize >= 1u) {
                std::memcpy(&msg.keyId, payload, sizeof(msg.keyId));
            }
        }

        if (!queue_.TryPush(msg)) {
            if (isPing) {
                pingPending_.store(false, std::memory_order_release);
            }
            return false;
        }
        return true;
    }

    /// Game-thread only: drains all queued control messages into `out`, cancels stale
    /// down->up key pairs that annihilated during a stall, and stamps telemetry pings.
    void drain(std::vector<OutgoingControlMessage>& out, Clock::time_point now = Clock::now()) {
        out.clear();
        rawScratch_.clear();

        OutgoingControlMessage msg;
        while (queue_.TryPop(msg)) {
            if (msg.packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::PlusTelemetryPing)) {
                pingPending_.store(false, std::memory_order_release);
                stampPingTimestamp(msg, now);
            }
            rawScratch_.push_back(msg);
        }

        if (rawScratch_.empty()) {
            lastDrainTime_ = now;
            return;
        }

        const bool threadStalled =
            (lastDrainTime_.time_since_epoch().count() != 0 &&
             (now - lastDrainTime_) >= kStaleKeyStallThreshold);
        lastDrainTime_ = now;

        keepScratch_.assign(rawScratch_.size(), 1u);
        for (auto& stack : activePressIndices_) {
            stack.clear();
        }

        std::size_t lastLatencyIdx = rawScratch_.size();
        for (std::size_t i = 0; i < rawScratch_.size(); ++i) {
            const auto& cur = rawScratch_[i];
            if (cur.packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::PressKey)) {
                const bool stale = threadStalled || ((now - cur.enqueuedAt) >= kStaleKeyStallThreshold);
                if (stale) {
                    activePressIndices_[cur.keyId].push_back(i);
                }
            } else if (cur.packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::ReleaseKey)) {
                auto& stack = activePressIndices_[cur.keyId];
                if (!stack.empty()) {
                    const std::size_t matchedPressIdx = stack.back();
                    stack.pop_back();
                    keepScratch_[matchedPressIdx] = 0u;
                    keepScratch_[i] = 0u;
                }
            } else if (cur.packetId == static_cast<std::uint16_t>(protocol::ControlPacketType::PlusVoiceLatency)) {
                if (lastLatencyIdx < rawScratch_.size()) {
                    keepScratch_[lastLatencyIdx] = 0u;
                }
                lastLatencyIdx = i;
            }
        }

        for (std::size_t i = 0; i < rawScratch_.size(); ++i) {
            if (keepScratch_[i] != 0u) {
                out.push_back(rawScratch_[i]);
            }
        }
    }

    /// Thread-safe: clears queued packets and resets the pending-ping flag.
    void clear() noexcept {
        OutgoingControlMessage msg;
        while (queue_.TryPop(msg)) {}
        pingPending_.store(false, std::memory_order_release);
    }

    /// Game-thread only: clears queue state on disconnect/reconnect.
    void resetOnMainThread() noexcept {
        clear();
        lastDrainTime_ = {};
    }

    [[nodiscard]] bool isPingPending() const noexcept {
        return pingPending_.load(std::memory_order_acquire);
    }

private:
    static void stampPingTimestamp(OutgoingControlMessage& msg, Clock::time_point now) noexcept {
        const std::size_t headerOffset = 1u + sizeof(protocol::ControlPacketHeader);
        if (static_cast<std::size_t>(msg.size) < headerOffset + sizeof(protocol::PlusTelemetryPingPacket)) {
            return;
        }
        protocol::PlusTelemetryPingPacket ping{};
        ping.clientTimestampUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
        std::memcpy(msg.bytes.data() + headerOffset, &ping, sizeof(ping));
    }

    concurrency::MpmcQueue<OutgoingControlMessage> queue_;
    std::atomic<bool>                              pingPending_{false};
    Clock::time_point                              lastDrainTime_{};
    std::vector<OutgoingControlMessage>            rawScratch_{};
    std::vector<std::uint8_t>                      keepScratch_{};
    std::array<std::vector<std::size_t>, 256>      activePressIndices_{};
};

} // namespace svplus::client
