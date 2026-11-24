/*
 * SampVoice Plus - Legacy SA-MP 0.3.7 Server Host Adapter
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "SampStructs.hpp"
#include "svplus/server/IServerAdapter.hpp"
#include "svplus/server/ServerConfig.hpp"

namespace svplus::server {
struct ServerStartOptions;
class ServerCore;
}

namespace svplus::server::samp {

class SampAdapter final : public IServerAdapter {
public:
    SampAdapter() = default;

    void SetNetGame(SampNetGame* netGame) noexcept { netGame_ = netGame; }
    SampNetGame* GetNetGame() const noexcept { return netGame_; }
    void Set03DL(bool is03DL) noexcept { is03DL_ = is03DL; }

    void LoadConfigOnce(const std::string& path = "server.cfg");
    bool ResolveStartOptions(ServerStartOptions& outOptions) const;

    void TrackPlayerConnected(uint16_t playerId) noexcept;
    void TrackPlayerDisconnected(uint16_t playerId) noexcept;
    void DetectDisconnects(ServerCore& core) noexcept;

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
    SampNetGame* netGame_{nullptr};
    bool is03DL_{false};
    ServerCfgMap cfgMap_{};
    std::array<bool, kSampMaxPlayers> trackedConnected_{};
};

} // namespace svplus::server::samp
