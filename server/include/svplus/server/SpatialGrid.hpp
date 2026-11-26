/*
 * SampVoice Plus - O(1) Spatial Hashing Grid
 *
 * Replaces the O(N^2) all-pairs player loop in DynamicLocalStream::Tick() with
 * a zero-allocation intrusive spatial hash grid partitioned by 2D world cell (cellX, cellY).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IServerAdapter.hpp"

namespace svplus::server {

struct NearbyPlayer {
    uint16_t playerId{0};
    float distanceSq{0.0f};
};

class SpatialGrid {
public:
    explicit SpatialGrid(float cellSize = 50.0f);

    /**
     * Clears the grid for the current tick while preserving allocated capacity.
     */
    void Clear() noexcept;

    /**
     * Inserts a connected voice-enabled player snapshot into the spatial grid.
     */
    void Insert(const PlayerSpatialSnapshot& snapshot);

    /**
     * Finds up to maxResults nearest players within radius of (x, y, z).
     * Interior and virtualWorld filters can be enabled independently (e.g. vehicles filter world only).
     * If maxResults == 0, returns all matches.
     */
    void QueryRadius(
        float x, float y, float z,
        int32_t interior, int32_t virtualWorld,
        bool filterInterior,
        bool filterWorld,
        float radius,
        uint16_t excludePlayerId,
        std::size_t maxResults,
        std::vector<NearbyPlayer>& outResults
    ) const;

    std::size_t PlayerCount() const noexcept { return entries_.size(); }
    float CellSize() const noexcept { return cellSize_; }

private:
    static constexpr std::size_t kInitialBucketCount = 4096u; // Power of 2; >= 4x kMaxPlayers

    struct Entry {
        PlayerSpatialSnapshot snapshot{};
        int32_t nextInCell{-1};
    };

    struct CellBucket {
        uint64_t packedKey{0};
        uint32_t stamp{0};
        int32_t  head{-1};
    };

    static uint64_t PackCellKey(int32_t cx, int32_t cy) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32)
             | static_cast<uint64_t>(static_cast<uint32_t>(cy));
    }

    static std::size_t HashPackedKey(uint64_t h) noexcept {
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        return static_cast<std::size_t>(h);
    }

    void RehashBuckets(std::size_t newCapacity);
    int32_t FindCellHead(int32_t cx, int32_t cy) const noexcept;
    int32_t ToCellCoord(float v) const noexcept;

    float cellSize_{50.0f};
    float invCellSize_{1.0f / 50.0f};
    std::vector<Entry> entries_;
    std::vector<CellBucket> buckets_;
    std::size_t occupiedCells_{0};
    uint32_t currentStamp_{1};
};

} // namespace svplus::server
