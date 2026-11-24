#pragma once

#include "svplus/common/Logger.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace svplus::client::samp {

// Offset from RakPeer base to its RakClientInterface subobject across all SA-MP 0.3.7/0.3.DL builds.
// Verified identical (0xDDE) in the RakClient destructor routine:
//   R1 (0x342D0), R2 (0x343B0), R3-1 (0x37680), R4 (0x37D70),
//   R4-2 (0x37DC0), R5-1 / R5-2-MP (0x37DC0), DL-R1 / DL-R1-2-MP (0x37880).
// Sequence: mov eax,[esp+4]; test eax,eax; jz; lea ecx,[eax-0xDDE]; test ecx,ecx; jz; mov eax,[ecx]; push 1; call [eax]; ret
inline constexpr std::uintptr_t kRakClientInRakPeerOffset = 0xDDEu;
inline constexpr std::size_t    kShadowRakPeerVtableSlots = 64u;

/// Scans [base, base + size) read-only for the RakNet RakClient teardown pattern
/// and extracts the negative displacement (expected 0xDDE). Never modifies code in samp.dll.
inline bool scanSampRakClientDestroyDisplacement(
    const std::uint8_t* imageBase,
    std::size_t imageSize,
    std::uintptr_t& outRva,
    std::int32_t& outDisp) noexcept {
    // 8B 44 24 04 85 C0 74 ?? 8D 88 <disp32> 85 C9 74 ?? 8B 01 6A 01 FF 10 C3 (25 bytes)
    constexpr std::size_t kPatternLen = 25u;
    if (!imageBase || imageSize < kPatternLen) {
        return false;
    }

    for (std::size_t i = 0; i <= imageSize - kPatternLen; ++i) {
        if (imageBase[i + 0] == 0x8B && imageBase[i + 1] == 0x44 &&
            imageBase[i + 2] == 0x24 && imageBase[i + 3] == 0x04 &&
            imageBase[i + 4] == 0x85 && imageBase[i + 5] == 0xC0 &&
            imageBase[i + 6] == 0x74 &&
            imageBase[i + 8] == 0x8D && imageBase[i + 9] == 0x88 &&
            imageBase[i + 14] == 0x85 && imageBase[i + 15] == 0xC9 &&
            imageBase[i + 16] == 0x74 &&
            imageBase[i + 18] == 0x8B && imageBase[i + 19] == 0x01 &&
            imageBase[i + 20] == 0x6A && imageBase[i + 21] == 0x01 &&
            imageBase[i + 22] == 0xFF && imageBase[i + 23] == 0x10 &&
            imageBase[i + 24] == 0xC3) {
            std::int32_t disp = 0;
            std::memcpy(&disp, imageBase + i + 10, sizeof(disp));
            outRva  = i;
            outDisp = disp;
            return true;
        }
    }
    return false;
}

/// Allocates a shadow RakPeer memory block with `ProxyT` constructed at `+0xDDE` (`kRakClientInRakPeerOffset`).
/// When samp.dll shuts down and invokes `((proxy - 0xDDE)->vtable[0])(1)`, our shadow deleting
/// destructor safely tears down `ProxyT`, forwards destruction to `orig - 0xDDE`, and leaks the
/// small shadow block intentionally for process teardown safety.
template <typename ProxyT, typename OrigT>
class ShadowRakPeerFactory {
public:
    template <typename... Args>
    static ProxyT* create(Args&&... args) {
        constexpr std::size_t kTotalBytes = kRakClientInRakPeerOffset + sizeof(ProxyT);
        auto* block = static_cast<std::uint8_t*>(
            ::operator new(kTotalBytes, std::align_val_t{16}));
        std::memset(block, 0, kTotalBytes);

        // Place shadow RakPeer vtable pointer at offset +0 of the enclosing block.
        *reinterpret_cast<const void* const**>(block) = vtable().data();

        // Construct ProxyT at +0xDDE (matching RakClient subobject layout inside RakPeer).
        auto* proxyStorage = block + kRakClientInRakPeerOffset;
        return new (proxyStorage) ProxyT(std::forward<Args>(args)...);
    }

private:
#if defined(_M_IX86)
    // On x86, __fastcall(ecx, edx, stackArg) uses the exact same register/stack/cleanup ABI as
    // __thiscall(this, arg) (ECX = this, [esp+4] = flags, callee cleans 4 bytes with `ret 4`).
    template <std::size_t Slot>
    static int __fastcall shadowSafeStub(void* /*self*/, void* /*edx*/) noexcept {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_acq_rel)) {
            SVPLUS_LOG_ERR_CAT("samp", "Shadow RakPeer vtable slot %u called unexpectedly",
                               static_cast<unsigned>(Slot));
        }
        return 0;
    }

    static void* __fastcall shadowDeletingDtor(void* shadowPeer, void* /*edx*/, int flags) noexcept {
        if (!shadowPeer) {
            return nullptr;
        }
        auto* proxy = reinterpret_cast<ProxyT*>(
            static_cast<std::uint8_t*>(shadowPeer) + kRakClientInRakPeerOffset);
        OrigT* orig = proxy->markDestroyedAndDetachOrig();
        if (orig) {
            auto* origPeer = reinterpret_cast<std::uint8_t*>(orig) - kRakClientInRakPeerOffset;
            using OrigDeletingDtorFn = void* (__fastcall*)(void*, void*, int);
            auto** origVtable = *reinterpret_cast<OrigDeletingDtorFn**>(origPeer);
            if (origVtable && origVtable[0]) {
                origVtable[0](origPeer, nullptr, flags);
            }
        }
        // Intentionally do not free `shadowPeer` on process shutdown so any trailing
        // references after teardown cannot hit freed memory.
        return shadowPeer;
    }
#else
    template <std::size_t Slot>
    static int shadowSafeStub(void* /*self*/) noexcept {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_acq_rel)) {
            SVPLUS_LOG_ERR_CAT("samp", "Shadow RakPeer vtable slot %u called unexpectedly",
                               static_cast<unsigned>(Slot));
        }
        return 0;
    }

    static void* shadowDeletingDtor(void* shadowPeer, int flags) noexcept {
        if (!shadowPeer) {
            return nullptr;
        }
        auto* proxy = reinterpret_cast<ProxyT*>(
            static_cast<std::uint8_t*>(shadowPeer) + kRakClientInRakPeerOffset);
        OrigT* orig = proxy->markDestroyedAndDetachOrig();
        if (orig) {
            auto* origPeer = reinterpret_cast<std::uint8_t*>(orig) - kRakClientInRakPeerOffset;
            using OrigDeletingDtorFn = void* (*)(void*, int);
            auto** origVtable = *reinterpret_cast<OrigDeletingDtorFn**>(origPeer);
            if (origVtable && origVtable[0]) {
                origVtable[0](origPeer, flags);
            }
        }
        return shadowPeer;
    }
#endif

    template <std::size_t... Is>
    static std::array<const void*, kShadowRakPeerVtableSlots> buildVtable(std::index_sequence<Is...>) noexcept {
        return {
            reinterpret_cast<const void*>(&shadowDeletingDtor),
            reinterpret_cast<const void*>(&shadowSafeStub<Is + 1>)...
        };
    }

    static const std::array<const void*, kShadowRakPeerVtableSlots>& vtable() noexcept {
        static const auto tbl = buildVtable(
            std::make_index_sequence<kShadowRakPeerVtableSlots - 1>{});
        return tbl;
    }
};

} // namespace svplus::client::samp
