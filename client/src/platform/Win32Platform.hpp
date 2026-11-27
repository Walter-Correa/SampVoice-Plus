#pragma once

#include "svplus/client/Platform.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace svplus::client::platform {

class Win32ControlTransport final : public IControlTransport {
public:
    [[nodiscard]] bool send(std::uint16_t packetId, const void* data, std::size_t size) override;
    void clearOutgoing() noexcept override;
};

class Win32GameState final : public IGameState {
public:
    [[nodiscard]] bool getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept override;
    [[nodiscard]] bool getLocalPedPosition(float pos[3]) const noexcept override;
    [[nodiscard]] bool getPlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept override;
    [[nodiscard]] bool getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept override;
    [[nodiscard]] bool getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept override;
    [[nodiscard]] bool isLocalPlayerInVehicle(std::uint16_t vehicleId) const noexcept override;

    [[nodiscard]] std::uint32_t nameCacheGeneration() const noexcept override;
    void copyCachedPlayerName(std::uint16_t playerId, std::string& out) const override;

    [[nodiscard]] bool hasOcclusion() const noexcept override { return true; }
    [[nodiscard]] const void* getObjectEntity(std::uint16_t objectId) const noexcept override;
    [[nodiscard]] bool isLineOfSightClear(
        const float origin[3],
        const float target[3],
        const void* ignoreEntity) const noexcept override;

    void decodeLegacyText(std::string_view raw, std::string& out) const override;
};

class Win32KeyInput final : public IKeyInput {
public:
    [[nodiscard]] bool isKeyDown(std::uint8_t keyCode) const noexcept override;
};

} // namespace svplus::client::platform
