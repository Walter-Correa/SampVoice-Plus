/*
 * SampVoice Plus - Type-Safe Control Packet Builder
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "svplus/common/Protocol.hpp"

namespace svplus::server {

class ControlPacketBuffer {
public:
    ControlPacketBuffer() = default;

    template <typename PayloadT>
    static ControlPacketBuffer Create(protocol::ControlPacketType type, const PayloadT& payload) {
        ControlPacketBuffer buf;
        buf.bytes_.resize(sizeof(protocol::ControlPacketHeader) + sizeof(PayloadT));
        auto* hdr = reinterpret_cast<protocol::ControlPacketHeader*>(buf.bytes_.data());
        hdr->packet = static_cast<uint16_t>(type);
        hdr->length = static_cast<uint16_t>(sizeof(PayloadT));
        std::memcpy(buf.bytes_.data() + sizeof(protocol::ControlPacketHeader), &payload, sizeof(PayloadT));
        return buf;
    }

    template <typename HeaderPayloadT>
    static ControlPacketBuffer CreateWithTrailingString(
        protocol::ControlPacketType type,
        const HeaderPayloadT& payload,
        const std::string& text
    ) {
        const std::size_t maxStrBytes = (protocol::kMaxControlPayloadSize > sizeof(HeaderPayloadT))
            ? (std::min<std::size_t>)(128u, protocol::kMaxControlPayloadSize - sizeof(HeaderPayloadT) - 1u)
            : 0u;
        const std::size_t clippedLen = (std::min)(text.size(), maxStrBytes);
        const std::size_t strBytes = clippedLen + 1;
        const std::size_t payloadSize = sizeof(HeaderPayloadT) + strBytes;
        ControlPacketBuffer buf;
        buf.bytes_.resize(sizeof(protocol::ControlPacketHeader) + payloadSize);

        auto* hdr = reinterpret_cast<protocol::ControlPacketHeader*>(buf.bytes_.data());
        hdr->packet = static_cast<uint16_t>(type);
        hdr->length = static_cast<uint16_t>(payloadSize);

        uint8_t* dst = buf.bytes_.data() + sizeof(protocol::ControlPacketHeader);
        std::memcpy(dst, &payload, sizeof(HeaderPayloadT));
        if (clippedLen > 0) {
            std::memcpy(dst + sizeof(HeaderPayloadT), text.data(), clippedLen);
        }
        dst[sizeof(HeaderPayloadT) + clippedLen] = '\0';
        return buf;
    }

    template <typename HeaderPayloadT>
    static ControlPacketBuffer CreateWithTrailingBytes(
        protocol::ControlPacketType type,
        const HeaderPayloadT& payload,
        const void* extra,
        std::size_t extraSize
    ) {
        const std::size_t maxExtra = (protocol::kMaxControlPayloadSize > sizeof(HeaderPayloadT))
            ? (protocol::kMaxControlPayloadSize - sizeof(HeaderPayloadT))
            : 0u;
        const std::size_t clampedExtra = (std::min)(extraSize, maxExtra);
        ControlPacketBuffer buf;
        const std::size_t payloadSize = sizeof(HeaderPayloadT) + clampedExtra;
        buf.bytes_.resize(sizeof(protocol::ControlPacketHeader) + payloadSize);

        auto* hdr = reinterpret_cast<protocol::ControlPacketHeader*>(buf.bytes_.data());
        hdr->packet = static_cast<uint16_t>(type);
        hdr->length = static_cast<uint16_t>(payloadSize);

        uint8_t* dst = buf.bytes_.data() + sizeof(protocol::ControlPacketHeader);
        std::memcpy(dst, &payload, sizeof(HeaderPayloadT));
        if (extra && clampedExtra > 0) {
            std::memcpy(dst + sizeof(HeaderPayloadT), extra, clampedExtra);
        }
        return buf;
    }

    static ControlPacketBuffer CreateEmpty(protocol::ControlPacketType type) {
        ControlPacketBuffer buf;
        buf.bytes_.resize(sizeof(protocol::ControlPacketHeader));
        auto* hdr = reinterpret_cast<protocol::ControlPacketHeader*>(buf.bytes_.data());
        hdr->packet = static_cast<uint16_t>(type);
        hdr->length = 0;
        return buf;
    }

    static ControlPacketBuffer CreateRaw(
        protocol::ControlPacketType type,
        const void* payload,
        std::size_t payloadSize
    ) {
        const std::size_t clampedSize = (std::min)(payloadSize, protocol::kMaxControlPayloadSize);
        ControlPacketBuffer buf;
        buf.bytes_.resize(sizeof(protocol::ControlPacketHeader) + clampedSize);
        auto* hdr = reinterpret_cast<protocol::ControlPacketHeader*>(buf.bytes_.data());
        hdr->packet = static_cast<uint16_t>(type);
        hdr->length = static_cast<uint16_t>(clampedSize);
        if (payload && clampedSize > 0) {
            std::memcpy(buf.bytes_.data() + sizeof(protocol::ControlPacketHeader), payload, clampedSize);
        }
        return buf;
    }

    const uint8_t* Data() const noexcept { return bytes_.data(); }
    std::size_t Size() const noexcept { return bytes_.size(); }
    bool Empty() const noexcept { return bytes_.empty(); }

    template <typename T>
    T* MutablePayload() noexcept {
        if (bytes_.size() < sizeof(protocol::ControlPacketHeader) + sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<T*>(bytes_.data() + sizeof(protocol::ControlPacketHeader));
    }

    template <typename T>
    const T* Payload() const noexcept {
        if (bytes_.size() < sizeof(protocol::ControlPacketHeader) + sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(bytes_.data() + sizeof(protocol::ControlPacketHeader));
    }

private:
    std::vector<uint8_t> bytes_;
};

} // namespace svplus::server
