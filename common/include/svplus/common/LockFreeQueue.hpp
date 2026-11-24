/*
 * SampVoice Plus - Lock-Free Bounded Ring Buffers (SPSC & MPMC)
 *
 * Eliminates mutex contention and artificial sleep polling from the real-time
 * audio packet pipeline. All indices are cache-line aligned (64 bytes) to avoid
 * false sharing across CPU cores.
 */

#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace svplus::concurrency {

inline constexpr std::size_t kCacheLineSize = 64;

/**
 * Lock-free Single-Producer Single-Consumer bounded ring buffer.
 * Capacity must be a power of two >= 2.
 */
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacityPow2)
        : capacity_(std::bit_ceil(capacityPow2 < 2 ? 2 : capacityPow2))
        , mask_(capacity_ - 1)
        , buffer_(static_cast<Slot*>(::operator new[](sizeof(Slot) * capacity_, std::align_val_t{alignof(Slot)}))) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            new (&buffer_[i]) Slot();
        }
    }

    ~SpscQueue() {
        T dummy;
        while (TryPop(dummy)) {}
        for (std::size_t i = 0; i < capacity_; ++i) {
            buffer_[i].~Slot();
        }
        ::operator delete[](buffer_, std::align_val_t{alignof(Slot)});
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    template <typename... Args>
    bool TryEmplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tailCache_) {
            tailCache_ = tail_.load(std::memory_order_acquire);
            if (next == tailCache_) {
                return false;
            }
        }

        new (&buffer_[head].storage) T(std::forward<Args>(args)...);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryPush(const T& value) { return TryEmplace(value); }
    bool TryPush(T&& value)      { return TryEmplace(std::move(value)); }

    bool TryPop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == headCache_) {
            headCache_ = head_.load(std::memory_order_acquire);
            if (tail == headCache_) {
                return false;
            }
        }

        T* ptr = reinterpret_cast<T*>(&buffer_[tail].storage);
        out = std::move(*ptr);
        ptr->~T();
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    bool IsEmpty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    std::size_t Capacity() const noexcept { return capacity_ - 1; }

private:
    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
    };

    const std::size_t capacity_;
    const std::size_t mask_;
    Slot* const buffer_;

    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    std::size_t tailCache_{0};

    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
    std::size_t headCache_{0};
};

/**
 * Lock-free Multi-Producer Multi-Consumer bounded ring buffer (Vyukov sequence algorithm).
 * Capacity must be a power of two >= 2.
 */
template <typename T>
class MpmcQueue {
public:
    explicit MpmcQueue(std::size_t capacityPow2)
        : capacity_(std::bit_ceil(capacityPow2 < 2 ? 2 : capacityPow2))
        , mask_(capacity_ - 1)
        , cells_(static_cast<Cell*>(::operator new[](sizeof(Cell) * capacity_, std::align_val_t{alignof(Cell)}))) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            new (&cells_[i]) Cell();
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpmcQueue() {
        T dummy;
        while (TryPop(dummy)) {}
        for (std::size_t i = 0; i < capacity_; ++i) {
            cells_[i].~Cell();
        }
        ::operator delete[](cells_, std::align_val_t{alignof(Cell)});
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    template <typename... Args>
    bool TryEmplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        Cell* cell = nullptr;
        std::size_t pos = enqueuePos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &cells_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);

            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // Full
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }

        new (&cell->storage) T(std::forward<Args>(args)...);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool TryPush(const T& value) { return TryEmplace(value); }
    bool TryPush(T&& value)      { return TryEmplace(std::move(value)); }

    bool TryPop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        Cell* cell = nullptr;
        std::size_t pos = dequeuePos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &cells_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1);

            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // Empty
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }

        T* ptr = reinterpret_cast<T*>(&cell->storage);
        out = std::move(*ptr);
        ptr->~T();
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }

    bool IsEmpty() const noexcept {
        const std::size_t pos = dequeuePos_.load(std::memory_order_acquire);
        const std::size_t seq = cells_[pos & mask_].sequence.load(std::memory_order_acquire);
        return static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1) < 0;
    }

    std::size_t Capacity() const noexcept { return capacity_; }

private:
    struct alignas(kCacheLineSize) Cell {
        std::atomic<std::size_t> sequence{0};
        alignas(T) std::byte storage[sizeof(T)];
    };

    const std::size_t capacity_;
    const std::size_t mask_;
    Cell* const cells_;

    alignas(kCacheLineSize) std::atomic<std::size_t> enqueuePos_{0};
    alignas(kCacheLineSize) std::atomic<std::size_t> dequeuePos_{0};
};

} // namespace svplus::concurrency

#ifdef _MSC_VER
#pragma warning(pop)
#endif
