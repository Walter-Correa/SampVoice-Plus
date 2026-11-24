/*
 * SampVoice Plus - open.mp Server Host Adapter
 *
 * Bridges ICore, IPlayerPool, IVehiclesComponent, IObjectsComponent, and
 * INetwork events to svplus::server::ServerCore.
 */

#pragma once

#include <sdk.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Vehicles/vehicles.hpp>

#include "svplus/server/IServerAdapter.hpp"
#include "svplus/server/ServerConfig.hpp"

namespace svplus::server {
struct ServerStartOptions;
}

namespace svplus::server::omp {

class OmpAdapter final : public IServerAdapter {
public:
    OmpAdapter() = default;

    void AttachHost(ICore* core, IVehiclesComponent* vehicles, IObjectsComponent* objects) noexcept;
    void DetachHost() noexcept;
    bool ResolveStartOptions(ServerStartOptions& outOptions) const;

    bool SendControlPacket(uint16_t playerId, const uint8_t* data, std::size_t size) override;
    uint32_t GetPlayerIpv4Net(uint16_t playerId) const override;
    bool IsPlayerConnected(uint16_t playerId) const override;
    EntitySpatialState GetEntitySpatialState(EntityKind kind, uint32_t targetId) const override;
    void CollectPlayerSnapshots(const std::function<void(const PlayerSpatialSnapshot&)>& sink) const override;

    void OnActivationKeyPress(uint16_t playerId, uint8_t keyId) override;
    void OnActivationKeyRelease(uint16_t playerId, uint8_t keyId) override;
    void OnMicroStateChange(uint16_t playerId, bool hasMicro) override;

    int GetConfigInt(const std::string& key, int defaultValue) const override;

private:
    ICore* core_{nullptr};
    IPlayerPool* players_{nullptr};
    IVehiclesComponent* vehicles_{nullptr};
    IObjectsComponent* objects_{nullptr};
    ServerCfgMap fallbackCfg_{};
};

} // namespace svplus::server::omp
