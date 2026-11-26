#include "ShutdownState.hpp"
#include "assets/EmbeddedAssets.hpp"
#include "audio/BassLoader.hpp"
#include "audio/CaptureEngine.hpp"
#include "audio/PlaybackEngine.hpp"
#include "config/ClientConfig.hpp"
#include "diag/ModDiagnostics.hpp"
#include "net/VoiceClient.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Version.hpp"
#include "ui/ConfigMenu.hpp"
#include "ui/D3D9Hook.hpp"
#include "ui/HudOverlay.hpp"
#include "ui/Localization.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <MinHook.h>
#include <imgui.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace svplus::client {
namespace {

std::atomic<bool>    g_sampValidated{false};
std::atomic<HMODULE> g_sampModule{nullptr};

std::mutex& detectionMutex() noexcept {
    static auto* m = new std::mutex();
    return *m;
}

SampModuleDetection& cachedDetection() noexcept {
    static auto* d = new SampModuleDetection();
    return *d;
}

bool readModulePeHeaderSafe(
    HMODULE       mod,
    std::uint32_t& outStamp,
    std::uint32_t& outEntryPoint,
    std::wstring&  outPath) noexcept {
    outStamp = 0;
    outEntryPoint = 0;
    if (!mod) {
        return false;
    }
    wchar_t pathBuf[MAX_PATH]{};
    const DWORD pathLen = GetModuleFileNameW(mod, pathBuf, MAX_PATH);
    if (pathLen > 0 && pathLen < MAX_PATH) {
        outPath.assign(pathBuf, pathLen);
    }

    const auto base = reinterpret_cast<std::uintptr_t>(mod);
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }
        outStamp = nt->FileHeader.TimeDateStamp;
        outEntryPoint = nt->OptionalHeader.AddressOfEntryPoint;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string wideToUtf8Local(std::wstring_view ws) {
    if (ws.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string computeModuleSha256Hex(HMODULE mod) {
    if (!mod) return {};
    wchar_t path[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(mod, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};

    HANDLE hFile = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::string hex;
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cbData = 0;
        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                              sizeof(objLen), &cbData, 0) == 0 && objLen > 0) {
            std::vector<UCHAR> obj(objLen);
            if (BCryptCreateHash(hAlg, &hHash, obj.data(), objLen, nullptr, 0, 0) == 0) {
                std::array<UCHAR, 32768> buf{};
                DWORD readBytes = 0;
                bool ok = true;
                while (ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &readBytes, nullptr) &&
                       readBytes > 0) {
                    if (BCryptHashData(hHash, buf.data(), readBytes, 0) != 0) {
                        ok = false;
                        break;
                    }
                }
                UCHAR digest[32]{};
                if (ok && BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    hex.reserve(64);
                    for (UCHAR b : digest) {
                        hex.push_back(kHex[(b >> 4) & 0xF]);
                        hex.push_back(kHex[b & 0xF]);
                    }
                }
                BCryptDestroyHash(hHash);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    CloseHandle(hFile);
    return hex;
}

std::uint64_t getModuleFileSizeBytes(HMODULE mod, const std::wstring& fallbackPath) noexcept {
    wchar_t pathBuf[MAX_PATH]{};
    const wchar_t* p = fallbackPath.empty() ? nullptr : fallbackPath.c_str();
    if (!p && mod && GetModuleFileNameW(mod, pathBuf, MAX_PATH) > 0) {
        p = pathBuf;
    }
    if (!p) {
        return 0;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &fad) != FALSE) {
        ULARGE_INTEGER sz{};
        sz.HighPart = fad.nFileSizeHigh;
        sz.LowPart  = fad.nFileSizeLow;
        return sz.QuadPart;
    }
    return 0;
}

} // namespace

bool isSampModuleLoaded() noexcept {
    return g_sampValidated.load(std::memory_order_acquire);
}

HMODULE detectedSampModuleHandle() noexcept {
    return g_sampModule.load(std::memory_order_acquire);
}

SampModuleDetection getDetectedSampModule() noexcept {
    std::lock_guard lock(detectionMutex());
    return cachedDetection();
}

SampModuleDetection probeValidatedSampModule() noexcept {
    std::lock_guard lock(detectionMutex());
    auto& cached = cachedDetection();
    if (cached.detected || cached.unsupportedSampDll) {
        return cached;
    }

    auto tryCandidate = [&](HMODULE mod, const wchar_t* name, std::uint32_t& outTs, std::uint32_t& outEp, std::wstring& outPath) -> bool {
        if (!mod) return false;
        if (!readModulePeHeaderSafe(mod, outTs, outEp, outPath)) {
            return false;
        }
        const samp::SampVersion ver = samp::detectSampVersion(outTs, outEp);
        if (ver != samp::SampVersion::Unknown) {
            cached.detected      = true;
            cached.module        = mod;
            cached.version       = ver;
            cached.timeDateStamp = outTs;
            cached.entryPointRva = outEp;
            cached.moduleName    = name;
            cached.modulePath    = outPath;
            g_sampModule.store(mod, std::memory_order_release);
            g_sampValidated.store(true, std::memory_order_release);
            return true;
        }
        if (_wcsicmp(name, L"samp.asi") == 0) {
            cached.rejectedSampAsi       = true;
            cached.rejectedTimeDateStamp = outTs;
            cached.rejectedEntryPointRva = outEp;
            cached.rejectedSampAsiPath   = outPath;
        }
        return false;
    };

    if (HMODULE sampDll = GetModuleHandleW(L"samp.dll")) {
        std::uint32_t ts = 0, ep = 0;
        std::wstring path;
        if (tryCandidate(sampDll, L"samp.dll", ts, ep, path)) {
            return cached;
        }
        // Any samp.dll present by exact name with unrecognized PE signature is an
        // unsupported SA-MP build (not singleplayer); emit an error log and disable hooks.
        cached.unsupportedSampDll = true;
        cached.module             = sampDll;
        cached.moduleName         = L"samp.dll";
        cached.modulePath         = std::move(path);
        cached.timeDateStamp      = ts;
        cached.entryPointRva      = ep;
        return cached;
    }
    if (HMODULE sampAsi = GetModuleHandleW(L"samp.asi")) {
        std::uint32_t ts = 0, ep = 0;
        std::wstring path;
        if (tryCandidate(sampAsi, L"samp.asi", ts, ep, path)) {
            return cached;
        }
    }
    return cached;
}

namespace {

HMODULE                     g_selfModule = nullptr;
HANDLE                      g_initThread = nullptr;
HANDLE                      g_instanceMutex = nullptr;
std::atomic<std::thread*>   g_audioThread{nullptr};
std::atomic<bool>           g_audioRunning{false};
std::atomic<bool>           g_sampConnected{false};
std::atomic<std::uint8_t>   g_uiInputBlockKind{0}; // 0=none, 1=samp_input, 2=menu_open

// Heap-allocated runtime state so static CRT teardown on ExitProcess (lpReserved != nullptr)
// never destroys mutexes, paths, or shared_ptr snapshots while background threads exist.
struct RuntimeState {
    std::mutex                                               uiMutex;
    std::mutex                                               configFileMutex;
    bool                                                     prevMenuOpen{false};
    std::filesystem::path                                    userDir;
    std::filesystem::path                                    configPath;
    config::ClientConfig                                     uiConfig;
    config::ClientConfig                                     lastSavedConfig;
    ui::Localization                                         localization;
    std::atomic<std::shared_ptr<const config::ClientConfig>> configSnapshot{};

    bool                                                     wasConnected{false};
    std::string                                              activeServerEndpoint{};
    std::string                                              activeServerHostname{};
    std::string                                              lastConfigSaveErrKey{};
};

RuntimeState& runtime() noexcept {
    static auto* s = new RuntimeState();
    return *s;
}

std::atomic<bool>         g_configDirty{false};
std::atomic<std::int64_t> g_lastDirtyMs{0};

constexpr std::int64_t kConfigSaveDebounceMs = 500;

std::int64_t steadyNowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void publishConfigSnapshotLocked(bool markDirty) {
    auto& rt = runtime();
    auto snapshot = std::make_shared<const config::ClientConfig>(rt.uiConfig);
    rt.configSnapshot.store(std::move(snapshot), std::memory_order_release);
    if (markDirty) {
        g_lastDirtyMs.store(steadyNowMs(), std::memory_order_release);
        g_configDirty.store(true, std::memory_order_release);
    }
}

std::filesystem::path moduleDirectoryPath(HMODULE mod) {
    wchar_t buf[MAX_PATH]{};
    if (mod && GetModuleFileNameW(mod, buf, MAX_PATH) > 0) {
        return std::filesystem::path(buf).parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path gameRootDirectoryPath() {
    wchar_t buf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
        return std::filesystem::path(buf).parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path userDataDirectoryPath() {
    wchar_t docsPath[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docsPath))) {
        const auto target =
            std::filesystem::path(docsPath) / L"GTA San Andreas User Files" / L"SAMP" / L"sampvoice";
        std::error_code ec;
        std::filesystem::create_directories(target, ec);
        if (!ec) {
            return target;
        }
    }
    return moduleDirectoryPath(g_selfModule);
}

bool acquireProcessInstanceMutex() noexcept {
    const std::string mutexName =
        "Local\\SampVoicePlus_Client_" + std::to_string(GetCurrentProcessId());
    g_instanceMutex = CreateMutexA(nullptr, FALSE, mutexName.c_str());
    if (!g_instanceMutex) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
        return false;
    }
    return true;
}

bool persistConfigFileLocked(
    RuntimeState&               rt,
    const config::ClientConfig& cfg,
    bool                        logDiff) {
    if (rt.configPath.empty()) {
        return false;
    }
    if (logDiff) {
        cfg.logConfigDiff(rt.lastSavedConfig);
    }
    errno = 0;
    SetLastError(0);
    if (cfg.saveToFile(rt.configPath)) {
        rt.lastSavedConfig = cfg;
        rt.lastConfigSaveErrKey.clear();
        return true;
    }
    const DWORD winErr = GetLastError();
    const int cErr = errno;
    char keyBuf[64]{};
    std::snprintf(keyBuf, sizeof(keyBuf), "%lu:%d", static_cast<unsigned long>(winErr), cErr);
    if (rt.lastConfigSaveErrKey != keyBuf) {
        rt.lastConfigSaveErrKey = keyBuf;
        SVPLUS_LOG_ERR_CAT(
            "config",
            "Failed to save sampvoice.ini to '%s' (winErr=%lu, errno=%d)",
            diag::formatPrivacyPath(rt.configPath, gameRootDirectoryPath()).c_str(),
            static_cast<unsigned long>(winErr),
            cErr);
    }
    return false;
}

void saveConfigWithDiffLocked(const std::shared_ptr<const config::ClientConfig>& cfg) {
    auto& rt = runtime();
    if (!cfg || rt.configPath.empty()) {
        return;
    }
    std::lock_guard fileLock(rt.configFileMutex);
    persistConfigFileLocked(rt, *cfg, true);
}

void flushPendingConfigSaveOnAudioThread(const std::shared_ptr<const config::ClientConfig>& cfg, bool force) {
    if (!cfg || !g_configDirty.load(std::memory_order_acquire)) {
        return;
    }
    if (!force && (steadyNowMs() - g_lastDirtyMs.load(std::memory_order_acquire)) < kConfigSaveDebounceMs) {
        return;
    }
    if (g_configDirty.exchange(false, std::memory_order_acq_rel)) {
        saveConfigWithDiffLocked(cfg);
    }
}

void saveConfigImmediatelyIfDirtyLocked() {
    auto& rt = runtime();
    if (rt.configPath.empty() || !g_configDirty.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    std::lock_guard fileLock(rt.configFileMutex);
    persistConfigFileLocked(rt, rt.uiConfig, true);
}

void logEnvironmentSummary(
    const SampModuleDetection&   sampDet,
    const std::filesystem::path& gtaDir,
    const std::filesystem::path& userDir) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    wchar_t exeW[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exeW, MAX_PATH);

    SVPLUS_LOG_INF_CAT(
        "core",
        "Environment: pid=%lu exe='%s' gta='%s' cfgDir='%s' samp=%s(%s) cores=%lu ramTotalMB=%llu desktop=%dx%d",
        static_cast<unsigned long>(GetCurrentProcessId()),
        diag::formatPrivacyPath(exeW, gtaDir).c_str(),
        diag::formatPrivacyPath(gtaDir, gtaDir).c_str(),
        diag::formatPrivacyPath(userDir, gtaDir).c_str(),
        wideToUtf8Local(sampDet.moduleName).c_str(),
        samp::getSampLayout(sampDet.version).name,
        static_cast<unsigned long>(si.dwNumberOfProcessors),
        static_cast<unsigned long long>(ms.ullTotalPhys / (1024ull * 1024ull)),
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN));
}

void audioThreadProc() {
    auto& rt       = runtime();
    auto& capture  = audio::CaptureEngine::instance();
    auto& voice    = net::VoiceClient::instance();
    auto& playback = audio::PlaybackEngine::instance();

    if (!audio::BassLoader::instance().initialize()) {
        SVPLUS_LOG_WRN_CAT("audio", "BassLoader initialization reported warnings");
    }
    {
        auto initCfg = rt.configSnapshot.load(std::memory_order_acquire);
        if (initCfg) {
            static_cast<void>(capture.initialize(*initCfg));
        }
        const bool initialMic = (initCfg ? initCfg->microEnable : true) && capture.hasMicrophone();
        samp::SampBridge::instance().setMicrophoneAvailable(initialMic);
    }

    bool lastMicAvailable = capture.hasMicrophone();
    std::int64_t lastMetricsLogMs = steadyNowMs();

    while (g_audioRunning.load(std::memory_order_acquire) &&
           !isProcessShuttingDown()) {
        (void)audio::BassLoader::instance().pollReady();

        auto cfg = rt.configSnapshot.load(std::memory_order_acquire);
        if (cfg) {
            const bool connected = g_sampConnected.load(std::memory_order_acquire);

            const HWND hwnd = ui::D3D9Hook::instance().gameWindowAtomic();
            const bool focused = (hwnd == nullptr) ||
                                 (GetForegroundWindow() == hwnd && !IsIconic(hwnd));
            const std::uint8_t uiKind = g_uiInputBlockKind.load(std::memory_order_acquire);
            const bool blocked = !focused || (uiKind != 0u);
            const char* blockReason = !focused
                ? "focus_lost"
                : (uiKind == 2u ? "menu_open" : (uiKind == 1u ? "samp_input" : nullptr));

            capture.tick(*cfg, blocked, blockReason, voice.isAllowToggleMode());
            const bool nowMicAvailable = cfg->microEnable && capture.hasMicrophone();
            if (nowMicAvailable != lastMicAvailable) {
                lastMicAvailable = nowMicAvailable;
                samp::SampBridge::instance().setMicrophoneAvailable(nowMicAvailable);
            }
            voice.tick(*cfg, connected);
            if (connected) {
                playback.tick(*cfg, voice.isVoiceOcclusionEnabled());
            }

            flushPendingConfigSaveOnAudioThread(cfg, false);

            const std::int64_t nowMs = steadyNowMs();
            if (connected && (nowMs - lastMetricsLogMs) >= 60000) {
                lastMetricsLogMs = nowMs;
                const auto pm = playback.metricsSnapshot();
                SVPLUS_LOG_INF_CAT(
                    "net",
                    "Metrics(60s): rtt=%.0f ms, streams=%zu, activeSpeakers=%zu, txPkts=%llu, rxPkts=%llu, voiceRx=%llu, rxDrop=%llu, rxIgnored=%llu",
                    voice.rttMs(),
                    pm.streamCount,
                    pm.activeSpeakerCount,
                    static_cast<unsigned long long>(voice.txVoicePackets()),
                    static_cast<unsigned long long>(voice.rxVoicePackets()),
                    static_cast<unsigned long long>(pm.packetsReceived),
                    static_cast<unsigned long long>(voice.rxQueueDropped() + pm.packetsDropped),
                    static_cast<unsigned long long>(pm.packetsIgnored));
            }
        }

        Sleep(10);
    }

    if (auto lastCfg = rt.configSnapshot.load(std::memory_order_acquire)) {
        flushPendingConfigSaveOnAudioThread(lastCfg, true);
    }
    voice.shutdown();
    capture.shutdown();
}

void onRenderFrame(IDirect3DDevice9*, HWND, const ui::UiTextures& textures) {
    if (isProcessShuttingDown()) {
        return;
    }

    auto& rt       = runtime();
    auto& bridge   = samp::SampBridge::instance();
    auto& playback = audio::PlaybackEngine::instance();
    auto& menu     = ui::ConfigMenu::instance();
    auto& hud      = ui::HudOverlay::instance();

    bridge.tick();
    if (bridge.hasConflictingClient()) {
        return;
    }

    const bool connected = bridge.isConnected();
    if (connected) {
        playback.updateSpatialFromMainThread(net::VoiceClient::instance().isVoiceOcclusionEnabled());
    }

    const bool menuOpen = menu.isOpen();
    g_sampConnected.store(connected, std::memory_order_release);
    const std::uint8_t blockKind = menuOpen ? 2u : (bridge.isTextInputBlockingKeys() ? 1u : 0u);
    g_uiInputBlockKind.store(blockKind, std::memory_order_release);

    ui::D3D9Hook::instance().setMenuOpenState(menuOpen);
    bridge.setMenuCursorMode(menuOpen);

    std::lock_guard lock(rt.uiMutex);

    bool serverStateChanged = false;
    bool refreshedServerNameDirty = false;
    if (connected) {
        const std::string epKey   = bridge.getServerEndpointKey();
        const std::string srvName = bridge.getServerHostname();
        if (!rt.wasConnected || epKey != rt.activeServerEndpoint) {
            rt.activeServerEndpoint = epKey;
            rt.activeServerHostname = srvName;
            playback.setCurrentServerEndpoint(epKey);
            ++rt.uiConfig.muteGeneration;
            ++rt.uiConfig.volumeGeneration;
            serverStateChanged = true;
            if (!epKey.empty() && !srvName.empty() &&
                rt.uiConfig.refreshServerNameForEndpoint(epKey, srvName)) {
                refreshedServerNameDirty = true;
            }
        } else if (!epKey.empty() && !srvName.empty() && srvName != rt.activeServerHostname) {
            rt.activeServerHostname = srvName;
            if (rt.uiConfig.refreshServerNameForEndpoint(epKey, srvName)) {
                refreshedServerNameDirty = true;
            }
        }
    } else if (rt.wasConnected) {
        rt.activeServerEndpoint.clear();
        rt.activeServerHostname.clear();
        playback.setCurrentServerEndpoint({});
        ++rt.uiConfig.muteGeneration;
        ++rt.uiConfig.volumeGeneration;
        serverStateChanged = true;
    }
    rt.wasConnected = connected;

    const bool wasOpen = rt.prevMenuOpen;
    bool dirty = refreshedServerNameDirty;
    hud.render(rt.uiConfig, rt.localization, textures, menuOpen, dirty);
    menu.render(rt.uiConfig, rt.localization, textures, dirty);

    if (dirty || serverStateChanged) {
        publishConfigSnapshotLocked(dirty);
    }
    const bool nowOpen = menu.isOpen();
    if (wasOpen && !nowOpen) {
        saveConfigImmediatelyIfDirtyLocked();
        ui::D3D9Hook::instance().setMenuOpenState(false);
        bridge.setMenuCursorMode(false);
    }
    rt.prevMenuOpen = nowOpen;
}

bool onWindowMessage(HWND, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (isProcessShuttingDown() || !isSampModuleLoaded()) {
        return false;
    }

    auto& rt     = runtime();
    auto& bridge = samp::SampBridge::instance();
    const bool blocked = bridge.isTextInputBlockingKeys();

    bool closedNow = false;
    bool consumed  = false;
    {
        std::lock_guard lock(rt.uiMutex);
        auto& menu = ui::ConfigMenu::instance();
        const bool wasOpen = menu.isOpen();
        bool dirty = false;
        consumed = menu.handleWindowMessage(
            msg, wParam, lParam, rt.uiConfig, blocked, dirty);
        if (dirty) {
            publishConfigSnapshotLocked(true);
        }
        const bool nowOpen = menu.isOpen();
        ui::D3D9Hook::instance().setMenuOpenState(nowOpen);
        if (wasOpen && !nowOpen) {
            saveConfigImmediatelyIfDirtyLocked();
            closedNow = true;
        }
        rt.prevMenuOpen = nowOpen;
    }

    if (closedNow) {
        bridge.setMenuCursorMode(false);
    }
    return consumed;
}

DWORD WINAPI initThreadProc(LPVOID) {
    // Wait up to 10s for samp.dll (recognized or unknown build) or validated samp.asi.
    // In singleplayer (absence of SA-MP after 10s), perform ZERO disk/registry/mutex/BASS/MinHook actions and exit.
    constexpr DWORD kWaitWindowMs = 10000;
    constexpr DWORD kPollIntervalMs = 250;
    const std::uint64_t startMs = GetTickCount64();

    SampModuleDetection sampDet{};
    while (!isProcessShuttingDown()) {
        sampDet = probeValidatedSampModule();
        if (sampDet.detected || sampDet.unsupportedSampDll) {
            break;
        }
        if ((GetTickCount64() - startMs) >= kWaitWindowMs) {
            return 0;
        }
        Sleep(kPollIntervalMs);
    }
    if ((!sampDet.detected && !sampDet.unsupportedSampDll) || isProcessShuttingDown()) {
        return 0;
    }

    // Acquire single-instance mutex BEFORE creating any directory or log file.
    // A second copy of SampVoice Plus in the same process exits silently touching zero files.
    if (!acquireProcessInstanceMutex()) {
        return 0;
    }

    assets::setResourceModule(g_selfModule);

    auto& rt = runtime();
    rt.userDir    = userDataDirectoryPath();
    rt.configPath = rt.userDir / "sampvoice.ini";
    const auto gtaDir = gameRootDirectoryPath();

    log::ClientSessionOptions logOpts{};
    logOpts.dirUtf8          = wideToUtf8Local(rt.userDir.wstring());
    logOpts.version          = svplus::version::kString;
    logOpts.sampBanner       = sampDet.unsupportedSampDll
                                   ? std::string("SA-MP (unknown build)")
                                   : (std::string("SA-MP ") + samp::getSampLayout(sampDet.version).name);
    logOpts.selfSha256       = computeModuleSha256Hex(g_selfModule);
    if (logOpts.selfSha256.empty()) {
        logOpts.sha256FailReason = "unreadable module file";
    }
    log::Logger::InitClientSession(logOpts);

    if (sampDet.unsupportedSampDll) {
        const std::uint64_t sampSize = getModuleFileSizeBytes(sampDet.module, sampDet.modulePath);
        const std::string sampSha = computeModuleSha256Hex(sampDet.module);
        SVPLUS_LOG_ERR_CAT(
            "samp",
            "Unsupported samp.dll signature (%s, TimeDateStamp=0x%08X, EntryPoint=0x%08X, size=%llu bytes, sha256=%s); "
            "SA-MP hooks and voice features disabled.",
            diag::formatPrivacyPath(sampDet.modulePath, gtaDir).c_str(),
            static_cast<unsigned>(sampDet.timeDateStamp),
            static_cast<unsigned>(sampDet.entryPointRva),
            static_cast<unsigned long long>(sampSize),
            sampSha.empty() ? "unavailable" : sampSha.c_str());
        return 0;
    }

    if (sampDet.rejectedSampAsi) {
        SVPLUS_LOG_WRN_CAT(
            "samp",
            "Ignored samp.asi (%s, stamp=0x%08X, EP=0x%08X) because its PE signature is not an official SA-MP build",
            diag::formatPrivacyPath(sampDet.rejectedSampAsiPath, gtaDir).c_str(),
            static_cast<unsigned>(sampDet.rejectedTimeDateStamp),
            static_cast<unsigned>(sampDet.rejectedEntryPointRva));
    }

    SVPLUS_LOG_INF_CAT(
        "samp",
        "Validated SA-MP module %s -> %s (stamp=0x%08X, EP=0x%08X)",
        wideToUtf8Local(sampDet.moduleName).c_str(),
        samp::getSampLayout(sampDet.version).name,
        static_cast<unsigned>(sampDet.timeDateStamp),
        static_cast<unsigned>(sampDet.entryPointRva));

    logEnvironmentSummary(sampDet, gtaDir, rt.userDir);

    std::wstring conflictPath;
    if (samp::SampBridge::detectConflictingClientModule(g_selfModule, &conflictPath)) {
        SVPLUS_LOG_ERR_CAT(
            "core",
            "Conflicting voice client detected at '%s'; remove one copy and restart.",
            diag::formatPrivacyPath(conflictPath, gtaDir).c_str());
        log::Logger::ShutdownClientSession(false);
        return 0;
    }

    ui::HudOverlay::instance().setSaMpConfigPath(rt.userDir.parent_path() / L"sa-mp.cfg");

    {
        std::lock_guard lock(rt.uiMutex);
        const bool loaded = rt.uiConfig.loadFromFile(rt.configPath);
        rt.localization.setActiveLanguage(ui::Localization::parseCode(rt.uiConfig.language));
        rt.uiConfig.language = ui::Localization::codeOf(rt.localization.activeLanguage());
        rt.lastSavedConfig = rt.uiConfig;
        publishConfigSnapshotLocked(false);

        if (loaded) {
            rt.uiConfig.logLoadedSummary(rt.configPath);
        } else {
            SVPLUS_LOG_INF_CAT("config", "No sampvoice.ini found; using defaults (%s)",
                               diag::formatPrivacyPath(rt.configPath, gtaDir).c_str());
        }
    }

    diag::startBackgroundDiagnostics(gtaDir, sampDet, g_selfModule);

    if (MH_Initialize() != MH_OK) {
        SVPLUS_LOG_ERR_CAT("core", "MinHook initialization failed");
        return 1;
    }

    audio::CaptureEngine::instance().setCallbacks(
        [](std::span<const std::uint8_t> opusPacket, std::uint32_t seq) {
            net::VoiceClient::instance().sendEncodedVoiceFrame(opusPacket, seq);
        },
        [](std::uint8_t keyId, bool pressed) {
            net::VoiceClient::instance().sendKeyEvent(keyId, pressed);
        });

    auto& bridge = samp::SampBridge::instance();
    bridge.setSelfModule(g_selfModule);
    bridge.setCallbacks(
        [](std::uint16_t packetId, const std::uint8_t* data, std::size_t size) {
            net::VoiceClient::instance().handleControlPacket(packetId, data, size);
        },
        [](std::string_view host, std::uint32_t ipv4Net, std::uint16_t port, bool microSent) {
            net::VoiceClient::instance().setServerEndpointInfo(host, ipv4Net, port, microSent);
        });

    if (isProcessShuttingDown()) {
        return 0;
    }

    g_audioRunning.store(true, std::memory_order_release);
    auto* threadObj = new std::thread(&audioThreadProc);
    auto* prev = g_audioThread.exchange(threadObj, std::memory_order_acq_rel);
    delete prev;

    // Retry D3D9 hook initialization with backoff up to ~30s (fallback uses GTA 0xC97C28 device).
    auto& d3dHook = ui::D3D9Hook::instance();
    if (!d3dHook.initialize(&onRenderFrame, &onWindowMessage, false)) {
        constexpr DWORD kBackoffsMs[] = {250, 500, 1000, 2000, 4000, 8000, 8000, 6250};
        for (DWORD waitMs : kBackoffsMs) {
            if (isProcessShuttingDown()) {
                break;
            }
            Sleep(waitMs);
            if (isProcessShuttingDown()) {
                break;
            }
            if (d3dHook.initialize(&onRenderFrame, &onWindowMessage, true)) {
                break;
            }
        }
        if (!d3dHook.isInitialized() && !isProcessShuttingDown()) {
            SVPLUS_LOG_ERR_CAT("render", "D3D9 hook initialization failed after ~30s retry window (UI unavailable)");
        }
    }

    return 0;
}

} // namespace
} // namespace svplus::client

// Permanent identification marker read by the SampVoice Plus installer (IsPlusClientAsi)
// and conflict checks. O instalador depende desta marca exportada e ela não pode ser
// removida nem alterada.
extern "C" __declspec(dllexport) const char SvPlusClientMarker[] = "SampVoice Plus Client";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        svplus::client::g_selfModule = hModule;
        svplus::client::g_initThread = CreateThread(
            nullptr, 0, &svplus::client::initThreadProc, nullptr, 0, nullptr);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        svplus::client::markProcessShuttingDown();
        svplus::client::g_audioRunning.store(false, std::memory_order_release);
        const bool processTerminating = (lpReserved != nullptr);
        svplus::client::diag::shutdownDiagnostics(processTerminating);
        svplus::log::Logger::ShutdownClientSession(processTerminating);
        if (processTerminating) {
            return TRUE;
        }
        if (auto* t = svplus::client::g_audioThread.exchange(nullptr, std::memory_order_acq_rel)) {
            if (t->joinable()) {
                t->detach();
            }
            delete t;
        }
        if (svplus::client::g_instanceMutex) {
            CloseHandle(svplus::client::g_instanceMutex);
            svplus::client::g_instanceMutex = nullptr;
        }
        if (svplus::client::g_initThread) {
            CloseHandle(svplus::client::g_initThread);
            svplus::client::g_initThread = nullptr;
        }
    }
    return TRUE;
}
