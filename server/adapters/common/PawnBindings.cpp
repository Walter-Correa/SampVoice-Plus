/*
 * SampVoice Plus - Shared Pawn AMX Native Bindings
 */

#include "PawnBindings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_map>

#include "svplus/common/Logger.hpp"
#include "svplus/common/Protocol.hpp"

namespace svplus::server {

std::mutex PawnBindings::mutex_;
std::vector<PawnBindings::ScriptEntry> PawnBindings::scripts_;

namespace {

std::atomic<ServerCore*> gActiveCore{nullptr};
constexpr auto kRateLimitWindow = std::chrono::seconds(60);

struct NativeWarnKey {
    const AMX* amx{nullptr};
    const char* nativeName{nullptr};

    bool operator==(const NativeWarnKey& o) const noexcept {
        return amx == o.amx && nativeName == o.nativeName;
    }
};

struct NativeWarnKeyHash {
    std::size_t operator()(const NativeWarnKey& k) const noexcept {
        const auto h1 = std::hash<const void*>{}(k.amx);
        const auto h2 = std::hash<const void*>{}(k.nativeName);
        return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6u) + (h1 >> 2u));
    }
};

struct NativeWarnState {
    std::chrono::steady_clock::time_point lastLog{};
    bool hasLogged{false};
    uint32_t suppressed{0};
};

std::mutex gNativeWarnMutex;
std::unordered_map<NativeWarnKey, NativeWarnState, NativeWarnKeyHash> gNativeWarnWindows;

const char* FormatBadFloat(float v, char* buf, std::size_t bufSize) noexcept {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return (v > 0.0f) ? "+Inf" : "-Inf";
    }
    std::snprintf(buf, bufSize, "%.4g", static_cast<double>(v));
    return buf;
}

bool ShouldLogNativeWarning(const AMX* amx, const char* nativeName, uint32_t& outSuppressedCount) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(gNativeWarnMutex);
    auto& st = gNativeWarnWindows[NativeWarnKey{amx, nativeName}];
    if (st.hasLogged && (now - st.lastLog) < kRateLimitWindow) {
        ++st.suppressed;
        return false;
    }
    outSuppressedCount = st.suppressed;
    st.lastLog = now;
    st.hasLogged = true;
    st.suppressed = 0;
    return true;
}

void WarnInvalidNativeFloat(const AMX* amx, const char* nativeName, const char* reason, const char* argName, float val) {
    uint32_t suppressedCount = 0;
    if (!ShouldLogNativeWarning(amx, nativeName, suppressedCount)) {
        return;
    }

    char valBuf[32]{};
    const char* valStr = FormatBadFloat(val, valBuf, sizeof(valBuf));
    if (suppressedCount > 0) {
        log::Logger::Log(
            log::Level::Warn,
            "%s: rejected %s %s (%s) (%u suppressed in last 60s)",
            nativeName, reason, argName, valStr, suppressedCount
        );
    } else {
        log::Logger::Log(
            log::Level::Warn,
            "%s: rejected %s %s (%s)",
            nativeName, reason, argName, valStr
        );
    }
}

void WarnInvalidNativeInt(const AMX* amx, const char* nativeName, const char* reason, const char* argName, int32_t val) {
    uint32_t suppressedCount = 0;
    if (!ShouldLogNativeWarning(amx, nativeName, suppressedCount)) {
        return;
    }

    if (suppressedCount > 0) {
        log::Logger::Log(
            log::Level::Warn,
            "%s: rejected %s %s (%d) (%u suppressed in last 60s)",
            nativeName, reason, argName, val, suppressedCount
        );
    } else {
        log::Logger::Log(
            log::Level::Warn,
            "%s: rejected %s %s (%d)",
            nativeName, reason, argName, val
        );
    }
}

bool ValidateWorldOrInterior(const AMX* amx, const char* nativeName, const char* argName, int32_t val) {
    if (val < -1) {
        WarnInvalidNativeInt(amx, nativeName, "invalid", argName, val);
        return false;
    }
    return true;
}

bool TryGetAmxArray(AMX* amx, cell amxAddr, cell sizeCell, cell*& outPhys, std::size_t& outCap) noexcept {
    if (amx == nullptr || sizeCell <= 0) {
        return false;
    }
    const auto cap = static_cast<std::size_t>(sizeCell);
    constexpr std::size_t kMaxSpan = static_cast<std::size_t>(std::numeric_limits<cell>::max()) / sizeof(cell);
    if (cap - 1u > kMaxSpan) {
        return false;
    }
    cell* startPhys = nullptr;
    if (amx_GetAddr(amx, amxAddr, &startPhys) != AMX_ERR_NONE || startPhys == nullptr) {
        return false;
    }
    const cell endOffset = static_cast<cell>((cap - 1u) * sizeof(cell));
    if (amxAddr > std::numeric_limits<cell>::max() - endOffset) {
        return false;
    }
    cell* endPhys = nullptr;
    if (amx_GetAddr(amx, amxAddr + endOffset, &endPhys) != AMX_ERR_NONE || endPhys == nullptr) {
        return false;
    }
    if (endPhys != startPhys + (cap - 1u)) {
        return false;
    }
    outPhys = startPhys;
    outCap = cap;
    return true;
}

bool ValidateFiniteFloat(const AMX* amx, const char* nativeName, const char* argName, float val) {
    if (!std::isfinite(val)) {
        WarnInvalidNativeFloat(amx, nativeName, "non-finite", argName, val);
        return false;
    }
    return true;
}

bool ValidateDistance(const AMX* amx, const char* nativeName, float distance) {
    if (!ValidateFiniteFloat(amx, nativeName, "distance", distance)) {
        return false;
    }
    if (distance < 0.0f) {
        WarnInvalidNativeFloat(amx, nativeName, "negative", "distance", distance);
        return false;
    }
    return true;
}

bool ValidateCoords(const AMX* amx, const char* nativeName, float x, float y, float z) {
    return ValidateFiniteFloat(amx, nativeName, "posx", x) &&
           ValidateFiniteFloat(amx, nativeName, "posy", y) &&
           ValidateFiniteFloat(amx, nativeName, "posz", z);
}

bool ValidateParameterValue(const AMX* amx, const char* nativeName, uint32_t paramId, const char* argName, float val) {
    if (!ValidateFiniteFloat(amx, nativeName, argName, val)) {
        return false;
    }
    using PT = protocol::ParameterType;
    if (paramId == static_cast<uint32_t>(PT::Volume) && val < 0.0f) {
        WarnInvalidNativeFloat(amx, nativeName, "negative", argName, val);
        return false;
    }
    if (paramId == static_cast<uint32_t>(PT::Frequency) && val < 0.0f) {
        WarnInvalidNativeFloat(amx, nativeName, "negative", argName, val);
        return false;
    }
    return true;
}

struct FloatField {
    const char* name;
    float val;
};

bool ValidateFloatFields(const AMX* amx, const char* nativeName, std::initializer_list<FloatField> fields) {
    for (const auto& f : fields) {
        if (!ValidateFiniteFloat(amx, nativeName, f.name, f.val)) {
            return false;
        }
    }
    return true;
}

inline constexpr cell kMaxValidVehicleId = 2000;
inline constexpr cell kMaxValidObjectId  = 2100;

inline bool TryGetPlayerId(cell c, uint16_t& out) noexcept {
    if (c < 0 || c >= static_cast<cell>(kMaxPlayers)) {
        return false;
    }
    out = static_cast<uint16_t>(c);
    return true;
}

inline bool TryGetVehicleId(cell c, uint16_t& out) noexcept {
    if (c < 1 || c > kMaxValidVehicleId) {
        return false;
    }
    out = static_cast<uint16_t>(c);
    return true;
}

inline bool TryGetObjectId(cell c, uint16_t& out) noexcept {
    if (c < 1 || c > kMaxValidObjectId) {
        return false;
    }
    out = static_cast<uint16_t>(c);
    return true;
}

inline bool TryGetKeyId(cell c, uint8_t& out) noexcept {
    if (c < 0 || c > 0xFF) {
        return false;
    }
    out = static_cast<uint8_t>(c);
    return true;
}

inline bool CheckParamCount(const cell* params, int expectedCount) noexcept {
    return params != nullptr && params[0] == static_cast<cell>(expectedCount * sizeof(cell));
}

inline float CellToFloat(cell c) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &c, sizeof(f));
    return f;
}

inline cell FloatToCell(float f) noexcept {
    cell c = 0;
    std::memcpy(&c, &f, sizeof(f));
    return c;
}

std::string ReadAmxString(AMX* amx, cell amxAddr) {
    cell* physAddr = nullptr;
    int len = 0;
    if (amx_GetAddr(amx, amxAddr, &physAddr) != AMX_ERR_NONE || physAddr == nullptr) {
        return {};
    }
    if (amx_StrLen(physAddr, &len) != AMX_ERR_NONE || len <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(len) + 1, '\0');
    if (amx_GetString(out.data(), physAddr, 0, len + 1) != AMX_ERR_NONE) {
        return {};
    }
    out.resize(static_cast<std::size_t>(len));
    return out;
}

#define SV_GET_CORE_OR_RET(retVal)                                             \
    ServerCore* const core = gActiveCore.load(std::memory_order_acquire);      \
    if (core == nullptr) return (retVal)

cell AMX_NATIVE_CALL n_SvDebug(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    core->SvDebug(params[1] != 0);
    return 1;
}

cell AMX_NATIVE_CALL n_SvInit(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    core->SvInit(static_cast<uint32_t>(params[1]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvGetVersion(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return static_cast<cell>(core->SvGetVersion(playerId));
}

cell AMX_NATIVE_CALL n_SvGetClientProtocol(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return static_cast<cell>(core->SvGetClientProtocol(playerId));
}

cell AMX_NATIVE_CALL n_SvGetPlusCapabilities(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return static_cast<cell>(core->SvGetPlusCapabilities(playerId));
}

cell AMX_NATIVE_CALL n_SvGetPlusClientVersion(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return static_cast<cell>(core->SvGetPlusClientVersion(playerId));
}

cell AMX_NATIVE_CALL n_SvIsPlayerVoiceReady(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvIsPlayerVoiceReady(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvIsPlayerSpeaking(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvIsPlayerSpeaking(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvGetPlayerVoiceStatus(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return -1;
    return static_cast<cell>(core->SvGetPlayerVoiceStatus(playerId));
}

cell AMX_NATIVE_CALL n_SvGetPlayerVoiceLatency(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return -1;
    return static_cast<cell>(core->SvGetPlayerVoiceLatency(playerId));
}

cell AMX_NATIVE_CALL n_SvHasMicro(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvHasMicro(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStartRecord(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvStartRecord(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStopRecord(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvStopRecord(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvAddKey(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    uint8_t keyId = 0;
    if (!TryGetPlayerId(params[1], playerId) || !TryGetKeyId(params[2], keyId)) return 0;
    return core->SvAddKey(playerId, keyId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvHasKey(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    uint8_t keyId = 0;
    if (!TryGetPlayerId(params[1], playerId) || !TryGetKeyId(params[2], keyId)) return 0;
    return core->SvHasKey(playerId, keyId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvRemoveKey(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    uint8_t keyId = 0;
    if (!TryGetPlayerId(params[1], playerId) || !TryGetKeyId(params[2], keyId)) return 0;
    return core->SvRemoveKey(playerId, keyId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvRemoveAllKeys(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvRemoveAllKeys(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvMutePlayerStatus(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvMutePlayerStatus(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvMutePlayerEnable(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!params) return 0;
    const cell count = params[0] / static_cast<cell>(sizeof(cell));
    if (count < 1 || count > 3) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    const bool releaseKeys = (count >= 2) ? (params[2] != 0) : false;
    const uint32_t timeMs = (count >= 3) ? static_cast<uint32_t>(params[3]) : 0u;
    return core->SvMutePlayerEnable(playerId, releaseKeys, timeMs) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvMutePlayerDisable(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvMutePlayerDisable(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvMutePlayerRemaining(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return -1;
    return static_cast<cell>(core->SvMutePlayerRemaining(playerId));
}

cell AMX_NATIVE_CALL n_SvEnableListener(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvEnableListener(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvDisableListener(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvDisableListener(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvCheckListener(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    return core->SvCheckListener(playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvMutePlayerForListener(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t speakerId = 0;
    uint16_t listenerId = 0;
    if (!TryGetPlayerId(params[1], speakerId) || !TryGetPlayerId(params[2], listenerId)) return 0;
    return core->SvMutePlayerForListener(speakerId, listenerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvUnmutePlayerForListener(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t speakerId = 0;
    uint16_t listenerId = 0;
    if (!TryGetPlayerId(params[1], speakerId) || !TryGetPlayerId(params[2], listenerId)) return 0;
    return core->SvUnmutePlayerForListener(speakerId, listenerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvIsPlayerMutedForListener(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t speakerId = 0;
    uint16_t listenerId = 0;
    if (!TryGetPlayerId(params[1], speakerId) || !TryGetPlayerId(params[2], listenerId)) return 0;
    return core->SvIsPlayerMutedForListener(speakerId, listenerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvCreateGStream(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return static_cast<cell>(core->SvCreateGStream(
        static_cast<uint32_t>(params[1]),
        ReadAmxString(amx, params[2]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtPoint(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 6)) return 0;
    const float dist = CellToFloat(params[1]);
    const float x = CellToFloat(params[2]);
    const float y = CellToFloat(params[3]);
    const float z = CellToFloat(params[4]);
    if (!ValidateDistance(amx, "SvCreateSLStreamAtPoint", dist) ||
        !ValidateCoords(amx, "SvCreateSLStreamAtPoint", x, y, z)) {
        return 0;
    }
    return static_cast<cell>(core->SvCreateSLStreamAtPoint(
        dist, x, y, z,
        static_cast<uint32_t>(params[5]),
        ReadAmxString(amx, params[6]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtVehicle(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    const float dist = CellToFloat(params[1]);
    if (!ValidateDistance(amx, "SvCreateSLStreamAtVehicle", dist)) return 0;
    uint16_t vehicleId = 0;
    if (!TryGetVehicleId(params[2], vehicleId)) return 0;
    return static_cast<cell>(core->SvCreateSLStreamAtVehicle(
        dist,
        vehicleId,
        static_cast<uint32_t>(params[3]),
        ReadAmxString(amx, params[4]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtPlayer(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    const float dist = CellToFloat(params[1]);
    if (!ValidateDistance(amx, "SvCreateSLStreamAtPlayer", dist)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return static_cast<cell>(core->SvCreateSLStreamAtPlayer(
        dist,
        playerId,
        static_cast<uint32_t>(params[3]),
        ReadAmxString(amx, params[4]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtObject(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    const float dist = CellToFloat(params[1]);
    if (!ValidateDistance(amx, "SvCreateSLStreamAtObject", dist)) return 0;
    uint16_t objectId = 0;
    if (!TryGetObjectId(params[2], objectId)) return 0;
    return static_cast<cell>(core->SvCreateSLStreamAtObject(
        dist,
        objectId,
        static_cast<uint32_t>(params[3]),
        ReadAmxString(amx, params[4]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtPoint(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!params) return 0;
    const cell count = params[0] / static_cast<cell>(sizeof(cell));
    if (count != 7 && count != 9) return 0;
    const float dist = CellToFloat(params[1]);
    const float x = CellToFloat(params[3]);
    const float y = CellToFloat(params[4]);
    const float z = CellToFloat(params[5]);
    if (!ValidateDistance(amx, "SvCreateDLStreamAtPoint", dist) ||
        !ValidateCoords(amx, "SvCreateDLStreamAtPoint", x, y, z)) {
        return 0;
    }
    int32_t worldId = -1;
    int32_t interiorId = -1;
    if (count == 9) {
        worldId = static_cast<int32_t>(params[8]);
        interiorId = static_cast<int32_t>(params[9]);
        if (!ValidateWorldOrInterior(amx, "SvCreateDLStreamAtPoint", "worldid", worldId) ||
            !ValidateWorldOrInterior(amx, "SvCreateDLStreamAtPoint", "interiorid", interiorId)) {
            return 0;
        }
    }
    return static_cast<cell>(core->SvCreateDLStreamAtPoint(
        dist,
        static_cast<uint32_t>(params[2]),
        x, y, z,
        static_cast<uint32_t>(params[6]),
        ReadAmxString(amx, params[7]),
        amx,
        worldId,
        interiorId));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtVehicle(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    const float dist = CellToFloat(params[1]);
    if (!ValidateDistance(amx, "SvCreateDLStreamAtVehicle", dist)) return 0;
    uint16_t vehicleId = 0;
    if (!TryGetVehicleId(params[3], vehicleId)) return 0;
    return static_cast<cell>(core->SvCreateDLStreamAtVehicle(
        dist,
        static_cast<uint32_t>(params[2]),
        vehicleId,
        static_cast<uint32_t>(params[4]),
        ReadAmxString(amx, params[5]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtPlayer(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    const float dist = CellToFloat(params[1]);
    if (!ValidateDistance(amx, "SvCreateDLStreamAtPlayer", dist)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[3], playerId)) return 0;
    return static_cast<cell>(core->SvCreateDLStreamAtPlayer(
        dist,
        static_cast<uint32_t>(params[2]),
        playerId,
        static_cast<uint32_t>(params[4]),
        ReadAmxString(amx, params[5]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtObject(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!params) return 0;
    const cell count = params[0] / static_cast<cell>(sizeof(cell));
    if (count != 5 && count != 7) return 0;
    const float dist = CellToFloat(params[1]);
    if (!ValidateDistance(amx, "SvCreateDLStreamAtObject", dist)) return 0;
    uint16_t objectId = 0;
    if (!TryGetObjectId(params[3], objectId)) return 0;
    int32_t worldId = -1;
    int32_t interiorId = -1;
    if (count == 7) {
        worldId = static_cast<int32_t>(params[6]);
        interiorId = static_cast<int32_t>(params[7]);
        if (!ValidateWorldOrInterior(amx, "SvCreateDLStreamAtObject", "worldid", worldId) ||
            !ValidateWorldOrInterior(amx, "SvCreateDLStreamAtObject", "interiorid", interiorId)) {
            return 0;
        }
    }
    return static_cast<cell>(core->SvCreateDLStreamAtObject(
        dist,
        static_cast<uint32_t>(params[2]),
        objectId,
        static_cast<uint32_t>(params[4]),
        ReadAmxString(amx, params[5]),
        amx,
        worldId,
        interiorId));
}

cell AMX_NATIVE_CALL n_SvUpdateDistanceForLStream(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    const float dist = CellToFloat(params[2]);
    if (!ValidateDistance(amx, "SvUpdateDistanceForLStream", dist)) return 0;
    return core->SvUpdateDistanceForLStream(static_cast<uint32_t>(params[1]), dist) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvUpdatePositionForLPStream(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    const float x = CellToFloat(params[2]);
    const float y = CellToFloat(params[3]);
    const float z = CellToFloat(params[4]);
    if (!ValidateCoords(amx, "SvUpdatePositionForLPStream", x, y, z)) return 0;
    return core->SvUpdatePositionForLPStream(static_cast<uint32_t>(params[1]), x, y, z) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvAttachListenerToStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!params) return 0;
    const cell count = params[0] / static_cast<cell>(sizeof(cell));
    if (count != 2 && count != 3) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    if (count == 3) {
        return core->SvAttachListenerToStream(
            static_cast<uint32_t>(params[1]), playerId, CellToFloat(params[3])) ? 1 : 0;
    }
    return core->SvAttachListenerToStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamSetListenerVolume(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvStreamSetListenerVolume(
        static_cast<uint32_t>(params[1]), playerId, CellToFloat(params[3])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamGetListenerVolume(AMX*, cell* params) {
    const cell invalidRet = FloatToCell(-1.0f);
    SV_GET_CORE_OR_RET(invalidRet);
    if (!CheckParamCount(params, 2)) return invalidRet;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return invalidRet;
    return FloatToCell(core->SvStreamGetListenerVolume(static_cast<uint32_t>(params[1]), playerId));
}

cell AMX_NATIVE_CALL n_SvGetStreamListenerCount(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    return static_cast<cell>(core->SvGetStreamListenerCount(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvGetStreamSpeakerCount(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    return static_cast<cell>(core->SvGetStreamSpeakerCount(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvIsValidStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return core->SvIsValidStream(static_cast<uint32_t>(params[1])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvHasListenerInStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvHasListenerInStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvDetachListenerFromStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvDetachListenerFromStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvDetachAllListenersFromStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return core->SvDetachAllListenersFromStream(static_cast<uint32_t>(params[1])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvAttachSpeakerToStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvAttachSpeakerToStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvHasSpeakerInStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvHasSpeakerInStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvDetachSpeakerFromStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvDetachSpeakerFromStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvDetachAllSpeakersFromStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return core->SvDetachAllSpeakersFromStream(static_cast<uint32_t>(params[1])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamParameterSet(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    const uint32_t streamId = static_cast<uint32_t>(params[1]);
    const uint32_t paramId = static_cast<uint32_t>(params[2]);
    const float val = CellToFloat(params[3]);
    if (!ValidateParameterValue(amx, "SvStreamParameterSet", paramId, "value", val)) return 0;
    return core->SvStreamParameterSet(streamId, paramId, val) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamParameterReset(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvStreamParameterReset(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamParameterHas(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvStreamParameterHas(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamParameterGet(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return FloatToCell(core->SvStreamParameterGet(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2])));
}

cell AMX_NATIVE_CALL n_SvStreamParameterSlideFromTo(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    const uint32_t streamId = static_cast<uint32_t>(params[1]);
    const uint32_t paramId = static_cast<uint32_t>(params[2]);
    const float startVal = CellToFloat(params[3]);
    const float endVal = CellToFloat(params[4]);
    if (!ValidateParameterValue(amx, "SvStreamParameterSlideFromTo", paramId, "startvalue", startVal) ||
        !ValidateParameterValue(amx, "SvStreamParameterSlideFromTo", paramId, "endvalue", endVal)) {
        return 0;
    }
    return core->SvStreamParameterSlideFromTo(
        streamId, paramId, startVal, endVal, static_cast<uint32_t>(params[5])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamParameterSlideTo(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    const uint32_t streamId = static_cast<uint32_t>(params[1]);
    const uint32_t paramId = static_cast<uint32_t>(params[2]);
    const float endVal = CellToFloat(params[3]);
    if (!ValidateParameterValue(amx, "SvStreamParameterSlideTo", paramId, "endvalue", endVal)) return 0;
    return core->SvStreamParameterSlideTo(
        streamId, paramId, endVal, static_cast<uint32_t>(params[4])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamParameterSlide(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    const float deltaVal = CellToFloat(params[3]);
    if (!ValidateFiniteFloat(amx, "SvStreamParameterSlide", "deltavalue", deltaVal)) return 0;
    return core->SvStreamParameterSlide(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2]),
        deltaVal,
        static_cast<uint32_t>(params[4])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamSetIcon(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvStreamSetIcon(
        static_cast<uint32_t>(params[1]),
        ReadAmxString(amx, params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamSetName(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    cell* strPhys = nullptr;
    if (amx_GetAddr(amx, params[2], &strPhys) != AMX_ERR_NONE || strPhys == nullptr) {
        return 0;
    }
    return core->SvStreamSetName(
        static_cast<uint32_t>(params[1]),
        ReadAmxString(amx, params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamSetColor(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvStreamSetColor(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvStreamGetName(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    cell* destPhys = nullptr;
    std::size_t cap = 0;
    if (!TryGetAmxArray(amx, params[2], params[3], destPhys, cap)) return 0;
    std::string name;
    if (!core->SvStreamGetName(static_cast<uint32_t>(params[1]), name)) {
        return 0;
    }
    amx_SetString(destPhys, name.c_str(), 0, 0, cap);
    return 1;
}

cell AMX_NATIVE_CALL n_SvStreamGetColor(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return static_cast<cell>(core->SvStreamGetColor(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvGetStreamType(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    return static_cast<cell>(core->SvGetStreamType(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvGetStreamDistance(AMX*, cell* params) {
    const cell invalidRet = FloatToCell(-1.0f);
    SV_GET_CORE_OR_RET(invalidRet);
    if (!CheckParamCount(params, 1)) return invalidRet;
    return FloatToCell(core->SvGetStreamDistance(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvGetStreamPosition(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    cell* px = nullptr;
    cell* py = nullptr;
    cell* pz = nullptr;
    if (amx_GetAddr(amx, params[2], &px) != AMX_ERR_NONE || px == nullptr ||
        amx_GetAddr(amx, params[3], &py) != AMX_ERR_NONE || py == nullptr ||
        amx_GetAddr(amx, params[4], &pz) != AMX_ERR_NONE || pz == nullptr) {
        return 0;
    }
    protocol::Vec3f pos{};
    if (!core->SvGetStreamPosition(static_cast<uint32_t>(params[1]), pos)) {
        return 0;
    }
    *px = FloatToCell(pos.x);
    *py = FloatToCell(pos.y);
    *pz = FloatToCell(pos.z);
    return 1;
}

cell AMX_NATIVE_CALL n_SvGetStreamTarget(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    return static_cast<cell>(core->SvGetStreamTarget(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvGetStreamMaxPlayers(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-1);
    if (!CheckParamCount(params, 1)) return -1;
    return static_cast<cell>(core->SvGetStreamMaxPlayers(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvStreamParameterGetEx(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    cell* pval = nullptr;
    if (amx_GetAddr(amx, params[3], &pval) != AMX_ERR_NONE || pval == nullptr) {
        return 0;
    }
    float value = 0.0f;
    if (!core->SvStreamParameterGetEx(
            static_cast<uint32_t>(params[1]),
            static_cast<uint32_t>(params[2]),
            value)) {
        return 0;
    }
    *pval = FloatToCell(value);
    return 1;
}

cell AMX_NATIVE_CALL n_SvGetStreamListeners(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    cell* destPhys = nullptr;
    std::size_t cap = 0;
    if (!TryGetAmxArray(amx, params[2], params[3], destPhys, cap)) return 0;
    std::vector<uint16_t> tmp(cap);
    const std::size_t written = core->SvCopyStreamListeners(static_cast<uint32_t>(params[1]), tmp.data(), cap);
    for (std::size_t i = 0; i < written; ++i) {
        destPhys[i] = static_cast<cell>(tmp[i]);
    }
    return static_cast<cell>(written);
}

cell AMX_NATIVE_CALL n_SvGetStreamSpeakers(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    cell* destPhys = nullptr;
    std::size_t cap = 0;
    if (!TryGetAmxArray(amx, params[2], params[3], destPhys, cap)) return 0;
    std::vector<uint16_t> tmp(cap);
    const std::size_t written = core->SvCopyStreamSpeakers(static_cast<uint32_t>(params[1]), tmp.data(), cap);
    for (std::size_t i = 0; i < written; ++i) {
        destPhys[i] = static_cast<cell>(tmp[i]);
    }
    return static_cast<cell>(written);
}

cell AMX_NATIVE_CALL n_SvGetPlayerListenerStreams(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    cell* destPhys = nullptr;
    std::size_t cap = 0;
    if (!TryGetAmxArray(amx, params[2], params[3], destPhys, cap)) return 0;
    std::vector<uint32_t> tmp(cap);
    const std::size_t written = core->SvCopyPlayerListenerStreams(playerId, tmp.data(), cap);
    for (std::size_t i = 0; i < written; ++i) {
        destPhys[i] = static_cast<cell>(tmp[i]);
    }
    return static_cast<cell>(written);
}

cell AMX_NATIVE_CALL n_SvGetPlayerSpeakerStreams(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    cell* destPhys = nullptr;
    std::size_t cap = 0;
    if (!TryGetAmxArray(amx, params[2], params[3], destPhys, cap)) return 0;
    std::vector<uint32_t> tmp(cap);
    const std::size_t written = core->SvCopyPlayerSpeakerStreams(playerId, tmp.data(), cap);
    for (std::size_t i = 0; i < written; ++i) {
        destPhys[i] = static_cast<cell>(tmp[i]);
    }
    return static_cast<cell>(written);
}

cell AMX_NATIVE_CALL n_SvStreamSetWorld(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    const int32_t worldId = static_cast<int32_t>(params[2]);
    const int32_t interiorId = static_cast<int32_t>(params[3]);
    if (!ValidateWorldOrInterior(amx, "SvStreamSetWorld", "worldid", worldId) ||
        !ValidateWorldOrInterior(amx, "SvStreamSetWorld", "interiorid", interiorId)) {
        return 0;
    }
    return core->SvStreamSetWorld(static_cast<uint32_t>(params[1]), worldId, interiorId) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvGetStreamWorld(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-2);
    if (!CheckParamCount(params, 1)) return -2;
    return static_cast<cell>(core->SvGetStreamWorld(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvGetStreamInterior(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(-2);
    if (!CheckParamCount(params, 1)) return -2;
    return static_cast<cell>(core->SvGetStreamInterior(static_cast<uint32_t>(params[1])));
}

cell AMX_NATIVE_CALL n_SvDeleteStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return core->SvDeleteStream(static_cast<uint32_t>(params[1])) ? 1 : 0;
}

namespace {

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::ChorusParams& p) {
    p.wetDryMix = CellToFloat(params[2]);
    p.depth = CellToFloat(params[3]);
    p.feedback = CellToFloat(params[4]);
    p.frequency = CellToFloat(params[5]);
    p.waveform = static_cast<uint32_t>(params[6]);
    p.delay = CellToFloat(params[7]);
    p.phase = static_cast<uint32_t>(params[8]);
    return ValidateFloatFields(amx, nativeName, {
        {"wetdrymix", p.wetDryMix}, {"depth", p.depth}, {"feedback", p.feedback},
        {"frequency", p.frequency}, {"delay", p.delay}});
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::CompressorParams& p) {
    p.gain = CellToFloat(params[2]);
    p.attack = CellToFloat(params[3]);
    p.release = CellToFloat(params[4]);
    p.threshold = CellToFloat(params[5]);
    p.ratio = CellToFloat(params[6]);
    p.predelay = CellToFloat(params[7]);
    return ValidateFloatFields(amx, nativeName, {
        {"gain", p.gain}, {"attack", p.attack}, {"release", p.release},
        {"threshold", p.threshold}, {"ratio", p.ratio}, {"predelay", p.predelay}});
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::DistortionParams& p) {
    p.gain = CellToFloat(params[2]);
    p.edge = CellToFloat(params[3]);
    p.postEqCenterFrequency = CellToFloat(params[4]);
    p.postEqBandwidth = CellToFloat(params[5]);
    p.preLowpassCutoff = CellToFloat(params[6]);
    return ValidateFloatFields(amx, nativeName, {
        {"gain", p.gain}, {"edge", p.edge},
        {"posteqcenterfrequency", p.postEqCenterFrequency},
        {"posteqbandwidth", p.postEqBandwidth},
        {"prelowpasscutoff", p.preLowpassCutoff}});
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::EchoParams& p) {
    p.wetDryMix = CellToFloat(params[2]);
    p.feedback = CellToFloat(params[3]);
    p.leftDelay = CellToFloat(params[4]);
    p.rightDelay = CellToFloat(params[5]);
    p.panDelay = (params[6] != 0) ? 1 : 0;
    return ValidateFloatFields(amx, nativeName, {
        {"wetdrymix", p.wetDryMix}, {"feedback", p.feedback},
        {"leftdelay", p.leftDelay}, {"rightdelay", p.rightDelay}});
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::FlangerParams& p) {
    p.wetDryMix = CellToFloat(params[2]);
    p.depth = CellToFloat(params[3]);
    p.feedback = CellToFloat(params[4]);
    p.frequency = CellToFloat(params[5]);
    p.waveform = static_cast<uint32_t>(params[6]);
    p.delay = CellToFloat(params[7]);
    p.phase = static_cast<uint32_t>(params[8]);
    return ValidateFloatFields(amx, nativeName, {
        {"wetdrymix", p.wetDryMix}, {"depth", p.depth}, {"feedback", p.feedback},
        {"frequency", p.frequency}, {"delay", p.delay}});
}

bool ParseEffectParams(const AMX*, const char*, const cell* params, protocol::GargleParams& p) {
    p.rateHz = static_cast<uint32_t>(params[2]);
    p.waveShape = static_cast<uint32_t>(params[3]);
    return true;
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::I3dl2ReverbParams& p) {
    p.room = static_cast<int32_t>(params[2]);
    p.roomHf = static_cast<int32_t>(params[3]);
    p.roomRolloffFactor = CellToFloat(params[4]);
    p.decayTime = CellToFloat(params[5]);
    p.decayHfRatio = CellToFloat(params[6]);
    p.reflections = static_cast<int32_t>(params[7]);
    p.reflectionsDelay = CellToFloat(params[8]);
    p.reverb = static_cast<int32_t>(params[9]);
    p.reverbDelay = CellToFloat(params[10]);
    p.diffusion = CellToFloat(params[11]);
    p.density = CellToFloat(params[12]);
    p.hfReference = CellToFloat(params[13]);
    return ValidateFloatFields(amx, nativeName, {
        {"roomrollofffactor", p.roomRolloffFactor}, {"decaytime", p.decayTime},
        {"decayhfratio", p.decayHfRatio}, {"reflectionsdelay", p.reflectionsDelay},
        {"reverbdelay", p.reverbDelay}, {"diffusion", p.diffusion},
        {"density", p.density}, {"hfreference", p.hfReference}});
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::ParamEqParams& p) {
    p.center = CellToFloat(params[2]);
    p.bandwidth = CellToFloat(params[3]);
    p.gain = CellToFloat(params[4]);
    return ValidateFloatFields(amx, nativeName, {
        {"center", p.center}, {"bandwidth", p.bandwidth}, {"gain", p.gain}});
}

bool ParseEffectParams(const AMX* amx, const char* nativeName, const cell* params, protocol::ReverbParams& p) {
    p.inGain = CellToFloat(params[2]);
    p.reverbMix = CellToFloat(params[3]);
    p.reverbTime = CellToFloat(params[4]);
    p.highFreqRtRatio = CellToFloat(params[5]);
    return ValidateFloatFields(amx, nativeName, {
        {"ingain", p.inGain}, {"reverbmix", p.reverbMix},
        {"reverbtime", p.reverbTime}, {"highfreqrtratio", p.highFreqRtRatio}});
}

template <typename ParamsT>
cell HandleCreateEffect(AMX* amx, cell* params, int expectedParams, const char* nativeName, protocol::EffectType type) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, expectedParams)) return 0;
    ParamsT p{};
    if (!ParseEffectParams(amx, nativeName, params, p)) return 0;
    return static_cast<cell>(core->CreateEffect(type, static_cast<int32_t>(params[1]), p, amx));
}

template <typename ParamsT>
cell HandleUpdateEffect(AMX* amx, cell* params, int expectedParams, const char* nativeName, protocol::EffectType type) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, expectedParams)) return 0;
    ParamsT p{};
    if (!ParseEffectParams(amx, nativeName, params, p)) return 0;
    return core->UpdateEffect(static_cast<uint32_t>(params[1]), type, p) ? 1 : 0;
}

} // namespace

cell AMX_NATIVE_CALL n_SvEffectCreateChorus(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::ChorusParams>(amx, params, 8, "SvEffectCreateChorus", protocol::EffectType::Chorus);
}
cell AMX_NATIVE_CALL n_SvEffectCreateCompressor(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::CompressorParams>(amx, params, 7, "SvEffectCreateCompressor", protocol::EffectType::Compressor);
}
cell AMX_NATIVE_CALL n_SvEffectCreateDistortion(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::DistortionParams>(amx, params, 6, "SvEffectCreateDistortion", protocol::EffectType::Distortion);
}
cell AMX_NATIVE_CALL n_SvEffectCreateEcho(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::EchoParams>(amx, params, 6, "SvEffectCreateEcho", protocol::EffectType::Echo);
}
cell AMX_NATIVE_CALL n_SvEffectCreateFlanger(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::FlangerParams>(amx, params, 8, "SvEffectCreateFlanger", protocol::EffectType::Flanger);
}
cell AMX_NATIVE_CALL n_SvEffectCreateGargle(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::GargleParams>(amx, params, 3, "SvEffectCreateGargle", protocol::EffectType::Gargle);
}
cell AMX_NATIVE_CALL n_SvEffectCreateI3dl2reverb(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::I3dl2ReverbParams>(amx, params, 13, "SvEffectCreateI3dl2reverb", protocol::EffectType::I3dl2Reverb);
}
cell AMX_NATIVE_CALL n_SvEffectCreateParameq(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::ParamEqParams>(amx, params, 4, "SvEffectCreateParameq", protocol::EffectType::ParamEq);
}
cell AMX_NATIVE_CALL n_SvEffectCreateReverb(AMX* amx, cell* params) {
    return HandleCreateEffect<protocol::ReverbParams>(amx, params, 5, "SvEffectCreateReverb", protocol::EffectType::Reverb);
}

cell AMX_NATIVE_CALL n_SvEffectUpdateChorus(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::ChorusParams>(amx, params, 8, "SvEffectUpdateChorus", protocol::EffectType::Chorus);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateCompressor(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::CompressorParams>(amx, params, 7, "SvEffectUpdateCompressor", protocol::EffectType::Compressor);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateDistortion(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::DistortionParams>(amx, params, 6, "SvEffectUpdateDistortion", protocol::EffectType::Distortion);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateEcho(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::EchoParams>(amx, params, 6, "SvEffectUpdateEcho", protocol::EffectType::Echo);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateFlanger(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::FlangerParams>(amx, params, 8, "SvEffectUpdateFlanger", protocol::EffectType::Flanger);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateGargle(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::GargleParams>(amx, params, 3, "SvEffectUpdateGargle", protocol::EffectType::Gargle);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateI3dl2reverb(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::I3dl2ReverbParams>(amx, params, 13, "SvEffectUpdateI3dl2reverb", protocol::EffectType::I3dl2Reverb);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateParameq(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::ParamEqParams>(amx, params, 4, "SvEffectUpdateParameq", protocol::EffectType::ParamEq);
}
cell AMX_NATIVE_CALL n_SvEffectUpdateReverb(AMX* amx, cell* params) {
    return HandleUpdateEffect<protocol::ReverbParams>(amx, params, 5, "SvEffectUpdateReverb", protocol::EffectType::Reverb);
}

cell AMX_NATIVE_CALL n_SvEffectAttachStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvEffectAttachStream(static_cast<uint32_t>(params[1]), static_cast<uint32_t>(params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvEffectDetachStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvEffectDetachStream(static_cast<uint32_t>(params[1]), static_cast<uint32_t>(params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvEffectDelete(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return core->SvEffectDelete(static_cast<uint32_t>(params[1])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvIsValidEffect(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    return core->SvIsValidEffect(static_cast<uint32_t>(params[1])) ? 1 : 0;
}

#undef SV_GET_CORE_OR_RET

#define SV_NATIVE_ENTRY(fn) { #fn, &n_##fn }

constexpr std::array<AMX_NATIVE_INFO, 99> kNatives = {{
    SV_NATIVE_ENTRY(SvDebug),
    SV_NATIVE_ENTRY(SvInit),
    SV_NATIVE_ENTRY(SvGetVersion),
    SV_NATIVE_ENTRY(SvGetClientProtocol),
    SV_NATIVE_ENTRY(SvGetPlusCapabilities),
    SV_NATIVE_ENTRY(SvGetPlusClientVersion),
    SV_NATIVE_ENTRY(SvIsPlayerVoiceReady),
    SV_NATIVE_ENTRY(SvIsPlayerSpeaking),
    SV_NATIVE_ENTRY(SvGetPlayerVoiceStatus),
    SV_NATIVE_ENTRY(SvGetPlayerVoiceLatency),
    SV_NATIVE_ENTRY(SvHasMicro),
    SV_NATIVE_ENTRY(SvStartRecord),
    SV_NATIVE_ENTRY(SvStopRecord),
    SV_NATIVE_ENTRY(SvAddKey),
    SV_NATIVE_ENTRY(SvHasKey),
    SV_NATIVE_ENTRY(SvRemoveKey),
    SV_NATIVE_ENTRY(SvRemoveAllKeys),
    SV_NATIVE_ENTRY(SvMutePlayerStatus),
    SV_NATIVE_ENTRY(SvMutePlayerEnable),
    SV_NATIVE_ENTRY(SvMutePlayerDisable),
    SV_NATIVE_ENTRY(SvMutePlayerRemaining),
    SV_NATIVE_ENTRY(SvMutePlayerForListener),
    SV_NATIVE_ENTRY(SvUnmutePlayerForListener),
    SV_NATIVE_ENTRY(SvIsPlayerMutedForListener),
    SV_NATIVE_ENTRY(SvEnableListener),
    SV_NATIVE_ENTRY(SvDisableListener),
    SV_NATIVE_ENTRY(SvCheckListener),
    SV_NATIVE_ENTRY(SvCreateGStream),
    SV_NATIVE_ENTRY(SvCreateSLStreamAtPoint),
    SV_NATIVE_ENTRY(SvCreateSLStreamAtVehicle),
    SV_NATIVE_ENTRY(SvCreateSLStreamAtPlayer),
    SV_NATIVE_ENTRY(SvCreateSLStreamAtObject),
    SV_NATIVE_ENTRY(SvCreateDLStreamAtPoint),
    SV_NATIVE_ENTRY(SvCreateDLStreamAtVehicle),
    SV_NATIVE_ENTRY(SvCreateDLStreamAtPlayer),
    SV_NATIVE_ENTRY(SvCreateDLStreamAtObject),
    SV_NATIVE_ENTRY(SvUpdateDistanceForLStream),
    SV_NATIVE_ENTRY(SvUpdatePositionForLPStream),
    SV_NATIVE_ENTRY(SvAttachListenerToStream),
    SV_NATIVE_ENTRY(SvStreamSetListenerVolume),
    SV_NATIVE_ENTRY(SvStreamGetListenerVolume),
    SV_NATIVE_ENTRY(SvGetStreamListenerCount),
    SV_NATIVE_ENTRY(SvGetStreamSpeakerCount),
    SV_NATIVE_ENTRY(SvHasListenerInStream),
    SV_NATIVE_ENTRY(SvDetachListenerFromStream),
    SV_NATIVE_ENTRY(SvDetachAllListenersFromStream),
    SV_NATIVE_ENTRY(SvAttachSpeakerToStream),
    SV_NATIVE_ENTRY(SvHasSpeakerInStream),
    SV_NATIVE_ENTRY(SvDetachSpeakerFromStream),
    SV_NATIVE_ENTRY(SvDetachAllSpeakersFromStream),
    SV_NATIVE_ENTRY(SvIsValidStream),
    SV_NATIVE_ENTRY(SvStreamParameterSet),
    SV_NATIVE_ENTRY(SvStreamParameterReset),
    SV_NATIVE_ENTRY(SvStreamParameterHas),
    SV_NATIVE_ENTRY(SvStreamParameterGet),
    SV_NATIVE_ENTRY(SvStreamParameterGetEx),
    SV_NATIVE_ENTRY(SvStreamParameterSlideFromTo),
    SV_NATIVE_ENTRY(SvStreamParameterSlideTo),
    SV_NATIVE_ENTRY(SvStreamParameterSlide),
    SV_NATIVE_ENTRY(SvStreamSetIcon),
    SV_NATIVE_ENTRY(SvStreamSetName),
    SV_NATIVE_ENTRY(SvStreamSetColor),
    SV_NATIVE_ENTRY(SvStreamGetName),
    SV_NATIVE_ENTRY(SvStreamGetColor),
    SV_NATIVE_ENTRY(SvGetStreamType),
    SV_NATIVE_ENTRY(SvGetStreamDistance),
    SV_NATIVE_ENTRY(SvGetStreamPosition),
    SV_NATIVE_ENTRY(SvGetStreamTarget),
    SV_NATIVE_ENTRY(SvGetStreamMaxPlayers),
    SV_NATIVE_ENTRY(SvGetStreamListeners),
    SV_NATIVE_ENTRY(SvGetStreamSpeakers),
    SV_NATIVE_ENTRY(SvGetPlayerListenerStreams),
    SV_NATIVE_ENTRY(SvGetPlayerSpeakerStreams),
    SV_NATIVE_ENTRY(SvStreamSetWorld),
    SV_NATIVE_ENTRY(SvGetStreamWorld),
    SV_NATIVE_ENTRY(SvGetStreamInterior),
    SV_NATIVE_ENTRY(SvDeleteStream),
    SV_NATIVE_ENTRY(SvEffectCreateChorus),
    SV_NATIVE_ENTRY(SvEffectCreateCompressor),
    SV_NATIVE_ENTRY(SvEffectCreateDistortion),
    SV_NATIVE_ENTRY(SvEffectCreateEcho),
    SV_NATIVE_ENTRY(SvEffectCreateFlanger),
    SV_NATIVE_ENTRY(SvEffectCreateGargle),
    SV_NATIVE_ENTRY(SvEffectCreateI3dl2reverb),
    SV_NATIVE_ENTRY(SvEffectCreateParameq),
    SV_NATIVE_ENTRY(SvEffectCreateReverb),
    SV_NATIVE_ENTRY(SvEffectUpdateChorus),
    SV_NATIVE_ENTRY(SvEffectUpdateCompressor),
    SV_NATIVE_ENTRY(SvEffectUpdateDistortion),
    SV_NATIVE_ENTRY(SvEffectUpdateEcho),
    SV_NATIVE_ENTRY(SvEffectUpdateFlanger),
    SV_NATIVE_ENTRY(SvEffectUpdateGargle),
    SV_NATIVE_ENTRY(SvEffectUpdateI3dl2reverb),
    SV_NATIVE_ENTRY(SvEffectUpdateParameq),
    SV_NATIVE_ENTRY(SvEffectUpdateReverb),
    SV_NATIVE_ENTRY(SvEffectAttachStream),
    SV_NATIVE_ENTRY(SvEffectDetachStream),
    SV_NATIVE_ENTRY(SvEffectDelete),
    SV_NATIVE_ENTRY(SvIsValidEffect)
}};

#undef SV_NATIVE_ENTRY

} // namespace

void PawnBindings::AttachCore(ServerCore* core) noexcept {
    std::lock_guard lock(mutex_);
    gActiveCore.store(core, std::memory_order_release);
}

void PawnBindings::DetachCore() noexcept {
    {
        std::lock_guard lock(mutex_);
        gActiveCore.store(nullptr, std::memory_order_release);
        scripts_.clear();
    }
    std::lock_guard wlock(gNativeWarnMutex);
    gNativeWarnWindows.clear();
}

void PawnBindings::RegisterScript(AMX* amx) {
    if (amx == nullptr) return;
    amx_Register(amx, kNatives.data(), static_cast<int>(kNatives.size()));

    std::lock_guard lock(mutex_);
    for (auto& e : scripts_) {
        if (e.amx == amx) {
            e.publicIdx.fill(-2);
            e.execWarns.fill({});
            return;
        }
    }
    ScriptEntry entry{};
    entry.amx = amx;
    entry.publicIdx.fill(-2);
    scripts_.push_back(entry);
}

void PawnBindings::UnregisterScript(AMX* amx) {
    if (amx == nullptr) return;
    {
        std::lock_guard lock(mutex_);
        scripts_.erase(
            std::remove_if(scripts_.begin(), scripts_.end(),
                           [amx](const ScriptEntry& e) { return e.amx == amx; }),
            scripts_.end());
    }
    {
        std::lock_guard wlock(gNativeWarnMutex);
        for (auto it = gNativeWarnWindows.begin(); it != gNativeWarnWindows.end();) {
            if (it->first.amx == amx) {
                it = gNativeWarnWindows.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (ServerCore* const core = gActiveCore.load(std::memory_order_acquire)) {
        core->CleanupAmxResources(amx);
    }
}

void PawnBindings::NotifyActivationKeyPress(uint16_t playerId, uint8_t keyId) noexcept {
    const cell args[2] = {static_cast<cell>(playerId), static_cast<cell>(keyId)};
    InvokeCallback(CallbackId::ActivationKeyPress, "OnPlayerActivationKeyPress", args, 2);
}

void PawnBindings::NotifyActivationKeyRelease(uint16_t playerId, uint8_t keyId) noexcept {
    const cell args[2] = {static_cast<cell>(playerId), static_cast<cell>(keyId)};
    InvokeCallback(CallbackId::ActivationKeyRelease, "OnPlayerActivationKeyRelease", args, 2);
}

void PawnBindings::NotifyMicroStateChange(uint16_t playerId, bool hasMicro) noexcept {
    const cell args[2] = {static_cast<cell>(playerId), static_cast<cell>(hasMicro ? 1 : 0)};
    InvokeCallback(CallbackId::MicroStateChange, "OnPlayerMicroStateChange", args, 2);
}

void PawnBindings::NotifyVoiceMute(uint16_t playerId, bool releaseKeys, uint32_t timeMs, bool speaking) noexcept {
    const cell args[4] = {
        static_cast<cell>(playerId),
        static_cast<cell>(releaseKeys ? 1 : 0),
        static_cast<cell>(timeMs),
        static_cast<cell>(speaking ? 1 : 0)
    };
    InvokeCallback(CallbackId::VoiceMute, "OnPlayerVoiceMute", args, 4);
}

void PawnBindings::NotifyVoiceUnmute(uint16_t playerId, bool expired) noexcept {
    const cell args[2] = {static_cast<cell>(playerId), static_cast<cell>(expired ? 1 : 0)};
    InvokeCallback(CallbackId::VoiceUnmute, "OnPlayerVoiceUnmute", args, 2);
}

void PawnBindings::NotifyPlayerVoiceReady(uint16_t playerId) noexcept {
    const cell args[1] = {static_cast<cell>(playerId)};
    InvokeCallback(CallbackId::PlayerVoiceReady, "OnPlayerVoiceReady", args, 1);
}

void PawnBindings::NotifyPlayerVoiceStart(uint16_t playerId) noexcept {
    const cell args[1] = {static_cast<cell>(playerId)};
    InvokeCallback(CallbackId::PlayerVoiceStart, "OnPlayerVoiceStart", args, 1);
}

void PawnBindings::NotifyPlayerVoiceStop(uint16_t playerId) noexcept {
    const cell args[1] = {static_cast<cell>(playerId)};
    InvokeCallback(CallbackId::PlayerVoiceStop, "OnPlayerVoiceStop", args, 1);
}

void PawnBindings::InvokeCallback(CallbackId cbId, const char* name, const cell* args, std::size_t count) noexcept {
    const std::size_t cbIdx = static_cast<std::size_t>(cbId);

    std::array<AMX*, 64> stackSnapshot{};
    std::vector<AMX*> heapSnapshot;
    const AMX* const* snapshotPtr = nullptr;
    std::size_t snapshotCount = 0;

    {
        std::lock_guard lock(mutex_);
        if (scripts_.empty()) return;
        snapshotCount = scripts_.size();
        if (snapshotCount <= stackSnapshot.size()) {
            for (std::size_t i = 0; i < snapshotCount; ++i) {
                stackSnapshot[i] = scripts_[i].amx;
            }
            snapshotPtr = stackSnapshot.data();
        } else {
            heapSnapshot.reserve(snapshotCount);
            for (const auto& e : scripts_) {
                heapSnapshot.push_back(e.amx);
            }
            snapshotPtr = heapSnapshot.data();
        }
    }

    for (std::size_t s = 0; s < snapshotCount; ++s) {
        AMX* amx = const_cast<AMX*>(snapshotPtr[s]);
        if (amx == nullptr) continue;

        int idx = -1;
        {
            std::lock_guard lock(mutex_);
            ScriptEntry* foundEntry = nullptr;
            for (auto& e : scripts_) {
                if (e.amx == amx) {
                    foundEntry = &e;
                    break;
                }
            }
            if (foundEntry == nullptr) {
                continue; // Script was unloaded mid-delivery
            }
            idx = foundEntry->publicIdx[cbIdx];
            if (idx == -2) {
                int qIdx = -1;
                idx = (amx_FindPublic(amx, name, &qIdx) == AMX_ERR_NONE && qIdx >= 0) ? qIdx : -1;
                foundEntry->publicIdx[cbIdx] = idx;
            }
            if (idx < 0) {
                continue;
            }
        }

        for (std::size_t i = count; i-- > 0;) {
            amx_Push(amx, args[i]);
        }
        cell ret = 0;
        const int err = amx_Exec(amx, &ret, idx);
        if (err != AMX_ERR_NONE) {
            bool shouldLog = false;
            uint32_t suppressedCount = 0;
            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard lock(mutex_);
                ScriptEntry* foundEntry = nullptr;
                for (auto& e : scripts_) {
                    if (e.amx == amx) {
                        foundEntry = &e;
                        break;
                    }
                }
                if (foundEntry == nullptr) {
                    shouldLog = true;
                } else {
                    auto& ws = foundEntry->execWarns[cbIdx];
                    if (!ws.hasLogged || (now - ws.lastLog) >= kRateLimitWindow) {
                        suppressedCount = ws.suppressed;
                        ws.lastLog = now;
                        ws.hasLogged = true;
                        ws.suppressed = 0;
                        shouldLog = true;
                    } else {
                        ++ws.suppressed;
                    }
                }
            }
            if (shouldLog) {
                if (suppressedCount > 0) {
                    log::Logger::Log(
                        log::Level::Warn,
                        "[pawn] Callback '%s' (script %p) AMX execution error %d (%u suppressed in last 60s)",
                        name, static_cast<const void*>(amx), err, suppressedCount
                    );
                } else {
                    log::Logger::Log(
                        log::Level::Warn,
                        "[pawn] Callback '%s' (script %p) AMX execution error %d",
                        name, static_cast<const void*>(amx), err
                    );
                }
            }
        }
    }
}

} // namespace svplus::server
