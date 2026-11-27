#pragma once

#include "samp/VersionDetector.hpp"
#include "svplus/client/Platform.hpp"

#include <atomic>
#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace svplus::client {

struct SampModuleDetection {
    bool              detected{false};
    HMODULE           module{nullptr};
    samp::SampVersion version{samp::SampVersion::Unknown};
    std::uint32_t     timeDateStamp{0};
    std::uint32_t     entryPointRva{0};
    std::wstring      moduleName{};
    std::wstring      modulePath{};

    bool              unsupportedSampDll{false};
    bool              rejectedSampAsi{false};
    std::uint32_t     rejectedTimeDateStamp{0};
    std::uint32_t     rejectedEntryPointRva{0};
    std::wstring      rejectedSampAsiPath{};
};

/// Returns true once an official SA-MP module (`samp.dll` or validated `samp.asi`) has been detected.
[[nodiscard]] bool isSampModuleLoaded() noexcept;

/// Returns the cached HMODULE of the validated SA-MP module (`samp.dll` or `samp.asi`), or nullptr.
[[nodiscard]] HMODULE detectedSampModuleHandle() noexcept;

/// Returns the full cached validation snapshot (populated once detected).
[[nodiscard]] SampModuleDetection getDetectedSampModule() noexcept;

/// Probes in-memory modules (`samp.dll` first, then `samp.asi`) and validates PE signature strictly.
/// Updates cached detection state only when a valid SA-MP module is confirmed.
[[nodiscard]] SampModuleDetection probeValidatedSampModule() noexcept;

} // namespace svplus::client
