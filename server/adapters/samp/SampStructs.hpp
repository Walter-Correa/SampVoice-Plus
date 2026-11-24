/*
 * SampVoice Plus - Minimal Verified 32-bit SA-MP 0.3.7 & 0.3.DL Server Memory Layouts
 *
 * Exact byte offsets verified across 32-bit Win32 and Linux SA-MP 0.3.7-R2/R3 & 0.3.DL-R1 ABI.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace svplus::server::samp {

inline constexpr uint32_t kSampMaxPlayers    = 1000u;
inline constexpr uint32_t kSampMaxVehicles   = 2000u;
inline constexpr uint32_t kSampMaxObjects    = 1000u;
inline constexpr uint32_t kSampDLMaxObjects  = 2000u;

#pragma pack(push, 1)

struct SampVector3 {
    float x;
    float y;
    float z;
};
static_assert(sizeof(SampVector3) == 12, "SampVector3 must be 12 bytes");

struct SampMatrix4x4 {
    SampVector3 right;
    uint32_t flags;
    SampVector3 up;
    float padU;
    SampVector3 at;
    float padA;
    SampVector3 pos;
    float padP;
};
static_assert(sizeof(SampMatrix4x4) == 64, "SampMatrix4x4 must be 64 bytes");

struct SampPlayer {
    uint8_t     padBeforePos[10517];
    SampVector3 vecPosition;
    uint8_t     padPosToInterior[799];
    int32_t     iInteriorId;
    uint8_t     padInteriorToSpectate[120];
    uint8_t     byteSpectateType; // 0=none, 1=player, 2=vehicle (verified Win32 + Linux R2/R3)
    int32_t     dwSpectateId;
    uint8_t     padAfterSpectate[29];
};
static_assert(sizeof(SampPlayer) == 11486, "SampPlayer must be 11486 bytes");
static_assert(offsetof(SampPlayer, vecPosition) == 10517, "SampPlayer::vecPosition offset mismatch");
static_assert(offsetof(SampPlayer, iInteriorId) == 11328, "SampPlayer::iInteriorId offset mismatch");
static_assert(offsetof(SampPlayer, byteSpectateType) == 0x2CBC, "SampPlayer::byteSpectateType offset mismatch");
static_assert(offsetof(SampPlayer, dwSpectateId) == 0x2CBD, "SampPlayer::dwSpectateId offset mismatch");

struct SampPlayerDL {
    uint8_t     padBeforePos[0x2CFD];
    SampVector3 vecPosition;
    uint8_t     padPosToInterior[0x3030 - 0x2CFD - sizeof(SampVector3)];
    int32_t     iInteriorId;
    uint8_t     padInteriorToSpectate[0x30AC - 0x3030 - sizeof(int32_t)];
    uint8_t     byteSpectateType;
    int32_t     dwSpectateId;
};
static_assert(offsetof(SampPlayerDL, vecPosition) == 0x2CFD, "SampPlayerDL::vecPosition offset mismatch");
static_assert(offsetof(SampPlayerDL, iInteriorId) == 0x3030, "SampPlayerDL::iInteriorId offset mismatch");
static_assert(offsetof(SampPlayerDL, byteSpectateType) == 0x30AC, "SampPlayerDL::byteSpectateType offset mismatch");
static_assert(offsetof(SampPlayerDL, dwSpectateId) == 0x30AD, "SampPlayerDL::dwSpectateId offset mismatch");

struct SampPlayerPool {
    uint32_t dwVirtualWorld[kSampMaxPlayers];
    uint32_t dwPlayersCount;
    uint32_t dwLastMarkerUpdate;
    float    fUpdatePlayerGameTimers;
    uint32_t dwScore[kSampMaxPlayers];
    uint32_t dwMoney[kSampMaxPlayers];
    uint32_t dwDrunkLevel[kSampMaxPlayers];
    uint32_t dwLastScoreUpdate[kSampMaxPlayers];
    char     szSerial[kSampMaxPlayers][101];
    char     szVersion[kSampMaxPlayers][25];
    void*    pRemoteSystem[kSampMaxPlayers];
    int32_t  bIsPlayerConnected[kSampMaxPlayers];
    SampPlayer* pPlayer[kSampMaxPlayers];
    char     szName[kSampMaxPlayers][25];
    int32_t  bIsAnAdmin[kSampMaxPlayers];
    int32_t  bIsNPC[kSampMaxPlayers];
    uint8_t  pad0[8000];
    uint32_t dwConnectedPlayers;
    uint32_t dwPlayerPoolSize;
    uint32_t dwUnk;
};
static_assert(sizeof(SampPlayerPool) == 199024, "SampPlayerPool must be 199024 bytes");

struct SampVehicle {
    SampVector3 vecPosition;
    uint8_t padTail[267 - 12];
};
static_assert(sizeof(SampVehicle) == 267, "SampVehicle must be 267 bytes");

struct SampVehiclePool {
    uint8_t      byteVehicleModelsUsed[212];
    int32_t      iVirtualWorld[kSampMaxVehicles];
    int32_t      bVehicleSlotState[kSampMaxVehicles];
    SampVehicle* pVehicle[kSampMaxVehicles];
    uint32_t     dwVehiclePoolSize;
};
static_assert(sizeof(SampVehiclePool) == 24216, "SampVehiclePool must be 24216 bytes");

struct SampObject {
    uint16_t      wObjectId;
    int32_t       iModel;
    int32_t       bActive;
    SampMatrix4x4 matWorld;
    uint8_t       padTail[3701 - (10 + 64)];
};
static_assert(sizeof(SampObject) == 3701, "SampObject must be 3701 bytes");

struct SampObjectPool {
    int32_t     bPlayerObjectSlotState[kSampMaxPlayers][kSampMaxObjects];
    int32_t     bPlayersObject[kSampMaxObjects];
    SampObject* pPlayerObjects[kSampMaxPlayers][kSampMaxObjects];
    int32_t     bObjectSlotState[kSampMaxObjects];
    SampObject* pObjects[kSampMaxObjects];
};

struct SampPlayerPoolDL {
    uint32_t      dwVirtualWorld[kSampMaxPlayers];
    uint8_t       pad0[0x24DE4 - sizeof(uint32_t) * kSampMaxPlayers];
    int32_t       bIsPlayerConnected[kSampMaxPlayers];
    SampPlayerDL* pPlayer[kSampMaxPlayers];
    uint8_t       pad1[0x2DE6C - 0x24DE4 - sizeof(int32_t) * kSampMaxPlayers - sizeof(SampPlayerDL*) * kSampMaxPlayers];
    int32_t       bIsNPC[kSampMaxPlayers];
    uint8_t       pad2[0x30D4C - 0x2DE6C - sizeof(int32_t) * kSampMaxPlayers];
    uint32_t      dwConnectedPlayers;
    uint32_t      dwPlayerPoolSize;
};
static_assert(offsetof(SampPlayerPoolDL, bIsPlayerConnected) == 0x24DE4, "SampPlayerPoolDL::bIsPlayerConnected offset mismatch");
static_assert(offsetof(SampPlayerPoolDL, pPlayer) == 0x25D84, "SampPlayerPoolDL::pPlayer offset mismatch");
static_assert(offsetof(SampPlayerPoolDL, bIsNPC) == 0x2DE6C, "SampPlayerPoolDL::bIsNPC offset mismatch");
static_assert(offsetof(SampPlayerPoolDL, dwConnectedPlayers) == 0x30D4C, "SampPlayerPoolDL::dwConnectedPlayers offset mismatch");
static_assert(offsetof(SampPlayerPoolDL, dwPlayerPoolSize) == 0x30D50, "SampPlayerPoolDL::dwPlayerPoolSize offset mismatch");
static_assert(sizeof(SampPlayerPoolDL) == 0x30D54, "SampPlayerPoolDL size mismatch");

struct SampObjectPoolDL {
    int32_t     bPlayerObjectSlotState[kSampMaxPlayers][kSampDLMaxObjects];
    int32_t     bPlayersObject[kSampDLMaxObjects];
    SampObject* pPlayerObjects[kSampMaxPlayers][kSampDLMaxObjects];
    int32_t     bObjectSlotState[kSampDLMaxObjects];
    SampObject* pObjects[kSampDLMaxObjects];
};
static_assert(offsetof(SampObjectPoolDL, bObjectSlotState) == 0xF44340, "SampObjectPoolDL::bObjectSlotState offset mismatch");
static_assert(offsetof(SampObjectPoolDL, pObjects) == 0xF46280, "SampObjectPoolDL::pObjects offset mismatch");
static_assert(sizeof(SampObjectPoolDL) == 0xF481C0, "SampObjectPoolDL size mismatch");

struct SampNetGame {
    void*            pGameModePool;
    void*            pFilterScriptPool;
    SampPlayerPool*  pPlayerPool;
    SampVehiclePool* pVehiclePool;
    void*            pPickupPool;
    SampObjectPool*  pObjectPool;
    void*            pMenuPool;
    void*            pTextDrawPool;
    void*            p3DTextPool;
    void*            pGangZonePool;
    void*            pActorPool;
    int32_t          iCurrentGameModeIndex;
    int32_t          iCurrentGameModeRepeat;
    int32_t          bFirstGameModeLoaded;
    void*            pHttpClient;
    void*            pScriptTimers;
    void*            pRak;
};
static_assert(offsetof(SampNetGame, pRak) == 64, "SampNetGame::pRak offset mismatch");

// Minimal RakNet ABI structures matching SA-MP 0.3.7 server
struct RakPlayerId {
    uint32_t binaryAddress{0xFFFFFFFFu};
    uint16_t port{0xFFFFu};
};

struct RakPacket {
    uint16_t    playerIndex;
    RakPlayerId playerId;
    uint32_t    length;
    uint32_t    bitSize;
    uint8_t*    data;
    bool        deleteData;
};

struct RakRpcParameters {
    uint8_t*    input;
    uint32_t    numberOfBitsOfData;
    RakPlayerId sender;
};

struct RakBitStream {
    int32_t  numberOfBitsUsed{0};
    int32_t  numberOfBitsAllocated{0};
    int32_t  readOffset{0};
    uint8_t* data{nullptr};
    bool     copyData{false};
    uint8_t  stackData[256]{};
};

#pragma pack(pop)

} // namespace svplus::server::samp
