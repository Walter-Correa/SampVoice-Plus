/*
 * SampVoice Plus - Legacy SA-MP 0.3.7 & 0.3.DL RakServer VTable & Connect Hook
 */

#include "SampRakNetHook.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "svplus/common/Logger.hpp"
#include "svplus/common/Protocol.hpp"

namespace svplus::server::samp {

namespace {

#ifdef _WIN32
constexpr const uint8_t kGetRakServerPattern[] = {
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00, 0x51,
    0x68, 0x18, 0x0E, 0x00, 0x00, 0xE8, 0xFF, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x04, 0x89, 0x04,
    0x24, 0x85, 0xC0, 0xC7, 0x44, 0x24, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x74, 0x16
};
constexpr const char kGetRakServerMask[] = "xxxxxxxxxxxxxxxx????x????xxxxxxxxxxx?xxxxxx";
#else
constexpr const uint8_t kGetRakServerPattern[] = {
    0x04, 0x24, 0xF4, 0x0D, 0x00, 0x00, 0x89, 0x75, 0xFF, 0x89, 0x5D, 0xFF, 0xE8, 0xFF, 0xFF,
    0xFF, 0xFF, 0x89, 0x04, 0x24, 0x89, 0xC6, 0xE8, 0xFF, 0xFF, 0xFF, 0xFF, 0x89, 0xF0, 0x8B,
    0x5D, 0xFF, 0x8B, 0x75, 0xFF, 0x89, 0xEC, 0x5D, 0xC3
};
constexpr const char kGetRakServerMask[] = "xxxxxxxx?xx?x????xxxxxx????xxxx?xx?xxxx";
#endif

struct VtableIndex {
#ifdef _WIN32
    static constexpr std::size_t Destructor           = 0;
    static constexpr std::size_t Send                 = 7;
    static constexpr std::size_t Receive              = 10;
    static constexpr std::size_t DeallocatePacket     = 12;
    static constexpr std::size_t RegisterRpc          = 29;
    static constexpr std::size_t UnregisterRpc        = 31;
    static constexpr std::size_t GetIndexFromPlayerId = 57;
    static constexpr std::size_t GetPlayerIdFromIndex = 58;
#else
    static constexpr std::size_t Destructor           = 1;
    static constexpr std::size_t Send                 = 9;
    static constexpr std::size_t Receive              = 11;
    static constexpr std::size_t DeallocatePacket     = 13;
    static constexpr std::size_t RegisterRpc          = 30;
    static constexpr std::size_t UnregisterRpc        = 32;
    static constexpr std::size_t GetIndexFromPlayerId = 58;
    static constexpr std::size_t GetPlayerIdFromIndex = 59;
#endif
};

#ifdef _WIN32
#define SV_THISCALL __thiscall
using DestructorFn = void*(__thiscall*)(void*, unsigned int);
#else
#define SV_THISCALL
using DestructorFn = void(*)(void*);
#endif

using SendFn                 = bool(SV_THISCALL*)(void*, RakBitStream*, int, int, uint32_t, RakPlayerId, bool);
using ReceiveFn              = RakPacket*(SV_THISCALL*)(void*);
using DeallocatePacketFn     = void(SV_THISCALL*)(void*, RakPacket*);
using RegisterRpcFn          = void(SV_THISCALL*)(void*, uint8_t*, void(*)(RakRpcParameters*));
using UnregisterRpcFn        = void(SV_THISCALL*)(void*, uint8_t*);
using GetIndexFromPlayerIdFn = int(SV_THISCALL*)(void*, RakPlayerId);
using GetPlayerIdFromIndexFn = RakPlayerId(SV_THISCALL*)(void*, int32_t);

#ifndef _WIN32
int QueryPageProtectionFlags(uintptr_t startAddr, uintptr_t endAddr) noexcept {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) {
        return PROT_READ | PROT_WRITE | PROT_EXEC;
    }

    char line[512];
    bool matchedAll = false;
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        unsigned long long lo = 0;
        unsigned long long hi = 0;
        char perms[8]{};
        if (std::sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) == 3) {
            if (static_cast<uintptr_t>(lo) <= startAddr && endAddr <= static_cast<uintptr_t>(hi)) {
                int parsed = 0;
                if (perms[0] == 'r') parsed |= PROT_READ;
                if (perms[1] == 'w') parsed |= PROT_WRITE;
                if (perms[2] == 'x') parsed |= PROT_EXEC;
                prot = parsed;
                matchedAll = true;
                break;
            }
        }
    }
    std::fclose(f);
    return matchedAll ? prot : (PROT_READ | PROT_WRITE | PROT_EXEC);
}
#endif

struct UnprotectGuard {
    void* address{nullptr};
    std::size_t size{0};
#ifdef _WIN32
    DWORD oldProtect{0};
#else
    uintptr_t pageStart{0};
    std::size_t pageSize{0};
    int oldProt{PROT_READ | PROT_WRITE | PROT_EXEC};
#endif

    UnprotectGuard(void* addr, std::size_t sz) : address(addr), size(sz) {
#ifdef _WIN32
        VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
#else
        const long ps = sysconf(_SC_PAGESIZE);
        const uintptr_t p = reinterpret_cast<uintptr_t>(address);
        pageStart = p & ~static_cast<uintptr_t>(ps - 1);
        const uintptr_t pageEnd = (p + size + static_cast<uintptr_t>(ps) - 1) & ~static_cast<uintptr_t>(ps - 1);
        pageSize = static_cast<std::size_t>(pageEnd - pageStart);
        oldProt = QueryPageProtectionFlags(pageStart, pageStart + pageSize);
        mprotect(reinterpret_cast<void*>(pageStart), pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    }

    ~UnprotectGuard() {
#ifdef _WIN32
        DWORD dummy = 0;
        VirtualProtect(address, size, oldProtect, &dummy);
#else
        mprotect(reinterpret_cast<void*>(pageStart), pageSize, oldProt);
#endif
    }
};

struct InlineJumpHook {
    uint8_t* target{nullptr};
    std::array<uint8_t, 5> origBytes{};
    std::array<uint8_t, 5> jumpBytes{};
    bool active{false};

    bool Install(void* targetAddr, void* detour) noexcept {
        if (!targetAddr || !detour) return false;
        target = static_cast<uint8_t*>(targetAddr);
        std::memcpy(origBytes.data(), target, 5);

        const intptr_t rel = reinterpret_cast<intptr_t>(detour) - (reinterpret_cast<intptr_t>(target) + 5);
        jumpBytes[0] = 0xE9;
        const int32_t rel32 = static_cast<int32_t>(rel);
        std::memcpy(jumpBytes.data() + 1, &rel32, 4);

        Enable();
        return true;
    }

    void Enable() noexcept {
        if (!target || active) return;
        UnprotectGuard guard(target, 5);
        std::memcpy(target, jumpBytes.data(), 5);
        active = true;
    }

    void Disable() noexcept {
        if (!target || !active) return;
        UnprotectGuard guard(target, 5);
        std::memcpy(target, origBytes.data(), 5);
        active = false;
    }

    void Reset() noexcept {
        Disable();
        target = nullptr;
    }
};

uint8_t* PatternScan(const uint8_t* base, std::size_t size, const uint8_t* pattern, std::size_t patLen, const char* mask) noexcept {
    if (size < patLen || patLen == 0) return nullptr;

    for (std::size_t i = 0; i <= size - patLen; ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < patLen; ++j) {
            if ((!mask || mask[j] == 'x') && base[i + j] != pattern[j]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return const_cast<uint8_t*>(base + i);
        }
    }
    return nullptr;
}

enum class SectionFilter {
    ExecutableCode,
    NonWritableData
};

struct SectionScanResult {
    bool        queriedOk{false};
    std::size_t matchCount{0};
    uint8_t*    firstMatch{nullptr};
};

SectionScanResult ScanServerSections(
    const void* anchor,
    SectionFilter filter,
    const uint8_t* pattern,
    std::size_t patLen,
    const char* mask
) noexcept {
    SectionScanResult result{};
    if (patLen == 0) return result;

    auto scanRange = [&](const uint8_t* start, std::size_t len) {
        std::size_t offset = 0;
        while (offset + patLen <= len) {
            uint8_t* hit = PatternScan(start + offset, len - offset, pattern, patLen, mask);
            if (!hit) break;
            if (!result.firstMatch) {
                result.firstMatch = hit;
            }
            ++result.matchCount;
            offset = static_cast<std::size_t>(hit - start) + 1u;
        }
    };

#ifdef _WIN32
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(anchor),
            &mod) || !mod) {
        mod = GetModuleHandleA(nullptr);
    }
    if (!mod) return result;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return result;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(mod) + dos->e_lfanew
    );
    if (nt->Signature != IMAGE_NT_SIGNATURE) return result;

    result.queriedOk = true;
    const uint8_t* modBase = reinterpret_cast<const uint8_t*>(mod);
    const std::size_t imgSize = nt->OptionalHeader.SizeOfImage;
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& sec = sections[i];
        const bool isExec  = (sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        const bool isWrite = (sec.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        const bool isRead  = (sec.Characteristics & IMAGE_SCN_MEM_READ) != 0;

        if (filter == SectionFilter::ExecutableCode && !isExec) {
            continue;
        }
        // Scan all readable non-writable sections (R-- and R-X) for version strings
        if (filter == SectionFilter::NonWritableData && (!isRead || isWrite)) {
            continue;
        }
        if (sec.VirtualAddress >= imgSize) {
            continue;
        }
        const std::size_t secSize = std::min<std::size_t>(
            sec.Misc.VirtualSize > 0 ? sec.Misc.VirtualSize : sec.SizeOfRawData,
            imgSize - sec.VirtualAddress
        );
        scanRange(modBase + sec.VirtualAddress, secSize);
    }
    return result;
#else
    Dl_info info{};
    if (!dladdr(anchor, &info) || !info.dli_fbase) {
        return result;
    }
    const auto* ehdr = reinterpret_cast<const Elf32_Ehdr*>(info.dli_fbase);
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0) return result;

    result.queriedOk = true;
    const uintptr_t loadBias = (ehdr->e_type == ET_EXEC)
        ? 0u
        : reinterpret_cast<uintptr_t>(info.dli_fbase);
    const auto* phdr = reinterpret_cast<const Elf32_Phdr*>(
        reinterpret_cast<const uint8_t*>(info.dli_fbase) + ehdr->e_phoff
    );
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }
        const bool isExec  = (phdr[i].p_flags & PF_X) != 0;
        const bool isWrite = (phdr[i].p_flags & PF_W) != 0;
        const bool isRead  = (phdr[i].p_flags & PF_R) != 0;

        if (filter == SectionFilter::ExecutableCode && !isExec) {
            continue;
        }
        // On samp03svr ELF, .rodata is merged into the R-X PT_LOAD segment; scan all R/R-X non-writable PT_LOAD segments
        if (filter == SectionFilter::NonWritableData && (!isRead || isWrite)) {
            continue;
        }
        const uint8_t* segStart = reinterpret_cast<const uint8_t*>(loadBias + phdr[i].p_vaddr);
        const std::size_t segSize = phdr[i].p_filesz > 0 ? phdr[i].p_filesz : phdr[i].p_memsz;
        scanRange(segStart, segSize);
    }
    return result;
#endif
}

bool ContainsAsciiInNonWritableData(const void* anchor, const char* text) noexcept {
    return ScanServerSections(
        anchor,
        SectionFilter::NonWritableData,
        reinterpret_cast<const uint8_t*>(text),
        std::strlen(text),
        nullptr
    ).firstMatch != nullptr;
}

void*                  gRakServer{nullptr};
void**                 gRakServerVtable{nullptr};
bool                   gRakServerDestroyed{false};
DestructorFn           gFuncDestructor{nullptr};
SendFn                 gFuncSend{nullptr};
ReceiveFn              gFuncReceive{nullptr};
DeallocatePacketFn     gFuncDeallocate{nullptr};
RegisterRpcFn          gFuncRegisterRpc{nullptr};
UnregisterRpcFn        gFuncUnregisterRpc{nullptr};
GetIndexFromPlayerIdFn gFuncGetIndex{nullptr};
GetPlayerIdFromIndexFn gFuncGetPlayerId{nullptr};
void(*gOrigConnectHandler)(RakRpcParameters*){nullptr};
bool gIs03DL{false};

InlineJumpHook                     gGetRakServerHook;
SampRakNetHook::ConnectCallback    gOnConnect;
SampRakNetHook::DisconnectCallback gOnDisconnect;
SampRakNetHook::PacketCallback     gOnPacket;

} // namespace

bool SampRakNetHook::Is03DL() noexcept {
    return gIs03DL;
}

bool SampRakNetHook::Initialize(const void* anchorInServerModule) {
    gIs03DL = false;
    const SectionScanResult scan = ScanServerSections(
        anchorInServerModule,
        SectionFilter::ExecutableCode,
        kGetRakServerPattern,
        std::strlen(kGetRakServerMask),
        kGetRakServerMask
    );
    if (!scan.queriedOk) {
        log::Logger::Log(log::Level::Error, "Failed to query SA-MP server executable memory range");
        return false;
    }

    if (scan.matchCount == 0 || !scan.firstMatch) {
        log::Logger::Log(
            log::Level::Error,
            "Unsupported SA-MP server binary: GetRakServerInterface signature not found in executable code sections. "
            "Legacy plugin mode supports SA-MP 0.3.7-R2, 0.3.7-R3, and 0.3.DL only (open.mp must load SampVoice Plus via components/)."
        );
        return false;
    }

    if (scan.matchCount > 1) {
        log::Logger::Log(
            log::Level::Error,
            "Ambiguous SA-MP GetRakServerInterface signature (%zu matches found in executable code sections); refusing to hook.",
            scan.matchCount
        );
        return false;
    }

    const char* detectedVersion = nullptr;
    if (ContainsAsciiInNonWritableData(anchorInServerModule, "0.3.DL")) {
        detectedVersion = "0.3.DL";
        gIs03DL = true;
    } else if (ContainsAsciiInNonWritableData(anchorInServerModule, "0.3.7-R2")) {
        detectedVersion = "0.3.7-R2";
    } else if (ContainsAsciiInNonWritableData(anchorInServerModule, "0.3.7-R3")) {
        detectedVersion = "0.3.7-R3";
    } else {
        log::Logger::Log(
            log::Level::Error,
            "Unsupported SA-MP server version: expected '0.3.7-R2', '0.3.7-R3', or '0.3.DL' signature in non-writable sections."
        );
        return false;
    }

    log::Logger::Log(log::Level::Debug, "Detected SA-MP server %s; installing one-shot RakServer hook", detectedVersion);

    // Function prologue begins 7 bytes before matched signature
    if (!gGetRakServerHook.Install(scan.firstMatch - 7, reinterpret_cast<void*>(&GetRakServerInterfaceHook))) {
        log::Logger::Log(log::Level::Error, "Failed to install SA-MP GetRakServerInterface hook");
        return false;
    }

    return true;
}

void SampRakNetHook::AttachRakServerIfUnbound(void* rakServer) {
    if (gRakServer == nullptr && rakServer != nullptr) {
        BindRakServer(rakServer);
    }
    if (gRakServer != nullptr && gGetRakServerHook.target != nullptr) {
        // Inline hook is no longer needed once RakServer is bound; remove permanently for Pawn.RakNet/YSF coexistence
        gGetRakServerHook.Reset();
    }
}

void SampRakNetHook::Shutdown() noexcept {
    void** vtable = gRakServerVtable;
    if (vtable != nullptr) {
        if (!gRakServerDestroyed && gRakServer != nullptr &&
            gOrigConnectHandler && gFuncUnregisterRpc && gFuncRegisterRpc) {
            uint8_t connectRpcId = protocol::kRakNetRpcPlayerConnect;
            gFuncUnregisterRpc(gRakServer, &connectRpcId);
            gFuncRegisterRpc(gRakServer, &connectRpcId, gOrigConnectHandler);
        }
        gOrigConnectHandler = nullptr;

        if (gFuncDestructor) {
            if (vtable[VtableIndex::Destructor] == reinterpret_cast<void*>(&DestructorHook)) {
                UnprotectGuard guard(&vtable[VtableIndex::Destructor], sizeof(void*));
                vtable[VtableIndex::Destructor] = reinterpret_cast<void*>(gFuncDestructor);
            } else {
                log::Logger::Log(log::Level::Debug, "SA-MP RakServer::~RakServer vtable slot owned by another plugin; leaving intact on shutdown");
            }
        }
        if (gFuncRegisterRpc) {
            if (vtable[VtableIndex::RegisterRpc] == reinterpret_cast<void*>(&RegisterRpcHook)) {
                UnprotectGuard guard(&vtable[VtableIndex::RegisterRpc], sizeof(void*));
                vtable[VtableIndex::RegisterRpc] = reinterpret_cast<void*>(gFuncRegisterRpc);
            } else {
                log::Logger::Log(log::Level::Debug, "SA-MP RakServer::RegisterRpc vtable slot owned by another plugin; leaving intact on shutdown");
            }
        }
        if (gFuncReceive) {
            if (vtable[VtableIndex::Receive] == reinterpret_cast<void*>(&ReceiveHook)) {
                UnprotectGuard guard(&vtable[VtableIndex::Receive], sizeof(void*));
                vtable[VtableIndex::Receive] = reinterpret_cast<void*>(gFuncReceive);
            } else {
                log::Logger::Log(log::Level::Debug, "SA-MP RakServer::Receive vtable slot owned by another plugin; leaving intact on shutdown");
            }
        }
    }

    gGetRakServerHook.Reset();
    gIs03DL = false;
    gOrigConnectHandler = nullptr;
    gRakServer = nullptr;
    gRakServerVtable = nullptr;
    gRakServerDestroyed = false;
    gFuncDestructor = nullptr;
    gFuncSend = nullptr;
    gFuncReceive = nullptr;
    gFuncDeallocate = nullptr;
    gFuncRegisterRpc = nullptr;
    gFuncUnregisterRpc = nullptr;
    gFuncGetIndex = nullptr;
    gFuncGetPlayerId = nullptr;
    gOnConnect = nullptr;
    gOnDisconnect = nullptr;
    gOnPacket = nullptr;
}

void SampRakNetHook::SetCallbacks(
    ConnectCallback onConnect,
    DisconnectCallback onDisconnect,
    PacketCallback onPacket
) {
    gOnConnect = std::move(onConnect);
    gOnDisconnect = std::move(onDisconnect);
    gOnPacket = std::move(onPacket);
}

bool SampRakNetHook::SendPacket(uint16_t playerId, const uint8_t* bytes, std::size_t size) noexcept {
    if (!gRakServer || gRakServerDestroyed || !gFuncSend || !gFuncGetPlayerId || !bytes || size == 0) {
        return false;
    }

    RakBitStream bs{};
    bs.numberOfBitsUsed = static_cast<int32_t>(size * 8);
    bs.numberOfBitsAllocated = static_cast<int32_t>(size * 8);
    bs.readOffset = 0;
    bs.data = const_cast<uint8_t*>(bytes);
    bs.copyData = false;

    const RakPlayerId pid = gFuncGetPlayerId(gRakServer, static_cast<int32_t>(playerId));
    if (pid.binaryAddress == 0xFFFFFFFFu && playerId != 0xFFFFu) {
        return false;
    }

    // PacketPriority::MEDIUM_PRIORITY = 2, PacketReliability::RELIABLE_ORDERED = 9
    return gFuncSend(gRakServer, &bs, 2, 9, 0, pid, playerId == 0xFFFFu);
}

uint32_t SampRakNetHook::GetPlayerIpv4Net(uint16_t playerId) noexcept {
    if (!gRakServer || gRakServerDestroyed || !gFuncGetPlayerId) {
        return 0;
    }
    const RakPlayerId pid = gFuncGetPlayerId(gRakServer, static_cast<int32_t>(playerId));
    if (pid.binaryAddress == 0xFFFFFFFFu) {
        return 0;
    }
    return pid.binaryAddress;
}

void SampRakNetHook::BindRakServer(void* rakServer) noexcept {
    if (!rakServer || gRakServer != nullptr) return;

    void** vtable = *static_cast<void***>(rakServer);
    gRakServerVtable   = vtable;
    gRakServerDestroyed = false;
    gFuncDestructor    = reinterpret_cast<DestructorFn>(vtable[VtableIndex::Destructor]);
    gFuncSend          = reinterpret_cast<SendFn>(vtable[VtableIndex::Send]);
    gFuncReceive       = reinterpret_cast<ReceiveFn>(vtable[VtableIndex::Receive]);
    gFuncDeallocate    = reinterpret_cast<DeallocatePacketFn>(vtable[VtableIndex::DeallocatePacket]);
    gFuncRegisterRpc   = reinterpret_cast<RegisterRpcFn>(vtable[VtableIndex::RegisterRpc]);
    gFuncUnregisterRpc = reinterpret_cast<UnregisterRpcFn>(vtable[VtableIndex::UnregisterRpc]);
    gFuncGetIndex      = reinterpret_cast<GetIndexFromPlayerIdFn>(vtable[VtableIndex::GetIndexFromPlayerId]);
    gFuncGetPlayerId   = reinterpret_cast<GetPlayerIdFromIndexFn>(vtable[VtableIndex::GetPlayerIdFromIndex]);

    {
        UnprotectGuard guard(&vtable[VtableIndex::Destructor], sizeof(void*));
        vtable[VtableIndex::Destructor] = reinterpret_cast<void*>(&DestructorHook);
    }
    {
        UnprotectGuard guard(&vtable[VtableIndex::RegisterRpc], sizeof(void*));
        vtable[VtableIndex::RegisterRpc] = reinterpret_cast<void*>(&RegisterRpcHook);
    }
    {
        UnprotectGuard guard(&vtable[VtableIndex::Receive], sizeof(void*));
        vtable[VtableIndex::Receive] = reinterpret_cast<void*>(&ReceiveHook);
    }

    gRakServer = rakServer;
}

void* SampRakNetHook::GetRakServerInterfaceHook() {
    gGetRakServerHook.Disable();
    using GetRakFn = void* (*)();
    void* rakServer = reinterpret_cast<GetRakFn>(gGetRakServerHook.target)();

    if (rakServer != nullptr) {
        BindRakServer(rakServer);
        // One-shot: permanently remove inline hook once bound so subsequent calls / urmem never contend
        gGetRakServerHook.Reset();
    } else {
        gGetRakServerHook.Enable();
    }
    return rakServer;
}

void SampRakNetHook::ConnectRpcHook(RakRpcParameters* rpc) {
    if (rpc && gOnConnect && gRakServer && !gRakServerDestroyed && gFuncGetIndex) {
        const int sender = gFuncGetIndex(gRakServer, rpc->sender);
        if (sender >= 0 && sender < static_cast<int>(kSampMaxPlayers)) {
            const std::size_t totalBytes = (rpc->numberOfBitsOfData + 7u) >> 3;
            gOnConnect(static_cast<uint16_t>(sender), rpc->input, totalBytes);
        }
    }
    if (gOrigConnectHandler) {
        gOrigConnectHandler(rpc);
    }
}

#ifdef _WIN32
void* __fastcall SampRakNetHook::DestructorHook(void* _this, void*, unsigned int flags)
#else
void SampRakNetHook::DestructorHook(void* _this)
#endif
{
    if (_this == gRakServer) {
        gRakServerDestroyed = true;
    }
#ifdef _WIN32
    return gFuncDestructor ? gFuncDestructor(_this, flags) : _this;
#else
    if (gFuncDestructor) {
        gFuncDestructor(_this);
    }
#endif
}

#ifdef _WIN32
void __fastcall SampRakNetHook::RegisterRpcHook(void* _this, void*, uint8_t* rpcIdPtr, void(*handler)(RakRpcParameters*))
#else
void SampRakNetHook::RegisterRpcHook(void* _this, uint8_t* rpcIdPtr, void(*handler)(RakRpcParameters*))
#endif
{
    if (rpcIdPtr && *rpcIdPtr == protocol::kRakNetRpcPlayerConnect) {
        gOrigConnectHandler = handler;
        handler = &ConnectRpcHook;
    }
    gFuncRegisterRpc(_this, rpcIdPtr, handler);
}

#ifdef _WIN32
RakPacket* __fastcall SampRakNetHook::ReceiveHook(void* _this, void*)
#else
RakPacket* SampRakNetHook::ReceiveHook(void* _this)
#endif
{
    RakPacket* packet = gFuncReceive(_this);
    while (packet != nullptr) {
        if (packet->data != nullptr && packet->length >= 1) {
            const uint8_t pktId = packet->data[0];
            if (pktId == protocol::kRakNetPacketIdDisconnect ||
                pktId == protocol::kRakNetPacketIdConnectionLost) {
                if (gOnDisconnect) {
                    gOnDisconnect(packet->playerIndex);
                }
                return packet; // Do NOT consume disconnect notifications
            }
        }
        if (gOnPacket && gOnPacket(packet->playerIndex, packet->data, packet->length)) {
            gFuncDeallocate(_this, packet);
            packet = gFuncReceive(_this);
            continue;
        }
        break;
    }
    return packet;
}

} // namespace svplus::server::samp
