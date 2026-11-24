/*
 * SampVoice Plus - Audio Effect Descriptor
 *
 * Uses safe 32-bit generational handles instead of raw memory pointers.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "ControlBuilder.hpp"
#include "svplus/common/Protocol.hpp"

namespace svplus::server {

class Effect {
public:
    template <typename ParamsT>
    Effect(protocol::EffectType type, int32_t priority, const ParamsT& params)
        : type_(type)
        , priority_(priority)
        , paramBytes_(sizeof(ParamsT)) {
        std::memcpy(paramBytes_.data(), &params, sizeof(ParamsT));
    }

    void SetHandle(uint32_t handle) noexcept { handle_ = handle; }
    uint32_t GetHandle() const noexcept { return handle_; }
    void SetOwnerAmx(void* amx) noexcept { ownerAmx_ = amx; }
    void* GetOwnerAmx() const noexcept { return ownerAmx_; }
    protocol::EffectType GetType() const noexcept { return type_; }
    int32_t GetPriority() const noexcept { return priority_; }

    ControlPacketBuffer BuildCreatePacket(uint32_t streamId) const;
    ControlPacketBuffer BuildDeletePacket(uint32_t streamId) const;

    bool AttachToStream(uint32_t streamId);
    bool DetachFromStream(uint32_t streamId);
    const std::vector<uint32_t>& AttachedStreams() const noexcept { return attachedStreams_; }
    void ClearAttachedStreams() noexcept { attachedStreams_.clear(); }

private:
    uint32_t handle_{0};
    void* ownerAmx_{nullptr};
    protocol::EffectType type_;
    int32_t priority_{0};
    std::vector<uint8_t> paramBytes_;
    std::vector<uint32_t> attachedStreams_;
};

} // namespace svplus::server
