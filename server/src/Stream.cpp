/*
 * SampVoice Plus - Unified Voice Stream & Parameter Engine Implementation
 */

#include "svplus/server/Stream.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace svplus::server {
namespace {

// Scale distance by 2x for legacy v11 listeners so BASS 3D min=0.1*max matches Plus 0.2*real_max with slower rolloff.
constexpr float kLegacyDistanceScale = 2.0f;

bool IsPlusPlayer(uint16_t playerId, const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags) noexcept {
    return plusFlags != nullptr && playerId < kMaxPlayers && (*plusFlags)[playerId].load(std::memory_order_acquire);
}

ControlPacketBuffer BuildLocalCreatePacket(
    EntityKind kind, float distance, uint32_t targetId,
    protocol::Vec3f position, uint32_t color, const std::string& name
) {
    if (kind == EntityKind::Point) {
        protocol::CreateLPStreamPacket pkt{};
        pkt.stream = 0;
        pkt.distance = distance;
        pkt.position = position;
        pkt.color = color;
        return ControlPacketBuffer::CreateWithTrailingString(
            protocol::ControlPacketType::CreateLPStream, pkt, name
        );
    }

    protocol::ControlPacketType pktType = protocol::ControlPacketType::CreateLStreamAtPlayer;
    if (kind == EntityKind::Vehicle) {
        pktType = protocol::ControlPacketType::CreateLStreamAtVehicle;
    } else if (kind == EntityKind::Object) {
        pktType = protocol::ControlPacketType::CreateLStreamAtObject;
    }

    protocol::CreateLStreamAtPacket pkt{};
    pkt.stream = 0;
    pkt.distance = distance;
    pkt.target = targetId;
    pkt.color = color;
    return ControlPacketBuffer::CreateWithTrailingString(pktType, pkt, name);
}

void StampCreatePacket(ControlPacketBuffer& buf, Stream::Mode mode, EntityKind kind, uint32_t handle) noexcept {
    if (buf.Size() == 0) {
        return;
    }
    if (mode == Stream::Mode::Global) {
        if (auto* p = buf.MutablePayload<protocol::CreateGStreamPacket>()) {
            p->stream = handle;
        }
    } else if (kind == EntityKind::Point) {
        if (auto* p = buf.MutablePayload<protocol::CreateLPStreamPacket>()) {
            p->stream = handle;
        }
    } else {
        if (auto* p = buf.MutablePayload<protocol::CreateLStreamAtPacket>()) {
            p->stream = handle;
        }
    }
}

void UpdateCreatePacketDistance(ControlPacketBuffer& buf, EntityKind kind, float distance) noexcept {
    if (buf.Size() == 0) {
        return;
    }
    if (kind == EntityKind::Point) {
        if (auto* p = buf.MutablePayload<protocol::CreateLPStreamPacket>()) {
            p->distance = distance;
        }
    } else {
        if (auto* p = buf.MutablePayload<protocol::CreateLStreamAtPacket>()) {
            p->distance = distance;
        }
    }
}

void UpdateCreatePacketPosition(ControlPacketBuffer& buf, protocol::Vec3f position) noexcept {
    if (buf.Size() == 0) {
        return;
    }
    if (auto* p = buf.MutablePayload<protocol::CreateLPStreamPacket>()) {
        p->position = position;
    }
}

void UpdateDistancePacketValue(ControlPacketBuffer& buf, float distance) noexcept {
    if (auto* p = buf.MutablePayload<protocol::UpdateLStreamDistancePacket>()) {
        p->distance = distance;
    }
}

} // namespace

// -----------------------------------------------------------------------------
// StreamParameter
// -----------------------------------------------------------------------------

bool StreamParameter::IsValidParameter(uint32_t parameterId) noexcept {
    using PT = protocol::ParameterType;
    switch (static_cast<PT>(parameterId)) {
        case PT::Frequency:
        case PT::Volume:
        case PT::Panning:
        case PT::EaxMix:
        case PT::Src:
            return true;
    }
    return false;
}

float StreamParameter::DefaultValue(uint32_t parameterId) noexcept {
    using PT = protocol::ParameterType;
    switch (static_cast<PT>(parameterId)) {
        case PT::Frequency: return 0.0f;
        case PT::Volume:    return 1.0f;
        case PT::Panning:   return 0.0f;
        case PT::EaxMix:    return -1.0f;
        case PT::Src:       return 1.0f;
    }
    return -1.0f;
}

namespace {

float ResolveWireFrequency(uint32_t parameterId, float value) noexcept {
    if (parameterId == static_cast<uint32_t>(protocol::ParameterType::Frequency) && value == 0.0f) {
        return static_cast<float>(protocol::kFrequency);
    }
    return value;
}

} // namespace

StreamParameter::StreamParameter(uint32_t streamId, uint32_t parameterId, float initValue)
    : streamId_(streamId)
    , parameterId_(parameterId)
    , value_(initValue) {}

float StreamParameter::Get() const noexcept {
    if (sliding_) {
        const auto now = Clock::now();
        if (now < endTime_) {
            const auto remainingMs = std::chrono::duration<float, std::milli>(endTime_ - now).count();
            return endValue_ - remainingMs * ratio_;
        }
    }
    return value_;
}

void StreamParameter::Set(float value) noexcept {
    value_ = value;
    sliding_ = false;
}

void StreamParameter::SlideFromTo(float startValue, float endValue, uint32_t timeMs) noexcept {
    if (timeMs == 0) {
        Set(endValue);
        return;
    }
    const float wireStart = ResolveWireFrequency(parameterId_, startValue);
    const float wireEnd = ResolveWireFrequency(parameterId_, endValue);
    ratio_ = (wireEnd - wireStart) / static_cast<float>(timeMs);
    endTime_ = Clock::now() + std::chrono::milliseconds(timeMs);
    endValue_ = wireEnd;
    value_ = endValue;
    sliding_ = true;
}

void StreamParameter::SlideTo(float endValue, uint32_t timeMs) noexcept {
    SlideFromTo(Get(), endValue, timeMs);
}

void StreamParameter::Slide(float deltaValue, uint32_t timeMs) noexcept {
    const float current = ResolveWireFrequency(parameterId_, Get());
    SlideFromTo(current, current + deltaValue, timeMs);
}

ControlPacketBuffer StreamParameter::BuildApplyPacket(float volumeScale) const {
    return BuildApplyPacketAt(Clock::now(), volumeScale);
}

ControlPacketBuffer StreamParameter::BuildApplyPacketAt(Clock::time_point now, float volumeScale) const {
    const bool applyScale = (volumeScale != 1.0f);
    if (sliding_) {
        if (now < endTime_) {
            const auto remainingMs = static_cast<uint32_t>(
                std::chrono::ceil<std::chrono::milliseconds>(endTime_ - now).count()
            );
            const float currentStart = endValue_ - static_cast<float>(remainingMs) * ratio_;

            protocol::SlideStreamParameterPacket pkt{};
            pkt.stream = streamId_;
            pkt.parameter = parameterId_;
            pkt.startValue = applyScale ? (currentStart * volumeScale) : currentStart;
            pkt.endValue = applyScale ? (endValue_ * volumeScale) : endValue_;
            pkt.timeMs = remainingMs > 0 ? remainingMs : 1u;
            return ControlPacketBuffer::Create(protocol::ControlPacketType::SlideStreamParameter, pkt);
        }
    }

    const float effectiveValue = ResolveWireFrequency(parameterId_, value_);
    protocol::SetStreamParameterPacket pkt{};
    pkt.stream = streamId_;
    pkt.parameter = parameterId_;
    pkt.value = applyScale ? (effectiveValue * volumeScale) : effectiveValue;
    return ControlPacketBuffer::Create(protocol::ControlPacketType::SetStreamParameter, pkt);
}

// -----------------------------------------------------------------------------
// Stream Factories
// -----------------------------------------------------------------------------

Stream::Stream() {
    listenerVolumeFactors_.fill(1.0f);
}

std::shared_ptr<Stream> Stream::CreateGlobal(uint32_t color, const std::string& name) {
    auto s = std::shared_ptr<Stream>(new Stream());
    s->mode_ = Mode::Global;
    s->rawColor_ = color;
    s->color_ = color;
    s->name_ = name.substr(0, 128u);
    s->RebuildCreatePackets();
    return s;
}

std::shared_ptr<Stream> Stream::CreateStaticLocal(
    EntityKind kind, float distance, uint32_t targetId,
    protocol::Vec3f position, uint32_t color, const std::string& name
) {
    if (!std::isfinite(distance) || distance < 0.0f) {
        return nullptr;
    }
    if (kind == EntityKind::Point &&
        (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))) {
        return nullptr;
    }
    auto s = std::shared_ptr<Stream>(new Stream());
    s->mode_ = Mode::StaticLocal;
    s->entityKind_ = kind;
    s->distance_ = distance;
    s->targetId_ = targetId;
    s->position_ = position;
    s->rawColor_ = color;
    s->color_ = color;
    s->name_ = name.substr(0, 128u);
    s->RebuildCreatePackets();
    return s;
}

std::shared_ptr<Stream> Stream::CreateDynamicLocal(
    EntityKind kind, float distance, uint32_t maxPlayers, uint32_t targetId,
    protocol::Vec3f position, uint32_t color, const std::string& name,
    int32_t worldId, int32_t interiorId
) {
    if ((kind == EntityKind::Point || kind == EntityKind::Object) &&
        (worldId < -1 || interiorId < -1)) {
        return nullptr;
    }
    auto s = CreateStaticLocal(kind, distance, targetId, position, color, name);
    if (!s) {
        return nullptr;
    }
    s->mode_ = Mode::DynamicLocal;
    s->maxPlayers_ = maxPlayers;
    if (kind == EntityKind::Point || kind == EntityKind::Object) {
        s->worldId_ = worldId;
        s->interiorId_ = interiorId;
    }
    return s;
}

void Stream::RebuildCreatePackets() {
    if (mode_ == Mode::Global) {
        protocol::CreateGStreamPacket pkt{};
        pkt.stream = handle_;
        pkt.color = color_;
        packetCreate_ = ControlPacketBuffer::CreateWithTrailingString(
            protocol::ControlPacketType::CreateGStream, pkt, name_
        );
        packetCreateLegacy_ = {};
        return;
    }
    packetCreate_ = BuildLocalCreatePacket(entityKind_, distance_, targetId_, position_, color_, name_);
    packetCreateLegacy_ = BuildLocalCreatePacket(
        entityKind_, distance_ * kLegacyDistanceScale, targetId_, position_, color_, name_
    );
    if (handle_ != 0u) {
        StampCreatePacket(packetCreate_, mode_, entityKind_, handle_);
        StampCreatePacket(packetCreateLegacy_, mode_, entityKind_, handle_);
    }
}

int32_t Stream::GetStreamType() const noexcept {
    if (mode_ == Mode::Global) {
        return 0;
    }
    const int32_t base = (mode_ == Mode::DynamicLocal) ? 5 : 1;
    switch (entityKind_) {
        case EntityKind::Point:   return base + 0;
        case EntityKind::Vehicle: return base + 1;
        case EntityKind::Player:  return base + 2;
        case EntityKind::Object:  return base + 3;
    }
    return -1;
}

bool Stream::SetWorldAndInterior(int32_t worldId, int32_t interiorId) noexcept {
    if (!IsDynamic() || (entityKind_ != EntityKind::Point && entityKind_ != EntityKind::Object)) {
        return false;
    }
    if (worldId < -1 || interiorId < -1) {
        return false;
    }
    worldId_ = worldId;
    interiorId_ = interiorId;
    return true;
}

void Stream::SetHandle(uint32_t handle) {
    handle_ = handle;

    // Stamp stream handle into prebuilt control packets
    StampCreatePacket(packetCreate_, mode_, entityKind_, handle_);
    StampCreatePacket(packetCreateLegacy_, mode_, entityKind_, handle_);

    protocol::DeleteStreamPacket delPkt{handle_};
    packetDelete_ = ControlPacketBuffer::Create(protocol::ControlPacketType::DeleteStream, delPkt);

    if (IsLocal()) {
        protocol::UpdateLStreamDistancePacket distPkt{handle_, distance_};
        packetUpdateDistance_ = ControlPacketBuffer::Create(
            protocol::ControlPacketType::UpdateLStreamDistance, distPkt
        );
        protocol::UpdateLStreamDistancePacket legacyDistPkt{handle_, distance_ * kLegacyDistanceScale};
        packetUpdateDistanceLegacy_ = ControlPacketBuffer::Create(
            protocol::ControlPacketType::UpdateLStreamDistance, legacyDistPkt
        );
    }

    if (IsPointStream()) {
        protocol::UpdateLPStreamPositionPacket posPkt{handle_, position_};
        packetUpdatePosition_ = ControlPacketBuffer::Create(
            protocol::ControlPacketType::UpdateLPStreamPosition, posPkt
        );
    }
}

// -----------------------------------------------------------------------------
// Listener & Speaker Management
// -----------------------------------------------------------------------------

void Stream::PublishListenerSnapshotLocked() {
    auto next = std::make_shared<const std::vector<uint16_t>>(listenerList_);
    std::atomic_store_explicit(&listenerSnapshot_, std::move(next), std::memory_order_release);
}

std::shared_ptr<const std::vector<uint16_t>> Stream::GetListenersSnapshot() const noexcept {
    return std::atomic_load_explicit(&listenerSnapshot_, std::memory_order_acquire);
}

void Stream::RecordVoiceForwarded(int64_t steadyTimeNs) noexcept {
    int64_t prev = lastVoiceForwardNs_.load(std::memory_order_relaxed);
    while (steadyTimeNs > prev &&
           !lastVoiceForwardNs_.compare_exchange_weak(
               prev, steadyTimeNs, std::memory_order_release, std::memory_order_relaxed)) {
    }
}

void Stream::SetListenerVolumeFactorLocked(uint16_t playerId, float factor) noexcept {
    if (playerId >= kMaxPlayers) {
        return;
    }
    const float prev = listenerVolumeFactors_[playerId];
    if (prev != 1.0f && nonDefaultVolumeCount_ > 0) {
        --nonDefaultVolumeCount_;
    }
    listenerVolumeFactors_[playerId] = factor;
    if (factor != 1.0f) {
        ++nonDefaultVolumeCount_;
    }
}

void Stream::ClearListenerVolumeFactor(uint16_t playerId) noexcept {
    SetListenerVolumeFactorLocked(playerId, 1.0f);
}

float Stream::GetListenerVolumeFactor(uint16_t playerId) const noexcept {
    if (playerId >= kMaxPlayers) {
        return -1.0f;
    }
    return listenerVolumeFactors_[playerId];
}

std::size_t Stream::GetListenerCount() const noexcept {
    const auto snap = GetListenersSnapshot();
    return snap ? snap->size() : 0u;
}

bool Stream::SetListenerVolumeFactor(uint16_t playerId, float volume, IServerAdapter& adapter) {
    if (playerId >= kMaxPlayers || std::isnan(volume) || volume < kMinListenerVolume) {
        return false;
    }
    if (!IsDynamic() && !HasListener(playerId)) {
        return false;
    }
    const float clamped = (std::min)(volume, kMaxListenerVolume);
    SetListenerVolumeFactorLocked(playerId, clamped);

    if (HasListener(playerId) && adapter.IsPlayerConnected(playerId)) {
        const auto volKey = static_cast<uint32_t>(protocol::ParameterType::Volume);
        const auto it = parameters_.find(volKey);
        if (it != parameters_.end()) {
            const auto pkt = it->second.BuildApplyPacket(clamped);
            adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
        } else {
            protocol::SetStreamParameterPacket volPkt{};
            volPkt.stream = handle_;
            volPkt.parameter = volKey;
            volPkt.value = clamped;
            const auto pkt = ControlPacketBuffer::Create(
                protocol::ControlPacketType::SetStreamParameter, volPkt);
            adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
        }
    }
    return true;
}

void Stream::SendAttachStatePackets(uint16_t playerId, bool isPlus, IServerAdapter& adapter) const {
    const float listenerFactor = (playerId < kMaxPlayers) ? listenerVolumeFactors_[playerId] : 1.0f;
    bool sentVolume = false;
    const auto volParamId = static_cast<uint32_t>(protocol::ParameterType::Volume);
    for (const auto& [paramId, param] : parameters_) {
        const float scale = (paramId == volParamId) ? listenerFactor : 1.0f;
        const auto pkt = param.BuildApplyPacket(scale);
        adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
        if (paramId == volParamId) {
            sentVolume = true;
        }
    }
    if (!sentVolume && listenerFactor != 1.0f) {
        protocol::SetStreamParameterPacket volPkt{};
        volPkt.stream = handle_;
        volPkt.parameter = volParamId;
        volPkt.value = listenerFactor;
        const auto pkt = ControlPacketBuffer::Create(
            protocol::ControlPacketType::SetStreamParameter, volPkt);
        adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
    }
    for (const auto& [effectId, createPkt] : effects_) {
        (void)effectId;
        adapter.SendControlPacket(playerId, createPkt.Data(), createPkt.Size());
    }
    if (iconId_ != protocol::StreamIconId::Speaker && isPlus) {
        const auto iconPkt = BuildIconPacket();
        adapter.SendControlPacket(playerId, iconPkt.Data(), iconPkt.Size());
    }
}

void Stream::SendReattachSyncPackets(uint16_t playerId, bool isPlus, IServerAdapter& adapter) const {
    if (IsLocal()) {
        const ControlPacketBuffer& distPkt =
            isPlus ? packetUpdateDistance_ : packetUpdateDistanceLegacy_;
        adapter.SendControlPacket(playerId, distPkt.Data(), distPkt.Size());
    }
    if (IsPointStream()) {
        adapter.SendControlPacket(playerId, packetUpdatePosition_.Data(), packetUpdatePosition_.Size());
    }
    const float listenerFactor = (playerId < kMaxPlayers) ? listenerVolumeFactors_[playerId] : 1.0f;
    bool sentVolume = false;
    const auto volParamId = static_cast<uint32_t>(protocol::ParameterType::Volume);
    for (const auto& [paramId, param] : parameters_) {
        const float scale = (paramId == volParamId) ? listenerFactor : 1.0f;
        const auto pkt = param.BuildApplyPacket(scale);
        adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
        if (paramId == volParamId) {
            sentVolume = true;
        }
    }
    if (!sentVolume && listenerFactor != 1.0f) {
        protocol::SetStreamParameterPacket volPkt{};
        volPkt.stream = handle_;
        volPkt.parameter = volParamId;
        volPkt.value = listenerFactor;
        const auto pkt = ControlPacketBuffer::Create(
            protocol::ControlPacketType::SetStreamParameter, volPkt);
        adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
    }
}

bool Stream::AttachListenerInternal(
    uint16_t playerId,
    IServerAdapter& adapter,
    bool sendCreatePacket,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    if (listenerMask_[playerId].exchange(true, std::memory_order_acq_rel)) {
        return false;
    }

    {
        std::unique_lock lock(listenerMutex_);
        listenerList_.push_back(playerId);
        PublishListenerSnapshotLocked();
    }

    if (deferredDeletes) {
        deferredDeletes->OnStreamListenerAttached(playerId, handle_);
    }

    bool canceledPendingDelete = false;
    bool pendingOutdated = false;
    if (deferredDeletes) {
        canceledPendingDelete = deferredDeletes->CancelPendingDeleteStream(playerId, handle_, &pendingOutdated);
        (void)deferredDeletes->CancelPendingRecreate(playerId, handle_);
    }

    if (sendCreatePacket && adapter.IsPlayerConnected(playerId)) {
        const bool isPlus = IsPlusPlayer(playerId, plusFlags);
        if (!canceledPendingDelete) {
            const ControlPacketBuffer& createPkt =
                (!isPlus && IsLocal()) ? packetCreateLegacy_ : packetCreate_;
            adapter.SendControlPacket(playerId, createPkt.Data(), createPkt.Size());
            SendAttachStatePackets(playerId, isPlus, adapter);
        } else if (pendingOutdated) {
            const int64_t lastVoiceNs = lastVoiceForwardNs_.load(std::memory_order_acquire);
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            constexpr int64_t kVoiceSilenceDelayNs = 1'000'000'000LL;
            const bool voiceActive = (lastVoiceNs > 0) && (nowNs >= lastVoiceNs) &&
                                     ((nowNs - lastVoiceNs) < kVoiceSilenceDelayNs);
            if (voiceActive && deferredDeletes) {
                SendReattachSyncPackets(playerId, isPlus, adapter);
                deferredDeletes->ScheduleOrRecreateStream(playerId, handle_, lastVoiceNs);
            } else {
                SendRecreateToListener(playerId, adapter, plusFlags, deferredDeletes);
            }
        } else {
            SendReattachSyncPackets(playerId, isPlus, adapter);
        }
    }

    return true;
}

bool Stream::DetachListenerInternal(
    uint16_t playerId,
    IServerAdapter& adapter,
    bool sendDeletePacket,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    if (!listenerMask_[playerId].exchange(false, std::memory_order_acq_rel)) {
        return false;
    }

    {
        std::unique_lock lock(listenerMutex_);
        const auto it = std::find(listenerList_.begin(), listenerList_.end(), playerId);
        if (it != listenerList_.end()) {
            listenerList_.erase(it);
            PublishListenerSnapshotLocked();
        }
    }
    bool hadPendingRecreate = false;
    if (deferredDeletes) {
        deferredDeletes->OnStreamListenerDetached(playerId, handle_);
        hadPendingRecreate = deferredDeletes->CancelPendingRecreate(playerId, handle_);
    }
    if (!IsDynamic()) {
        SetListenerVolumeFactorLocked(playerId, 1.0f);
    }

    if (sendDeletePacket && adapter.IsPlayerConnected(playerId)) {
        if (deferredDeletes) {
            deferredDeletes->ScheduleOrSendDeleteStream(
                playerId,
                handle_,
                lastVoiceForwardNs_.load(std::memory_order_acquire),
                packetDelete_,
                adapter,
                hadPendingRecreate
            );
        } else {
            adapter.SendControlPacket(playerId, packetDelete_.Data(), packetDelete_.Size());
        }
    }

    return true;
}

bool Stream::AttachListener(
    uint16_t playerId,
    IServerAdapter& adapter,
    bool sendCreatePacket,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    IDeferredStreamDeleteSink* deferredDeletes,
    float volume
) {
    if (playerId >= kMaxPlayers || std::isnan(volume) || volume < kMinListenerVolume) {
        return false;
    }
    if (HasListener(playerId)) {
        return false;
    }
    const float clamped = (std::min)(volume, kMaxListenerVolume);
    if (IsDynamic()) {
        SetListenerVolumeFactorLocked(playerId, clamped);
        UnblockListener(playerId);
        return AttachDynamicListenerIfInRange(playerId, adapter, plusFlags, deferredDeletes);
    }

    SetListenerVolumeFactorLocked(playerId, clamped);
    if (!AttachListenerInternal(playerId, adapter, sendCreatePacket, plusFlags, deferredDeletes)) {
        SetListenerVolumeFactorLocked(playerId, 1.0f);
        return false;
    }
    return true;
}

bool Stream::AttachDynamicListenerIfInRange(
    uint16_t playerId,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    if (!IsDynamic() || playerId >= kMaxPlayers || HasListener(playerId) || maxPlayers_ == 0) {
        return false;
    }

    if (maxPlayers_ != static_cast<uint32_t>(-1)) {
        const auto snap = GetListenersSnapshot();
        if (snap && snap->size() >= maxPlayers_) {
            return false;
        }
    }

    if (entityKind_ == EntityKind::Player && playerId == static_cast<uint16_t>(targetId_)) {
        return false;
    }

    EntitySpatialState anchor{};
    if (entityKind_ == EntityKind::Point) {
        anchor.valid = true;
        anchor.x = position_.x;
        anchor.y = position_.y;
        anchor.z = position_.z;
    } else {
        anchor = adapter.GetEntitySpatialState(entityKind_, targetId_);
    }
    if (!anchor.valid) {
        return false;
    }

    const EntitySpatialState listenerState = adapter.GetEntitySpatialState(EntityKind::Player, playerId);
    if (!listenerState.valid) {
        return false;
    }
    if (entityKind_ == EntityKind::Player &&
        (listenerState.interior != anchor.interior || listenerState.virtualWorld != anchor.virtualWorld)) {
        return false;
    }
    if (entityKind_ == EntityKind::Vehicle && listenerState.virtualWorld != anchor.virtualWorld) {
        return false;
    }
    if (entityKind_ == EntityKind::Point || entityKind_ == EntityKind::Object) {
        if (worldId_ >= 0 && listenerState.virtualWorld != worldId_) {
            return false;
        }
        if (interiorId_ >= 0 && listenerState.interior != interiorId_) {
            return false;
        }
    }

    const float dx = listenerState.x - anchor.x;
    const float dy = listenerState.y - anchor.y;
    const float dz = listenerState.z - anchor.z;
    if ((dx * dx + dy * dy + dz * dz) > (distance_ * distance_)) {
        return false;
    }

    return AttachListenerInternal(playerId, adapter, true, plusFlags, deferredDeletes);
}

bool Stream::DetachListener(
    uint16_t playerId,
    IServerAdapter& adapter,
    bool sendDeletePacket,
    bool blockIfDynamic,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    if (playerId < kMaxPlayers && IsDynamic() && blockIfDynamic) {
        std::unique_lock lock(listenerMutex_);
        blockedListeners_[playerId] = true;
    }
    return DetachListenerInternal(playerId, adapter, sendDeletePacket, deferredDeletes);
}

void Stream::UnblockListener(uint16_t playerId) noexcept {
    if (playerId >= kMaxPlayers) {
        return;
    }
    std::unique_lock lock(listenerMutex_);
    blockedListeners_[playerId] = false;
}

bool Stream::HasListener(uint16_t playerId) const noexcept {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    return listenerMask_[playerId].load(std::memory_order_acquire);
}

std::vector<uint16_t> Stream::DetachAllListeners(
    IServerAdapter& adapter,
    bool sendDeletePacket,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    std::vector<uint16_t> detached;
    {
        std::unique_lock lock(listenerMutex_);
        detached.swap(listenerList_);
        PublishListenerSnapshotLocked();
    }

    const int64_t lastVoiceNs = lastVoiceForwardNs_.load(std::memory_order_acquire);
    const bool clearFactors = !IsDynamic();
    for (uint16_t pid : detached) {
        listenerMask_[pid].store(false, std::memory_order_release);
        bool hadPendingRecreate = false;
        if (deferredDeletes) {
            deferredDeletes->OnStreamListenerDetached(pid, handle_);
            hadPendingRecreate = deferredDeletes->CancelPendingRecreate(pid, handle_);
        }
        if (clearFactors) {
            SetListenerVolumeFactorLocked(pid, 1.0f);
        }
        if (sendDeletePacket && adapter.IsPlayerConnected(pid)) {
            if (deferredDeletes) {
                deferredDeletes->ScheduleOrSendDeleteStream(
                    pid,
                    handle_,
                    lastVoiceNs,
                    packetDelete_,
                    adapter,
                    hadPendingRecreate
                );
            } else {
                adapter.SendControlPacket(pid, packetDelete_.Data(), packetDelete_.Size());
            }
        }
    }
    return detached;
}

bool Stream::AttachSpeaker(uint16_t playerId) noexcept {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    if (speakerMask_[playerId].exchange(true, std::memory_order_acq_rel)) {
        return false;
    }
    speakerList_.push_back(playerId);
    return true;
}

bool Stream::DetachSpeaker(uint16_t playerId) noexcept {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    if (!speakerMask_[playerId].exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    const auto it = std::find(speakerList_.begin(), speakerList_.end(), playerId);
    if (it != speakerList_.end()) {
        speakerList_.erase(it);
    }
    return true;
}

bool Stream::HasSpeaker(uint16_t playerId) const noexcept {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    return speakerMask_[playerId].load(std::memory_order_acquire);
}

std::vector<uint16_t> Stream::DetachAllSpeakers() {
    std::vector<uint16_t> detached;
    detached.swap(speakerList_);
    for (uint16_t pid : detached) {
        speakerMask_[pid].store(false, std::memory_order_release);
    }
    return detached;
}

// -----------------------------------------------------------------------------
// Spatial Updates & O(1) Dynamic Tick
// -----------------------------------------------------------------------------

bool Stream::UpdatePosition(float x, float y, float z, IServerAdapter& adapter) {
    if (!IsPointStream() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }
    position_ = {x, y, z};
    UpdateCreatePacketPosition(packetCreate_, position_);
    UpdateCreatePacketPosition(packetCreateLegacy_, position_);
    if (auto* p = packetUpdatePosition_.MutablePayload<protocol::UpdateLPStreamPositionPacket>()) {
        p->position = position_;
    }
    BroadcastControlPacket(packetUpdatePosition_, adapter);
    return true;
}

bool Stream::UpdateDistance(
    float distance,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags
) {
    if (!IsLocal() || !std::isfinite(distance) || distance < 0.0f) {
        return false;
    }
    distance_ = distance;
    const float legacyDistance = distance * kLegacyDistanceScale;
    UpdateCreatePacketDistance(packetCreate_, entityKind_, distance);
    UpdateCreatePacketDistance(packetCreateLegacy_, entityKind_, legacyDistance);
    UpdateDistancePacketValue(packetUpdateDistance_, distance);
    UpdateDistancePacketValue(packetUpdateDistanceLegacy_, legacyDistance);

    const auto listeners = GetListenersSnapshot();
    for (uint16_t pid : *listeners) {
        if (adapter.IsPlayerConnected(pid)) {
            const ControlPacketBuffer& pkt =
                IsPlusPlayer(pid, plusFlags) ? packetUpdateDistance_ : packetUpdateDistanceLegacy_;
            adapter.SendControlPacket(pid, pkt.Data(), pkt.Size());
        }
    }
    return true;
}

void Stream::TickDynamic(
    const SpatialGrid& grid,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>& playerHasPlugin,
    const std::array<std::atomic<bool>, kMaxPlayers>& playerIsPlus,
    std::vector<NearbyPlayer>& scratchBuffer,
    std::vector<uint32_t>& desiredMarks,
    uint32_t desiredGen,
    std::vector<NearbyPlayer>& addScratch,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    if (!IsDynamic()) {
        return;
    }

    // Legacy maxplayers == 0 means 0 listeners allowed
    if (maxPlayers_ == 0) {
        if (!GetListenersSnapshot()->empty()) {
            DetachAllListeners(adapter, true, deferredDeletes);
        }
        return;
    }

    EntitySpatialState anchor{};
    if (entityKind_ == EntityKind::Point) {
        anchor.valid = true;
        anchor.x = position_.x;
        anchor.y = position_.y;
        anchor.z = position_.z;
    } else {
        anchor = adapter.GetEntitySpatialState(entityKind_, targetId_);
    }

    if (!anchor.valid) {
        DetachAllListeners(adapter, true, deferredDeletes);
        return;
    }

    bool filterInterior = false;
    bool filterWorld = false;
    if (entityKind_ == EntityKind::Player) {
        filterInterior = true;
        filterWorld = true;
    } else if (entityKind_ == EntityKind::Vehicle) {
        filterWorld = true;
    } else if (entityKind_ == EntityKind::Point || entityKind_ == EntityKind::Object) {
        if (worldId_ >= 0) {
            filterWorld = true;
            anchor.virtualWorld = worldId_;
        }
        if (interiorId_ >= 0) {
            filterInterior = true;
            anchor.interior = interiorId_;
        }
    }
    const uint16_t excludeId = (entityKind_ == EntityKind::Player)
        ? static_cast<uint16_t>(targetId_)
        : protocol::kInvalidPlayerId;

    // Query all in-range candidates (0 = unsorted full range) so existing listeners stay sticky
    grid.QueryRadius(
        anchor.x, anchor.y, anchor.z,
        anchor.interior, anchor.virtualWorld,
        filterInterior,
        filterWorld,
        distance_,
        excludeId,
        0,
        scratchBuffer
    );

    if (desiredMarks.size() < kMaxPlayers) {
        desiredMarks.assign(kMaxPlayers, 0u);
    }

    {
        std::shared_lock blockLock(listenerMutex_);
        for (const auto& candidate : scratchBuffer) {
            const uint16_t pid = candidate.playerId;
            if (pid < kMaxPlayers &&
                !blockedListeners_[pid] &&
                playerHasPlugin[pid].load(std::memory_order_acquire)) {
                desiredMarks[pid] = desiredGen;
            }
        }
    }

    // 1. Detach only listeners that left range/world or lost plugin state; keep current valid ones
    const auto currentSnap = GetListenersSnapshot();
    std::uint32_t keptCount = 0;
    for (uint16_t existingId : *currentSnap) {
        if (existingId < kMaxPlayers && desiredMarks[existingId] == desiredGen) {
            ++keptCount;
        } else {
            DetachListenerInternal(existingId, adapter, true, deferredDeletes);
        }
    }

    // SV_INFINITY (-1 as uint32_t = 0xFFFFFFFF) means unlimited listeners
    const bool unlimited = (maxPlayers_ == static_cast<uint32_t>(-1));
    if (!unlimited && keptCount >= maxPlayers_) {
        return;
    }

    // 2. Collect vacant in-range candidates not yet attached
    addScratch.clear();
    for (const auto& candidate : scratchBuffer) {
        const uint16_t pid = candidate.playerId;
        if (pid < kMaxPlayers && desiredMarks[pid] == desiredGen && !HasListener(pid)) {
            addScratch.push_back(candidate);
        }
    }
    if (addScratch.empty()) {
        return;
    }

    std::size_t toAttach = addScratch.size();
    if (!unlimited) {
        const std::size_t vacancies = static_cast<std::size_t>(maxPlayers_ - keptCount);
        if (toAttach > vacancies) {
            std::partial_sort(
                addScratch.begin(),
                addScratch.begin() + static_cast<std::ptrdiff_t>(vacancies),
                addScratch.end(),
                [](const NearbyPlayer& a, const NearbyPlayer& b) {
                    return a.distanceSq < b.distanceSq;
                }
            );
            toAttach = vacancies;
        }
    }

    for (std::size_t i = 0; i < toAttach; ++i) {
        AttachListenerInternal(addScratch[i].playerId, adapter, true, &playerIsPlus, deferredDeletes);
    }
}

ControlPacketBuffer Stream::BuildIconPacket() const {
    protocol::PlusStreamIconPacket pkt{};
    pkt.stream = handle_;
    pkt.iconId = static_cast<uint8_t>(iconId_);
    return ControlPacketBuffer::Create(protocol::ControlPacketType::PlusStreamIcon, pkt);
}

ControlPacketBuffer Stream::BuildInfoPacket() const {
    protocol::PlusStreamInfoPacket pkt{};
    pkt.stream = handle_;
    pkt.color = color_;
    return ControlPacketBuffer::CreateWithTrailingString(
        protocol::ControlPacketType::PlusStreamInfo, pkt, name_
    );
}

void Stream::SendRecreateToListener(
    uint16_t playerId,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    IDeferredStreamDeleteSink* deferredDeletes
) const {
    if (playerId >= kMaxPlayers || !HasListener(playerId) || !adapter.IsPlayerConnected(playerId)) {
        return;
    }
    if (deferredDeletes) {
        (void)deferredDeletes->CancelPendingDeleteStream(playerId, handle_);
    }
    const bool isPlus = IsPlusPlayer(playerId, plusFlags);
    adapter.SendControlPacket(playerId, packetDelete_.Data(), packetDelete_.Size());
    const ControlPacketBuffer& createPkt =
        (!isPlus && IsLocal()) ? packetCreateLegacy_ : packetCreate_;
    adapter.SendControlPacket(playerId, createPkt.Data(), createPkt.Size());
    SendAttachStatePackets(playerId, isPlus, adapter);
}

void Stream::NotifyInfoChanged(
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions,
    IDeferredStreamDeleteSink* deferredDeletes
) const {
    const auto listeners = GetListenersSnapshot();
    if (!listeners || listeners->empty()) {
        return;
    }
    const auto infoPkt = BuildInfoPacket();
    const int64_t lastVoiceNs = lastVoiceForwardNs_.load(std::memory_order_acquire);
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    constexpr int64_t kVoiceSilenceDelayNs = 1'000'000'000LL;
    const bool voiceActive = (lastVoiceNs > 0) && (nowNs >= lastVoiceNs) &&
                             ((nowNs - lastVoiceNs) < kVoiceSilenceDelayNs);

    for (uint16_t pid : *listeners) {
        if (!adapter.IsPlayerConnected(pid)) {
            continue;
        }
        const uint32_t plusVer = (plusVersions && pid < kMaxPlayers)
            ? (*plusVersions)[pid].load(std::memory_order_acquire)
            : 0u;
        if (IsPlusPlayer(pid, plusFlags) && plusVer >= 10300u) {
            if (deferredDeletes) {
                (void)deferredDeletes->CancelPendingRecreate(pid, handle_);
            }
            adapter.SendControlPacket(pid, infoPkt.Data(), infoPkt.Size());
        } else if (voiceActive && deferredDeletes) {
            deferredDeletes->ScheduleOrRecreateStream(pid, handle_, lastVoiceNs);
        } else {
            if (deferredDeletes) {
                (void)deferredDeletes->CancelPendingRecreate(pid, handle_);
            }
            SendRecreateToListener(pid, adapter, plusFlags, deferredDeletes);
        }
    }
}

bool Stream::UpdateName(
    const std::string& name,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    const std::string clamped = name.substr(0, 128u);
    if (clamped == name_) {
        return true;
    }
    name_ = clamped;
    RebuildCreatePackets();
    NotifyInfoChanged(adapter, plusFlags, plusVersions, deferredDeletes);
    return true;
}

bool Stream::UpdateColor(
    uint32_t wireColor,
    uint32_t rawColor,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions,
    IDeferredStreamDeleteSink* deferredDeletes
) {
    if (wireColor == color_ && rawColor == rawColor_) {
        return true;
    }
    color_ = wireColor;
    rawColor_ = rawColor;
    RebuildCreatePackets();
    NotifyInfoChanged(adapter, plusFlags, plusVersions, deferredDeletes);
    return true;
}

void Stream::SetIcon(
    protocol::StreamIconId iconId,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>& plusFlags
) {
    iconId_ = iconId;
    const auto packet = BuildIconPacket();
    const auto listeners = GetListenersSnapshot();
    for (uint16_t pid : *listeners) {
        if (pid < kMaxPlayers &&
            plusFlags[pid].load(std::memory_order_acquire) &&
            adapter.IsPlayerConnected(pid)) {
            adapter.SendControlPacket(pid, packet.Data(), packet.Size());
        }
    }
}

// -----------------------------------------------------------------------------
// Parameters & Effects
// -----------------------------------------------------------------------------

void Stream::BroadcastParameterUpdate(
    const StreamParameter& param,
    uint32_t parameterId,
    IServerAdapter& adapter
) const {
    const auto now = std::chrono::steady_clock::now();
    if (parameterId != static_cast<uint32_t>(protocol::ParameterType::Volume) ||
        nonDefaultVolumeCount_ == 0u) {
        BroadcastControlPacket(param.BuildApplyPacketAt(now), adapter);
        return;
    }

    const auto defaultPkt = param.BuildApplyPacketAt(now);
    const auto listeners = GetListenersSnapshot();
    for (uint16_t pid : *listeners) {
        if (!adapter.IsPlayerConnected(pid)) {
            continue;
        }
        const float factor = (pid < kMaxPlayers) ? listenerVolumeFactors_[pid] : 1.0f;
        if (factor == 1.0f) {
            adapter.SendControlPacket(pid, defaultPkt.Data(), defaultPkt.Size());
        } else {
            const auto scaledPkt = param.BuildApplyPacketAt(now, factor);
            adapter.SendControlPacket(pid, scaledPkt.Data(), scaledPkt.Size());
        }
    }
}

namespace {
bool IsValidParamValue(uint32_t parameterId, float value) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    using PT = protocol::ParameterType;
    if (parameterId == static_cast<uint32_t>(PT::Volume)) {
        return value >= 0.0f;
    }
    if (parameterId == static_cast<uint32_t>(PT::Frequency)) {
        return value >= 0.0f;
    }
    return true;
}
} // namespace

bool Stream::SetParameter(uint32_t parameterId, float value, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId) || !IsValidParamValue(parameterId, value)) {
        return false;
    }
    auto [it, inserted] = parameters_.try_emplace(parameterId, handle_, parameterId, value);
    if (!inserted) {
        it->second.Set(value);
    }
    BroadcastParameterUpdate(it->second, parameterId, adapter);
    return true;
}

bool Stream::ResetParameter(uint32_t parameterId, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return false;
    }
    const auto it = parameters_.find(parameterId);
    if (it != parameters_.end()) {
        it->second.Set(StreamParameter::DefaultValue(parameterId));
        BroadcastParameterUpdate(it->second, parameterId, adapter);
        parameters_.erase(it);
    }
    return true;
}

bool Stream::HasParameter(uint32_t parameterId) const {
    return parameters_.find(parameterId) != parameters_.end();
}

float Stream::GetParameter(uint32_t parameterId) const {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return -1.0f;
    }
    const auto it = parameters_.find(parameterId);
    return it != parameters_.end() ? it->second.Get() : StreamParameter::DefaultValue(parameterId);
}

bool Stream::SlideParameterFromTo(
    uint32_t parameterId, float startVal, float endVal, uint32_t timeMs, IServerAdapter& adapter
) {
    if (!StreamParameter::IsValidParameter(parameterId) ||
        !IsValidParamValue(parameterId, startVal) ||
        !IsValidParamValue(parameterId, endVal)) {
        return false;
    }
    auto [it, inserted] = parameters_.try_emplace(
        parameterId, handle_, parameterId, StreamParameter::DefaultValue(parameterId)
    );
    (void)inserted;
    it->second.SlideFromTo(startVal, endVal, timeMs);
    BroadcastParameterUpdate(it->second, parameterId, adapter);
    return true;
}

bool Stream::SlideParameterTo(uint32_t parameterId, float endVal, uint32_t timeMs, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId) || !IsValidParamValue(parameterId, endVal)) {
        return false;
    }
    auto [it, inserted] = parameters_.try_emplace(
        parameterId, handle_, parameterId, StreamParameter::DefaultValue(parameterId)
    );
    (void)inserted;
    it->second.SlideTo(endVal, timeMs);
    BroadcastParameterUpdate(it->second, parameterId, adapter);
    return true;
}

bool Stream::SlideParameter(uint32_t parameterId, float deltaVal, uint32_t timeMs, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId) || !std::isfinite(deltaVal)) {
        return false;
    }
    auto it = parameters_.find(parameterId);
    const float rawCurrent = (it != parameters_.end())
        ? it->second.Get()
        : StreamParameter::DefaultValue(parameterId);
    const float current = ResolveWireFrequency(parameterId, rawCurrent);
    const float targetVal = current + deltaVal;
    if (!std::isfinite(targetVal)) {
        return false;
    }
    if (it == parameters_.end()) {
        it = parameters_.try_emplace(
            parameterId, handle_, parameterId, StreamParameter::DefaultValue(parameterId)
        ).first;
    }
    it->second.Slide(deltaVal, timeMs);
    BroadcastParameterUpdate(it->second, parameterId, adapter);
    return true;
}

void Stream::RegisterEffectPacket(uint32_t effectId, ControlPacketBuffer createPacket, IServerAdapter& adapter) {
    BroadcastControlPacket(createPacket, adapter);
    effects_[effectId] = std::move(createPacket);
}

void Stream::UpdateEffectPacket(
    uint32_t effectId,
    ControlPacketBuffer createPacket,
    const ControlPacketBuffer& deletePacket,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags,
    const std::array<std::atomic<uint32_t>, kMaxPlayers>* plusVersions
) {
    auto it = effects_.find(effectId);
    if (it == effects_.end()) {
        return;
    }
    it->second = std::move(createPacket);
    const auto listeners = GetListenersSnapshot();
    if (!listeners || listeners->empty()) {
        return;
    }
    for (uint16_t pid : *listeners) {
        if (!adapter.IsPlayerConnected(pid)) {
            continue;
        }
        const uint32_t plusVer = (plusVersions && pid < kMaxPlayers)
            ? (*plusVersions)[pid].load(std::memory_order_acquire)
            : 0u;
        if (IsPlusPlayer(pid, plusFlags) && plusVer >= 10300u) {
            adapter.SendControlPacket(pid, it->second.Data(), it->second.Size());
        } else {
            adapter.SendControlPacket(pid, deletePacket.Data(), deletePacket.Size());
            adapter.SendControlPacket(pid, it->second.Data(), it->second.Size());
        }
    }
}

void Stream::UnregisterEffectPacket(uint32_t effectId, ControlPacketBuffer deletePacket, IServerAdapter& adapter) {
    if (effects_.erase(effectId) > 0) {
        BroadcastControlPacket(deletePacket, adapter);
    }
}

std::vector<uint32_t> Stream::ClearEffects() {
    std::vector<uint32_t> ids;
    ids.reserve(effects_.size());
    for (const auto& [effectId, _] : effects_) {
        ids.push_back(effectId);
    }
    effects_.clear();
    return ids;
}

void Stream::BroadcastControlPacket(const ControlPacketBuffer& packet, IServerAdapter& adapter) const {
    const auto listeners = GetListenersSnapshot();
    for (uint16_t pid : *listeners) {
        if (adapter.IsPlayerConnected(pid)) {
            adapter.SendControlPacket(pid, packet.Data(), packet.Size());
        }
    }
}

} // namespace svplus::server
