#include "samp/SampBridge.hpp"

#include "ShutdownState.hpp"
#include "diag/ModDiagnostics.hpp"
#include "samp/ShadowRakBlock.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Version.hpp"
#include "util/Utf8Util.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <RakNet/BitStream.h>
#include <RakNet/RakClientInterface.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace svplus::client::samp {
namespace {

constexpr std::uintptr_t kGtaCameraAddr           = 0xB6F028u; // TheCamera (CPlaceable)
constexpr std::uintptr_t kGtaCameraPtrOffset      = 0x14u;     // CPlaceable::m_matrix (CMatrixLink*)
constexpr std::uintptr_t kGtaCameraWorldPosAddr   = 0xB6F9CCu; // Active camera world coordinates float[3]
constexpr std::uintptr_t kGtaActiveVehicleAddr    = 0xBA18FCu; // Current local player CVehicle* (0 when on foot)
constexpr std::uintptr_t kGtaMenuActiveAddr       = 0xBA67A4u;
constexpr std::uintptr_t kGtaCalcScreenCoordsAddr = 0x71DA00u;
constexpr std::uintptr_t kGtaPedGetBonePosAddr    = 0x5E4280u; // void __thiscall CPed::GetBonePosition(RwV3d* out, int boneId, bool update)
constexpr std::uintptr_t kGtaLineOfSightClearAddr = 0x56A490u; // bool __cdecl CWorld::GetIsLineOfSightClear(origin, target, ...)
constexpr std::uintptr_t kGtaProcessLineOfSightAddr = 0x56BA00u; // bool __cdecl CWorld::ProcessLineOfSight(...)
constexpr std::uintptr_t kGtaWorldIgnoreEntityAddr = 0xB7CD68u; // CEntity* CWorld::pIgnoreEntity
constexpr std::uint32_t  kGtaPedRwObjectOffset    = 0x18u;     // CEntity::m_pRwObject

inline bool isPlausiblePointer(const void* ptr) noexcept {
    const auto addr = reinterpret_cast<std::uintptr_t>(ptr);
    return addr >= 0x10000u && addr < 0x7FFF0000u;
}

// Isolated SEH POD memory-read helpers (no C++ objects with destructors -> /EHsc safe)
bool safeReadBytes(const void* src, void* dst, std::size_t bytes) noexcept {
    if (!isPlausiblePointer(src) || !dst || bytes == 0) {
        return false;
    }
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool safeReadValue(const void* base, std::uint32_t offset, T& out) noexcept {
    if (!isPlausiblePointer(base)) {
        return false;
    }
    const auto* src = static_cast<const std::uint8_t*>(base) + offset;
    return safeReadBytes(src, &out, sizeof(T));
}

bool safeReadCString(const void* src, char* out, std::size_t maxChars, std::size_t& outLen) noexcept {
    outLen = 0;
    if (!isPlausiblePointer(src) || !out || maxChars == 0) {
        return false;
    }
    __try {
        const auto* cstr = static_cast<const char*>(src);
        std::size_t len = 0;
        while (len < maxChars && cstr[len] != '\0') {
            out[len] = cstr[len];
            ++len;
        }
        out[len] = '\0';
        outLen = len;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        outLen = 0;
        return false;
    }
}

bool safeScanAsciiInRange(const std::uint8_t* base, std::size_t size, const char* needle, std::size_t needleLen) noexcept {
    if (!base || size < needleLen || needleLen == 0) {
        return false;
    }
    __try {
        for (std::size_t i = 0; i <= size - needleLen; ++i) {
            if (base[i] == static_cast<std::uint8_t>(needle[0]) &&
                std::memcmp(base + i, needle, needleLen) == 0) {
                return true;
            }
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool isLegacySampVoiceBinary(HMODULE mod) noexcept {
    if (!mod) {
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    if (!safeReadBytes(mod, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    IMAGE_NT_HEADERS nt{};
    if (!safeReadBytes(reinterpret_cast<const std::uint8_t*>(mod) + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const auto* modBase = reinterpret_cast<const std::uint8_t*>(mod);
    const std::size_t imgSize = nt.OptionalHeader.SizeOfImage;
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        modBase + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader);

    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER sec{};
        if (!safeReadBytes(&sections[i], &sec, sizeof(sec))) {
            break;
        }
        const bool isRead  = (sec.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        const bool isWrite = (sec.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        const bool isExec  = (sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (!isRead || isWrite || isExec || sec.VirtualAddress >= imgSize) {
            continue;
        }
        const std::size_t secSize = (std::min<std::size_t>)(
            static_cast<std::size_t>(sec.Misc.VirtualSize > 0 ? sec.Misc.VirtualSize : sec.SizeOfRawData),
            static_cast<std::size_t>(imgSize - sec.VirtualAddress));
        const auto* secPtr = modBase + sec.VirtualAddress;
        if (safeScanAsciiInRange(secPtr, secSize, "[sv:dbg:", 8) ||
            safeScanAsciiInRange(secPtr, secSize, "[sv:err:", 8) ||
            safeScanAsciiInRange(secPtr, secSize, "SampVoice Plus Client", 21)) {
            return true;
        }
    }
    return false;
}

bool equalsIgnoreCaseWideSuffix(const wchar_t* str, const wchar_t* suffix) noexcept {
    if (!str || !suffix) return false;
    const std::size_t sLen = std::wcslen(str);
    const std::size_t pLen = std::wcslen(suffix);
    if (sLen < pLen) return false;
    return _wcsicmp(str + (sLen - pLen), suffix) == 0;
}

bool containsIgnoreCaseWide(const wchar_t* haystack, const wchar_t* needle) noexcept {
    if (!haystack || !needle) return false;
    const std::size_t hLen = std::wcslen(haystack);
    const std::size_t nLen = std::wcslen(needle);
    if (nLen == 0 || hLen < nLen) return false;
    for (std::size_t i = 0; i <= hLen - nLen; ++i) {
        if (_wcsnicmp(haystack + i, needle, nLen) == 0) {
            return true;
        }
    }
    return false;
}

bool readForeignMsvcStringBuffer(const void* bxPtr, char* out, std::size_t capOut, std::size_t& outLen) noexcept {
    outLen = 0;
    if (!out || capOut == 0) {
        return false;
    }
    out[0] = '\0';

    std::uint8_t ssoHeader[24]{};
    if (!safeReadBytes(bxPtr, ssoHeader, sizeof(ssoHeader))) {
        return false;
    }
    std::uint32_t size = 0;
    std::uint32_t cap  = 0;
    std::memcpy(&size, ssoHeader + 0x10u, sizeof(size));
    std::memcpy(&cap,  ssoHeader + 0x14u, sizeof(cap));
    if (size == 0 || size > 64 || cap < 15 || cap > 4096) {
        return false;
    }

    const std::size_t maxCopy = (std::min)(static_cast<std::size_t>(size), capOut - 1u);
    if (cap < 16) {
        std::size_t len = 0;
        while (len < maxCopy && ssoHeader[len] != '\0') {
            out[len] = static_cast<char>(ssoHeader[len]);
            ++len;
        }
        out[len] = '\0';
        outLen = len;
        return true;
    }

    const char* textPtr = nullptr;
    std::memcpy(&textPtr, ssoHeader, sizeof(textPtr));
    return safeReadCString(textPtr, out, maxCopy, outLen);
}

std::string readForeignMsvcString(const void* bxPtr) {
    char buf[65]{};
    std::size_t len = 0;
    if (!readForeignMsvcStringBuffer(bxPtr, buf, sizeof(buf), len) || len == 0) {
        return {};
    }
    return std::string(buf, len);
}

bool readGtaEntityPosition(const void* gtaEntity, float outPos[3]) noexcept {
    void* matPtr = nullptr;
    if (!safeReadValue(gtaEntity, 0x14u, matPtr)) {
        return false;
    }
    if (isPlausiblePointer(matPtr)) {
        float pos[3]{};
        if (!safeReadBytes(static_cast<const std::uint8_t*>(matPtr) + 0x30u, pos, sizeof(pos))) {
            return false;
        }
        outPos[0] = pos[0];
        outPos[1] = pos[1];
        outPos[2] = pos[2];
    } else {
        float pos[3]{};
        if (!safeReadBytes(static_cast<const std::uint8_t*>(gtaEntity) + 0x04u, pos, sizeof(pos))) {
            return false;
        }
        outPos[0] = pos[0];
        outPos[1] = pos[1];
        outPos[2] = pos[2];
    }
    return std::isfinite(outPos[0]) && std::isfinite(outPos[1]) && std::isfinite(outPos[2]);
}

bool tryGetGtaPedBonePosition(const void* gtaPed, int boneId, float outPos[3]) noexcept {
    if (!isPlausiblePointer(gtaPed)) {
        return false;
    }
    void* rwObj = nullptr;
    if (!safeReadValue(gtaPed, kGtaPedRwObjectOffset, rwObj) || !isPlausiblePointer(rwObj)) {
        return false;
    }
    struct RwV3d { float x, y, z; } boneOut{};
    using GetBonePosFn = void(__fastcall*)(void* self, void* edx, RwV3d* outVec, int boneId, int update);
    __try {
        reinterpret_cast<GetBonePosFn>(kGtaPedGetBonePosAddr)(
            const_cast<void*>(gtaPed), nullptr, &boneOut, boneId, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!std::isfinite(boneOut.x) || !std::isfinite(boneOut.y) || !std::isfinite(boneOut.z)) {
        return false;
    }
    outPos[0] = boneOut.x;
    outPos[1] = boneOut.y;
    outPos[2] = boneOut.z;
    return true;
}

bool tryCheckGtaLineOfSightClear(const float origin[3],
                                  const float target[3],
                                  bool        failOpen) noexcept {
    struct RwV3d { float x, y, z; };
    const RwV3d a{origin[0], origin[1], origin[2]};
    const RwV3d b{target[0], target[1], target[2]};
    using LosFn = bool(__cdecl*)(const RwV3d*, const RwV3d*, bool, bool, bool, bool, bool, bool, bool);
    __try {
        // Check buildings & objects (matches SA-MP nametag LOS; excludes peds and vehicles).
        return reinterpret_cast<LosFn>(kGtaLineOfSightClearAddr)(
            &a, &b, true, false, false, true, false, false, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return failOpen;
    }
}

bool tryCheckGtaOcclusionLineOfSightClear(const float origin[3],
                                           const float target[3],
                                           bool        failOpen,
                                           const void* ignoreEntity = nullptr) noexcept {
    struct RwV3d { float x, y, z; };
    struct alignas(4) ColPointBuf { unsigned char bytes[0x2C]; }; // sizeof(CColPoint) == 0x2C
    static_assert(sizeof(ColPointBuf) == 0x2C);

    const RwV3d a{origin[0], origin[1], origin[2]};
    const RwV3d b{target[0], target[1], target[2]};
    ColPointBuf colPoint{};
    void* hitEntity = nullptr;

    using ProcessLosFn = bool(__cdecl*)(
        const RwV3d*, const RwV3d*, ColPointBuf*, void**,
        bool, bool, bool, bool, bool, bool, bool, bool);
    auto* const pIgnoreSlot = reinterpret_cast<const void* volatile*>(kGtaWorldIgnoreEntityAddr);
    const void* volatile prevIgnore = nullptr;
    volatile bool wroteIgnore = false;
    __try {
        if (ignoreEntity != nullptr) {
            prevIgnore = *pIgnoreSlot;
            *pIgnoreSlot = ignoreEntity;
            wroteIgnore = true;
        }
        // buildings=1, vehicles=0, peds=0, objects=1, dummies=0, doSeeThroughCheck=0, doCameraIgnoreCheck=0, doShootThroughCheck=1
        // Returns true if an intersection occurred before target; sound passes shoot-through fences/scaffolding/cloth while glass still muffles.
        const bool hit = reinterpret_cast<ProcessLosFn>(kGtaProcessLineOfSightAddr)(
            &a, &b, &colPoint, &hitEntity,
            true, false, false, true, false, false, false, true);
        if (wroteIgnore) {
            *pIgnoreSlot = prevIgnore;
        }
        return !hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (wroteIgnore) {
            __try {
                *pIgnoreSlot = prevIgnore;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return failOpen;
    }
}

void* readNetGameFromBase(const SampLayout& layout, std::uintptr_t sampBase) noexcept {
    if (!sampBase || !layout.netGamePtr) {
        return nullptr;
    }
    void* ptr = nullptr;
    if (!safeReadBytes(reinterpret_cast<const void*>(sampBase + layout.netGamePtr), &ptr, sizeof(ptr))) {
        return nullptr;
    }
    return isPlausiblePointer(ptr) ? ptr : nullptr;
}

const void* resolvePlayerPoolFromNetGame(const SampLayout& layout, const void* ng) noexcept {
    if (!ng || layout.version == SampVersion::Unknown) {
        return nullptr;
    }
    const void* pools = nullptr;
    const void* playerPool = nullptr;
    if (!safeReadValue(ng, layout.netGamePoolsOffset, pools) ||
        !safeReadValue(pools, layout.poolsPlayerOffset, playerPool) ||
        !isPlausiblePointer(playerPool)) {
        return nullptr;
    }
    return playerPool;
}

std::uint16_t readLocalPlayerIdFromPool(const SampLayout& layout, const void* playerPool) noexcept {
    std::uint16_t localId = protocol::kInvalidPlayerId;
    if (!playerPool || !safeReadValue(playerPool, layout.playerPoolLocalIdOffset, localId)) {
        return protocol::kInvalidPlayerId;
    }
    return localId;
}

const void* resolveRemotePlayerFromPool(
    const SampLayout& layout,
    const void* playerPool,
    std::uint16_t playerId) noexcept {
    if (!playerPool || playerId >= kMaxPlayerSlots || layout.version == SampVersion::Unknown) {
        return nullptr;
    }
    const auto* notEmpty = static_cast<const std::uint8_t*>(playerPool) + layout.playerPoolNotEmptyOffset;
    BOOL present = FALSE;
    if (!safeReadValue(notEmpty, static_cast<std::uint32_t>(playerId * sizeof(BOOL)), present) || !present) {
        return nullptr;
    }
    const auto* objects = static_cast<const std::uint8_t*>(playerPool) + layout.playerPoolArrayOffset;
    const void* info = nullptr;
    const void* remote = nullptr;
    if (!safeReadValue(objects, static_cast<std::uint32_t>(playerId * sizeof(void*)), info) ||
        !safeReadValue(info, layout.playerInfoRemoteOffset, remote) ||
        !isPlausiblePointer(remote)) {
        return nullptr;
    }
    return remote;
}

const void* resolveLocalGtaPedFromBase(const SampLayout& layout, std::uintptr_t sampBase) noexcept {
    if (!sampBase || !layout.gamePtr) {
        return nullptr;
    }
    const void* game = nullptr;
    if (!safeReadBytes(reinterpret_cast<const void*>(sampBase + layout.gamePtr), &game, sizeof(game))) {
        return nullptr;
    }
    const void* ped = nullptr;
    const void* gtaPed = nullptr;
    if (!safeReadValue(game, layout.gamePlayerPedOffset, ped) ||
        !safeReadValue(ped, layout.pedGamePedOffset, gtaPed)) {
        return nullptr;
    }
    return gtaPed;
}

const void* resolveRemoteGtaPedFromRemote(const SampLayout& layout, const void* remote) noexcept {
    if (!remote) {
        return nullptr;
    }
    const void* ped = nullptr;
    const void* gtaPed = nullptr;
    if (!safeReadValue(remote, layout.remotePlayerPedOffset, ped) ||
        !safeReadValue(ped, layout.pedGamePedOffset, gtaPed)) {
        return nullptr;
    }
    return gtaPed;
}

bool resolveHeadPositionFromGtaPed(
    const void* gtaPed,
    NametagHeadPosFn headPosLookup,
    void* callbackCtx,
    float outPos[3],
    bool* outUsedFallback = nullptr) noexcept {
    if (outUsedFallback) {
        *outUsedFallback = false;
    }
    if (!gtaPed) {
        return false;
    }
    float pedPos[3]{};
    const bool hasPedPos = readGtaEntityPosition(gtaPed, pedPos);
    float bonePos[3]{};
    if (headPosLookup && headPosLookup(gtaPed, bonePos, callbackCtx) &&
        std::isfinite(bonePos[0]) && std::isfinite(bonePos[1]) && std::isfinite(bonePos[2])) {
        if (!hasPedPos) {
            outPos[0] = bonePos[0];
            outPos[1] = bonePos[1];
            outPos[2] = bonePos[2];
            return true;
        }
        const float dx = bonePos[0] - pedPos[0];
        const float dy = bonePos[1] - pedPos[1];
        const float dz = bonePos[2] - pedPos[2];
        if ((dx * dx + dy * dy + dz * dz) <= 9.0f) {
            outPos[0] = bonePos[0];
            outPos[1] = bonePos[1];
            outPos[2] = bonePos[2];
            return true;
        }
    }
    if (!hasPedPos) {
        return false;
    }
    if (outUsedFallback) {
        *outUsedFallback = true;
    }
    outPos[0] = pedPos[0];
    outPos[1] = pedPos[1];
    outPos[2] = pedPos[2] + 0.65f;
    return true;
}

bool gtaHeadBoneLookup(const void* gtaPed, float outPos[3], void* /*ctx*/) noexcept {
    return tryGetGtaPedBonePosition(gtaPed, 8, outPos);
}

bool gtaLineOfSightClearCheck(const float origin[3], const float target[3], void* /*ctx*/) noexcept {
    return tryCheckGtaLineOfSightClear(origin, target, false);
}

bool isValidBasisVector(const float v[3]) noexcept {
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) {
        return false;
    }
    const float lenSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    return lenSq > 1e-4f && lenSq < 1e4f;
}

} // namespace

// -----------------------------------------------------------------------------
// ProxyRakClient (heap wrapper around RakClientInterface, game-thread only)
// -----------------------------------------------------------------------------

class SampBridge::ProxyRakClient final : public RakClientInterface {
public:
    ProxyRakClient(SampBridge& owner, RakClientInterface* orig) noexcept
        : owner_(owner), orig_(orig) {}

    RakClientInterface* orig() const noexcept {
        return destroyed_.load(std::memory_order_acquire) ? nullptr : orig_;
    }

    bool isDestroyed() const noexcept {
        return destroyed_.load(std::memory_order_acquire);
    }

    RakClientInterface* markDestroyedAndDetachOrig() noexcept {
        const bool wasDestroyed = destroyed_.exchange(true, std::memory_order_acq_rel);
        RakClientInterface* detached = wasDestroyed ? nullptr : orig_;
        orig_ = nullptr;
        owner_.onProxyDestroyed(this);
        return detached;
    }

    bool Connect(const char* host, unsigned short serverPort, unsigned short clientPort,
                 unsigned int depreciated, int threadSleepTimer) override {
        auto* o = orig();
        if (!o) {
            return false;
        }
        if (owner_.onConnectPrepared_ && host) {
            owner_.onConnectPrepared_(host, 0u, serverPort, owner_.microAvailable_.load(std::memory_order_acquire));
        }
        return o->Connect(host, serverPort, clientPort, depreciated, threadSleepTimer);
    }
    void Disconnect(unsigned int blockDuration, unsigned char orderingChannel = 0) override {
        if (auto* o = orig()) o->Disconnect(blockDuration, orderingChannel);
    }
    void InitializeSecurity(const char* privKeyP, const char* privKeyQ) override {
        if (auto* o = orig()) o->InitializeSecurity(privKeyP, privKeyQ);
    }
    void SetPassword(const char* password) override {
        if (auto* o = orig()) o->SetPassword(password);
    }
    bool HasPassword(void) const override {
        auto* o = orig();
        return o ? o->HasPassword() : false;
    }

    bool Send(const char* data, const int length, PacketPriority priority,
              PacketReliability reliability, char orderingChannel) override {
        auto* o = orig();
        return o ? o->Send(data, length, priority, reliability, orderingChannel) : false;
    }

    bool Send(RakNet::BitStream* bitStream, PacketPriority priority,
              PacketReliability reliability, char orderingChannel) override {
        auto* o = orig();
        return o ? o->Send(bitStream, priority, reliability, orderingChannel) : false;
    }

    Packet* Receive(void) override {
        auto* o = orig();
        if (!o) {
            return nullptr;
        }
        owner_.flushOutgoingControlPacketsOnMainThread();
        while (true) {
            o = orig();
            if (!o) {
                return nullptr;
            }
            Packet* pkt = o->Receive();
            if (!pkt) {
                return nullptr;
            }
            if (pkt->data && pkt->length >= 1 + sizeof(protocol::ControlPacketHeader) &&
                pkt->data[0] == protocol::kRakNetPacketIdControl) {
                protocol::ControlPacketHeader hdr{};
                std::memcpy(&hdr, pkt->data + 1, sizeof(hdr));
                const std::size_t avail = pkt->length - 1 - sizeof(hdr);
                if (hdr.length <= avail && owner_.onControl_) {
                    owner_.onControl_(hdr.packet, pkt->data + 1 + sizeof(hdr), hdr.length);
                }
                if (auto* oDealloc = orig()) {
                    oDealloc->DeallocatePacket(pkt);
                }
                continue;
            }
            return pkt;
        }
    }

    void DeallocatePacket(Packet* packet) override {
        if (auto* o = orig()) o->DeallocatePacket(packet);
    }
    void PingServer(void) override {
        if (auto* o = orig()) o->PingServer();
    }
    void PingServer(const char* host, unsigned short serverPort, unsigned short clientPort,
                    bool onlyReplyOnAcceptingConnections) override {
        if (auto* o = orig()) o->PingServer(host, serverPort, clientPort, onlyReplyOnAcceptingConnections);
    }
    int GetAveragePing(void) override {
        auto* o = orig();
        return o ? o->GetAveragePing() : -1;
    }
    int GetLastPing(void) const override {
        auto* o = orig();
        return o ? o->GetLastPing() : -1;
    }
    int GetLowestPing(void) const override {
        auto* o = orig();
        return o ? o->GetLowestPing() : -1;
    }
    int GetPlayerPing(const PlayerID playerId) override {
        auto* o = orig();
        return o ? o->GetPlayerPing(playerId) : -1;
    }
    void StartOccasionalPing(void) override {
        if (auto* o = orig()) o->StartOccasionalPing();
    }
    void StopOccasionalPing(void) override {
        if (auto* o = orig()) o->StopOccasionalPing();
    }
    bool IsConnected(void) const override {
        auto* o = orig();
        return o ? o->IsConnected() : false;
    }
    unsigned int GetSynchronizedRandomInteger(void) const override {
        auto* o = orig();
        return o ? o->GetSynchronizedRandomInteger() : 0u;
    }
    bool GenerateCompressionLayer(unsigned int table[256], bool inputLayer) override {
        auto* o = orig();
        return o ? o->GenerateCompressionLayer(table, inputLayer) : false;
    }
    bool DeleteCompressionLayer(bool inputLayer) override {
        auto* o = orig();
        return o ? o->DeleteCompressionLayer(inputLayer) : false;
    }
    void RegisterAsRemoteProcedureCall(int* uniqueID, void (*fn)(RPCParameters*)) override {
        if (auto* o = orig()) o->RegisterAsRemoteProcedureCall(uniqueID, fn);
    }
    void RegisterClassMemberRPC(int* uniqueID, void* fn) override {
        if (auto* o = orig()) o->RegisterClassMemberRPC(uniqueID, fn);
    }
    void UnregisterAsRemoteProcedureCall(int* uniqueID) override {
        if (auto* o = orig()) o->UnregisterAsRemoteProcedureCall(uniqueID);
    }

    bool RPC(int* uniqueID, const char* data, unsigned int bitLength, PacketPriority priority,
             PacketReliability reliability, char orderingChannel, bool shiftTimestamp) override {
        auto* o = orig();
        return o ? o->RPC(uniqueID, data, bitLength, priority, reliability, orderingChannel, shiftTimestamp) : false;
    }

    bool RPC(int* uniqueID, RakNet::BitStream* bitStream, PacketPriority priority,
             PacketReliability reliability, char orderingChannel, bool shiftTimestamp) override {
        auto* o = orig();
        if (!o) {
            return false;
        }
        if (uniqueID && *uniqueID == protocol::kRakNetRpcPlayerConnect && bitStream &&
            !owner_.conflictingClient_.load(std::memory_order_acquire)) {
            RakNet::BitStream patchedBs;
            const auto origBits = bitStream->GetNumberOfBitsUsed();
            if (origBits > 0) {
                patchedBs.WriteBits(bitStream->GetData(), origBits, false);
            }

            // Always advertise wire version 11 for full compatibility with CyberMor v3.x / AmyrAhmady
            // servers; SampVoice Plus servers detect ConnectPlusExtension and upgrade to proto v12.
            protocol::ConnectPacket conn{};
            conn.signature = protocol::kConnectSignature;
            conn.version   = protocol::kLegacyVersion;
            conn.micro     = owner_.microAvailable_.load(std::memory_order_acquire) ? 1 : 0;

            protocol::ConnectPlusExtension ext{};
            ext.magic        = protocol::kPlusExtensionMagic;
            ext.capabilities =
                static_cast<std::uint32_t>(protocol::PlusCapabilityFlags::AdaptiveJitter) |
                static_cast<std::uint32_t>(protocol::PlusCapabilityFlags::Telemetry);
            ext.clientBuild = svplus::version::kClientBuild;

            patchedBs.Write(reinterpret_cast<const char*>(&conn), sizeof(conn));
            patchedBs.Write(reinterpret_cast<const char*>(&ext), sizeof(ext));

            const PlayerID serverId = o->GetServerID();
            const std::uint32_t ipv4Net = (serverId.binaryAddress != 0xFFFFFFFFu) ? serverId.binaryAddress : 0u;
            const std::string host = owner_.getServerHostAddress();
            const int port = (serverId.port != 0) ? static_cast<int>(serverId.port) : owner_.getServerPort();
            owner_.connectedIpv4Net_.store(ipv4Net, std::memory_order_release);
            owner_.connectedRakPort_.store(
                (port > 0 && port <= 65535) ? static_cast<std::uint16_t>(port) : 0u,
                std::memory_order_release);
            if (owner_.onConnectPrepared_) {
                owner_.onConnectPrepared_(
                    host, ipv4Net, port > 0 ? static_cast<std::uint16_t>(port) : 0u, conn.micro != 0);
            }
            SVPLUS_LOG_INF_CAT("samp", "Sent SampVoice handshake on RPC 25 (wire=v%u, build=0x%04X, micro=%u, server=%s:%d)",
                               static_cast<unsigned>(conn.version),
                               static_cast<unsigned>(ext.clientBuild),
                               static_cast<unsigned>(conn.micro),
                               host.empty() ? "<unknown>" : host.c_str(),
                               port);
            return o->RPC(uniqueID, &patchedBs, priority, reliability, orderingChannel, shiftTimestamp);
        }
        return o->RPC(uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestamp);
    }

    bool RPC_(int* uniqueID, RakNet::BitStream* bitStream, PacketPriority priority,
              PacketReliability reliability, char orderingChannel, bool shiftTimestamp, NetworkID networkID) override {
        auto* o = orig();
        return o ? o->RPC_(uniqueID, bitStream, priority, reliability, orderingChannel, shiftTimestamp, networkID) : false;
    }

    void SetTrackFrequencyTable(bool b) override {
        if (auto* o = orig()) o->SetTrackFrequencyTable(b);
    }
    bool GetSendFrequencyTable(unsigned int out[256]) override {
        auto* o = orig();
        return o ? o->GetSendFrequencyTable(out) : false;
    }
    float GetCompressionRatio(void) const override {
        auto* o = orig();
        return o ? o->GetCompressionRatio() : 0.0f;
    }
    float GetDecompressionRatio(void) const override {
        auto* o = orig();
        return o ? o->GetDecompressionRatio() : 0.0f;
    }
    void AttachPlugin(void* mh) override {
        if (auto* o = orig()) o->AttachPlugin(mh);
    }
    void DetachPlugin(void* mh) override {
        if (auto* o = orig()) o->DetachPlugin(mh);
    }
    RakNet::BitStream* GetStaticServerData(void) override {
        auto* o = orig();
        return o ? o->GetStaticServerData() : nullptr;
    }
    void SetStaticServerData(const char* data, const int length) override {
        if (auto* o = orig()) o->SetStaticServerData(data, length);
    }
    RakNet::BitStream* GetStaticClientData(const PlayerID id) override {
        auto* o = orig();
        return o ? o->GetStaticClientData(id) : nullptr;
    }
    void SetStaticClientData(const PlayerID id, const char* data, const int length) override {
        if (auto* o = orig()) o->SetStaticClientData(id, data, length);
    }
    void SendStaticClientDataToServer(void) override {
        if (auto* o = orig()) o->SendStaticClientDataToServer();
    }
    PlayerID GetServerID(void) const override {
        auto* o = orig();
        return o ? o->GetServerID() : UNASSIGNED_PLAYER_ID;
    }
    PlayerID GetPlayerID(void) const override {
        auto* o = orig();
        return o ? o->GetPlayerID() : UNASSIGNED_PLAYER_ID;
    }
    PlayerID GetInternalID(void) const override {
        auto* o = orig();
        return o ? o->GetInternalID() : UNASSIGNED_PLAYER_ID;
    }
    const char* PlayerIDToDottedIP(const PlayerID id) const override {
        auto* o = orig();
        return o ? o->PlayerIDToDottedIP(id) : "";
    }
    void PushBackPacket(Packet* packet, bool pushAtHead) override {
        if (auto* o = orig()) o->PushBackPacket(packet, pushAtHead);
    }
    void SetRouterInterface(void* ri) override {
        if (auto* o = orig()) o->SetRouterInterface(ri);
    }
    void RemoveRouterInterface(void* ri) override {
        if (auto* o = orig()) o->RemoveRouterInterface(ri);
    }
    void SetTimeoutTime(RakNetTime timeMS) override {
        if (auto* o = orig()) o->SetTimeoutTime(timeMS);
    }
    bool SetMTUSize(int size) override {
        auto* o = orig();
        return o ? o->SetMTUSize(size) : false;
    }
    int GetMTUSize(void) const override {
        auto* o = orig();
        return o ? o->GetMTUSize() : 0;
    }
    void AllowConnectionResponseIPMigration(bool allow) override {
        if (auto* o = orig()) o->AllowConnectionResponseIPMigration(allow);
    }
    void AdvertiseSystem(const char* host, unsigned short remotePort, const char* data, int len) override {
        if (auto* o = orig()) o->AdvertiseSystem(host, remotePort, data, len);
    }
    void* const GetStatistics(void) override {
        auto* o = orig();
        return o ? o->GetStatistics() : nullptr;
    }
    void ApplyNetworkSimulator(double maxSendBPS, unsigned short minExtra, unsigned short var) override {
        if (auto* o = orig()) o->ApplyNetworkSimulator(maxSendBPS, minExtra, var);
    }
    bool IsNetworkSimulatorActive(void) override {
        auto* o = orig();
        return o ? o->IsNetworkSimulatorActive() : false;
    }
    PlayerIndex GetPlayerIndex(void) override {
        auto* o = orig();
        return o ? o->GetPlayerIndex() : 0;
    }

private:
    SampBridge&              owner_;
    RakClientInterface*      orig_;
    std::atomic<bool>        destroyed_{false};
};

// -----------------------------------------------------------------------------
// SampBridge Implementation
// -----------------------------------------------------------------------------

SampBridge& SampBridge::instance() noexcept {
    static auto* inst = new SampBridge();
    return *inst;
}

bool SampBridge::detectConflictingClientModule(
    void*         selfModule,
    std::wstring* outConflictPath) noexcept {
    if (outConflictPath) {
        outConflictPath->clear();
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool found = false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (me.hModule == static_cast<HMODULE>(selfModule)) {
                continue;
            }
            if (!equalsIgnoreCaseWideSuffix(me.szModule, L".asi")) {
                continue;
            }
            if (containsIgnoreCaseWide(me.szModule, L"sampvoice") || isLegacySampVoiceBinary(me.hModule)) {
                if (outConflictPath) {
                    *outConflictPath = (me.szExePath[0] != L'\0') ? me.szExePath : me.szModule;
                }
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }

    CloseHandle(snap);
    return found;
}

void SampBridge::setCallbacks(ControlPacketCallback onControl, ConnectStateCallback onConnectPrepared) {
    onControl_ = std::move(onControl);
    onConnectPrepared_ = std::move(onConnectPrepared);
}

bool SampBridge::isVtableInSamp(const void* objPtr) const noexcept {
    if (sampBase_ == 0 || sampSize_ == 0) {
        return false;
    }
    std::uintptr_t vtableAddr = 0;
    if (!safeReadValue(objPtr, 0u, vtableAddr)) {
        return false;
    }
    return vtableAddr >= sampBase_ && vtableAddr < (sampBase_ + sampSize_);
}

void SampBridge::checkConflictWindow() noexcept {
    if (conflictingClient_.load(std::memory_order_acquire) || conflictWindowExpired_) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (firstTickTime_.time_since_epoch().count() == 0) {
        firstTickTime_ = now;
    }
    if ((now - firstTickTime_) > std::chrono::seconds(10)) {
        conflictWindowExpired_ = true;
        return;
    }
    if (lastConflictCheckTime_.time_since_epoch().count() != 0 &&
        (now - lastConflictCheckTime_) < std::chrono::milliseconds(500)) {
        return;
    }
    lastConflictCheckTime_ = now;

    std::wstring conflictPath;
    if (detectConflictingClientModule(selfModule_, &conflictPath)) {
        conflictingClient_.store(true, std::memory_order_release);
        std::string conflictUtf8 = "<unknown>";
        if (!conflictPath.empty()) {
            const int needed = WideCharToMultiByte(
                CP_UTF8, 0, conflictPath.c_str(), static_cast<int>(conflictPath.size()),
                nullptr, 0, nullptr, nullptr);
            if (needed > 0) {
                conflictUtf8.assign(static_cast<std::size_t>(needed), '\0');
                WideCharToMultiByte(
                    CP_UTF8, 0, conflictPath.c_str(), static_cast<int>(conflictPath.size()),
                    conflictUtf8.data(), needed, nullptr, nullptr);
            }
        }
        SVPLUS_LOG_ERR_CAT(
            "samp",
            "Conflicting voice client detected at '%s' after startup; remove one copy and restart. Disabling SampVoice Plus RakNet hooks.",
            conflictUtf8.c_str());
        shutdown();
    }
}

void SampBridge::detectVersionIfNeeded() noexcept {
    if (sampBase_ != 0 && layout_.version != SampVersion::Unknown) {
        return;
    }
    if (unsupportedDetected_) {
        return;
    }

    const HMODULE mod = detectedSampModuleHandle();
    if (!mod) {
        return;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(mod);
    IMAGE_DOS_HEADER dos{};
    if (!safeReadBytes(reinterpret_cast<const void*>(base), &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    IMAGE_NT_HEADERS nt{};
    if (!safeReadBytes(reinterpret_cast<const void*>(base + dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return;
    }

    const SampVersion ver = detectSampVersion(
        nt.FileHeader.TimeDateStamp,
        nt.OptionalHeader.AddressOfEntryPoint);
    if (ver != SampVersion::Unknown) {
        sampBase_ = base;
        sampSize_ = nt.OptionalHeader.SizeOfImage;
        layout_   = getSampLayout(ver);
        SVPLUS_LOG_INF_CAT("samp",
                           "Bound SampBridge to %s (base=0x%08X, size=0x%X, stamp=0x%08X, EP=0x%08X, colorTableRVA=0x%06X)",
                           layout_.name,
                           static_cast<unsigned>(sampBase_),
                           static_cast<unsigned>(sampSize_),
                           static_cast<unsigned>(nt.FileHeader.TimeDateStamp),
                           static_cast<unsigned>(nt.OptionalHeader.AddressOfEntryPoint),
                           static_cast<unsigned>(layout_.colorTableRva));

        // Read-only verification of SA-MP RakClient teardown displacement (no patches in SA-MP).
        std::vector<std::uint8_t> imageCopy(sampSize_);
        if (safeReadBytes(reinterpret_cast<const void*>(sampBase_), imageCopy.data(), imageCopy.size())) {
            std::uintptr_t dtorRva = 0;
            std::int32_t disp = 0;
            if (scanSampRakClientDestroyDisplacement(imageCopy.data(), imageCopy.size(), dtorRva, disp)) {
                const std::ptrdiff_t negOffset = -static_cast<std::ptrdiff_t>(disp);
                if (negOffset == static_cast<std::ptrdiff_t>(kRakClientInRakPeerOffset)) {
                    rakDtorVerified_ = true;
                    SVPLUS_LOG_INF_CAT("samp", "Verified RakClient dtor layout on %s (rva=0x%05X, offset=0x%X)",
                                       layout_.name,
                                       static_cast<unsigned>(dtorRva),
                                       static_cast<unsigned>(kRakClientInRakPeerOffset));
                } else {
                    SVPLUS_LOG_WRN_CAT("samp", "Unexpected RakClient dtor displacement on %s (rva=0x%05X, disp=%d, expected=-0x%X)",
                                       layout_.name,
                                       static_cast<unsigned>(dtorRva),
                                       static_cast<int>(disp),
                                       static_cast<unsigned>(kRakClientInRakPeerOffset));
                }
            } else {
                SVPLUS_LOG_WRN_CAT("samp", "RakClient teardown pattern not matched in %s; using shadow RakPeer offset 0x%X",
                                   layout_.name,
                                   static_cast<unsigned>(kRakClientInRakPeerOffset));
            }
        }
    } else {
        unsupportedDetected_ = true;
        SVPLUS_LOG_ERR_CAT(
            "samp",
            "Unsupported SA-MP module signature (TimeDateStamp=0x%08X, EP=0x%08X); SA-MP hooks disabled.",
            static_cast<unsigned>(nt.FileHeader.TimeDateStamp),
            static_cast<unsigned>(nt.OptionalHeader.AddressOfEntryPoint));
    }
}

void* SampBridge::currentNetGamePtr() const noexcept {
    return readNetGameFromBase(layout_, sampBase_);
}

void SampBridge::tryHookRakClient() {
    if (conflictingClient_.load(std::memory_order_acquire) || rakTornDown_) {
        return;
    }
    void* netGame = currentNetGamePtr();
    if (!netGame) {
        return;
    }

    RakClientInterface* current = nullptr;
    if (!safeReadValue(netGame, layout_.netGameRakClientOffset, current) || !isPlausiblePointer(current)) {
        return;
    }

    for (ProxyRakClient* existing : proxies_) {
        if (current == existing) {
            if (!existing || existing->isDestroyed() || !existing->orig()) {
                return;
            }
            activeProxy_ = existing;
            rakHooked_ = true;
            return;
        }
    }

    if (!isVtableInSamp(current)) {
        return;
    }

    diag::reportRakClientInspection(current);

    ProxyRakClient* wrapper = ShadowRakPeerFactory<ProxyRakClient, RakClientInterface>::create(*this, current);
    activeProxy_ = wrapper;
    proxies_.push_back(wrapper);
    auto** rakSlot = reinterpret_cast<RakClientInterface**>(
        static_cast<std::uint8_t*>(netGame) + layout_.netGameRakClientOffset);
    *rakSlot = activeProxy_;
    rakHooked_ = true;
    SVPLUS_LOG_INF_CAT("samp", "Attached ProxyRakClient in shadow RakPeer block on %s (CNetGame=0x%p, proxy=0x%p)",
                       layout_.name, netGame, static_cast<void*>(activeProxy_));
}

void SampBridge::onProxyDestroyed(const ProxyRakClient* proxy) noexcept {
    rakTornDown_ = true;
    if (activeProxy_ == proxy) {
        activeProxy_ = nullptr;
        rakHooked_ = false;
    }
}

void SampBridge::flushOutgoingControlPacketsOnMainThread() noexcept {
    outgoingControlQueue_.drain(outgoingDrainScratch_);
    for (const auto& msg : outgoingDrainScratch_) {
        if (activeProxy_ && activeProxy_->orig() && activeProxy_->orig()->IsConnected() && msg.size > 0) {
            activeProxy_->orig()->Send(
                reinterpret_cast<const char*>(msg.bytes.data()),
                msg.size,
                HIGH_PRIORITY,
                RELIABLE,
                0);
        }
    }
}

void SampBridge::updateSnapshotsOnMainThread() {
    const auto now = std::chrono::steady_clock::now();
    if (lastNameRefreshTime_.time_since_epoch().count() != 0 &&
        (now - lastNameRefreshTime_) < std::chrono::milliseconds(250)) {
        return;
    }
    lastNameRefreshTime_ = now;

    void* ng = currentNetGamePtr();
    const void* playerPool = nullptr;
    if (ng && layout_.version != SampVersion::Unknown) {
        const void* pools = nullptr;
        if (safeReadValue(ng, layout_.netGamePoolsOffset, pools) && isPlausiblePointer(pools)) {
            safeReadValue(pools, layout_.poolsPlayerOffset, playerPool);
        }
    }

    std::lock_guard lock(cacheMutex_);
    bool changed = false;
    char stackBuf[65]{};

    if (!isPlausiblePointer(playerPool)) {
        for (std::size_t pid = 0; pid < kMaxPlayerSlots; ++pid) {
            if (!cachedPlayerNames_[pid].empty() || !cachedRawPlayerNames_[pid].empty()) {
                cachedRawPlayerNames_[pid].clear();
                cachedPlayerNames_[pid].clear();
                changed = true;
            }
        }
    } else {
        const auto* objects  = static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolArrayOffset;
        const auto* notEmpty = static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolNotEmptyOffset;
        for (std::size_t pid = 0; pid < kMaxPlayerSlots; ++pid) {
            BOOL present = FALSE;
            const void* info = nullptr;
            std::size_t nameLen = 0;
            bool ok = safeReadValue(notEmpty, static_cast<std::uint32_t>(pid * sizeof(BOOL)), present) &&
                      present != FALSE &&
                      safeReadValue(objects, static_cast<std::uint32_t>(pid * sizeof(void*)), info) &&
                      isPlausiblePointer(info) &&
                      readForeignMsvcStringBuffer(
                          static_cast<const std::uint8_t*>(info) + layout_.playerInfoNickOffset,
                          stackBuf, sizeof(stackBuf), nameLen);
            if (!ok) {
                if (!cachedPlayerNames_[pid].empty() || !cachedRawPlayerNames_[pid].empty()) {
                    cachedRawPlayerNames_[pid].clear();
                    cachedPlayerNames_[pid].clear();
                    changed = true;
                }
                continue;
            }
            const std::string_view sv(stackBuf, nameLen);
            if (cachedRawPlayerNames_[pid] != sv) {
                cachedRawPlayerNames_[pid].assign(sv);
                cachedPlayerNames_[pid] = util::ensureUtf8Name(sv);
                changed = true;
            }
        }
    }

    if (changed) {
        nameCacheGen_.fetch_add(1, std::memory_order_release);
    }
}

void SampBridge::tick() {
    checkConflictWindow();
    if (conflictingClient_.load(std::memory_order_acquire)) {
        return;
    }

    detectVersionIfNeeded();
    if (layout_.version != SampVersion::Unknown) {
        tryHookRakClient();
        flushOutgoingControlPacketsOnMainThread();
        updateSnapshotsOnMainThread();

        const bool nowConn = isConnected();
        if (nowConn && activeProxy_ && activeProxy_->orig()) {
            const PlayerID sid = activeProxy_->orig()->GetServerID();
            if (sid.binaryAddress != 0u && sid.binaryAddress != 0xFFFFFFFFu) {
                connectedIpv4Net_.store(sid.binaryAddress, std::memory_order_release);
            }
            if (sid.port != 0u) {
                connectedRakPort_.store(sid.port, std::memory_order_release);
            }
        } else if (!nowConn) {
            connectedIpv4Net_.store(0u, std::memory_order_release);
            connectedRakPort_.store(0u, std::memory_order_release);
        }
        if (nowConn != prevConnected_) {
            prevConnected_ = nowConn;
            if (nowConn) {
                const std::string host = getServerHostAddress();
                const int port = getServerPort();
                const std::uint16_t localId = getLocalPlayerId();
                const std::string localName = getLocalPlayerName();
                SVPLUS_LOG_INF_CAT("samp", "SA-MP connected: server=%s:%d localPlayer=[%u] '%s'",
                                   host.empty() ? "<unknown>" : host.c_str(),
                                   port,
                                   static_cast<unsigned>(localId),
                                   localName.c_str());
            } else {
                prevSpawned_ = false;
                outgoingControlQueue_.resetOnMainThread();
                SVPLUS_LOG_INF_CAT("samp", "SA-MP disconnected from server");
            }
        }
        if (nowConn) {
            const bool nowSpawn = isLocalPlayerSpawned();
            if (nowSpawn != prevSpawned_) {
                prevSpawned_ = nowSpawn;
                SVPLUS_LOG_INF_CAT("samp", "Local player state changed: %s",
                                   nowSpawn ? "spawned" : "unspawned/selection");
            }
        }
    }
}

void SampBridge::shutdown() noexcept {
    if (cursorOpenedByMenu_) {
        setMenuCursorMode(false);
    }
    if (!rakTornDown_ && activeProxy_ && !activeProxy_->isDestroyed()) {
        if (void* netGame = currentNetGamePtr()) {
            RakClientInterface* current = nullptr;
            if (safeReadValue(netGame, layout_.netGameRakClientOffset, current)) {
                for (ProxyRakClient* p : proxies_) {
                    if (p && !p->isDestroyed() && current == p) {
                        auto** rakSlot = reinterpret_cast<RakClientInterface**>(
                            static_cast<std::uint8_t*>(netGame) + layout_.netGameRakClientOffset);
                        *rakSlot = p->orig();
                        break;
                    }
                }
            }
        }
    }
    activeProxy_ = nullptr;
    outgoingControlQueue_.resetOnMainThread();
    rakHooked_ = false;
    cameraBasisChecked_ = false;
}

bool SampBridge::isConnected() const noexcept {
    if (!rakHooked_ || !activeProxy_ || !activeProxy_->orig()) {
        return false;
    }
    void* ng = currentNetGamePtr();
    if (!ng) {
        return false;
    }
    return activeProxy_->orig()->IsConnected();
}

bool SampBridge::isLocalPlayerSpawned() const noexcept {
    if (!isConnected()) {
        return false;
    }
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown) {
        return false;
    }
    if (layout_.playerPoolLocalPlayerOffset != 0u && layout_.localPlayerIsActiveOffset != 0u) {
        const void* pools = nullptr;
        const void* playerPool = nullptr;
        const void* localPlayer = nullptr;
        BOOL isActive = FALSE;
        if (!safeReadValue(ng, layout_.netGamePoolsOffset, pools) ||
            !safeReadValue(pools, layout_.poolsPlayerOffset, playerPool) ||
            !safeReadValue(playerPool, layout_.playerPoolLocalPlayerOffset, localPlayer) ||
            !localPlayer ||
            !safeReadValue(localPlayer, layout_.localPlayerIsActiveOffset, isActive)) {
            return false;
        }
        return isActive != FALSE;
    }
    float dummyPos[3]{};
    return getLocalPlayerPedPosition(dummyPos);
}

bool SampBridge::isChatInputOpen() const noexcept {
    if (!sampBase_ || !layout_.inputPtr) {
        return false;
    }
    const void* input = nullptr;
    if (!safeReadBytes(reinterpret_cast<const void*>(sampBase_ + layout_.inputPtr), &input, sizeof(input))) {
        return false;
    }
    BOOL enabled = FALSE;
    return safeReadValue(input, layout_.inputEnabledOffset, enabled) && enabled != FALSE;
}

bool SampBridge::isDialogOpen() const noexcept {
    if (!sampBase_ || !layout_.dialogPtr) {
        return false;
    }
    const void* dlg = nullptr;
    if (!safeReadBytes(reinterpret_cast<const void*>(sampBase_ + layout_.dialogPtr), &dlg, sizeof(dlg))) {
        return false;
    }
    BOOL active = FALSE;
    return safeReadValue(dlg, layout_.dialogIsActiveOffset, active) && active != FALSE;
}

bool SampBridge::isScoreboardOpen() const noexcept {
    if (!sampBase_ || !layout_.scoreboardPtr) {
        return false;
    }
    const void* sb = nullptr;
    if (!safeReadBytes(reinterpret_cast<const void*>(sampBase_ + layout_.scoreboardPtr), &sb, sizeof(sb))) {
        return false;
    }
    BOOL enabled = FALSE;
    return safeReadValue(sb, layout_.scoreboardEnabledOffset, enabled) && enabled != FALSE;
}

bool SampBridge::isGameMenuOpen() const noexcept {
    bool open = false;
    return safeReadBytes(reinterpret_cast<const void*>(kGtaMenuActiveAddr), &open, sizeof(open)) && open;
}

bool SampBridge::isTextInputBlockingKeys() const noexcept {
    return isChatInputOpen() || isDialogOpen() || isGameMenuOpen();
}

void SampBridge::setMenuCursorMode(bool open) noexcept {
    if (open == cursorOpenedByMenu_) {
        return;
    }
    cursorOpenedByMenu_ = open;

    void* game = nullptr;
    using SetCursorModeFn = void(__thiscall*)(void*, int, BOOL);
    SetCursorModeFn fn = nullptr;
    if (sampBase_ && layout_.gamePtr && layout_.gameSetCursorModeRva) {
        if (safeReadBytes(reinterpret_cast<const void*>(sampBase_ + layout_.gamePtr), &game, sizeof(game)) &&
            isPlausiblePointer(game)) {
            fn = reinterpret_cast<SetCursorModeFn>(sampBase_ + layout_.gameSetCursorModeRva);
        }
    }

    if (open) {
        CURSORINFO ci{};
        ci.cbSize = sizeof(ci);
        savedCursorState_.winCursorShowing = (GetCursorInfo(&ci) != FALSE) && ((ci.flags & CURSOR_SHOWING) != 0);
        savedCursorState_.winCursorHandle  = static_cast<void*>(GetCursor());

        RECT rc{};
        if (GetClipCursor(&rc) != FALSE) {
            savedCursorState_.hasClipRect = true;
            savedCursorState_.clipLeft   = rc.left;
            savedCursorState_.clipTop    = rc.top;
            savedCursorState_.clipRight  = rc.right;
            savedCursorState_.clipBottom = rc.bottom;
        } else {
            savedCursorState_.hasClipRect = false;
        }

        const int probe = ShowCursor(TRUE);
        ShowCursor(FALSE);
        savedCursorState_.winShowCursorCount = probe - 1;
        // Normal gameplay has OS cursor hidden (mode 0); active SA-MP UI dialogs/SelectTextDraw show it (mode 3).
        savedCursorState_.sampCursorMode = savedCursorState_.winCursorShowing ? 3 : 0;
        savedCursorState_.valid = true;

        if (fn && game) {
            fn(game, 3, FALSE);
        }

        // Ensure Win32 cursor display counter is >= 0 while the configuration UI is active.
        const int openProbe = ShowCursor(TRUE);
        ShowCursor(FALSE);
        int curCount = openProbe - 1;
        int safety = 0;
        while (curCount < 0 && safety++ < 32) {
            curCount = ShowCursor(TRUE);
        }
    } else {
        const int restoreMode = savedCursorState_.valid ? savedCursorState_.sampCursorMode : 0;
        if (fn && game) {
            fn(game, restoreMode, restoreMode == 0 ? TRUE : FALSE);
        }

        if (savedCursorState_.valid) {
            if (savedCursorState_.hasClipRect) {
                const RECT rc{
                    savedCursorState_.clipLeft,
                    savedCursorState_.clipTop,
                    savedCursorState_.clipRight,
                    savedCursorState_.clipBottom
                };
                ClipCursor(&rc);
            } else {
                ClipCursor(nullptr);
            }

            const int nowProbe = ShowCursor(TRUE);
            ShowCursor(FALSE);
            int curCount = nowProbe - 1;
            int safety = 0;
            while (curCount < savedCursorState_.winShowCursorCount && safety++ < 32) {
                curCount = ShowCursor(TRUE);
            }
            safety = 0;
            while (curCount > savedCursorState_.winShowCursorCount && safety++ < 32) {
                curCount = ShowCursor(FALSE);
            }

            SetCursor(savedCursorState_.winCursorShowing
                          ? static_cast<HCURSOR>(savedCursorState_.winCursorHandle)
                          : nullptr);
            savedCursorState_.valid = false;
        } else {
            SetCursor(nullptr);
        }
    }
}

bool SampBridge::sendControlPacket(std::uint16_t packetId, const void* payload, std::uint16_t payloadSize) {
    return outgoingControlQueue_.enqueue(packetId, payload, payloadSize);
}

void SampBridge::clearOutgoingControlQueue() noexcept {
    outgoingControlQueue_.clear();
}

bool SampBridge::getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept {
    // GTA SA TheCamera (0xB6F028, CPlaceable): m_matrix pointer is at +0x14 (CMatrixLink*).
    // CMatrix layout: right (+0x00, float[0..2]), forward/plugin-sdk up (+0x10, float[4..6]),
    //                 up/plugin-sdk at (+0x20, float[8..10]), pos (+0x30, float[12..14]).
    const void* matPtr = nullptr;
    if (!safeReadValue(reinterpret_cast<const void*>(kGtaCameraAddr), kGtaCameraPtrOffset, matPtr) ||
        !isPlausiblePointer(matPtr)) {
        return false;
    }

    float mat[16]{};
    if (!safeReadBytes(matPtr, mat, sizeof(mat))) {
        return false;
    }

    return readCameraBasisFromMatrix(mat, pos, front, top);
}

bool SampBridge::readCameraBasisFromMatrix(const float mat[16], float pos[3], float front[3], float top[3]) const noexcept {
    if (!mat || !pos || !front || !top) {
        return false;
    }

    const float rawRight[3]{mat[0], mat[1], mat[2]};    // CMatrix::right (+0x00)
    const float rawFront[3]{mat[4], mat[5], mat[6]};    // CMatrix::up    (+0x10 = forward)
    const float rawTop[3]{mat[8], mat[9], mat[10]};     // CMatrix::at    (+0x20 = up)
    const float rawPos[3]{mat[12], mat[13], mat[14]};   // CMatrix::pos   (+0x30)

    if (!isValidBasisVector(rawFront) || !isValidBasisVector(rawTop) ||
        !std::isfinite(rawPos[0]) || !std::isfinite(rawPos[1]) || !std::isfinite(rawPos[2])) {
        return false;
    }

    const float frontLen = std::sqrt(rawFront[0] * rawFront[0] + rawFront[1] * rawFront[1] + rawFront[2] * rawFront[2]);
    const float topLen   = std::sqrt(rawTop[0] * rawTop[0] + rawTop[1] * rawTop[1] + rawTop[2] * rawTop[2]);

    front[0] = rawFront[0] / frontLen;
    front[1] = rawFront[1] / frontLen;
    front[2] = rawFront[2] / frontLen;
    top[0]   = rawTop[0] / topLen;
    top[1]   = rawTop[1] / topLen;
    top[2]   = rawTop[2] / topLen;
    pos[0]   = rawPos[0];
    pos[1]   = rawPos[1];
    pos[2]   = rawPos[2];

    // Cross-check collinearity of front x top against CMatrix::right once on the first valid camera reading
    // (GTA SA RenderWare CMatrix::right points left relative to front x top, so valid matrices have |dot| ~= 1.0f).
    if (!cameraBasisChecked_ && isValidBasisVector(rawRight)) {
        const float cx = front[1] * top[2] - front[2] * top[1];
        const float cy = front[2] * top[0] - front[0] * top[2];
        const float cz = front[0] * top[1] - front[1] * top[0];
        const float crossLen = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (crossLen > 1e-4f) {
            cameraBasisChecked_ = true;
            const float rightLen = std::sqrt(
                rawRight[0] * rawRight[0] + rawRight[1] * rawRight[1] + rawRight[2] * rawRight[2]);
            const float rx = rawRight[0] / rightLen;
            const float ry = rawRight[1] / rightLen;
            const float rz = rawRight[2] / rightLen;
            const float dot =
                (cx / crossLen) * rx +
                (cy / crossLen) * ry +
                (cz / crossLen) * rz;
            const float absDot = std::fabs(dot);
            if (absDot < 0.9f) {
                SVPLUS_LOG_WRN_CAT(
                    "samp",
                    "Camera basis mismatch on initial reading (|dot|=%.3f < 0.9): right=(%.3f,%.3f,%.3f) front=(%.3f,%.3f,%.3f) top=(%.3f,%.3f,%.3f)",
                    absDot,
                    rx, ry, rz,
                    front[0], front[1], front[2],
                    top[0], top[1], top[2]);
            }
        }
    }
    return true;
}

bool SampBridge::getLocalPlayerPedPosition(float pos[3]) const noexcept {
    const void* gtaPed = resolveLocalGtaPedFromBase(layout_, sampBase_);
    return gtaPed && readGtaEntityPosition(gtaPed, pos);
}

std::uint16_t SampBridge::getLocalPlayerId() const noexcept {
    const void* playerPool = resolvePlayerPoolFromNetGame(layout_, currentNetGamePtr());
    return readLocalPlayerIdFromPool(layout_, playerPool);
}

bool SampBridge::getCameraWorldPosition(float pos[3]) const noexcept {
    float raw[3]{};
    if (safeReadBytes(reinterpret_cast<const void*>(kGtaCameraWorldPosAddr), raw, sizeof(raw)) &&
        std::isfinite(raw[0]) && std::isfinite(raw[1]) && std::isfinite(raw[2])) {
        pos[0] = raw[0];
        pos[1] = raw[1];
        pos[2] = raw[2];
        return true;
    }
    float front[3]{};
    float top[3]{};
    return getCameraBasis(pos, front, top);
}

const void* SampBridge::resolveRemotePlayerPtr(std::uint16_t playerId) const noexcept {
    const void* playerPool = resolvePlayerPoolFromNetGame(layout_, currentNetGamePtr());
    return resolveRemotePlayerFromPool(layout_, playerPool, playerId);
}

const void* SampBridge::resolvePlayerGtaPed(std::uint16_t playerId) const noexcept {
    if (playerId == getLocalPlayerId()) {
        return resolveLocalGtaPedFromBase(layout_, sampBase_);
    }
    return resolveRemoteGtaPedFromRemote(layout_, resolveRemotePlayerPtr(playerId));
}

bool SampBridge::getRemotePlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept {
    const void* gtaPed = resolvePlayerGtaPed(playerId);
    return gtaPed && readGtaEntityPosition(gtaPed, pos);
}

bool SampBridge::getRemotePlayerHeadBonePosition(std::uint16_t playerId, float pos[3]) const noexcept {
    const void* gtaPed = resolvePlayerGtaPed(playerId);
    bool usedFallback = false;
    const bool ok = resolveHeadPositionFromGtaPed(gtaPed, &gtaHeadBoneLookup, nullptr, pos, &usedFallback);
    if (usedFallback && !boneFallbackLogged_) {
        boneFallbackLogged_ = true;
        SVPLUS_LOG_WRN_CAT("samp", "CPed::GetBonePosition(8) unavailable/unreadable; falling back to pedPos.z + 0.65f");
    }
    return ok;
}

bool SampBridge::getStreamedRemotePlayerDistance(std::uint16_t playerId, float& outDistanceMeters) const noexcept {
    if (!isConnected() || playerId >= kMaxPlayerSlots || playerId == getLocalPlayerId()) {
        return false;
    }
    float localPos[3]{};
    float remPos[3]{};
    if (!getLocalPlayerPedPosition(localPos) || !getRemotePlayerPosition(playerId, remPos)) {
        return false;
    }
    const float dx = remPos[0] - localPos[0];
    const float dy = remPos[1] - localPos[1];
    const float dz = remPos[2] - localPos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(dist)) {
        return false;
    }
    outDistanceMeters = dist;
    return true;
}

NametagFrameContext prepareNametagFrameContextFromMemory(
    const SampLayout& layout,
    std::uintptr_t    sampBase,
    const float       camPos[3]) noexcept {
    NametagFrameContext ctx{};
    if (!sampBase ||
        layout.version == SampVersion::Unknown ||
        layout.netGameSettingsOffset == 0u ||
        layout.remotePlayerShowNameTagOffset == 0u ||
        layout.settingsNameTagsDrawDistOffset == 0u ||
        layout.settingsNoNametagsBehindWallsOffset == 0u ||
        layout.settingsNameTagsEnabledOffset == 0u ||
        !camPos ||
        !std::isfinite(camPos[0]) ||
        !std::isfinite(camPos[1]) ||
        !std::isfinite(camPos[2])) {
        return ctx;
    }

    const void* ng = readNetGameFromBase(layout, sampBase);
    const void* settings = nullptr;
    if (!ng || !safeReadValue(ng, layout.netGameSettingsOffset, settings) || !isPlausiblePointer(settings)) {
        return ctx;
    }

    std::uint8_t nameTagsEnabled = 0;
    float drawDist = 0.0f;
    std::uint8_t noNametagsBehindWalls = 1;
    if (!safeReadValue(settings, layout.settingsNameTagsEnabledOffset, nameTagsEnabled) || !nameTagsEnabled ||
        !safeReadValue(settings, layout.settingsNameTagsDrawDistOffset, drawDist) || !std::isfinite(drawDist) || drawDist <= 0.0f ||
        !safeReadValue(settings, layout.settingsNoNametagsBehindWallsOffset, noNametagsBehindWalls)) {
        return ctx;
    }

    const void* playerPool = resolvePlayerPoolFromNetGame(layout, ng);
    if (!playerPool) {
        return ctx;
    }

    const void* localGtaPed = resolveLocalGtaPedFromBase(layout, sampBase);
    if (!localGtaPed || !readGtaEntityPosition(localGtaPed, ctx.localPos)) {
        return ctx;
    }

    ctx.playerPool    = playerPool;
    ctx.localPlayerId = readLocalPlayerIdFromPool(layout, playerPool);
    ctx.camPos[0]     = camPos[0];
    ctx.camPos[1]     = camPos[1];
    ctx.camPos[2]     = camPos[2];
    ctx.drawDist      = drawDist;
    ctx.checkLos      = (noNametagsBehindWalls != 0);
    ctx.valid         = true;
    return ctx;
}

bool evaluateRemoteNametagWithContext(
    const SampLayout&          layout,
    const NametagFrameContext& ctx,
    std::uint16_t              playerId,
    float                      maxDistanceMeters,
    NametagHeadPosFn           headPosLookup,
    NametagLosFn               losCheck,
    void*                      callbackCtx,
    float*                     outCamDist,
    float*                     outHeadPos) noexcept {
    if (!ctx.valid ||
        !ctx.playerPool ||
        layout.version == SampVersion::Unknown ||
        layout.remotePlayerShowNameTagOffset == 0u ||
        playerId >= kMaxPlayerSlots ||
        playerId == ctx.localPlayerId ||
        !std::isfinite(maxDistanceMeters) ||
        maxDistanceMeters <= 0.0f) {
        return false;
    }

    // 1. Cheap checks first: streamed-in remote ped and distance <= min(maxDistanceMeters, drawDist)
    const void* remote = resolveRemotePlayerFromPool(layout, ctx.playerPool, playerId);
    if (!remote) {
        return false;
    }
    const void* remGtaPed = resolveRemoteGtaPedFromRemote(layout, remote);
    float remPos[3]{};
    if (!remGtaPed || !readGtaEntityPosition(remGtaPed, remPos)) {
        return false;
    }

    const float pdx = remPos[0] - ctx.localPos[0];
    const float pdy = remPos[1] - ctx.localPos[1];
    const float pdz = remPos[2] - ctx.localPos[2];
    const float pedDist = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
    if (!std::isfinite(pedDist) || pedDist > maxDistanceMeters || pedDist > ctx.drawDist) {
        return false;
    }

    // 2. Only for streamed-in players in range: check ShowNameTag, head bone, and optional LOS
    BOOL showTag = FALSE;
    if (!safeReadValue(remote, layout.remotePlayerShowNameTagOffset, showTag) || !showTag) {
        return false;
    }

    float headPos[3]{};
    if (!resolveHeadPositionFromGtaPed(remGtaPed, headPosLookup, callbackCtx, headPos)) {
        return false;
    }
    const float cdx = headPos[0] - ctx.camPos[0];
    const float cdy = headPos[1] - ctx.camPos[1];
    const float cdz = headPos[2] - ctx.camPos[2];
    const float camDist = std::sqrt(cdx * cdx + cdy * cdy + cdz * cdz);
    if (!std::isfinite(camDist)) {
        return false;
    }
    if (outCamDist) {
        *outCamDist = camDist;
    }
    if (outHeadPos) {
        outHeadPos[0] = headPos[0];
        outHeadPos[1] = headPos[1];
        outHeadPos[2] = headPos[2];
    }

    if (ctx.checkLos) {
        if (!losCheck || !losCheck(ctx.camPos, headPos, callbackCtx)) {
            return false;
        }
    }
    return true;
}

bool evaluateRemoteNametagFromMemory(
    const SampLayout& layout,
    std::uintptr_t    sampBase,
    std::uint16_t     playerId,
    float             maxDistanceMeters,
    const float       camPos[3],
    NametagHeadPosFn  headPosLookup,
    NametagLosFn      losCheck,
    void*             callbackCtx,
    float*            outCamDist) noexcept {
    const NametagFrameContext ctx = prepareNametagFrameContextFromMemory(layout, sampBase, camPos);
    if (!ctx.valid) {
        return false;
    }
    return evaluateRemoteNametagWithContext(
        layout,
        ctx,
        playerId,
        maxDistanceMeters,
        headPosLookup,
        losCheck,
        callbackCtx,
        outCamDist,
        nullptr);
}

NametagFrameContext SampBridge::beginNametagIconFrame(const float* knownCamPos) const noexcept {
    if (!isConnected() || !isLocalPlayerSpawned() || isGameMenuOpen()) {
        return {};
    }
    float camPos[3]{};
    if (knownCamPos) {
        camPos[0] = knownCamPos[0];
        camPos[1] = knownCamPos[1];
        camPos[2] = knownCamPos[2];
    } else if (!getCameraWorldPosition(camPos)) {
        return {};
    }
    return prepareNametagFrameContextFromMemory(layout_, sampBase_, camPos);
}

bool SampBridge::shouldShowRemoteNametagIconInFrame(
    const NametagFrameContext& ctx,
    std::uint16_t playerId,
    float maxDistanceMeters,
    float* outCamDist,
    float* outHeadPos) const noexcept {
    return evaluateRemoteNametagWithContext(
        layout_,
        ctx,
        playerId,
        maxDistanceMeters,
        &gtaHeadBoneLookup,
        &gtaLineOfSightClearCheck,
        nullptr,
        outCamDist,
        outHeadPos);
}

bool SampBridge::shouldShowRemoteNametagIcon(
    std::uint16_t playerId,
    float maxDistanceMeters,
    float* outCamDist
) const noexcept {
    const NametagFrameContext ctx = beginNametagIconFrame(nullptr);
    if (!ctx.valid) {
        return false;
    }
    return shouldShowRemoteNametagIconInFrame(ctx, playerId, maxDistanceMeters, outCamDist, nullptr);
}

namespace {

const void* resolveVehicleGtaEntityFromNetGame(
    const SampLayout& layout,
    const void* ng,
    std::uint16_t vehicleId
) noexcept {
    if (!ng || vehicleId >= layout.maxVehicles || layout.version == SampVersion::Unknown) {
        return nullptr;
    }
    const void* pools = nullptr;
    const void* vehiclePool = nullptr;
    if (!safeReadValue(ng, layout.netGamePoolsOffset, pools) ||
        !safeReadValue(pools, layout.poolsVehicleOffset, vehiclePool) ||
        !isPlausiblePointer(vehiclePool)) {
        return nullptr;
    }

    const auto* notEmpty = static_cast<const std::uint8_t*>(vehiclePool) + layout.vehiclePoolNotEmptyOffset;
    BOOL present = FALSE;
    if (!safeReadValue(notEmpty, static_cast<std::uint32_t>(vehicleId * sizeof(BOOL)), present) || !present) {
        return nullptr;
    }

    const auto* gameObjects = static_cast<const std::uint8_t*>(vehiclePool) + layout.vehiclePoolGameObjectOffset;
    const void* gtaVehicle = nullptr;
    if (!safeReadValue(gameObjects, static_cast<std::uint32_t>(vehicleId * sizeof(void*)), gtaVehicle) ||
        !isPlausiblePointer(gtaVehicle)) {
        return nullptr;
    }
    return gtaVehicle;
}

const void* resolveObjectGtaEntityFromNetGame(
    const SampLayout& layout,
    const void* ng,
    std::uint16_t objectId
) noexcept {
    if (!ng || objectId >= layout.maxObjects || layout.version == SampVersion::Unknown) {
        return nullptr;
    }
    const void* pools = nullptr;
    const void* objectPool = nullptr;
    if (!safeReadValue(ng, layout.netGamePoolsOffset, pools) ||
        !safeReadValue(pools, layout.poolsObjectOffset, objectPool) ||
        !isPlausiblePointer(objectPool)) {
        return nullptr;
    }

    const auto* notEmpty = static_cast<const std::uint8_t*>(objectPool) + layout.objectPoolNotEmptyOffset;
    BOOL present = FALSE;
    if (!safeReadValue(notEmpty, static_cast<std::uint32_t>(objectId * sizeof(BOOL)), present) || !present) {
        return nullptr;
    }

    const auto* objects = static_cast<const std::uint8_t*>(objectPool) + layout.objectPoolArrayOffset;
    const void* obj = nullptr;
    const void* gtaEntity = nullptr;
    if (!safeReadValue(objects, static_cast<std::uint32_t>(objectId * sizeof(void*)), obj) ||
        !safeReadValue(obj, layout.objectEntityOffset, gtaEntity)) {
        return nullptr;
    }
    return isPlausiblePointer(gtaEntity) ? gtaEntity : nullptr;
}

} // namespace

bool SampBridge::getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept {
    const void* gtaVehicle = resolveVehicleGtaEntityFromNetGame(layout_, currentNetGamePtr(), vehicleId);
    return gtaVehicle && readGtaEntityPosition(gtaVehicle, pos);
}

bool SampBridge::isLocalPlayerInVehicle(std::uint16_t vehicleId) const noexcept {
    if (!isConnected()) {
        return false;
    }
    const void* gtaVehicle = resolveVehicleGtaEntityFromNetGame(layout_, currentNetGamePtr(), vehicleId);
    if (!gtaVehicle) {
        return false;
    }
    const void* activeVehicle = nullptr;
    if (!safeReadBytes(reinterpret_cast<const void*>(kGtaActiveVehicleAddr), &activeVehicle, sizeof(activeVehicle))) {
        return false;
    }
    return isPlausiblePointer(activeVehicle) && activeVehicle == gtaVehicle;
}

bool SampBridge::checkLineOfSightClear(const float origin[3],
                                       const float target[3],
                                       const void* ignoreEntity) const noexcept {
    if (!origin || !target ||
        !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2]) ||
        !std::isfinite(target[0]) || !std::isfinite(target[1]) || !std::isfinite(target[2])) {
        return true; // Fail open for voice occlusion on invalid coordinates
    }
    return tryCheckGtaOcclusionLineOfSightClear(origin, target, true, ignoreEntity);
}

const void* SampBridge::getObjectEntity(std::uint16_t objectId) const noexcept {
    return resolveObjectGtaEntityFromNetGame(layout_, currentNetGamePtr(), objectId);
}

bool SampBridge::getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept {
    const void* gtaEntity = resolveObjectGtaEntityFromNetGame(layout_, currentNetGamePtr(), objectId);
    return gtaEntity && readGtaEntityPosition(gtaEntity, pos);
}

bool SampBridge::worldToScreen(const float worldPos[3], float screenOut[2], float* outDist) const noexcept {
    struct RwV3d { float x, y, z; };
    RwV3d in{worldPos[0], worldPos[1], worldPos[2]};
    RwV3d out{};
    float w = 0.0f;
    float h = 0.0f;
    using CalcScreenCoordsFn = bool(__cdecl*)(const RwV3d*, RwV3d*, float*, float*, bool, bool);
    if (!reinterpret_cast<CalcScreenCoordsFn>(kGtaCalcScreenCoordsAddr)(&in, &out, &w, &h, true, true)) {
        return false;
    }
    if (!std::isfinite(out.x) || !std::isfinite(out.y) || out.z <= 0.0f) {
        return false;
    }
    screenOut[0] = out.x;
    screenOut[1] = out.y;
    if (outDist) {
        *outDist = out.z;
    }
    return true;
}

bool SampBridge::isRemotePlayerConnected(std::uint16_t playerId) const noexcept {
    const void* ng = currentNetGamePtr();
    if (!ng || playerId >= kMaxPlayerSlots || layout_.version == SampVersion::Unknown) {
        return false;
    }
    const void* pools = nullptr;
    const void* playerPool = nullptr;
    if (!safeReadValue(ng, layout_.netGamePoolsOffset, pools) ||
        !safeReadValue(pools, layout_.poolsPlayerOffset, playerPool) ||
        !isPlausiblePointer(playerPool)) {
        return false;
    }
    const auto* notEmpty = static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolNotEmptyOffset;
    BOOL present = FALSE;
    if (!safeReadValue(notEmpty, static_cast<std::uint32_t>(playerId * sizeof(BOOL)), present) || !present) {
        return false;
    }
    const auto* objects = static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolArrayOffset;
    const void* info = nullptr;
    return safeReadValue(objects, static_cast<std::uint32_t>(playerId * sizeof(void*)), info) && isPlausiblePointer(info);
}

std::size_t SampBridge::getConnectedRemotePlayerCount() const noexcept {
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown) {
        return 0u;
    }
    const void* playerPool = resolvePlayerPoolFromNetGame(layout_, ng);
    if (!playerPool) {
        return 0u;
    }
    const std::uint16_t localId = readLocalPlayerIdFromPool(layout_, playerPool);
    BOOL notEmpty[kMaxPlayerSlots]{};
    const void* objects[kMaxPlayerSlots]{};
    if (!safeReadBytes(static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolNotEmptyOffset,
                       notEmpty, sizeof(notEmpty)) ||
        !safeReadBytes(static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolArrayOffset,
                       objects, sizeof(objects))) {
        return 0u;
    }
    std::size_t count = 0u;
    for (std::size_t pid = 0; pid < kMaxPlayerSlots; ++pid) {
        if (pid != localId && notEmpty[pid] != FALSE && isPlausiblePointer(objects[pid])) {
            ++count;
        }
    }
    return count;
}

void SampBridge::collectConnectedRemotePlayers(
    std::vector<RemotePlayerMenuSnapshot>& out,
    bool includeDistances) const {
    out.clear();
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown) {
        return;
    }
    const void* playerPool = resolvePlayerPoolFromNetGame(layout_, ng);
    if (!playerPool) {
        return;
    }
    const std::uint16_t localId = readLocalPlayerIdFromPool(layout_, playerPool);
    BOOL notEmpty[kMaxPlayerSlots]{};
    const void* objects[kMaxPlayerSlots]{};
    if (!safeReadBytes(static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolNotEmptyOffset,
                       notEmpty, sizeof(notEmpty)) ||
        !safeReadBytes(static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolArrayOffset,
                       objects, sizeof(objects))) {
        return;
    }

    float localPos[3]{};
    bool hasLocalPos = false;
    if (includeDistances) {
        const void* localGtaPed = resolveLocalGtaPedFromBase(layout_, sampBase_);
        hasLocalPos = localGtaPed && readGtaEntityPosition(localGtaPed, localPos);
    }

    for (std::size_t pid = 0; pid < kMaxPlayerSlots; ++pid) {
        if (pid == localId || notEmpty[pid] == FALSE || !isPlausiblePointer(objects[pid])) {
            continue;
        }
        RemotePlayerMenuSnapshot snap{};
        snap.id = static_cast<std::uint16_t>(pid);
        if (!readForeignMsvcStringBuffer(
                static_cast<const std::uint8_t*>(objects[pid]) + layout_.playerInfoNickOffset,
                snap.rawName.data(), snap.rawName.size(), snap.rawNameLen) ||
            snap.rawNameLen == 0) {
            continue;
        }
        if (hasLocalPos) {
            const void* remote = nullptr;
            if (safeReadValue(objects[pid], layout_.playerInfoRemoteOffset, remote) && isPlausiblePointer(remote)) {
                const void* remGtaPed = resolveRemoteGtaPedFromRemote(layout_, remote);
                float remPos[3]{};
                if (remGtaPed && readGtaEntityPosition(remGtaPed, remPos)) {
                    const float dx = remPos[0] - localPos[0];
                    const float dy = remPos[1] - localPos[1];
                    const float dz = remPos[2] - localPos[2];
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (std::isfinite(dist)) {
                        snap.streamedIn = true;
                        snap.distanceMeters = dist;
                    }
                }
            }
        }
        out.push_back(snap);
    }
}

std::uint32_t SampBridge::getPlayerColorArgb(std::uint16_t playerId) const noexcept {
    constexpr std::uint32_t kDefaultWhiteArgb = 0xFFFFFFFFu;
    if (!sampBase_ || layout_.colorTableRva == 0u || playerId >= 4000u) {
        return kDefaultWhiteArgb;
    }
    std::uint32_t rgba = 0u;
    const auto* tablePtr = reinterpret_cast<const std::uint8_t*>(sampBase_ + layout_.colorTableRva);
    if (!safeReadValue(tablePtr, static_cast<std::uint32_t>(playerId * sizeof(std::uint32_t)), rgba) ||
        rgba == 0u) {
        return kDefaultWhiteArgb;
    }
    // SA-MP player color table entries store 0xRRGGBBAA; force full alpha for readable HUD text.
    return 0xFF000000u | ((rgba >> 8u) & 0x00FFFFFFu);
}

std::string SampBridge::getPlayerName(std::uint16_t playerId) const {
    const void* ng = currentNetGamePtr();
    if (!ng || playerId >= kMaxPlayerSlots || layout_.version == SampVersion::Unknown) {
        return {};
    }
    const void* pools = nullptr;
    const void* playerPool = nullptr;
    if (!safeReadValue(ng, layout_.netGamePoolsOffset, pools) ||
        !safeReadValue(pools, layout_.poolsPlayerOffset, playerPool) ||
        !isPlausiblePointer(playerPool)) {
        return {};
    }
    const auto* notEmpty = static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolNotEmptyOffset;
    BOOL present = FALSE;
    if (!safeReadValue(notEmpty, static_cast<std::uint32_t>(playerId * sizeof(BOOL)), present) || !present) {
        return {};
    }
    const auto* objects = static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolArrayOffset;
    const void* info = nullptr;
    if (!safeReadValue(objects, static_cast<std::uint32_t>(playerId * sizeof(void*)), info) || !isPlausiblePointer(info)) {
        return {};
    }
    return util::ensureUtf8Name(readForeignMsvcString(static_cast<const std::uint8_t*>(info) + layout_.playerInfoNickOffset));
}

std::string SampBridge::getLocalPlayerName() const {
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown) {
        return {};
    }
    const void* pools = nullptr;
    const void* playerPool = nullptr;
    if (!safeReadValue(ng, layout_.netGamePoolsOffset, pools) ||
        !safeReadValue(pools, layout_.poolsPlayerOffset, playerPool) ||
        !isPlausiblePointer(playerPool)) {
        return {};
    }
    return util::ensureUtf8Name(
        readForeignMsvcString(static_cast<const std::uint8_t*>(playerPool) + layout_.playerPoolLocalNameOffset));
}

std::string SampBridge::getServerHostAddress() const {
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown) {
        return {};
    }
    char buf[258]{};
    std::size_t len = 0;
    const auto* raw = static_cast<const std::uint8_t*>(ng) + layout_.netGameHostOffset;
    if (!safeReadCString(raw, buf, 257u, len) || len == 0) {
        return {};
    }
    return std::string(buf, len);
}

std::string SampBridge::getServerHostname() const {
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown || layout_.netGameHostnameOffset == 0u) {
        return {};
    }
    char buf[258]{};
    std::size_t len = 0;
    const auto* raw = static_cast<const std::uint8_t*>(ng) + layout_.netGameHostnameOffset;
    if (!safeReadCString(raw, buf, 257u, len) || len == 0) {
        return {};
    }
    return util::ensureUtf8Name(std::string_view(buf, len));
}

std::string SampBridge::getServerEndpointKey() const {
    const std::uint32_t ip = connectedIpv4Net_.load(std::memory_order_acquire);
    std::uint16_t port = connectedRakPort_.load(std::memory_order_acquire);
    if (port == 0u) {
        const int pRaw = getServerPort();
        if (pRaw > 0 && pRaw <= 65535) {
            port = static_cast<std::uint16_t>(pRaw);
        }
    }
    if (ip != 0u && ip != 0xFFFFFFFFu && port > 0u) {
        const auto* b = reinterpret_cast<const std::uint8_t*>(&ip);
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                      static_cast<unsigned>(b[0]),
                      static_cast<unsigned>(b[1]),
                      static_cast<unsigned>(b[2]),
                      static_cast<unsigned>(b[3]),
                      static_cast<unsigned>(port));
        return buf;
    }
    const std::string host = getServerHostAddress();
    if (!host.empty() && port > 0u) {
        return host + ":" + std::to_string(port);
    }
    return host;
}

int SampBridge::getServerPort() const noexcept {
    const void* ng = currentNetGamePtr();
    if (!ng || layout_.version == SampVersion::Unknown) {
        return 0;
    }
    int port = 0;
    return safeReadValue(ng, layout_.netGamePortOffset, port) ? port : 0;
}

std::string SampBridge::getCachedPlayerName(std::uint16_t playerId) const {
    std::string out;
    copyCachedPlayerName(playerId, out);
    return out;
}

void SampBridge::copyCachedPlayerName(std::uint16_t playerId, std::string& out) const {
    if (playerId >= kMaxPlayerSlots) {
        out.clear();
        return;
    }
    std::lock_guard lock(cacheMutex_);
    out.assign(cachedPlayerNames_[playerId]);
}

} // namespace svplus::client::samp
