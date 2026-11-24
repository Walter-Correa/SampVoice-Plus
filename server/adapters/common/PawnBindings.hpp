/*
 * SampVoice Plus - Shared Pawn AMX Native Bindings
 *
 * Exposes all SampVoice Pawn natives and forwards OnPlayerActivationKeyPress /
 * OnPlayerActivationKeyRelease across all loaded AMX scripts. Designed so both
 * the open.mp component and the legacy SA-MP 0.3.7 plugin share identical script
 * semantics.
 */

#pragma once

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

private:
    static void InvokeTwoArgCallback(const char* name, cell arg0, cell arg1) noexcept;

    static std::mutex mutex_;
    static std::vector<AMX*> scripts_;
};

} // namespace svplus::server
