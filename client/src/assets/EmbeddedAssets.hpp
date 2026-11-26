#pragma once

#include <cstdint>
#include <span>

namespace svplus::client::assets {

void setResourceModule(void* hModule) noexcept;

[[nodiscard]] std::span<const std::uint8_t> kFontTtf() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kTitleFontTtf() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kMicroActivePng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kMicroMutedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kMicroPassivePng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kSpeakerPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kRadioPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kPhonePng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kMegaphonePng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kPolicePng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kAdminPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kGlobalPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kSpeakerBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kRadioBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kPhoneBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kMegaphoneBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kPoliceBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kAdminBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kGlobalBlockedPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kSpeakerDeafPng() noexcept;
[[nodiscard]] std::span<const std::uint8_t> kMicroMissingPng() noexcept;

} // namespace svplus::client::assets
