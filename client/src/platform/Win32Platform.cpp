#include "platform/Win32Platform.hpp"

#include "samp/SampBridge.hpp"
#include "util/Utf8Util.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace svplus::client::platform {

bool Win32ControlTransport::send(std::uint16_t packetId, const void* data, std::size_t size) {
    if (size > 0xFFFFu) {
        return false;
    }
    return samp::SampBridge::instance().sendControlPacket(
        packetId, data, static_cast<std::uint16_t>(size));
}

void Win32ControlTransport::clearOutgoing() noexcept {
    samp::SampBridge::instance().clearOutgoingControlQueue();
}

bool Win32GameState::getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept {
    return samp::SampBridge::instance().getCameraBasis(pos, front, top);
}

bool Win32GameState::getLocalPedPosition(float pos[3]) const noexcept {
    return samp::SampBridge::instance().getLocalPlayerPedPosition(pos);
}

bool Win32GameState::getPlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept {
    return samp::SampBridge::instance().getRemotePlayerPosition(playerId, pos);
}

bool Win32GameState::getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept {
    return samp::SampBridge::instance().getVehiclePosition(vehicleId, pos);
}

bool Win32GameState::getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept {
    return samp::SampBridge::instance().getObjectPosition(objectId, pos);
}

bool Win32GameState::isLocalPlayerInVehicle(std::uint16_t vehicleId) const noexcept {
    return samp::SampBridge::instance().isLocalPlayerInVehicle(vehicleId);
}

std::uint32_t Win32GameState::nameCacheGeneration() const noexcept {
    return samp::SampBridge::instance().nameCacheGeneration();
}

void Win32GameState::copyCachedPlayerName(std::uint16_t playerId, std::string& out) const {
    samp::SampBridge::instance().copyCachedPlayerName(playerId, out);
}

const void* Win32GameState::getObjectEntity(std::uint16_t objectId) const noexcept {
    return samp::SampBridge::instance().getObjectEntity(objectId);
}

bool Win32GameState::isLineOfSightClear(
    const float origin[3],
    const float target[3],
    const void* ignoreEntity) const noexcept {
    return samp::SampBridge::instance().checkLineOfSightClear(origin, target, ignoreEntity);
}

void Win32GameState::decodeLegacyText(std::string_view raw, std::string& out) const {
    util::ensureUtf8NameInto(raw, out);
}

bool Win32KeyInput::isKeyDown(std::uint8_t keyCode) const noexcept {
    return (GetAsyncKeyState(keyCode) & 0x8000) != 0;
}

} // namespace svplus::client::platform
