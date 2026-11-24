#pragma once

#include "svplus/common/Protocol.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace svplus::client::audio {

struct ActiveStreamTag {
    std::uint32_t color{0};
    std::string   name{};
};

struct ActiveSpeakerInfo {
    std::uint16_t                playerId{0};
    std::uint32_t                streamColor{0xFFFFFFFFu};
    protocol::StreamIconId       iconId{protocol::StreamIconId::Speaker};
    bool                         blocked{false};
    std::string                  streamName{};
    std::string                  playerName{};
    std::vector<ActiveStreamTag> streams{};
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
        std::unordered_map<std::uint16_t, std::size_t> byId;
        byId.reserve(activeNow.size());
        std::vector<const ActiveSpeakerInfo*> uniqueNew;
        uniqueNew.reserve(activeNow.size());

        for (const auto& info : activeNow) {
            if (byId.emplace(info.playerId, uniqueNew.size()).second) {
                uniqueNew.push_back(&info);
            }
        }

        std::vector<ActiveSpeakerInfo> next;
        next.reserve(uniqueNew.size());
        std::vector<bool> inserted(uniqueNew.size(), false);

        for (const auto& existing : ordered_) {
            const auto it = byId.find(existing.playerId);
            if (it != byId.end()) {
                next.push_back(*uniqueNew[it->second]);
                inserted[it->second] = true;
            }
        }

        for (std::size_t i = 0; i < uniqueNew.size(); ++i) {
            if (!inserted[i]) {
                next.push_back(*uniqueNew[i]);
            }
        }

        ordered_ = std::move(next);
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
    std::vector<ActiveSpeakerInfo> ordered_{};
};

} // namespace svplus::client::audio
