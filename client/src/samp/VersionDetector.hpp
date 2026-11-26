#pragma once

#include <cstdint>

namespace svplus::client::samp {

enum class SampVersion : std::uint8_t {
    Unknown = 0,
    V037R1,
    V037R2,
    V037R3,
    V037R4,
    V037R4_2,
    V037R5,
    V03DL,
};

/// Binary-verified layout descriptor for every official SA-MP 0.3.7 / 0.3.DL build.
struct SampLayout {
    SampVersion   version{SampVersion::Unknown};
    const char*   name{"Unknown"};

    // Global singleton pointer RVAs relative to samp.dll base
    std::uint32_t netGamePtr{0};
    std::uint32_t chatPtr{0};
    std::uint32_t inputPtr{0};
    std::uint32_t scoreboardPtr{0};
    std::uint32_t dialogPtr{0};
    std::uint32_t gamePtr{0};

    // CNetGame offsets
    std::uint32_t netGameRakClientOffset{0};
    std::uint32_t netGamePoolsOffset{0};
    std::uint32_t netGameStateOffset{0};
    std::uint32_t netGameSettingsOffset{0}; // CNetGame::Settings*
    std::uint32_t netGameHostOffset{0};
    std::uint32_t netGameHostnameOffset{0};
    std::uint32_t netGamePortOffset{0};

    // CNetGame::Settings offsets
    std::uint32_t settingsNameTagsDrawDistOffset{0};      // float m_fNameTagsDrawDist
    std::uint32_t settingsNoNametagsBehindWallsOffset{0}; // BYTE  m_bNoNametagsBehindWalls
    std::uint32_t settingsNameTagsEnabledOffset{0};       // BYTE  m_bNameTags

    // Pools struct offsets
    std::uint32_t poolsPlayerOffset{0};
    std::uint32_t poolsVehicleOffset{0};
    std::uint32_t poolsObjectOffset{0};

    // CPlayerPool offsets
    std::uint32_t playerPoolLocalIdOffset{0};
    std::uint32_t playerPoolLocalNameOffset{0}; // Points to MSVC std::string _Bx
    std::uint32_t playerPoolLocalPlayerOffset{0}; // CLocalPlayer*
    std::uint32_t playerPoolArrayOffset{0};     // CPlayerInfo*[1004]
    std::uint32_t playerPoolNotEmptyOffset{0};  // BOOL[1004]

    // CLocalPlayer offsets
    std::uint32_t localPlayerIsActiveOffset{0}; // BOOL m_bIsActive (non-zero once spawned)

    // CPlayerInfo offsets
    std::uint32_t playerInfoRemoteOffset{0};    // CRemotePlayer*
    std::uint32_t playerInfoNickOffset{0};      // Points to MSVC std::string _Bx

    // CRemotePlayer / CPed / CEntity offsets
    std::uint32_t remotePlayerPedOffset{0};         // CPed*
    std::uint32_t remotePlayerShowNameTagOffset{0}; // BOOL m_bDrawLabels
    std::uint32_t pedGamePedOffset{0};              // GTA CPed* (0x2A4)
    std::uint32_t objectEntityOffset{0};        // GTA CEntity* (0x40)

    // CVehiclePool offsets
    std::uint32_t vehiclePoolObjectOffset{0};
    std::uint32_t vehiclePoolNotEmptyOffset{0};
    std::uint32_t vehiclePoolGameObjectOffset{0};
    std::uint32_t maxVehicles{2000};

    // CObjectPool offsets
    std::uint32_t objectPoolNotEmptyOffset{0};
    std::uint32_t objectPoolArrayOffset{0};
    std::uint32_t maxObjects{1000};

    // UI & Cursor offsets / RVAs
    std::uint32_t inputEnabledOffset{0x14E0};
    std::uint32_t scoreboardEnabledOffset{0x00};
    std::uint32_t dialogIsActiveOffset{0x28};
    std::uint32_t gamePlayerPedOffset{0x08};
    std::uint32_t gameSetCursorModeRva{0};

    // Per-player 0xRRGGBBAA color table RVA (DWORD[4000], followed by 100-entry default palette)
    std::uint32_t colorTableRva{0};
};

/// Matches paired PE header signatures (TimeDateStamp AND AddressOfEntryPoint) strictly.
[[nodiscard]] constexpr SampVersion detectSampVersion(
    std::uint32_t timeDateStamp,
    std::uint32_t entryPointRva) noexcept {
    if (timeDateStamp == 0x5542F47Au && entryPointRva == 0x31DF13u) return SampVersion::V037R1;
    if (timeDateStamp == 0x59C30C94u && entryPointRva == 0x3195DDu) return SampVersion::V037R2;
    if (timeDateStamp == 0x5C0B4243u && entryPointRva == 0x0CC4D0u) return SampVersion::V037R3;
    if (timeDateStamp == 0x5DD606CDu && entryPointRva == 0x0CBCB0u) return SampVersion::V037R4;
    if (timeDateStamp == 0x6094ACABu && entryPointRva == 0x0CBCD0u) return SampVersion::V037R4_2;
    if (timeDateStamp == 0x6372C39Eu && entryPointRva == 0x0CBC90u) return SampVersion::V037R5;
    if (timeDateStamp == 0x5A6A3130u && entryPointRva == 0x0FDB60u) return SampVersion::V03DL;
    return SampVersion::Unknown;
}

[[nodiscard]] constexpr SampLayout getSampLayout(SampVersion version) noexcept {
    switch (version) {
        case SampVersion::V037R1:
            return {
                .version = SampVersion::V037R1,
                .name = "0.3.7-R1",
                .netGamePtr = 0x21A0F8u,
                .chatPtr = 0x21A0E4u,
                .inputPtr = 0x21A0E8u,
                .scoreboardPtr = 0x21A0B4u,
                .dialogPtr = 0x21A0B8u,
                .gamePtr = 0x21A10Cu,
                .netGameRakClientOffset = 0x3C9u,
                .netGamePoolsOffset = 0x3CDu,
                .netGameStateOffset = 0x3BDu,
                .netGameSettingsOffset = 0x3C5u,
                .netGameHostOffset = 0x020u,
                .netGameHostnameOffset = 0x121u,
                .netGamePortOffset = 0x225u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x18u,
                .poolsVehicleOffset = 0x1Cu,
                .poolsObjectOffset = 0x04u,
                .playerPoolLocalIdOffset = 0x04u,
                .playerPoolLocalNameOffset = 0x0Au,
                .playerPoolLocalPlayerOffset = 0x22u,
                .playerPoolArrayOffset = 0x2Eu,
                .playerPoolNotEmptyOffset = 0xFDEu,
                .localPlayerIsActiveOffset = 0x0Cu,
                .playerInfoRemoteOffset = 0x00u, // first field (valid 0x00)
                .playerInfoNickOffset = 0x0Cu,
                .remotePlayerPedOffset = 0x00u, // first field (valid 0x00)
                .remotePlayerShowNameTagOffset = 0xB3u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0xFA4u,
                .maxObjects = 1000u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0x9BD30u,
                .colorTableRva = 0x216378u,
            };
        case SampVersion::V037R2:
            return {
                .version = SampVersion::V037R2,
                .name = "0.3.7-R2",
                .netGamePtr = 0x21A100u,
                .chatPtr = 0x21A0ECu,
                .inputPtr = 0x21A0F0u,
                .scoreboardPtr = 0x21A0BCu,
                .dialogPtr = 0x21A0C0u,
                .gamePtr = 0x21A114u,
                .netGameRakClientOffset = 0x018u,
                .netGamePoolsOffset = 0x3C5u,
                .netGameStateOffset = 0x3B9u,
                .netGameSettingsOffset = 0x3C1u,
                .netGameHostOffset = 0x01Cu,
                .netGameHostnameOffset = 0x11Du,
                .netGamePortOffset = 0x221u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x08u,
                .poolsVehicleOffset = 0x0Cu,
                .poolsObjectOffset = 0x14u,
                .playerPoolLocalIdOffset = 0x00u, // first field (valid 0x00)
                .playerPoolLocalNameOffset = 0x06u,
                .playerPoolLocalPlayerOffset = 0x1Eu,
                .playerPoolArrayOffset = 0x26u,
                .playerPoolNotEmptyOffset = 0xFD6u,
                .localPlayerIsActiveOffset = 0xF4u,
                .playerInfoRemoteOffset = 0x0Cu,
                .playerInfoNickOffset = 0x14u,
                .remotePlayerPedOffset = 0x1Cu,
                .remotePlayerShowNameTagOffset = 0x08u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0xFA4u,
                .maxObjects = 1000u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0x9BDD0u,
                .colorTableRva = 0x216380u,
            };
        case SampVersion::V037R3:
            return {
                .version = SampVersion::V037R3,
                .name = "0.3.7-R3",
                .netGamePtr = 0x26E8DCu,
                .chatPtr = 0x26E8C8u,
                .inputPtr = 0x26E8CCu,
                .scoreboardPtr = 0x26E894u,
                .dialogPtr = 0x26E898u,
                .gamePtr = 0x26E8F4u,
                .netGameRakClientOffset = 0x02Cu,
                .netGamePoolsOffset = 0x3DEu,
                .netGameStateOffset = 0x3CDu,
                .netGameSettingsOffset = 0x3D5u,
                .netGameHostOffset = 0x030u,
                .netGameHostnameOffset = 0x131u,
                .netGamePortOffset = 0x235u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x08u,
                .poolsVehicleOffset = 0x0Cu,
                .poolsObjectOffset = 0x14u,
                .playerPoolLocalIdOffset = 0x2F1Cu,
                .playerPoolLocalNameOffset = 0x2F22u,
                .playerPoolLocalPlayerOffset = 0x2F3Au,
                .playerPoolArrayOffset = 0x04u,
                .playerPoolNotEmptyOffset = 0xFB4u,
                .localPlayerIsActiveOffset = 0xF4u,
                .playerInfoRemoteOffset = 0x00u, // first field (valid 0x00)
                .playerInfoNickOffset = 0x0Cu,
                .remotePlayerPedOffset = 0x00u, // first field (valid 0x00)
                .remotePlayerShowNameTagOffset = 0x10u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0xFA4u,
                .maxObjects = 1000u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0x9FFE0u,
                .colorTableRva = 0x151578u,
            };
        case SampVersion::V037R4:
            return {
                .version = SampVersion::V037R4,
                .name = "0.3.7-R4",
                .netGamePtr = 0x26EA0Cu,
                .chatPtr = 0x26E9F8u,
                .inputPtr = 0x26E9FCu,
                .scoreboardPtr = 0x26E9C4u,
                .dialogPtr = 0x26E9C8u,
                .gamePtr = 0x26EA24u,
                .netGameRakClientOffset = 0x02Cu,
                .netGamePoolsOffset = 0x3DEu,
                .netGameStateOffset = 0x3CDu,
                .netGameSettingsOffset = 0x3D5u,
                .netGameHostOffset = 0x030u,
                .netGameHostnameOffset = 0x131u,
                .netGamePortOffset = 0x235u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x08u,
                .poolsVehicleOffset = 0x0Cu,
                .poolsObjectOffset = 0x14u,
                .playerPoolLocalIdOffset = 0x0Cu,
                .playerPoolLocalNameOffset = 0x12u,
                .playerPoolLocalPlayerOffset = 0x2Au,
                .playerPoolArrayOffset = 0x2Eu,
                .playerPoolNotEmptyOffset = 0xFDEu,
                .localPlayerIsActiveOffset = 0xF0u,
                .playerInfoRemoteOffset = 0x10u,
                .playerInfoNickOffset = 0x18u,
                .remotePlayerPedOffset = 0x1DDu,
                .remotePlayerShowNameTagOffset = 0x04u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0xFA4u,
                .maxObjects = 1000u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0xA0720u,
                .colorTableRva = 0x1516A0u,
            };
        case SampVersion::V037R4_2:
            return {
                .version = SampVersion::V037R4_2,
                .name = "0.3.7-R4-2",
                .netGamePtr = 0x26EA0Cu,
                .chatPtr = 0x26E9F8u,
                .inputPtr = 0x26E9FCu,
                .scoreboardPtr = 0x26E9C4u,
                .dialogPtr = 0x26E9C8u,
                .gamePtr = 0x26EA24u,
                .netGameRakClientOffset = 0x000u, // first field (valid 0x00)
                .netGamePoolsOffset = 0x3DEu,
                .netGameStateOffset = 0x3CDu,
                .netGameSettingsOffset = 0x3D5u,
                .netGameHostOffset = 0x030u,
                .netGameHostnameOffset = 0x131u,
                .netGamePortOffset = 0x235u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x04u,
                .poolsVehicleOffset = 0x00u, // first field (valid 0x00)
                .poolsObjectOffset = 0x0Cu,
                .playerPoolLocalIdOffset = 0x04u,
                .playerPoolLocalNameOffset = 0x0Au,
                .playerPoolLocalPlayerOffset = 0x26u,
                .playerPoolArrayOffset = 0x1F8Au,
                .playerPoolNotEmptyOffset = 0x2Au,
                .localPlayerIsActiveOffset = 0xF0u,
                .playerInfoRemoteOffset = 0x10u,
                .playerInfoNickOffset = 0x18u,
                .remotePlayerPedOffset = 0x1DDu,
                .remotePlayerShowNameTagOffset = 0x04u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0xFA4u,
                .maxObjects = 1000u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0xA0750u,
                .colorTableRva = 0x1516A0u,
            };
        case SampVersion::V037R5:
            return {
                .version = SampVersion::V037R5,
                .name = "0.3.7-R5",
                .netGamePtr = 0x26EB94u,
                .chatPtr = 0x26EB80u,
                .inputPtr = 0x26EB84u,
                .scoreboardPtr = 0x26EB4Cu,
                .dialogPtr = 0x26EB50u,
                .gamePtr = 0x26EBACu,
                .netGameRakClientOffset = 0x000u, // first field (valid 0x00)
                .netGamePoolsOffset = 0x3DEu,
                .netGameStateOffset = 0x3CDu,
                .netGameSettingsOffset = 0x3D5u,
                .netGameHostOffset = 0x030u,
                .netGameHostnameOffset = 0x131u,
                .netGamePortOffset = 0x235u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x04u,
                .poolsVehicleOffset = 0x00u, // first field (valid 0x00)
                .poolsObjectOffset = 0x0Cu,
                .playerPoolLocalIdOffset = 0x04u,
                .playerPoolLocalNameOffset = 0x0Au,
                .playerPoolLocalPlayerOffset = 0x26u,
                .playerPoolArrayOffset = 0x1F8Au,
                .playerPoolNotEmptyOffset = 0x2Au,
                .localPlayerIsActiveOffset = 0xF0u,
                .playerInfoRemoteOffset = 0x10u,
                .playerInfoNickOffset = 0x18u,
                .remotePlayerPedOffset = 0x1DDu,
                .remotePlayerShowNameTagOffset = 0x04u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0xFA4u,
                .maxObjects = 1000u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0xA06F0u,
                .colorTableRva = 0x151828u,
            };
        case SampVersion::V03DL:
            return {
                .version = SampVersion::V03DL,
                .name = "0.3.DL-R1",
                .netGamePtr = 0x2ACA24u,
                .chatPtr = 0x2ACA10u,
                .inputPtr = 0x2ACA14u,
                .scoreboardPtr = 0x2AC9DCu,
                .dialogPtr = 0x2AC9E0u,
                .gamePtr = 0x2ACA3Cu,
                .netGameRakClientOffset = 0x02Cu,
                .netGamePoolsOffset = 0x3DEu,
                .netGameStateOffset = 0x3CDu,
                .netGameSettingsOffset = 0x3D5u,
                .netGameHostOffset = 0x030u,
                .netGameHostnameOffset = 0x131u,
                .netGamePortOffset = 0x235u,
                .settingsNameTagsDrawDistOffset = 0x27u,
                .settingsNoNametagsBehindWallsOffset = 0x2Fu,
                .settingsNameTagsEnabledOffset = 0x38u,
                .poolsPlayerOffset = 0x08u,
                .poolsVehicleOffset = 0x0Cu,
                .poolsObjectOffset = 0x14u,
                .playerPoolLocalIdOffset = 0x00u, // first field (valid 0x00)
                .playerPoolLocalNameOffset = 0x06u,
                .playerPoolLocalPlayerOffset = 0x1Eu,
                .playerPoolArrayOffset = 0x26u,
                .playerPoolNotEmptyOffset = 0xFD6u,
                .localPlayerIsActiveOffset = 0xF4u,
                .playerInfoRemoteOffset = 0x08u,
                .playerInfoNickOffset = 0x14u,
                .remotePlayerPedOffset = 0x04u,
                .remotePlayerShowNameTagOffset = 0x10u,
                .pedGamePedOffset = 0x2A4u,
                .objectEntityOffset = 0x40u,
                .vehiclePoolObjectOffset = 0x1134u,
                .vehiclePoolNotEmptyOffset = 0x3074u,
                .vehiclePoolGameObjectOffset = 0x4FB4u,
                .maxVehicles = 2000u,
                .objectPoolNotEmptyOffset = 0x04u,
                .objectPoolArrayOffset = 0x20D4u,
                .maxObjects = 2100u,
                .inputEnabledOffset = 0x14E0u,
                .scoreboardEnabledOffset = 0x00u, // first field (valid 0x00)
                .dialogIsActiveOffset = 0x28u,
                .gamePlayerPedOffset = 0x08u,
                .gameSetCursorModeRva = 0xA0530u,
                .colorTableRva = 0x18F6C0u,
            };
        case SampVersion::Unknown:
        default:
            return {};
    }
}

} // namespace svplus::client::samp
