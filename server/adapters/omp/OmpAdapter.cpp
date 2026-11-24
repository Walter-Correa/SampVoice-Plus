/*
 * SampVoice Plus - open.mp Server Host Adapter Implementation
 */

#include "OmpAdapter.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../common/PawnBindings.hpp"
#include "svplus/common/Protocol.hpp"

namespace svplus::server::omp {

namespace {

class OmpCfgBackend final : public ConfigResolverBackend {
public:
    OmpCfgBackend(ICore* core, const ServerCfgMap& fallback) noexcept
        : core_(core), fallback_(fallback) {}

    bool TryGetRaw(const std::string& key, std::string& outVal, std::string& outSrc) const override {
        if (core_) {
            IConfig& cfg = core_->getConfig();
            const ConfigOptionType type = cfg.getType(key);
            if (type == ConfigOptionType_Int) {
                if (int* val = cfg.getInt(key)) {
                    outVal = std::to_string(*val);
                    outSrc = "config.json";
                    return true;
                }
            } else if (type == ConfigOptionType_String) {
                const StringView sv = cfg.getString(key);
                outVal.assign(sv.data(), sv.size());
                if (!outVal.empty()) {
                    outSrc = "config.json";
                    return true;
                }
            } else if (type == ConfigOptionType_Bool) {
                if (bool* val = cfg.getBool(key)) {
                    outVal = *val ? "true" : "false";
                    outSrc = "config.json";
                    return true;
                }
            } else if (type == ConfigOptionType_Float) {
                if (float* val = cfg.getFloat(key)) {
                    outVal = std::to_string(*val);
                    outSrc = "config.json";
                    return true;
                }
            }
        }

        if (fallback_.TryGet(key, outVal)) {
            outSrc = "server.cfg fallback";
            return true;
        }
        return false;
    }

private:
    ICore* core_;
    const ServerCfgMap& fallback_;
};

} // namespace

void OmpAdapter::AttachHost(ICore* core, IVehiclesComponent* vehicles, IObjectsComponent* objects) noexcept {
    core_ = core;
    players_ = core ? &core->getPlayers() : nullptr;
    vehicles_ = vehicles;
    objects_ = objects;
    if (!fallbackCfg_.IsLoaded()) {
        fallbackCfg_.LoadFromFile("server.cfg");
    }
}

bool OmpAdapter::ResolveStartOptions(ServerStartOptions& outOptions) const {
    OmpCfgBackend backend(core_, fallbackCfg_);
    return ResolveServerStartOptions(backend, "network.bind", outOptions);
}

void OmpAdapter::DetachHost() noexcept {
    core_ = nullptr;
    players_ = nullptr;
    vehicles_ = nullptr;
    objects_ = nullptr;
}

bool OmpAdapter::SendControlPacket(uint16_t playerId, const uint8_t* data, std::size_t size) {
    if (!players_ || !data || size == 0) {
        return false;
    }

    IPlayer* player = players_->get(playerId);
    if (!player) {
        return false;
    }

    const std::size_t totalBytes = 1 + size;
    std::array<uint8_t, 1024> stackBuf{};
    if (totalBytes <= stackBuf.size()) {
        stackBuf[0] = protocol::kRakNetPacketIdControl;
        std::memcpy(stackBuf.data() + 1, data, size);
        return player->sendPacket(
            Span<uint8_t>(stackBuf.data(), totalBytes * 8),
            OrderingChannel::OrderingChannel_Reliable
        );
    }

    std::vector<uint8_t> heapBuf(totalBytes);
    heapBuf[0] = protocol::kRakNetPacketIdControl;
    std::memcpy(heapBuf.data() + 1, data, size);
    return player->sendPacket(
        Span<uint8_t>(heapBuf.data(), totalBytes * 8),
        OrderingChannel::OrderingChannel_Reliable
    );
}

uint32_t OmpAdapter::GetPlayerIpv4Net(uint16_t playerId) const {
    if (!players_) {
        return 0;
    }

    IPlayer* player = players_->get(playerId);
    if (!player) {
        return 0;
    }

    const PeerAddress& addr = player->getNetworkData().networkID.address;
    if (!addr.ipv6) {
        return addr.v4;
    }

    // Handle IPv4-mapped IPv6 addresses (::ffff:a.b.c.d)
    for (int i = 0; i < 10; ++i) {
        if (addr.v6.bytes[i] != 0) {
            return 0;
        }
    }
    if (addr.v6.bytes[10] == 0xFF && addr.v6.bytes[11] == 0xFF) {
        uint32_t ipv4Net = 0;
        std::memcpy(&ipv4Net, &addr.v6.bytes[12], sizeof(ipv4Net));
        return ipv4Net;
    }

    return 0;
}

bool OmpAdapter::IsPlayerConnected(uint16_t playerId) const {
    return players_ != nullptr && players_->get(playerId) != nullptr;
}

EntitySpatialState OmpAdapter::GetEntitySpatialState(EntityKind kind, uint32_t targetId) const {
    EntitySpatialState state{};

    switch (kind) {
    case EntityKind::Point:
        // Point streams store fixed coordinates on Stream itself (no host entity, interior, or VW).
        break;
    case EntityKind::Player: {
        if (!players_) break;
        IPlayer* player = players_->get(static_cast<int>(targetId));
        if (!player) break;
        const Vector3 pos = player->getPosition();
        state.x = pos.x;
        state.y = pos.y;
        state.z = pos.z;
        state.virtualWorld = player->getVirtualWorld();
        state.interior = static_cast<int32_t>(player->getInterior());
        state.valid = true;
        break;
    }
    case EntityKind::Vehicle: {
        if (!vehicles_) break;
        IVehicle* vehicle = vehicles_->get(static_cast<int>(targetId));
        if (!vehicle) break;
        const Vector3 pos = vehicle->getPosition();
        state.x = pos.x;
        state.y = pos.y;
        state.z = pos.z;
        state.virtualWorld = vehicle->getVirtualWorld();
        state.interior = vehicle->getInterior();
        state.valid = true;
        break;
    }
    case EntityKind::Object: {
        if (!objects_) break;
        IObject* obj = objects_->get(static_cast<int>(targetId));
        if (!obj) break;
        const Vector3 pos = obj->getPosition();
        state.x = pos.x;
        state.y = pos.y;
        state.z = pos.z;
        state.virtualWorld = obj->getVirtualWorld();
        state.interior = -1;
        state.valid = true;
        break;
    }
    }

    return state;
}

void OmpAdapter::CollectPlayerSnapshots(
    const std::function<void(const PlayerSpatialSnapshot&)>& sink
) const {
    if (!players_) {
        return;
    }

    for (IPlayer* player : players_->players()) {
        if (!player) continue;
        const int id = player->getID();
        if (id < 0 || id >= static_cast<int>(kMaxPlayers)) {
            continue;
        }
        Vector3 pos = player->getPosition();
        int vw = player->getVirtualWorld();
        int32_t interior = static_cast<int32_t>(player->getInterior());

        const PlayerSpectateData& spec = player->getSpectateData();
        if (spec.spectating) {
            if (spec.type == PlayerSpectateData::Player && spec.spectateID >= 0 && spec.spectateID != id) {
                if (IPlayer* target = players_->get(spec.spectateID)) {
                    pos = target->getPosition();
                    vw = target->getVirtualWorld();
                    interior = static_cast<int32_t>(target->getInterior());
                }
            } else if (spec.type == PlayerSpectateData::Vehicle && vehicles_ && spec.spectateID >= 0) {
                if (IVehicle* veh = vehicles_->get(spec.spectateID)) {
                    pos = veh->getPosition();
                    vw = veh->getVirtualWorld();
                }
            }
        }

        PlayerSpatialSnapshot snap{};
        snap.playerId = static_cast<uint16_t>(id);
        snap.x = pos.x;
        snap.y = pos.y;
        snap.z = pos.z;
        snap.virtualWorld = vw;
        snap.interior = interior;
        sink(snap);
    }
}

void OmpAdapter::OnActivationKeyPress(uint16_t playerId, uint8_t keyId) {
    PawnBindings::NotifyActivationKeyPress(playerId, keyId);
}

void OmpAdapter::OnActivationKeyRelease(uint16_t playerId, uint8_t keyId) {
    PawnBindings::NotifyActivationKeyRelease(playerId, keyId);
}

void OmpAdapter::OnMicroStateChange(uint16_t playerId, bool hasMicro) {
    PawnBindings::NotifyMicroStateChange(playerId, hasMicro);
}

int OmpAdapter::GetConfigInt(const std::string& key, int defaultValue) const {
    if (core_) {
        if (int* val = core_->getConfig().getInt(key)) {
            return *val;
        }
    }
    std::string cfg;
    if (fallbackCfg_.TryGet(key, cfg)) {
        long long parsed = 0;
        if (detail::ParseStrictInt(cfg, parsed)) {
            return static_cast<int>(parsed);
        }
    }
    return defaultValue;
}

} // namespace svplus::server::omp
