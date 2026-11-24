/*
 * SampVoice Plus - Shared Pawn AMX Native Bindings
 */

#include "PawnBindings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>

#include "svplus/common/Logger.hpp"
#include "svplus/common/Protocol.hpp"

namespace svplus::server {

std::mutex PawnBindings::mutex_;
std::vector<AMX*> PawnBindings::scripts_;

namespace {

std::atomic<ServerCore*> gActiveCore{nullptr};

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
    core->SvRemoveAllKeys(playerId);
    return 1;
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
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    core->SvMutePlayerEnable(playerId);
    return 1;
}

cell AMX_NATIVE_CALL n_SvMutePlayerDisable(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[1], playerId)) return 0;
    core->SvMutePlayerDisable(playerId);
    return 1;
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
    return static_cast<cell>(core->SvCreateSLStreamAtPoint(
        CellToFloat(params[1]),
        CellToFloat(params[2]),
        CellToFloat(params[3]),
        CellToFloat(params[4]),
        static_cast<uint32_t>(params[5]),
        ReadAmxString(amx, params[6]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtVehicle(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    uint16_t vehicleId = 0;
    if (!TryGetVehicleId(params[2], vehicleId)) return 0;
    return static_cast<cell>(core->SvCreateSLStreamAtVehicle(
        CellToFloat(params[1]),
        vehicleId,
        static_cast<uint32_t>(params[3]),
        ReadAmxString(amx, params[4]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtPlayer(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return static_cast<cell>(core->SvCreateSLStreamAtPlayer(
        CellToFloat(params[1]),
        playerId,
        static_cast<uint32_t>(params[3]),
        ReadAmxString(amx, params[4]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateSLStreamAtObject(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    uint16_t objectId = 0;
    if (!TryGetObjectId(params[2], objectId)) return 0;
    return static_cast<cell>(core->SvCreateSLStreamAtObject(
        CellToFloat(params[1]),
        objectId,
        static_cast<uint32_t>(params[3]),
        ReadAmxString(amx, params[4]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtPoint(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 7)) return 0;
    return static_cast<cell>(core->SvCreateDLStreamAtPoint(
        CellToFloat(params[1]),
        static_cast<uint32_t>(params[2]),
        CellToFloat(params[3]),
        CellToFloat(params[4]),
        CellToFloat(params[5]),
        static_cast<uint32_t>(params[6]),
        ReadAmxString(amx, params[7]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtVehicle(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    uint16_t vehicleId = 0;
    if (!TryGetVehicleId(params[3], vehicleId)) return 0;
    return static_cast<cell>(core->SvCreateDLStreamAtVehicle(
        CellToFloat(params[1]),
        static_cast<uint32_t>(params[2]),
        vehicleId,
        static_cast<uint32_t>(params[4]),
        ReadAmxString(amx, params[5]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtPlayer(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[3], playerId)) return 0;
    return static_cast<cell>(core->SvCreateDLStreamAtPlayer(
        CellToFloat(params[1]),
        static_cast<uint32_t>(params[2]),
        playerId,
        static_cast<uint32_t>(params[4]),
        ReadAmxString(amx, params[5]),
        amx));
}

cell AMX_NATIVE_CALL n_SvCreateDLStreamAtObject(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    uint16_t objectId = 0;
    if (!TryGetObjectId(params[3], objectId)) return 0;
    return static_cast<cell>(core->SvCreateDLStreamAtObject(
        CellToFloat(params[1]),
        static_cast<uint32_t>(params[2]),
        objectId,
        static_cast<uint32_t>(params[4]),
        ReadAmxString(amx, params[5]),
        amx));
}

cell AMX_NATIVE_CALL n_SvUpdateDistanceForLStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    core->SvUpdateDistanceForLStream(static_cast<uint32_t>(params[1]), CellToFloat(params[2]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvUpdatePositionForLPStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    core->SvUpdatePositionForLPStream(
        static_cast<uint32_t>(params[1]),
        CellToFloat(params[2]),
        CellToFloat(params[3]),
        CellToFloat(params[4]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvAttachListenerToStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    uint16_t playerId = 0;
    if (!TryGetPlayerId(params[2], playerId)) return 0;
    return core->SvAttachListenerToStream(static_cast<uint32_t>(params[1]), playerId) ? 1 : 0;
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
    core->SvDetachAllListenersFromStream(static_cast<uint32_t>(params[1]));
    return 1;
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
    core->SvDetachAllSpeakersFromStream(static_cast<uint32_t>(params[1]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvStreamParameterSet(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    core->SvStreamParameterSet(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2]),
        CellToFloat(params[3]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvStreamParameterReset(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    core->SvStreamParameterReset(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2]));
    return 1;
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

cell AMX_NATIVE_CALL n_SvStreamParameterSlideFromTo(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    core->SvStreamParameterSlideFromTo(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2]),
        CellToFloat(params[3]),
        CellToFloat(params[4]),
        static_cast<uint32_t>(params[5]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvStreamParameterSlideTo(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    core->SvStreamParameterSlideTo(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2]),
        CellToFloat(params[3]),
        static_cast<uint32_t>(params[4]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvStreamParameterSlide(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    core->SvStreamParameterSlide(
        static_cast<uint32_t>(params[1]),
        static_cast<uint32_t>(params[2]),
        CellToFloat(params[3]),
        static_cast<uint32_t>(params[4]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvStreamSetIcon(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    return core->SvStreamSetIcon(
        static_cast<uint32_t>(params[1]),
        ReadAmxString(amx, params[2])) ? 1 : 0;
}

cell AMX_NATIVE_CALL n_SvDeleteStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    core->SvDeleteStream(static_cast<uint32_t>(params[1]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvEffectCreateChorus(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 8)) return 0;
    protocol::ChorusParams p{};
    p.wetDryMix = CellToFloat(params[2]);
    p.depth = CellToFloat(params[3]);
    p.feedback = CellToFloat(params[4]);
    p.frequency = CellToFloat(params[5]);
    p.waveform = static_cast<uint32_t>(params[6]);
    p.delay = CellToFloat(params[7]);
    p.phase = static_cast<uint32_t>(params[8]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Chorus, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateCompressor(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 7)) return 0;
    protocol::CompressorParams p{};
    p.gain = CellToFloat(params[2]);
    p.attack = CellToFloat(params[3]);
    p.release = CellToFloat(params[4]);
    p.threshold = CellToFloat(params[5]);
    p.ratio = CellToFloat(params[6]);
    p.predelay = CellToFloat(params[7]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Compressor, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateDistortion(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 6)) return 0;
    protocol::DistortionParams p{};
    p.gain = CellToFloat(params[2]);
    p.edge = CellToFloat(params[3]);
    p.postEqCenterFrequency = CellToFloat(params[4]);
    p.postEqBandwidth = CellToFloat(params[5]);
    p.preLowpassCutoff = CellToFloat(params[6]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Distortion, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateEcho(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 6)) return 0;
    protocol::EchoParams p{};
    p.wetDryMix = CellToFloat(params[2]);
    p.feedback = CellToFloat(params[3]);
    p.leftDelay = CellToFloat(params[4]);
    p.rightDelay = CellToFloat(params[5]);
    p.panDelay = (params[6] != 0) ? 1 : 0;
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Echo, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateFlanger(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 8)) return 0;
    protocol::FlangerParams p{};
    p.wetDryMix = CellToFloat(params[2]);
    p.depth = CellToFloat(params[3]);
    p.feedback = CellToFloat(params[4]);
    p.frequency = CellToFloat(params[5]);
    p.waveform = static_cast<uint32_t>(params[6]);
    p.delay = CellToFloat(params[7]);
    p.phase = static_cast<uint32_t>(params[8]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Flanger, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateGargle(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 3)) return 0;
    protocol::GargleParams p{};
    p.rateHz = static_cast<uint32_t>(params[2]);
    p.waveShape = static_cast<uint32_t>(params[3]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Gargle, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateI3dl2reverb(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 13)) return 0;
    protocol::I3dl2ReverbParams p{};
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
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::I3dl2Reverb, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateParameq(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 4)) return 0;
    protocol::ParamEqParams p{};
    p.center = CellToFloat(params[2]);
    p.bandwidth = CellToFloat(params[3]);
    p.gain = CellToFloat(params[4]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::ParamEq, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectCreateReverb(AMX* amx, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 5)) return 0;
    protocol::ReverbParams p{};
    p.inGain = CellToFloat(params[2]);
    p.reverbMix = CellToFloat(params[3]);
    p.reverbTime = CellToFloat(params[4]);
    p.highFreqRtRatio = CellToFloat(params[5]);
    return static_cast<cell>(core->CreateEffect(
        protocol::EffectType::Reverb, static_cast<int32_t>(params[1]), p, amx));
}

cell AMX_NATIVE_CALL n_SvEffectAttachStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    core->SvEffectAttachStream(static_cast<uint32_t>(params[1]), static_cast<uint32_t>(params[2]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvEffectDetachStream(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 2)) return 0;
    core->SvEffectDetachStream(static_cast<uint32_t>(params[1]), static_cast<uint32_t>(params[2]));
    return 1;
}

cell AMX_NATIVE_CALL n_SvEffectDelete(AMX*, cell* params) {
    SV_GET_CORE_OR_RET(0);
    if (!CheckParamCount(params, 1)) return 0;
    core->SvEffectDelete(static_cast<uint32_t>(params[1]));
    return 1;
}

#undef SV_GET_CORE_OR_RET

#define SV_NATIVE_ENTRY(fn) { #fn, &n_##fn }

constexpr std::array<AMX_NATIVE_INFO, 58> kNatives = {{
    SV_NATIVE_ENTRY(SvDebug),
    SV_NATIVE_ENTRY(SvInit),
    SV_NATIVE_ENTRY(SvGetVersion),
    SV_NATIVE_ENTRY(SvGetClientProtocol),
    SV_NATIVE_ENTRY(SvGetPlusCapabilities),
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
    SV_NATIVE_ENTRY(SvHasListenerInStream),
    SV_NATIVE_ENTRY(SvDetachListenerFromStream),
    SV_NATIVE_ENTRY(SvDetachAllListenersFromStream),
    SV_NATIVE_ENTRY(SvAttachSpeakerToStream),
    SV_NATIVE_ENTRY(SvHasSpeakerInStream),
    SV_NATIVE_ENTRY(SvDetachSpeakerFromStream),
    SV_NATIVE_ENTRY(SvDetachAllSpeakersFromStream),
    SV_NATIVE_ENTRY(SvStreamParameterSet),
    SV_NATIVE_ENTRY(SvStreamParameterReset),
    SV_NATIVE_ENTRY(SvStreamParameterHas),
    SV_NATIVE_ENTRY(SvStreamParameterGet),
    SV_NATIVE_ENTRY(SvStreamParameterSlideFromTo),
    SV_NATIVE_ENTRY(SvStreamParameterSlideTo),
    SV_NATIVE_ENTRY(SvStreamParameterSlide),
    SV_NATIVE_ENTRY(SvStreamSetIcon),
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
    SV_NATIVE_ENTRY(SvEffectAttachStream),
    SV_NATIVE_ENTRY(SvEffectDetachStream),
    SV_NATIVE_ENTRY(SvEffectDelete)
}};

#undef SV_NATIVE_ENTRY

} // namespace

void PawnBindings::AttachCore(ServerCore* core) noexcept {
    std::lock_guard lock(mutex_);
    gActiveCore.store(core, std::memory_order_release);
}

void PawnBindings::DetachCore() noexcept {
    std::lock_guard lock(mutex_);
    gActiveCore.store(nullptr, std::memory_order_release);
    scripts_.clear();
}

void PawnBindings::RegisterScript(AMX* amx) {
    if (amx == nullptr) return;
    amx_Register(amx, kNatives.data(), static_cast<int>(kNatives.size()));

    std::lock_guard lock(mutex_);
    if (std::find(scripts_.begin(), scripts_.end(), amx) == scripts_.end()) {
        scripts_.push_back(amx);
    }
}

void PawnBindings::UnregisterScript(AMX* amx) {
    if (amx == nullptr) return;
    {
        std::lock_guard lock(mutex_);
        scripts_.erase(std::remove(scripts_.begin(), scripts_.end(), amx), scripts_.end());
    }
    if (ServerCore* const core = gActiveCore.load(std::memory_order_acquire)) {
        core->CleanupAmxResources(amx);
    }
}

void PawnBindings::NotifyActivationKeyPress(uint16_t playerId, uint8_t keyId) noexcept {
    InvokeTwoArgCallback("OnPlayerActivationKeyPress", static_cast<cell>(playerId), static_cast<cell>(keyId));
}

void PawnBindings::NotifyActivationKeyRelease(uint16_t playerId, uint8_t keyId) noexcept {
    InvokeTwoArgCallback("OnPlayerActivationKeyRelease", static_cast<cell>(playerId), static_cast<cell>(keyId));
}

void PawnBindings::NotifyMicroStateChange(uint16_t playerId, bool hasMicro) noexcept {
    InvokeTwoArgCallback("OnPlayerMicroStateChange", static_cast<cell>(playerId), static_cast<cell>(hasMicro ? 1 : 0));
}

void PawnBindings::InvokeTwoArgCallback(const char* name, cell arg0, cell arg1) noexcept {
    std::vector<AMX*> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (scripts_.empty()) return;
        snapshot = scripts_;
    }

    for (AMX* amx : snapshot) {
        if (amx == nullptr) continue;
        int idx = -1;
        if (amx_FindPublic(amx, name, &idx) == AMX_ERR_NONE && idx >= 0) {
            amx_Push(amx, arg1);
            amx_Push(amx, arg0);
            cell ret = 0;
            const int err = amx_Exec(amx, &ret, idx);
            if (err != AMX_ERR_NONE) {
                log::Logger::Log(log::Level::Warn, "[pawn] Callback '%s' AMX execution error %d", name, err);
            }
        }
    }
}

} // namespace svplus::server
