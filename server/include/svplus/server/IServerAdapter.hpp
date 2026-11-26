/*
 * SampVoice Plus - Abstract Server Backend Adapter Interface
 *
 * Decouples the high-performance voice routing engine from the host server
 * implementation (open.mp native Component vs legacy SA-MP 0.3.7 Plugin).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace svplus::server {

inline constexpr uint16_t kMaxPlayers = 1000u;

enum class EntityKind : uint8_t {
    Point   = 0,
    Player  = 1,
    Vehicle = 2,
    Object  = 3
};

struct EntitySpatialState {
    bool valid{false};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    int32_t interior{0};
    int32_t virtualWorld{0};
};

struct PlayerSpatialSnapshot {
    uint16_t playerId{0};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    int32_t interior{0};
    int32_t virtualWorld{0};
};

class IServerAdapter {
public:
    virtual ~IServerAdapter() = default;

    /**
     * Sends a raw control packet payload (prefixed with RakNet packet ID 222)
     * to the specified player over reliable ordered RakNet channel.
     */
    virtual bool SendControlPacket(uint16_t playerId, const uint8_t* data, std::size_t size) = 0;

    /**
     * Returns the player's connected IPv4 address in network byte order, or 0 if unavailable.
     */
    virtual uint32_t GetPlayerIpv4Net(uint16_t playerId) const = 0;

    /**
     * Returns whether the player is currently connected to the game server.
     */
    virtual bool IsPlayerConnected(uint16_t playerId) const = 0;

    /**
     * Queries the current world position, interior, and virtual world of a player,
     * vehicle, or global object.
     */
    virtual EntitySpatialState GetEntitySpatialState(EntityKind kind, uint32_t targetId) const = 0;

    /**
     * Iterates over all connected players to populate the spatial grid snapshot.
     * Must be called from the server's main tick thread.
     */
    virtual void CollectPlayerSnapshots(const std::function<void(const PlayerSpatialSnapshot&)>& sink) const = 0;

    /**
     * Invokes Pawn callbacks OnPlayerActivationKeyPress / OnPlayerActivationKeyRelease
     * and OnPlayerMicroStateChange (SampVoice Plus v12+).
     */
    virtual void OnActivationKeyPress(uint16_t playerId, uint8_t keyId) = 0;
    virtual void OnActivationKeyRelease(uint16_t playerId, uint8_t keyId) = 0;
    virtual void OnMicroStateChange(uint16_t playerId, bool hasMicro) = 0;
    virtual void OnVoiceMute(uint16_t playerId, bool releaseKeys, uint32_t timeMs, bool speaking) = 0;
    virtual void OnVoiceUnmute(uint16_t playerId, bool expired) = 0;
    virtual void OnPlayerVoiceReady(uint16_t playerId) = 0;
    virtual void OnPlayerVoiceStart(uint16_t playerId) = 0;
    virtual void OnPlayerVoiceStop(uint16_t playerId) = 0;

    /**
     * Reads server configuration integers (omp.json / server.cfg).
     */
    virtual int GetConfigInt(const std::string& key, int defaultValue) const = 0;
};

} // namespace svplus::server
