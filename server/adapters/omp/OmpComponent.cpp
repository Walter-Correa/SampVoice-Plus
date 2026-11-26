/*
 * SampVoice Plus - open.mp Server Component Entry Point
 */

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <bitstream.hpp>
#include <sdk.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Pawn/pawn.hpp>
#include <Server/Components/Vehicles/vehicles.hpp>

#include "../common/PawnBindings.hpp"
#include "OmpAdapter.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Protocol.hpp"
#include "svplus/common/Version.hpp"
#include "svplus/server/ServerCore.hpp"

extern void* pAMXFunctions;

namespace svplus::server::omp {

class SampVoicePlusComponent final
    : public IComponent
    , public CoreEventHandler
    , public PawnEventHandler
    , public PlayerConnectEventHandler
    , public NetworkInEventHandler {
public:
    PROVIDE_UID(0x7A41C9E2B3D80514ULL);

    StringView componentName() const override {
        return "SampVoice Plus";
    }

    SemanticVersion componentVersion() const override {
        return SemanticVersion(
            static_cast<uint8_t>(svplus::version::kMajor),
            static_cast<uint8_t>(svplus::version::kMinor),
            static_cast<uint8_t>(svplus::version::kPatch),
            0
        );
    }

    void provideConfiguration(ILogger&, IEarlyConfig& config, bool defaults) override {
        if (defaults) {
            config.setInt("sampvoice.port", 0);
            config.setString("sampvoice.bind", "");
            config.setInt("sampvoice.threads", 4);
            config.setInt("sampvoice.updaterate", 30);
            config.setBool("sampvoice.legacy_color_format", false);
            config.setBool("sampvoice.voice_status", true);
            config.setBool("sampvoice.nearby_list", true);
            config.setBool("sampvoice.allow_toggle_mode", true);
            config.setBool("sampvoice.voice_occlusion", true);
        }
    }

    void onLoad(ICore* c) override {
        core_ = c;
    }

    void onInit(IComponentList* components) override {
        if (!core_ || !components) return;

        pawnComponent_ = components->queryComponent<IPawnComponent>();
        vehiclesComponent_ = components->queryComponent<IVehiclesComponent>();
        objectsComponent_ = components->queryComponent<IObjectsComponent>();

        if (!pawnComponent_) {
            core_->logLn(LogLevel::Error, "[sv+:ERR] Pawn component not found; SampVoice Plus disabled.");
            return;
        }

        // Route svplus logger output into open.mp logger directly without duplicating into svplus.log.
        log::Logger::Init("");
        log::Logger::SetMainThread();
        log::Logger::SetSink([this](log::Level level, const char* msg) {
            if (!core_ || !msg) return;
            LogLevel ompLevel = LogLevel::Message;
            switch (level) {
            // open.mp release builds drop LogLevel::Debug; SvDebug already gates emission and msg carries [sv+:DBG].
            case log::Level::Debug: ompLevel = LogLevel::Message; break;
            case log::Level::Info:  ompLevel = LogLevel::Message; break;
            case log::Level::Warn:  ompLevel = LogLevel::Warning; break;
            case log::Level::Error: ompLevel = LogLevel::Error; break;
            }
            core_->logLn(ompLevel, "%s", msg);
        });

        adapter_ = std::make_unique<OmpAdapter>();
        adapter_->AttachHost(core_, vehiclesComponent_, objectsComponent_);
        serverCore_ = std::make_unique<ServerCore>(*adapter_);

        pAMXFunctions = const_cast<void*>(static_cast<const void*>(&pawnComponent_->getAmxFunctions()));
        PawnBindings::AttachCore(serverCore_.get());

        pawnComponent_->getEventDispatcher().addEventHandler(this);
        core_->getEventDispatcher().addEventHandler(this);
        core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);

        for (INetwork* network : core_->getNetworks()) {
            if (network) {
                network->getInEventDispatcher().addEventHandler(this);
            }
        }
    }

    void onReady() override {
        EnsureStarted();
    }

    void onFree(IComponent* component) override {
        if (component == this || component == pawnComponent_) {
            Shutdown();
        }
    }

    void free() override {
        Shutdown();
        delete this;
    }

    void reset() override {
        // Per-AMX streams and effects are automatically reclaimed via CleanupAmxResources() in onAmxUnload().
    }

    void onTick(Microseconds, TimePoint) override {
        log::Logger::FlushSink();
        if (serverCore_) {
            serverCore_->Tick();
        }
    }

    void onAmxLoad(IPawnScript& script) override {
        EnsureStarted();
        PawnBindings::RegisterScript(script.GetAMX());
    }

    void onAmxUnload(IPawnScript& script) override {
        PawnBindings::UnregisterScript(script.GetAMX());
    }

    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason) override {
        if (serverCore_) {
            const int id = player.getID();
            if (id >= 0 && id < static_cast<int>(kMaxPlayers)) {
                serverCore_->HandlePlayerDisconnect(static_cast<uint16_t>(id));
            }
        }
    }

    bool onReceivePacket(IPlayer& peer, int id, NetworkBitStream& bs) override {
        if (id != protocol::kRakNetPacketIdControl || !serverCore_) {
            return true;
        }

        const int pid = peer.getID();
        if (pid < 0 || pid >= static_cast<int>(kMaxPlayers)) {
            return true;
        }

        const uint8_t* raw = bs.GetData();
        const int totalBytes = bs.GetNumberOfBytesUsed();
        int offsetBytes = bs.GetReadOffset() / 8;

        if (!raw || totalBytes <= offsetBytes) {
            return false;
        }

        // Guard against drivers invoking handlers before advancing read offset past packet ID
        if (offsetBytes == 0 && totalBytes >= 5 && raw[0] == protocol::kRakNetPacketIdControl) {
            offsetBytes = 1;
        }

        serverCore_->HandleControlPacket(
            static_cast<uint16_t>(pid),
            raw + offsetBytes,
            static_cast<std::size_t>(totalBytes - offsetBytes)
        );
        return false; // Consumed SampVoice control packet
    }

    bool onReceiveRPC(IPlayer& peer, int id, NetworkBitStream& bs) override {
        if (id != protocol::kRakNetRpcPlayerConnect || !serverCore_) {
            return true;
        }

        const int pid = peer.getID();
        if (pid < 0 || pid >= static_cast<int>(kMaxPlayers)) {
            return true;
        }

        const uint8_t* raw = bs.GetData();
        const int totalBytes = bs.GetNumberOfBytesUsed();
        if (raw && totalBytes > 0) {
            serverCore_->HandlePlayerConnectRpc(
                static_cast<uint16_t>(pid),
                raw,
                static_cast<std::size_t>(totalBytes)
            );
        }
        return true;
    }

private:
    void EnsureStarted() {
        if (!serverCore_ || serverCore_->IsRunning() || !adapter_ || startAttempted_) {
            return;
        }
        startAttempted_ = true;

        ServerStartOptions startOpts{};
        if (!adapter_->ResolveStartOptions(startOpts)) {
            log::Logger::Log(log::Level::Error, "Aborting voice UDP server startup due to invalid configuration");
            return;
        }

        if (!serverCore_->Start(startOpts)) {
            log::Logger::Log(log::Level::Error, "Failed to start voice UDP server!");
        }
    }

    void Shutdown() {
        if (shuttingDown_) return;
        shuttingDown_ = true;

        if (pawnComponent_) {
            pawnComponent_->getEventDispatcher().removeEventHandler(this);
        }
        if (core_) {
            core_->getEventDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
            for (INetwork* network : core_->getNetworks()) {
                if (network) {
                    network->getInEventDispatcher().removeEventHandler(this);
                }
            }
        }

        PawnBindings::DetachCore();

        if (serverCore_) {
            serverCore_->Stop();
            serverCore_.reset();
        }
        if (adapter_) {
            adapter_->DetachHost();
            adapter_.reset();
        }

        log::Logger::Shutdown();
        pawnComponent_ = nullptr;
        vehiclesComponent_ = nullptr;
        objectsComponent_ = nullptr;
        core_ = nullptr;
    }

    bool shuttingDown_{false};
    bool startAttempted_{false};
    ICore* core_{nullptr};
    IPawnComponent* pawnComponent_{nullptr};
    IVehiclesComponent* vehiclesComponent_{nullptr};
    IObjectsComponent* objectsComponent_{nullptr};
    std::unique_ptr<OmpAdapter> adapter_;
    std::unique_ptr<ServerCore> serverCore_;
};

} // namespace svplus::server::omp

COMPONENT_ENTRY_POINT() {
    return new svplus::server::omp::SampVoicePlusComponent();
}
