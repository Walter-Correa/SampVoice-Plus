/*
 * SampVoice Plus - Shared Pawn AMX Native Bindings
 *
 * Exposes all SampVoice Pawn natives and forwards OnPlayerActivationKeyPress /
 * OnPlayerActivationKeyRelease across all loaded AMX scripts. Designed so both
 * the open.mp component and the legacy SA-MP 0.3.7 plugin share identical script
 * semantics.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include <amx/amx.h>

#include "svplus/server/ServerCore.hpp"

namespace svplus::server {

class PawnBindings {
public:
    static void AttachCore(ServerCore* core) noexcept;
    static void DetachCore() noexcept;

    static void RegisterScript(AMX* amx);
    static void UnregisterScript(AMX* amx);

    static void NotifyActivationKeyPress(uint16_t playerId, uint8_t keyId) noexcept;
    static void NotifyActivationKeyRelease(uint16_t playerId, uint8_t keyId) noexcept;
    static void NotifyMicroStateChange(uint16_t playerId, bool hasMicro) noexcept;
    static void NotifyVoiceMute(uint16_t playerId, bool releaseKeys, uint32_t timeMs, bool speaking) noexcept;
    static void NotifyVoiceUnmute(uint16_t playerId, bool expired) noexcept;
    static void NotifyPlayerVoiceReady(uint16_t playerId) noexcept;
    static void NotifyPlayerVoiceStart(uint16_t playerId) noexcept;
    static void NotifyPlayerVoiceStop(uint16_t playerId) noexcept;

private:
    enum class CallbackId : uint8_t {
        ActivationKeyPress = 0,
        ActivationKeyRelease,
        MicroStateChange,
        VoiceMute,
        VoiceUnmute,
        PlayerVoiceReady,
        PlayerVoiceStart,
        PlayerVoiceStop,
        Count
    };

    static constexpr std::size_t kCallbackCount = static_cast<std::size_t>(CallbackId::Count);

    struct WarnState {
        std::chrono::steady_clock::time_point lastLog{};
        bool hasLogged{false};
        uint32_t suppressed{0};
    };

    struct ScriptEntry {
        AMX* amx{nullptr};
        std::array<int, kCallbackCount> publicIdx{};
        std::array<WarnState, kCallbackCount> execWarns{};
    };

    static void InvokeCallback(CallbackId cbId, const char* name, const cell* args, std::size_t count) noexcept;

    static std::mutex mutex_;
    static std::vector<ScriptEntry> scripts_;
};

} // namespace svplus::server
