/*
 * SampVoice Plus - Zero-Allocation Preallocated Packet Buffer Pool
 *
 * Eliminates heap allocations (malloc/new) from the UDP audio packet hot path.
 * Buffers are returned automatically via a custom deleter into a lock-free MPMC queue.
 */

#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "LockFreeQueue.hpp"
#include "Protocol.hpp"

namespace svplus::memory {

template <std::size_t BufferSize = protocol::kMaxUdpPacketSize>
class PacketPool;

template <std::size_t BufferSize = protocol::kMaxUdpPacketSize>
struct alignas(64) PacketBuffer {
    std::array<uint8_t, BufferSize> data{};
    std::size_t size{0};
    uint32_t poolIndex{0};
    uint32_t sourceIpv4{0};
    uint16_t sourcePort{0};
    PacketPool<BufferSize>* ownerPool{nullptr};
};

template <std::size_t BufferSize = protocol::kMaxUdpPacketSize>
struct PacketBufferDeleter {
    void operator()(PacketBuffer<BufferSize>* buf) const noexcept;
};

template <std::size_t BufferSize = protocol::kMaxUdpPacketSize>
using PooledPacket = std::unique_ptr<PacketBuffer<BufferSize>, PacketBufferDeleter<BufferSize>>;

template <std::size_t BufferSize>
class PacketPool {
public:
    explicit PacketPool(std::size_t capacityPow2 = 4096)
        : storage_(std::bit_ceil(capacityPow2 < 64 ? 64 : capacityPow2))
        , freeIndices_(storage_.size()) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(storage_.size()); ++i) {
            storage_[i].poolIndex = i;
            storage_[i].ownerPool = this;
            freeIndices_.TryPush(i);
        }
    }

    PacketPool(const PacketPool&) = delete;
    PacketPool& operator=(const PacketPool&) = delete;

    PooledPacket<BufferSize> Acquire() noexcept {
        uint32_t idx = 0;
        if (!freeIndices_.TryPop(idx)) {
            return nullptr;
        }
        auto* buf = &storage_[idx];
        buf->size = 0;
        buf->sourceIpv4 = 0;
        buf->sourcePort = 0;
        return PooledPacket<BufferSize>(buf);
    }

    void Release(PacketBuffer<BufferSize>* buf) noexcept {
        if (!buf) {
            return;
        }
        buf->size = 0;
        freeIndices_.TryPush(buf->poolIndex);
    }

    std::size_t Capacity() const noexcept { return storage_.size(); }

private:
    std::vector<PacketBuffer<BufferSize>> storage_;
    concurrency::MpmcQueue<uint32_t> freeIndices_;
};

template <std::size_t BufferSize>
inline void PacketBufferDeleter<BufferSize>::operator()(PacketBuffer<BufferSize>* buf) const noexcept {
    if (buf && buf->ownerPool) {
        buf->ownerPool->Release(buf);
    }
}

} // namespace svplus::memory

#ifdef _MSC_VER
#pragma warning(pop)
#endif
