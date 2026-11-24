#pragma once

#include "ShutdownState.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace svplus::client::diag {

/// Normalizes an absolute path for privacy-safe logging (`<GTA>\...` or `%USERPROFILE%\...`).
[[nodiscard]] std::string formatPrivacyPath(
    const std::filesystem::path& fullPath,
    const std::filesystem::path& gtaDir);

/// Resolves an in-process code address to `module.dll+0xOFFSET` (or `0xADDR`) and checks
/// whether its entry instructions look detoured (`E9`/`EB`/`FF 25`/`68..C3`/`MOV EAX JMP EAX`).
[[nodiscard]] std::string describeCodeAddressAndDetour(const void* addr);

/// Builds the read-only disk code-mod + root-proxy + clean GTA executable report for `gtaDir`.
/// Returns both the multi-line formatted block and any `[ATTENTION]` flags count.
[[nodiscard]] std::string scanGtaDirectoryReport(
    const std::filesystem::path& gtaDir,
    double*                      outElapsedMs = nullptr,
    const std::filesystem::path& selfModulePath = {});

/// Starts the low-priority background diagnostics scan & dynamic DLL load monitor after SA-MP detection.
void startBackgroundDiagnostics(
    const std::filesystem::path& gtaDir,
    const SampModuleDetection&   sampDetection,
    HMODULE                      selfModule = nullptr);

/// Records D3D9 hook state BEFORE and AFTER sampvoice installs its MinHook detour.
void reportD3D9HookInspection(
    const char*         hookName,
    const void*         targetAddr,
    const std::uint8_t* bytesBefore,
    std::size_t         bytesLen);

/// Records initial WndProc chain owner right before/after subclassing.
void reportWndProcAttach(HWND hwnd, WNDPROC origProc, WNDPROC ourProc);

/// Checks if another module replaced `GWL_WNDPROC` after sampvoice subclassed it; logs `[INF:mods]` once per new proc.
void checkWndProcChange(HWND hwnd, WNDPROC ourProc);

/// Inspects key `bass.dll` exported functions for third-party detours or hooks.
void reportBassHookInspection(HMODULE bassMod);

/// Inspects RakClient vtable slots for non-samp hooks.
void reportRakClientInspection(const void* rakClientPtr);

/// Unregisters dynamic loader notifications on clean DLL unload (skipped during ExitProcess).
void shutdownDiagnostics(bool processTerminating) noexcept;

} // namespace svplus::client::diag
