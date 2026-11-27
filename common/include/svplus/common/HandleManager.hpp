/*
 * SampVoice Plus - Generational Handle Pool (SlotMap)
 *
 * Replaces unsafe raw-pointer-to-uint32 casts with O(1) ABA-safe 32-bit handles.
 * Handles are guaranteed to be strictly positive 31-bit values (0x00010000..0x7FFFFFFF)
 * so they remain non-zero and positive in both C++ (uint32_t) and Pawn (signed 32-bit cell),
 * protecting against stale-handle dereferences and ABA bugs across script unloads.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace svplus::memory {

inline constexpr uint32_t kInvalidHandle = 0u;

/**
 * Thread-safe O(1) generational handle pool storing shared_ptr<T> instances.
 * Lookups acquire a shared lock and validate the 15-bit generation tag to prevent ABA bugs.
 */
template <typename T>
class HandlePool {
public:
    static constexpr uint32_t kIndexMask      = 0x0000FFFFu;
    static constexpr uint32_t kGenShift       = 16u;
    static constexpr uint16_t kMaxGeneration  = 0x7FFFu; // Keeps sign bit 0 for Pawn compatibility
    static constexpr std::size_t kMaxCapacity = 65536u;

    HandlePool() = default;
    HandlePool(const HandlePool&) = delete;
    HandlePool& operator=(const HandlePool&) = delete;

    /**
     * Inserts an object into the pool and returns its unique 32-bit handle,
     * or kInvalidHandle if the pool has reached maximum capacity (65,536 live objects).
     */
    uint32_t Insert(std::shared_ptr<T> object) {
        if (!object) {
            return kInvalidHandle;
        }

        std::unique_lock lock(mutex_);

        uint16_t index = 0;
        if (!freeIndices_.empty()) {
            index = freeIndices_.back();
            freeIndices_.pop_back();
        } else {
            if (slots_.size() >= kMaxCapacity) {
                return kInvalidHandle;
            }
            index = static_cast<uint16_t>(slots_.size());
            slots_.emplace_back();
        }

        Slot& slot = slots_[index];
        slot.object = std::move(object);
        slot.active = true;
        if (slot.generation == 0) {
            slot.generation = 1;
        }

        return MakeHandle(index, slot.generation);
    }

    /**
     * Constructs an object in-place and inserts it into the pool.
     */
    template <typename... Args>
    std::pair<uint32_t, std::shared_ptr<T>> Emplace(Args&&... args) {
        auto obj = std::make_shared<T>(std::forward<Args>(args)...);
        const uint32_t handle = Insert(obj);
        if (handle == kInvalidHandle) {
            return { kInvalidHandle, nullptr };
        }
        return { handle, std::move(obj) };
    }

    /**
     * Retrieves a shared_ptr to the object for the given handle in O(1) time.
     * Returns nullptr if the handle is invalid, stale (generation mismatch), or removed.
     */
    std::shared_ptr<T> Get(uint32_t handle) const {
        if (handle == kInvalidHandle) {
            return nullptr;
        }

        const uint16_t index = ExtractIndex(handle);
        const uint16_t gen   = ExtractGeneration(handle);

        std::shared_lock lock(mutex_);
        if (index >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[index];
        if (!slot.active || slot.generation != gen) {
            return nullptr;
        }

        return slot.object;
    }

    /**
     * Removes the object associated with the handle and increments the slot generation.
     * Returns the removed object, or nullptr if the handle was invalid or stale.
     */
    std::shared_ptr<T> Remove(uint32_t handle) {
        if (handle == kInvalidHandle) {
            return nullptr;
        }

        const uint16_t index = ExtractIndex(handle);
        const uint16_t gen   = ExtractGeneration(handle);

        std::unique_lock lock(mutex_);
        if (index >= slots_.size()) {
            return nullptr;
        }

        Slot& slot = slots_[index];
        if (!slot.active || slot.generation != gen) {
            return nullptr;
        }

        auto removed = std::move(slot.object);
        slot.active = false;
        slot.generation = NextGeneration(slot.generation);
        freeIndices_.push_back(index);
        return removed;
    }

    /**
     * Removes all entries and resets free lists while preserving generation counters.
     */
    void Clear() {
        std::unique_lock lock(mutex_);
        freeIndices_.clear();
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            Slot& slot = slots_[i];
            if (slot.active) {
                slot.object.reset();
                slot.active = false;
                slot.generation = NextGeneration(slot.generation);
            }
            freeIndices_.push_back(static_cast<uint16_t>(i));
        }
    }

    std::size_t ActiveCount() const {
        std::shared_lock lock(mutex_);
        return slots_.size() - freeIndices_.size();
    }

    void Snapshot(std::vector<std::shared_ptr<T>>& out) const {
        out.clear();
        std::shared_lock lock(mutex_);
        out.reserve(slots_.size());
        for (const Slot& slot : slots_) {
            if (slot.active && slot.object) {
                out.push_back(slot.object);
            }
        }
    }

private:
    struct Slot {
        std::shared_ptr<T> object;
        uint16_t generation = 1;
        bool active = false;
    };

    static constexpr uint32_t MakeHandle(uint16_t index, uint16_t gen) noexcept {
        return (static_cast<uint32_t>(gen) << kGenShift) | static_cast<uint32_t>(index);
    }

    static constexpr uint16_t ExtractIndex(uint32_t handle) noexcept {
        return static_cast<uint16_t>(handle & kIndexMask);
    }

    static constexpr uint16_t ExtractGeneration(uint32_t handle) noexcept {
        return static_cast<uint16_t>((handle >> kGenShift) & kMaxGeneration);
    }

    static constexpr uint16_t NextGeneration(uint16_t current) noexcept {
        return (current >= kMaxGeneration) ? 1u : static_cast<uint16_t>(current + 1u);
    }

    mutable std::shared_mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<uint16_t> freeIndices_;
};

} // namespace svplus::memory
