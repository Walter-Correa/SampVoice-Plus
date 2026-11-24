#include "audio/BassLoader.hpp"

#include "diag/ModDiagnostics.hpp"
#include "svplus/common/Logger.hpp"

namespace svplus::client::audio {
namespace {

template <typename Fn>
bool resolveProc(HMODULE sysMod, const char* name, Fn& out) noexcept {
    FARPROC proc = sysMod ? GetProcAddress(sysMod, name) : nullptr;
    out = reinterpret_cast<Fn>(proc);
    return out != nullptr;
}

} // namespace

BassLoader& BassLoader::instance() noexcept {
    static auto* inst = new BassLoader();
    return *inst;
}

bool BassLoader::pollReady() noexcept {
    if (bassInitialized_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!loaded_.load(std::memory_order_acquire) || !api_.GetDevice || !api_.ErrorGetCode) {
        return false;
    }
    const DWORD dev = api_.GetDevice();
    if (dev != DWORD(-1) && api_.ErrorGetCode() == BASS_OK) {
        bassInitialized_.store(true, std::memory_order_release);
        SVPLUS_LOG_INF_CAT("audio", "Detected initialized SA-MP BASS output device (%lu)", static_cast<unsigned long>(dev));
        return true;
    }
    return false;
}

bool BassLoader::initialize() noexcept {
    if (loaded_.load(std::memory_order_acquire)) {
        return true;
    }

    // Bind directly to SA-MP/GTA's loaded bass.dll without hooking BASS_Init or modifying 3D globals.
    bassSysModule_ = GetModuleHandleA("bass.dll");
    if (!bassSysModule_) {
        bassSysModule_ = LoadLibraryA("bass.dll");
        ownsModuleRef_ = (bassSysModule_ != nullptr);
    }
    if (!bassSysModule_) {
        SVPLUS_LOG_ERR_CAT("audio", "Failed to locate bass.dll in GTA/SA-MP directory");
        return false;
    }

    bool ok = true;
    ok &= resolveProc(bassSysModule_, "BASS_GetVersion", api_.GetVersion);
    ok &= resolveProc(bassSysModule_, "BASS_ErrorGetCode", api_.ErrorGetCode);
    ok &= resolveProc(bassSysModule_, "BASS_GetDevice", api_.GetDevice);
    ok &= resolveProc(bassSysModule_, "BASS_SetConfig", api_.SetConfig);
    ok &= resolveProc(bassSysModule_, "BASS_GetConfig", api_.GetConfig);
    ok &= resolveProc(bassSysModule_, "BASS_StreamCreate", api_.StreamCreate);
    ok &= resolveProc(bassSysModule_, "BASS_StreamFree", api_.StreamFree);
    ok &= resolveProc(bassSysModule_, "BASS_StreamPutData", api_.StreamPutData);
    ok &= resolveProc(bassSysModule_, "BASS_RecordGetDeviceInfo", api_.RecordGetDeviceInfo);
    ok &= resolveProc(bassSysModule_, "BASS_RecordInit", api_.RecordInit);
    ok &= resolveProc(bassSysModule_, "BASS_RecordFree", api_.RecordFree);
    ok &= resolveProc(bassSysModule_, "BASS_RecordSetDevice", api_.RecordSetDevice);
    ok &= resolveProc(bassSysModule_, "BASS_RecordGetDevice", api_.RecordGetDevice);
    ok &= resolveProc(bassSysModule_, "BASS_RecordStart", api_.RecordStart);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelIsActive", api_.ChannelIsActive);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelFlags", api_.ChannelFlags);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelPlay", api_.ChannelPlay);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelStop", api_.ChannelStop);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelPause", api_.ChannelPause);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelSetAttribute", api_.ChannelSetAttribute);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelGetAttribute", api_.ChannelGetAttribute);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelSlideAttribute", api_.ChannelSlideAttribute);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelGetData", api_.ChannelGetData);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelSetFX", api_.ChannelSetFX);
    ok &= resolveProc(bassSysModule_, "BASS_ChannelRemoveFX", api_.ChannelRemoveFX);
    ok &= resolveProc(bassSysModule_, "BASS_FXSetParameters", api_.FXSetParameters);

    if (!ok) {
        SVPLUS_LOG_ERR_CAT("audio", "Failed to resolve required exports from bass.dll");
        return false;
    }

    loaded_.store(true, std::memory_order_release);
    (void)pollReady();
    SVPLUS_LOG_INF_CAT("audio", "Bound to bass.dll (v0x%lX)", api_.GetVersion ? api_.GetVersion() : 0ul);
    diag::reportBassHookInspection(bassSysModule_);
    return true;
}

void BassLoader::shutdown() noexcept {
    if (ownsModuleRef_ && bassSysModule_) {
        FreeLibrary(bassSysModule_);
    }
    bassSysModule_ = nullptr;
    ownsModuleRef_ = false;
    loaded_.store(false, std::memory_order_release);
    bassInitialized_.store(false, std::memory_order_release);
}

} // namespace svplus::client::audio
