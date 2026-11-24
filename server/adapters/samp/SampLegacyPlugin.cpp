/*
 * SampVoice Plus - Legacy SA-MP 0.3.7 Server Plugin Entry Point (Win32 / Linux x86)
 */

#include <algorithm>
#include <memory>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include <amx/amx.h>
#include <plugincommon.h>

#include "../common/PawnBindings.hpp"
#include "SampAdapter.hpp"
#include "SampRakNetHook.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Protocol.hpp"
#include "svplus/common/Version.hpp"
#include "svplus/server/ServerCore.hpp"

extern void* pAMXFunctions;

namespace {

using LogPrintfFn = void(*)(const char*, ...);

void** gPluginData{nullptr};
LogPrintfFn gLogPrintf{nullptr};
std::unique_ptr<svplus::server::samp::SampAdapter> gAdapter;
std::unique_ptr<svplus::server::ServerCore> gServerCore;
bool gUnloaded{false};
bool gStartAttempted{false};

void EnsureServerStarted() {
    if (!gServerCore || gServerCore->IsRunning() || !gAdapter || gStartAttempted) {
        return;
    }
    gStartAttempted = true;

    gAdapter->LoadConfigOnce("server.cfg");
    svplus::server::ServerStartOptions startOpts{};
    if (!gAdapter->ResolveStartOptions(startOpts)) {
        svplus::log::Logger::Log(
            svplus::log::Level::Error,
            "Aborting SA-MP voice UDP server startup due to invalid configuration"
        );
        return;
    }

    if (!gServerCore->Start(startOpts)) {
        svplus::log::Logger::Log(svplus::log::Level::Error, "Failed to start SA-MP voice UDP server!");
    }
}

void ShutdownPlugin() noexcept {
    if (gUnloaded) return;
    gUnloaded = true;

    svplus::server::PawnBindings::DetachCore();

    if (gServerCore) {
        gServerCore->Stop();
        gServerCore.reset();
    }

    svplus::server::samp::SampRakNetHook::Shutdown();
    gAdapter.reset();
    svplus::log::Logger::Shutdown();
}

} // namespace

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
    return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** ppData) {
    if (!ppData) return false;

    gPluginData = ppData;
    pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];
    gLogPrintf = reinterpret_cast<LogPrintfFn>(ppData[PLUGIN_DATA_LOGPRINTF]);

    // When SA-MP logprintf sink is active, avoid duplicating logs to a separate svplus.log file.
    svplus::log::Logger::Init(gLogPrintf ? "" : "svplus.log");
    svplus::log::Logger::SetMainThread();
    svplus::log::Logger::SetSink([](svplus::log::Level, const char* msg) {
        if (gLogPrintf && msg) {
            gLogPrintf("%s", msg);
        }
    });

    gAdapter = std::make_unique<svplus::server::samp::SampAdapter>();
    gAdapter->LoadConfigOnce("server.cfg");
    gServerCore = std::make_unique<svplus::server::ServerCore>(*gAdapter);
    svplus::server::PawnBindings::AttachCore(gServerCore.get());

    svplus::server::samp::SampRakNetHook::SetCallbacks(
        [](uint16_t playerId, const uint8_t* rpcBytes, std::size_t size) {
            if (gAdapter) gAdapter->TrackPlayerConnected(playerId);
            if (gServerCore) gServerCore->HandlePlayerConnectRpc(playerId, rpcBytes, size);
        },
        [](uint16_t playerId) {
            if (gAdapter) gAdapter->TrackPlayerDisconnected(playerId);
            if (gServerCore) gServerCore->HandlePlayerDisconnect(playerId);
        },
        [](uint16_t playerId, const uint8_t* data, std::size_t size) -> bool {
            if (!data || size < 2 || data[0] != svplus::protocol::kRakNetPacketIdControl) {
                return false;
            }
            if (gServerCore) {
                gServerCore->HandleControlPacket(playerId, data + 1, size - 1);
            }
            return true; // Consumed
        }
    );

    if (!svplus::server::samp::SampRakNetHook::Initialize(ppData[PLUGIN_DATA_LOGPRINTF])) {
        ShutdownPlugin();
        return false;
    }

    gAdapter->Set03DL(svplus::server::samp::SampRakNetHook::Is03DL());
    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
    ShutdownPlugin();
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX* amx) {
    if (gPluginData && gAdapter) {
        if (!gAdapter->GetNetGame() && gPluginData[PLUGIN_DATA_NETGAME]) {
            using GetNetGameFn = svplus::server::samp::SampNetGame* (*)();
            auto* netGame = reinterpret_cast<GetNetGameFn>(gPluginData[PLUGIN_DATA_NETGAME])();
            gAdapter->SetNetGame(netGame);
            if (netGame && netGame->pRak) {
                svplus::server::samp::SampRakNetHook::AttachRakServerIfUnbound(netGame->pRak);
            }
        }
        if (gPluginData[PLUGIN_DATA_RAKSERVER]) {
            using GetRakFn = void* (*)();
            void* rak = reinterpret_cast<GetRakFn>(gPluginData[PLUGIN_DATA_RAKSERVER])();
            svplus::server::samp::SampRakNetHook::AttachRakServerIfUnbound(rak);
        }
    }

    EnsureServerStarted();
    svplus::server::PawnBindings::RegisterScript(amx);
    return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX* amx) {
    svplus::server::PawnBindings::UnregisterScript(amx);
    return AMX_ERR_NONE;
}

PLUGIN_EXPORT void PLUGIN_CALL ProcessTick() {
    svplus::log::Logger::FlushSink();
    if (gServerCore && gAdapter) {
        gAdapter->DetectDisconnects(*gServerCore);
        gServerCore->Tick();
    }
}
