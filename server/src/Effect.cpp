/*
 * SampVoice Plus - Audio Effect Implementation
 */

#include "svplus/server/Effect.hpp"

#include <algorithm>

namespace svplus::server {

ControlPacketBuffer Effect::BuildCreatePacket(uint32_t streamId) const {
    protocol::CreateEffectPacket pkt{};
    pkt.stream = streamId;
    pkt.effect = handle_;
    pkt.number = static_cast<uint32_t>(type_);
    pkt.priority = priority_;
    return ControlPacketBuffer::CreateWithTrailingBytes(
        protocol::ControlPacketType::CreateEffect,
        pkt,
        paramBytes_.data(),
        paramBytes_.size()
    );
}

ControlPacketBuffer Effect::BuildDeletePacket(uint32_t streamId) const {
    protocol::DeleteEffectPacket pkt{};
    pkt.stream = streamId;
    pkt.effect = handle_;
    return ControlPacketBuffer::Create(protocol::ControlPacketType::DeleteEffect, pkt);
}

bool Effect::AttachToStream(uint32_t streamId) {
    if (std::find(attachedStreams_.begin(), attachedStreams_.end(), streamId) != attachedStreams_.end()) {
        return false;
    }
    attachedStreams_.push_back(streamId);
    return true;
}

bool Effect::DetachFromStream(uint32_t streamId) {
    const auto it = std::find(attachedStreams_.begin(), attachedStreams_.end(), streamId);
    if (it == attachedStreams_.end()) {
        return false;
    }
    *it = attachedStreams_.back();
    attachedStreams_.pop_back();
    return true;
}

} // namespace svplus::server
