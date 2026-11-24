/*
 * SampVoice Plus - O(1) Spatial Hashing Grid Implementation
 */

#include "svplus/server/SpatialGrid.hpp"

#include <algorithm>
#include <cmath>

namespace svplus::server {

SpatialGrid::SpatialGrid(float cellSize)
    : cellSize_(cellSize > 1.0f ? cellSize : 50.0f)
    , invCellSize_(1.0f / cellSize_) {
    entries_.reserve(kMaxPlayers);
    cellHeads_.reserve(1024);
}

void SpatialGrid::Clear() noexcept {
    entries_.clear();
    cellHeads_.clear();
}

int32_t SpatialGrid::ToCellCoord(float v) const noexcept {
    return static_cast<int32_t>(std::floor(v * invCellSize_));
}

void SpatialGrid::Insert(const PlayerSpatialSnapshot& snapshot) {
    const int32_t entryIdx = static_cast<int32_t>(entries_.size());
    const CellKey key{
        ToCellCoord(snapshot.x),
        ToCellCoord(snapshot.y)
    };

    auto [it, inserted] = cellHeads_.emplace(key, entryIdx);
    int32_t prevHead = -1;
    if (!inserted) {
        prevHead = it->second;
        it->second = entryIdx;
    }

    entries_.push_back(Entry{snapshot, prevHead});
}

void SpatialGrid::QueryRadius(
    float x, float y, float z,
    int32_t interior, int32_t virtualWorld,
    bool filterInterior,
    bool filterWorld,
    float radius,
    uint16_t excludePlayerId,
    std::size_t maxResults,
    std::vector<NearbyPlayer>& outResults
) const {
    outResults.clear();
    if (radius < 0.0f || entries_.empty()) {
        return;
    }

    const float radiusSq = radius * radius;
    const int32_t minCx = ToCellCoord(x - radius);
    const int32_t maxCx = ToCellCoord(x + radius);
    const int32_t minCy = ToCellCoord(y - radius);
    const int32_t maxCy = ToCellCoord(y + radius);

    const uint64_t spanX = static_cast<uint64_t>(static_cast<int64_t>(maxCx) - static_cast<int64_t>(minCx) + 1);
    const uint64_t spanY = static_cast<uint64_t>(static_cast<int64_t>(maxCy) - static_cast<int64_t>(minCy) + 1);
    const uint64_t cellArea = spanX * spanY;

    auto tryAddEntry = [&](const PlayerSpatialSnapshot& s) {
        if (s.playerId == excludePlayerId) {
            return;
        }
        if (filterInterior && s.interior != interior) {
            return;
        }
        if (filterWorld && s.virtualWorld != virtualWorld) {
            return;
        }
        const float dx = s.x - x;
        const float dy = s.y - y;
        const float dz = s.z - z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= radiusSq) {
            outResults.push_back(NearbyPlayer{s.playerId, distSq});
        }
    };

    if (cellArea > entries_.size()) {
        for (const auto& entry : entries_) {
            tryAddEntry(entry.snapshot);
        }
    } else {
        for (int32_t cx = minCx; cx <= maxCx; ++cx) {
            for (int32_t cy = minCy; cy <= maxCy; ++cy) {
                const auto it = cellHeads_.find(CellKey{cx, cy});
                if (it == cellHeads_.end()) {
                    continue;
                }
                int32_t idx = it->second;
                while (idx >= 0) {
                    const Entry& entry = entries_[static_cast<std::size_t>(idx)];
                    tryAddEntry(entry.snapshot);
                    idx = entry.nextInCell;
                }
            }
        }
    }

    if (maxResults > 0 && outResults.size() > maxResults) {
        std::partial_sort(
            outResults.begin(),
            outResults.begin() + static_cast<std::ptrdiff_t>(maxResults),
            outResults.end(),
            [](const NearbyPlayer& a, const NearbyPlayer& b) {
                return a.distanceSq < b.distanceSq;
            }
        );
        outResults.resize(maxResults);
    }
}

} // namespace svplus::server
