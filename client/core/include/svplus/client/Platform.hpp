#pragma once

#include "svplus/client/BassApi.hpp"
#include "svplus/client/Utf8Util.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace svplus::client {

inline std::atomic<bool> g_processShuttingDown{false};

[[nodiscard]] inline bool isProcessShuttingDown() noexcept {
    return g_processShuttingDown.load(std::memory_order_acquire);
}

inline void markProcessShuttingDown() noexcept {
    g_processShuttingDown.store(true, std::memory_order_release);
}

/// Reliable control-channel transport (RakClient Packet 222 on PC; JNI/client socket on mobile).
class IControlTransport {
public:
    virtual ~IControlTransport() = default;

    [[nodiscard]] virtual bool send(std::uint16_t packetId, const void* data, std::size_t size) = 0;
    virtual void clearOutgoing() noexcept = 0;
};

/// Game world, camera, entity, and player-name queries needed by spatial playback and telemetry.
class IGameState {
public:
    virtual ~IGameState() = default;

    [[nodiscard]] virtual bool getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept = 0;
    [[nodiscard]] virtual bool getLocalPedPosition(float pos[3]) const noexcept = 0;
    [[nodiscard]] virtual bool getPlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept = 0;
    [[nodiscard]] virtual bool getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept = 0;
    [[nodiscard]] virtual bool getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept = 0;
    [[nodiscard]] virtual bool isLocalPlayerInVehicle(std::uint16_t vehicleId) const noexcept = 0;

    [[nodiscard]] virtual std::uint32_t nameCacheGeneration() const noexcept = 0;
    virtual void copyCachedPlayerName(std::uint16_t playerId, std::string& out) const = 0;

    /// Optional world-occlusion queries; when false, PlaybackEngine never occludes streams.
    [[nodiscard]] virtual bool hasOcclusion() const noexcept { return false; }
    [[nodiscard]] virtual const void* getObjectEntity(std::uint16_t /*objectId*/) const noexcept { return nullptr; }
    [[nodiscard]] virtual bool isLineOfSightClear(
        const float /*origin*/[3],
        const float /*target*/[3],
        const void* /*ignoreEntity*/) const noexcept {
        return true;
    }

    /// Converts legacy server control strings to UTF-8 (e.g. Win32 CP_ACP on PC; portable UTF-8/Latin-1 fallback elsewhere).
    virtual void decodeLegacyText(std::string_view raw, std::string& out) const {
        util::ensureUtf8FallbackInto(raw, out);
    }
};

/// Physical key/button polling for capture activation keys.
class IKeyInput {
public:
    virtual ~IKeyInput() = default;

    [[nodiscard]] virtual bool isKeyDown(std::uint8_t keyCode) const noexcept = 0;
};

/// Platform loader/lifecycle wrapper around BASS 2.4 C function pointers.
class IBassLibrary {
public:
    virtual ~IBassLibrary() = default;

    [[nodiscard]] virtual bool initialize() noexcept = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual bool pollReady() noexcept = 0;

    [[nodiscard]] virtual bool isLoaded() const noexcept = 0;
    [[nodiscard]] virtual bool isAudioReady() const noexcept = 0;
    [[nodiscard]] virtual const audio::BassApi& api() const noexcept = 0;
};

} // namespace svplus::client
