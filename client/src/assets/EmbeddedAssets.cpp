#include "assets/EmbeddedAssets.hpp"

#include "ResourceIds.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

namespace svplus::client::assets {
namespace {

std::atomic<HMODULE> gResourceModule{nullptr};

std::span<const std::uint8_t> loadRcData(int resourceId) noexcept {
    HMODULE mod = gResourceModule.load(std::memory_order_acquire);
    if (!mod) {
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&loadRcData),
            &mod);
    }
    if (!mod) {
        return {};
    }
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!res) {
        return {};
    }
    HGLOBAL loaded = LoadResource(mod, res);
    if (!loaded) {
        return {};
    }
    const DWORD size = SizeofResource(mod, res);
    const void* data = LockResource(loaded);
    if (!data || size == 0) {
        return {};
    }
    return {static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)};
}

} // namespace

void setResourceModule(void* hModule) noexcept {
    gResourceModule.store(static_cast<HMODULE>(hModule), std::memory_order_release);
}

std::span<const std::uint8_t> kFontTtf() noexcept         { return loadRcData(IDR_SV_FONT_TTF); }
std::span<const std::uint8_t> kTitleFontTtf() noexcept    { return loadRcData(IDR_SV_TITLE_FONT_TTF); }
std::span<const std::uint8_t> kMicroActivePng() noexcept  { return loadRcData(IDR_SV_MICRO_ACT_PNG); }
std::span<const std::uint8_t> kMicroMutedPng() noexcept   { return loadRcData(IDR_SV_MICRO_MUT_PNG); }
std::span<const std::uint8_t> kMicroPassivePng() noexcept { return loadRcData(IDR_SV_MICRO_PAS_PNG); }
std::span<const std::uint8_t> kSpeakerPng() noexcept      { return loadRcData(IDR_SV_SPEAKER_PNG); }
std::span<const std::uint8_t> kRadioPng() noexcept        { return loadRcData(IDR_SV_RADIO_PNG); }
std::span<const std::uint8_t> kPhonePng() noexcept        { return loadRcData(IDR_SV_PHONE_PNG); }
std::span<const std::uint8_t> kMegaphonePng() noexcept    { return loadRcData(IDR_SV_MEGAPHONE_PNG); }
std::span<const std::uint8_t> kPolicePng() noexcept          { return loadRcData(IDR_SV_POLICE_PNG); }
std::span<const std::uint8_t> kAdminPng() noexcept           { return loadRcData(IDR_SV_ADMIN_PNG); }
std::span<const std::uint8_t> kGlobalPng() noexcept          { return loadRcData(IDR_SV_GLOBAL_PNG); }
std::span<const std::uint8_t> kSpeakerBlockedPng() noexcept  { return loadRcData(IDR_SV_SPEAKER_BLOCKED_PNG); }
std::span<const std::uint8_t> kRadioBlockedPng() noexcept    { return loadRcData(IDR_SV_RADIO_BLOCKED_PNG); }
std::span<const std::uint8_t> kPhoneBlockedPng() noexcept    { return loadRcData(IDR_SV_PHONE_BLOCKED_PNG); }
std::span<const std::uint8_t> kMegaphoneBlockedPng() noexcept { return loadRcData(IDR_SV_MEGAPHONE_BLOCKED_PNG); }
std::span<const std::uint8_t> kPoliceBlockedPng() noexcept   { return loadRcData(IDR_SV_POLICE_BLOCKED_PNG); }
std::span<const std::uint8_t> kAdminBlockedPng() noexcept    { return loadRcData(IDR_SV_ADMIN_BLOCKED_PNG); }
std::span<const std::uint8_t> kGlobalBlockedPng() noexcept   { return loadRcData(IDR_SV_GLOBAL_BLOCKED_PNG); }
std::span<const std::uint8_t> kSpeakerDeafPng() noexcept     { return loadRcData(IDR_SV_SPEAKER_DEAF_PNG); }
std::span<const std::uint8_t> kMicroMissingPng() noexcept    { return loadRcData(IDR_SV_MICRO_MISSING_PNG); }

} // namespace svplus::client::assets
