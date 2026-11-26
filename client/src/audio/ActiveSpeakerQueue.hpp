#pragma once

#include "svplus/common/Protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace svplus::client::audio {

struct ActiveStreamTag {
    std::uint32_t color{0};
    std::string   name{};

    bool operator==(const ActiveStreamTag& other) const noexcept = default;
};

struct ActiveSpeakerInfo {
    std::uint16_t                playerId{0};
    std::uint32_t                streamColor{0xFFFFFFFFu};
    protocol::StreamIconId       iconId{protocol::StreamIconId::Speaker};
    bool                         blocked{false};
    std::string                  streamName{};
    std::string                  playerName{};
    std::vector<ActiveStreamTag> streams{};

    bool operator==(const ActiveSpeakerInfo& other) const noexcept = default;
};

/// Maintains stable FIFO ordering for currently speaking remote players.
/// - Preserves visual order of incumbent speakers when others start or stop speaking.
/// - Deduplicates players speaking on multiple simultaneous streams.
/// - Drops speakers cleanly when absent from active set (e.g. stopped or 400 ms channel timeout).
class ActiveSpeakerQueue {
public:
    void clear() noexcept {
        ordered_.clear();
    }

    void reconcile(std::span<const ActiveSpeakerInfo> activeNow) {
        static constexpr std::size_t kInvalidSlot = static_cast<std::size_t>(-1);
        slotById_.fill(kInvalidSlot);
        overflowById_.clear();
        uniqueNewScratch_.clear();

        for (const auto& info : activeNow) {
            const std::size_t idx = uniqueNewScratch_.size();
            if (info.playerId < kSlotTableSize) {
                if (slotById_[info.playerId] == kInvalidSlot) {
                    slotById_[info.playerId] = idx;
                    uniqueNewScratch_.push_back(&info);
                }
            } else if (overflowById_.emplace(info.playerId, idx).second) {
                uniqueNewScratch_.push_back(&info);
            }
        }

        std::size_t nextCount = 0;
        insertedScratch_.assign(uniqueNewScratch_.size(), 0u);

        auto appendNext = [&](const ActiveSpeakerInfo& src) {
            if (nextCount < nextScratch_.size()) {
                nextScratch_[nextCount] = src;
            } else {
                nextScratch_.push_back(src);
            }
            ++nextCount;
        };

        for (const auto& existing : ordered_) {
            std::size_t foundIdx = kInvalidSlot;
            if (existing.playerId < kSlotTableSize) {
                foundIdx = slotById_[existing.playerId];
            } else {
                const auto it = overflowById_.find(existing.playerId);
                if (it != overflowById_.end()) {
                    foundIdx = it->second;
                }
            }
            if (foundIdx != kInvalidSlot) {
                appendNext(*uniqueNewScratch_[foundIdx]);
                insertedScratch_[foundIdx] = 1u;
            }
        }

        for (std::size_t i = 0; i < uniqueNewScratch_.size(); ++i) {
            if (!insertedScratch_[i]) {
                appendNext(*uniqueNewScratch_[i]);
            }
        }

        nextScratch_.resize(nextCount);
        ordered_.swap(nextScratch_);
    }

    [[nodiscard]] const std::vector<ActiveSpeakerInfo>& all() const noexcept {
        return ordered_;
    }

    [[nodiscard]] std::vector<ActiveSpeakerInfo> visible(std::size_t maxCount) const {
        const std::size_t n = (std::min)(maxCount, ordered_.size());
        return std::vector<ActiveSpeakerInfo>(
            ordered_.begin(),
            ordered_.begin() + static_cast<std::ptrdiff_t>(n));
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return ordered_.size();
    }

private:
    static constexpr std::size_t kSlotTableSize = 1004u;

    std::vector<ActiveSpeakerInfo>              ordered_{};
    std::array<std::size_t, kSlotTableSize>     slotById_{};
    std::unordered_map<std::uint16_t, std::size_t> overflowById_{};
    std::vector<const ActiveSpeakerInfo*>       uniqueNewScratch_{};
    std::vector<ActiveSpeakerInfo>              nextScratch_{};
    std::vector<std::uint8_t>                   insertedScratch_{};
};

} // namespace svplus::client::audio
