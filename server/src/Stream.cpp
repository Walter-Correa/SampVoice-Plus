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
    ratio_ = (endValue - startValue) / static_cast<float>(timeMs);
    endTime_ = Clock::now() + std::chrono::milliseconds(timeMs);
    endValue_ = endValue;
    value_ = endValue;
    sliding_ = true;
}

void StreamParameter::SlideTo(float endValue, uint32_t timeMs) noexcept {
    SlideFromTo(Get(), endValue, timeMs);
}

void StreamParameter::Slide(float deltaValue, uint32_t timeMs) noexcept {
    const float current = Get();
    SlideFromTo(current, current + deltaValue, timeMs);
}

ControlPacketBuffer StreamParameter::BuildApplyPacket() const {
    if (sliding_) {
        const auto now = Clock::now();
        if (now < endTime_) {
            const auto remainingMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime_ - now).count()
            );
            const float currentStart = endValue_ - static_cast<float>(remainingMs) * ratio_;

            protocol::SlideStreamParameterPacket pkt{};
            pkt.stream = streamId_;
            pkt.parameter = parameterId_;
            pkt.startValue = currentStart;
            pkt.endValue = endValue_;
            pkt.timeMs = remainingMs > 0 ? remainingMs : 1u;
            return ControlPacketBuffer::Create(protocol::ControlPacketType::SlideStreamParameter, pkt);
        }
    }

    protocol::SetStreamParameterPacket pkt{};
    pkt.stream = streamId_;
    pkt.parameter = parameterId_;
    pkt.value = value_;
    return ControlPacketBuffer::Create(protocol::ControlPacketType::SetStreamParameter, pkt);
}

// -----------------------------------------------------------------------------
// Stream Factories
// -----------------------------------------------------------------------------

std::shared_ptr<Stream> Stream::CreateGlobal(uint32_t color, const std::string& name) {
    auto s = std::shared_ptr<Stream>(new Stream());
    s->mode_ = Mode::Global;
    s->color_ = color;
    s->name_ = name;

    protocol::CreateGStreamPacket pkt{};
    pkt.stream = 0;
    pkt.color = color;
    s->packetCreate_ = ControlPacketBuffer::CreateWithTrailingString(
        protocol::ControlPacketType::CreateGStream, pkt, name
    );
    return s;
}

std::shared_ptr<Stream> Stream::CreateStaticLocal(
    EntityKind kind, float distance, uint32_t targetId,
    protocol::Vec3f position, uint32_t color, const std::string& name
) {
    auto s = std::shared_ptr<Stream>(new Stream());
    s->mode_ = Mode::StaticLocal;
    s->entityKind_ = kind;
    s->distance_ = distance;
    s->targetId_ = targetId;
    s->position_ = position;
    s->color_ = color;
    s->name_ = name;

    s->packetCreate_ = BuildLocalCreatePacket(kind, distance, targetId, position, color, name);
    s->packetCreateLegacy_ = BuildLocalCreatePacket(
        kind, distance * kLegacyDistanceScale, targetId, position, color, name
    );

    return s;
}

std::shared_ptr<Stream> Stream::CreateDynamicLocal(
    EntityKind kind, float distance, uint32_t maxPlayers, uint32_t targetId,
    protocol::Vec3f position, uint32_t color, const std::string& name
) {
    auto s = CreateStaticLocal(kind, distance, targetId, position, color, name);
    s->mode_ = Mode::DynamicLocal;
    s->maxPlayers_ = maxPlayers;
    return s;
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

    bool canceledPendingDelete = false;
    if (deferredDeletes) {
        canceledPendingDelete = deferredDeletes->CancelPendingDeleteStream(playerId, handle_);
    }

    if (sendCreatePacket && adapter.IsPlayerConnected(playerId)) {
        const bool isPlus = IsPlusPlayer(playerId, plusFlags);
        if (!canceledPendingDelete) {
            const ControlPacketBuffer& createPkt =
                (!isPlus && IsLocal()) ? packetCreateLegacy_ : packetCreate_;
            adapter.SendControlPacket(playerId, createPkt.Data(), createPkt.Size());
        } else if (IsLocal()) {
            // Re-attached within deferred-delete window: keep existing stream handle and sync latest distance cleanly.
            const ControlPacketBuffer& distPkt =
                isPlus ? packetUpdateDistance_ : packetUpdateDistanceLegacy_;
            adapter.SendControlPacket(playerId, distPkt.Data(), distPkt.Size());
        }
        // On re-attach before deferred DeleteStream fires, resend idempotent parameters missed while
        // detached; skip CreateStream and CreateEffect so legacy v11 clients never duplicate handles.
        for (const auto& [paramId, param] : parameters_) {
            (void)paramId;
            const auto pkt = param.BuildApplyPacket();
            adapter.SendControlPacket(playerId, pkt.Data(), pkt.Size());
        }
        if (!canceledPendingDelete) {
            for (const auto& [effectId, createPkt] : effects_) {
                (void)effectId;
                adapter.SendControlPacket(playerId, createPkt.Data(), createPkt.Size());
            }
            if (iconId_ != protocol::StreamIconId::Speaker && isPlus) {
                const auto iconPkt = BuildIconPacket();
                adapter.SendControlPacket(playerId, iconPkt.Data(), iconPkt.Size());
            }
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
            *it = listenerList_.back();
            listenerList_.pop_back();
            PublishListenerSnapshotLocked();
        }
    }

    if (sendDeletePacket && adapter.IsPlayerConnected(playerId)) {
        if (deferredDeletes) {
            deferredDeletes->ScheduleOrSendDeleteStream(
                playerId,
                handle_,
                lastVoiceForwardNs_.load(std::memory_order_acquire),
                packetDelete_,
                adapter
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
    IDeferredStreamDeleteSink* deferredDeletes
) {
    UnblockListener(playerId);
    if (IsDynamic()) {
        return AttachDynamicListenerIfInRange(playerId, adapter, plusFlags, deferredDeletes);
    }
    return AttachListenerInternal(playerId, adapter, sendCreatePacket, plusFlags, deferredDeletes);
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
    for (uint16_t pid : detached) {
        listenerMask_[pid].store(false, std::memory_order_release);
        if (sendDeletePacket && adapter.IsPlayerConnected(pid)) {
            if (deferredDeletes) {
                deferredDeletes->ScheduleOrSendDeleteStream(pid, handle_, lastVoiceNs, packetDelete_, adapter);
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
        *it = speakerList_.back();
        speakerList_.pop_back();
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

void Stream::UpdatePosition(float x, float y, float z, IServerAdapter& adapter) {
    if (!IsPointStream()) {
        return;
    }
    position_ = {x, y, z};
    UpdateCreatePacketPosition(packetCreate_, position_);
    UpdateCreatePacketPosition(packetCreateLegacy_, position_);
    if (auto* p = packetUpdatePosition_.MutablePayload<protocol::UpdateLPStreamPositionPacket>()) {
        p->position = position_;
    }
    BroadcastControlPacket(packetUpdatePosition_, adapter);
}

void Stream::UpdateDistance(
    float distance,
    IServerAdapter& adapter,
    const std::array<std::atomic<bool>, kMaxPlayers>* plusFlags
) {
    if (!IsLocal()) {
        return;
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

    const bool filterInterior = (entityKind_ == EntityKind::Player);
    const bool filterWorld = (entityKind_ == EntityKind::Player || entityKind_ == EntityKind::Vehicle);
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

void Stream::SetParameter(uint32_t parameterId, float value, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return;
    }
    auto [it, inserted] = parameters_.try_emplace(parameterId, handle_, parameterId, value);
    if (!inserted) {
        it->second.Set(value);
    }
    BroadcastControlPacket(it->second.BuildApplyPacket(), adapter);
}

void Stream::ResetParameter(uint32_t parameterId, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return;
    }
    const auto it = parameters_.find(parameterId);
    if (it != parameters_.end()) {
        it->second.Set(StreamParameter::DefaultValue(parameterId));
        BroadcastControlPacket(it->second.BuildApplyPacket(), adapter);
        parameters_.erase(it);
    }
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

void Stream::SlideParameterFromTo(
    uint32_t parameterId, float startVal, float endVal, uint32_t timeMs, IServerAdapter& adapter
) {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return;
    }
    auto [it, inserted] = parameters_.try_emplace(
        parameterId, handle_, parameterId, StreamParameter::DefaultValue(parameterId)
    );
    (void)inserted;
    it->second.SlideFromTo(startVal, endVal, timeMs);
    BroadcastControlPacket(it->second.BuildApplyPacket(), adapter);
}

void Stream::SlideParameterTo(uint32_t parameterId, float endVal, uint32_t timeMs, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return;
    }
    auto [it, inserted] = parameters_.try_emplace(
        parameterId, handle_, parameterId, StreamParameter::DefaultValue(parameterId)
    );
    (void)inserted;
    it->second.SlideTo(endVal, timeMs);
    BroadcastControlPacket(it->second.BuildApplyPacket(), adapter);
}

void Stream::SlideParameter(uint32_t parameterId, float deltaVal, uint32_t timeMs, IServerAdapter& adapter) {
    if (!StreamParameter::IsValidParameter(parameterId)) {
        return;
    }
    auto [it, inserted] = parameters_.try_emplace(
        parameterId, handle_, parameterId, StreamParameter::DefaultValue(parameterId)
    );
    (void)inserted;
    it->second.Slide(deltaVal, timeMs);
    BroadcastControlPacket(it->second.BuildApplyPacket(), adapter);
}

void Stream::RegisterEffectPacket(uint32_t effectId, ControlPacketBuffer createPacket, IServerAdapter& adapter) {
    BroadcastControlPacket(createPacket, adapter);
    effects_[effectId] = std::move(createPacket);
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
