/*
 * SampVoice Plus - Legacy SA-MP 0.3.7 Server Host Adapter Implementation
 */

#include "SampAdapter.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "../common/PawnBindings.hpp"
#include "SampRakNetHook.hpp"
#include "svplus/common/Protocol.hpp"
#include "svplus/server/ServerCore.hpp"

namespace svplus::server::samp {

namespace {

class SampCfgBackend final : public ConfigResolverBackend {
public:
    explicit SampCfgBackend(const ServerCfgMap& map) noexcept : map_(map) {}

    bool TryGetRaw(const std::string& key, std::string& outVal, std::string& outSrc) const override {
        if (!map_.TryGet(key, outVal)) {
            return false;
        }
        outSrc = "server.cfg";
        return true;
    }

private:
    const ServerCfgMap& map_;
};

template <typename PoolT>
bool IsPlayerConnectedInPool(const PoolT* pool, uint16_t playerId) noexcept {
    return pool && playerId < kSampMaxPlayers &&
           pool->bIsPlayerConnected[playerId] != 0 &&
           pool->pPlayer[playerId] != nullptr;
}

template <typename PoolT>
void FillPlayerSpatialState(const PoolT* pool, uint32_t targetId, EntitySpatialState& state) noexcept {
    if (!pool || targetId >= kSampMaxPlayers) return;
    if (!pool->bIsPlayerConnected[targetId] || !pool->pPlayer[targetId]) return;
    const auto* p = pool->pPlayer[targetId];
    state.x = p->vecPosition.x;
    state.y = p->vecPosition.y;
    state.z = p->vecPosition.z;
    state.virtualWorld = static_cast<int32_t>(pool->dwVirtualWorld[targetId]);
    state.interior = p->iInteriorId;
    state.valid = true;
}

template <typename ObjectPoolT, uint32_t MaxObjs>
void FillObjectSpatialState(const ObjectPoolT* pool, uint32_t targetId, EntitySpatialState& state) noexcept {
    if (!pool || targetId >= MaxObjs) return;
    if (!pool->bObjectSlotState[targetId] || !pool->pObjects[targetId]) return;
    const SampObject* o = pool->pObjects[targetId];
    state.x = o->matWorld.pos.x;
    state.y = o->matWorld.pos.y;
    state.z = o->matWorld.pos.z;
    state.virtualWorld = -1;
    state.interior = -1;
    state.valid = true;
}

template <typename PoolT>
void CollectPoolPlayerSnapshots(
    const PoolT* pool,
    const SampVehiclePool* vehiclePool,
    const std::function<void(const PlayerSpatialSnapshot&)>& sink
) {
    if (!pool || pool->dwConnectedPlayers == 0) {
        return;
    }

    const uint32_t maxIdx = std::min<uint32_t>(pool->dwPlayerPoolSize + 1u, kSampMaxPlayers);
    for (uint32_t i = 0; i < maxIdx; ++i) {
        if (!pool->bIsPlayerConnected[i] || !pool->pPlayer[i]) continue;
        if (pool->bIsNPC[i]) continue;

        const auto* p = pool->pPlayer[i];
        PlayerSpatialSnapshot snap{};
        snap.playerId = static_cast<uint16_t>(i);
        snap.x = p->vecPosition.x;
        snap.y = p->vecPosition.y;
        snap.z = p->vecPosition.z;
        snap.virtualWorld = static_cast<int32_t>(pool->dwVirtualWorld[i]);
        snap.interior = p->iInteriorId;

        if (p->byteSpectateType == 1 && p->dwSpectateId >= 0 &&
            p->dwSpectateId < static_cast<int32_t>(kSampMaxPlayers) &&
            static_cast<uint32_t>(p->dwSpectateId) != i) {
            const uint32_t tid = static_cast<uint32_t>(p->dwSpectateId);
            if (pool->bIsPlayerConnected[tid] && pool->pPlayer[tid]) {
                const auto* target = pool->pPlayer[tid];
                snap.x = target->vecPosition.x;
                snap.y = target->vecPosition.y;
                snap.z = target->vecPosition.z;
                snap.virtualWorld = static_cast<int32_t>(pool->dwVirtualWorld[tid]);
                snap.interior = target->iInteriorId;
            }
        } else if (p->byteSpectateType == 2 && vehiclePool && p->dwSpectateId >= 0 &&
                   p->dwSpectateId < static_cast<int32_t>(kSampMaxVehicles)) {
            const uint32_t vid = static_cast<uint32_t>(p->dwSpectateId);
            if (vehiclePool->bVehicleSlotState[vid] && vehiclePool->pVehicle[vid]) {
                const SampVehicle* veh = vehiclePool->pVehicle[vid];
                snap.x = veh->vecPosition.x;
                snap.y = veh->vecPosition.y;
                snap.z = veh->vecPosition.z;
                snap.virtualWorld = vehiclePool->iVirtualWorld[vid];
            }
        }

        sink(snap);
    }
}

template <typename PoolT>
void DetectDisconnectsInPool(
    const PoolT* pool,
    std::array<bool, kSampMaxPlayers>& trackedConnected,
    ServerCore& core
) noexcept {
    if (!pool) return;
    const uint32_t maxIdx = std::min<uint32_t>(pool->dwPlayerPoolSize + 1u, kSampMaxPlayers);
    for (uint32_t i = 0; i < maxIdx; ++i) {
        const bool connectedNow = (pool->bIsPlayerConnected[i] != 0 && pool->pPlayer[i] != nullptr);
        if (trackedConnected[i] && !connectedNow) {
            trackedConnected[i] = false;
            core.HandlePlayerDisconnect(static_cast<uint16_t>(i));
        } else if (connectedNow && !trackedConnected[i]) {
            trackedConnected[i] = true;
        }
    }
    for (uint32_t i = maxIdx; i < kSampMaxPlayers; ++i) {
        if (trackedConnected[i]) {
            trackedConnected[i] = false;
            core.HandlePlayerDisconnect(static_cast<uint16_t>(i));
        }
    }
}

} // namespace

void SampAdapter::LoadConfigOnce(const std::string& path) {
    if (!cfgMap_.IsLoaded()) {
        cfgMap_.LoadFromFile(path);
    }
}

bool SampAdapter::ResolveStartOptions(ServerStartOptions& outOptions) const {
    SampCfgBackend backend(cfgMap_);
    return ResolveServerStartOptions(backend, "bind", outOptions);
}

void SampAdapter::TrackPlayerConnected(uint16_t playerId) noexcept {
    if (playerId < kSampMaxPlayers) {
        trackedConnected_[playerId] = true;
    }
}

void SampAdapter::TrackPlayerDisconnected(uint16_t playerId) noexcept {
    if (playerId < kSampMaxPlayers) {
        trackedConnected_[playerId] = false;
    }
}

void SampAdapter::DetectDisconnects(ServerCore& core) noexcept {
    if (!netGame_ || !netGame_->pPlayerPool) {
        return;
    }

    if (is03DL_) {
        DetectDisconnectsInPool(reinterpret_cast<const SampPlayerPoolDL*>(netGame_->pPlayerPool), trackedConnected_, core);
    } else {
        DetectDisconnectsInPool(netGame_->pPlayerPool, trackedConnected_, core);
    }
}

bool SampAdapter::SendControlPacket(uint16_t playerId, const uint8_t* data, std::size_t size) {
    if (!data || size == 0) {
        return false;
    }

    const std::size_t totalBytes = 1 + size;
    std::array<uint8_t, 1024> stackBuf{};
    if (totalBytes <= stackBuf.size()) {
        stackBuf[0] = protocol::kRakNetPacketIdControl;
        std::memcpy(stackBuf.data() + 1, data, size);
        return SampRakNetHook::SendPacket(playerId, stackBuf.data(), totalBytes);
    }

    std::vector<uint8_t> heapBuf(totalBytes);
    heapBuf[0] = protocol::kRakNetPacketIdControl;
    std::memcpy(heapBuf.data() + 1, data, size);
    return SampRakNetHook::SendPacket(playerId, heapBuf.data(), totalBytes);
}

uint32_t SampAdapter::GetPlayerIpv4Net(uint16_t playerId) const {
    if (playerId >= kSampMaxPlayers) {
        return 0;
    }
    return SampRakNetHook::GetPlayerIpv4Net(playerId);
}

bool SampAdapter::IsPlayerConnected(uint16_t playerId) const {
    if (!netGame_ || !netGame_->pPlayerPool || playerId >= kSampMaxPlayers) {
        return false;
    }
    if (is03DL_) {
        return IsPlayerConnectedInPool(reinterpret_cast<const SampPlayerPoolDL*>(netGame_->pPlayerPool), playerId);
    }
    return IsPlayerConnectedInPool(netGame_->pPlayerPool, playerId);
}

EntitySpatialState SampAdapter::GetEntitySpatialState(EntityKind kind, uint32_t targetId) const {
    EntitySpatialState state{};
    if (!netGame_) {
        return state;
    }

    switch (kind) {
    case EntityKind::Point:
        // Point streams store fixed coordinates on Stream itself (no host entity, interior, or VW).
        break;
    case EntityKind::Player: {
        if (is03DL_) {
            FillPlayerSpatialState(reinterpret_cast<const SampPlayerPoolDL*>(netGame_->pPlayerPool), targetId, state);
        } else {
            FillPlayerSpatialState(netGame_->pPlayerPool, targetId, state);
        }
        break;
    }
    case EntityKind::Vehicle: {
        if (!netGame_->pVehiclePool || targetId >= kSampMaxVehicles) break;
        const SampVehiclePool* pool = netGame_->pVehiclePool;
        if (!pool->bVehicleSlotState[targetId] || !pool->pVehicle[targetId]) break;
        const SampVehicle* v = pool->pVehicle[targetId];
        state.x = v->vecPosition.x;
        state.y = v->vecPosition.y;
        state.z = v->vecPosition.z;
        state.virtualWorld = pool->iVirtualWorld[targetId];
        state.interior = -1;
        state.valid = true;
        break;
    }
    case EntityKind::Object: {
        if (is03DL_) {
            FillObjectSpatialState<SampObjectPoolDL, kSampDLMaxObjects>(
                reinterpret_cast<const SampObjectPoolDL*>(netGame_->pObjectPool), targetId, state
            );
        } else {
            FillObjectSpatialState<SampObjectPool, kSampMaxObjects>(netGame_->pObjectPool, targetId, state);
        }
        break;
    }
    }

    return state;
}

void SampAdapter::CollectPlayerSnapshots(
    const std::function<void(const PlayerSpatialSnapshot&)>& sink
) const {
    if (!netGame_ || !netGame_->pPlayerPool) {
        return;
    }

    if (is03DL_) {
        CollectPoolPlayerSnapshots(
            reinterpret_cast<const SampPlayerPoolDL*>(netGame_->pPlayerPool),
            netGame_->pVehiclePool,
            sink
        );
    } else {
        CollectPoolPlayerSnapshots(netGame_->pPlayerPool, netGame_->pVehiclePool, sink);
    }
}

void SampAdapter::OnActivationKeyPress(uint16_t playerId, uint8_t keyId) {
    PawnBindings::NotifyActivationKeyPress(playerId, keyId);
}

void SampAdapter::OnActivationKeyRelease(uint16_t playerId, uint8_t keyId) {
    PawnBindings::NotifyActivationKeyRelease(playerId, keyId);
}

void SampAdapter::OnMicroStateChange(uint16_t playerId, bool hasMicro) {
    PawnBindings::NotifyMicroStateChange(playerId, hasMicro);
}

void SampAdapter::OnVoiceMute(uint16_t playerId, bool releaseKeys, uint32_t timeMs, bool speaking) {
    PawnBindings::NotifyVoiceMute(playerId, releaseKeys, timeMs, speaking);
}

void SampAdapter::OnVoiceUnmute(uint16_t playerId, bool expired) {
    PawnBindings::NotifyVoiceUnmute(playerId, expired);
}

int SampAdapter::GetConfigInt(const std::string& key, int defaultValue) const {
    std::string val;
    if (cfgMap_.TryGet(key, val)) {
        long long parsed = 0;
        if (detail::ParseStrictInt(val, parsed)) {
            return static_cast<int>(parsed);
        }
    }
    return defaultValue;
}

} // namespace svplus::server::samp
