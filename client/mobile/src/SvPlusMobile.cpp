#include "svplus_mobile.h"

#include "MobilePlatform.hpp"
#include "svplus/client/CaptureEngine.hpp"
#include "svplus/client/CoreConfig.hpp"
#include "svplus/client/PlaybackEngine.hpp"
#include "svplus/client/Utf8Util.hpp"
#include "svplus/client/VoiceClient.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Protocol.hpp"
#include "svplus/common/Version.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

using namespace svplus::client;
using namespace svplus::protocol;

static_assert(svplus::client::kMaxPlayerSlots == SVP_MAX_PLAYERS,
              "SVP_MAX_PLAYERS must match core kMaxPlayerSlots");

namespace {

inline constexpr std::size_t kHostV1Size        = sizeof(svp_host_t);
inline constexpr std::size_t kConfigV1Size      = sizeof(svp_config_t);
inline constexpr std::size_t kPlayerEntryV1Size = sizeof(svp_player_entry_t);
inline constexpr std::size_t kStatusV1Size      = offsetof(svp_status_t, speakers_generation) + sizeof(uint64_t);
inline constexpr std::size_t kSpeakerInfoV1Size = sizeof(svp_speaker_info_t);
inline constexpr std::size_t kLocalStreamV1Size = sizeof(svp_local_stream_t);
inline constexpr std::size_t kTxStatsV1Size     = sizeof(svp_tx_stats_t);

static_assert(kStatusV1Size == 168u, "svp_status_t V1 size must remain 168 bytes");
static_assert(offsetof(svp_status_t, audio_ready) == kStatusV1Size,
              "svp_status_t::audio_ready (V2) must immediately follow V1 fields");

int64_t steadyNowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::mutex        g_sessionMutex;
svp_session*      g_activeSession = nullptr;

#if defined(__ANDROID__)
int toAndroidLogPriority(svplus::log::Level lvl) noexcept {
    switch (lvl) {
        case svplus::log::Level::Debug: return ANDROID_LOG_DEBUG;
        case svplus::log::Level::Info:  return ANDROID_LOG_INFO;
        case svplus::log::Level::Warn:  return ANDROID_LOG_WARN;
        case svplus::log::Level::Error: return ANDROID_LOG_ERROR;
    }
    return ANDROID_LOG_INFO;
}
#endif

int32_t toMobileLogLevel(svplus::log::Level lvl) noexcept {
    switch (lvl) {
        case svplus::log::Level::Debug: return SVP_LOG_DEBUG;
        case svplus::log::Level::Info:  return SVP_LOG_INFO;
        case svplus::log::Level::Warn:  return SVP_LOG_WARN;
        case svplus::log::Level::Error: return SVP_LOG_ERROR;
    }
    return SVP_LOG_INFO;
}

template <std::size_t Cap>
void copyBoundedUtf8(std::string_view src, char (&dst)[Cap]) noexcept {
    static_assert(Cap > 0);
    const std::size_t n = svplus::client::util::utf8TruncateLen(src, Cap - 1u);
    if (n > 0u) {
        std::memcpy(dst, src.data(), n);
    }
    dst[n] = '\0';
}

std::string readBoundedCString(const char* ptr, std::size_t maxLen) {
    if (!ptr || maxLen == 0u) {
        return {};
    }
    const void* nul = std::memchr(ptr, '\0', maxLen);
    const std::size_t len = nul
        ? static_cast<std::size_t>(static_cast<const char*>(nul) - ptr)
        : maxLen;
    return std::string(ptr, len);
}

std::string formatServerEndpointKey(const char* host, std::uint32_t ipv4Net, std::uint16_t port) {
    if (ipv4Net != 0u && ipv4Net != 0xFFFFFFFFu && port > 0u) {
        const auto* b = reinterpret_cast<const std::uint8_t*>(&ipv4Net);
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                      static_cast<unsigned>(b[0]),
                      static_cast<unsigned>(b[1]),
                      static_cast<unsigned>(b[2]),
                      static_cast<unsigned>(b[3]),
                      static_cast<unsigned>(port));
        return buf;
    }
    std::string hostStr = host ? std::string(host) : std::string{};
    if (!hostStr.empty() && port > 0u) {
        return hostStr + ":" + std::to_string(port);
    }
    return hostStr;
}

bool parseHostCallbacks(const svp_host_t* in, svp_host_t& out) noexcept {
    if (!in || in->struct_size < kHostV1Size) {
        return false;
    }
    out = {};
    const std::size_t copyBytes = (std::min)(static_cast<std::size_t>(in->struct_size), sizeof(svp_host_t));
    std::memcpy(&out, in, copyBytes);
    out.struct_size = static_cast<uint32_t>(sizeof(svp_host_t));
    return out.send_control != nullptr &&
           out.get_camera != nullptr &&
           out.get_local_ped_position != nullptr;
}

bool parseConfigStruct(const svp_config_t* in, CoreConfig& cfg, bool& outDeafened) {
    CoreConfig defaults;
    cfg.soundEnable        = defaults.soundEnable;
    cfg.soundVolume        = defaults.soundVolume;
    cfg.localVolume        = defaults.localVolume;
    cfg.globalVolume       = defaults.globalVolume;
    cfg.soundBalancer      = defaults.soundBalancer;
    cfg.soundFilter        = defaults.soundFilter;
    cfg.microEnable        = defaults.microEnable;
    cfg.microGain          = defaults.microGain;
    cfg.microNoiseSuppress = defaults.microNoiseSuppress;
    cfg.talkMode           = defaults.talkMode;
    outDeafened            = false;

    if (!in) {
        return true;
    }
    if (in->struct_size < kConfigV1Size) {
        return false;
    }

    svp_config_t wire{};
    wire.sound_enable         = defaults.soundEnable ? 1u : 0u;
    wire.sound_volume         = defaults.soundVolume;
    wire.local_volume         = defaults.localVolume;
    wire.global_volume        = defaults.globalVolume;
    wire.sound_balancer       = defaults.soundBalancer ? 1u : 0u;
    wire.sound_filter         = defaults.soundFilter ? 1u : 0u;
    wire.micro_enable         = defaults.microEnable ? 1u : 0u;
    wire.micro_gain           = defaults.microGain;
    wire.micro_noise_suppress = defaults.microNoiseSuppress ? 1u : 0u;
    wire.talk_mode            = static_cast<uint8_t>(
        (defaults.talkMode == TalkMode::Toggle) ? SVP_TALK_MODE_TOGGLE : SVP_TALK_MODE_PUSH_TO_TALK);
    wire.deafened             = 0u;

    const std::size_t copyBytes = (std::min)(static_cast<std::size_t>(in->struct_size), sizeof(svp_config_t));
    std::memcpy(&wire, in, copyBytes);

    cfg.soundEnable        = (wire.sound_enable != 0u);
    cfg.soundVolume        = std::clamp(wire.sound_volume, 0, 100);
    cfg.localVolume        = std::clamp(wire.local_volume, 0, kMaxChannelVolume);
    cfg.globalVolume       = std::clamp(wire.global_volume, 0, kMaxChannelVolume);
    cfg.soundBalancer      = (wire.sound_balancer != 0u);
    cfg.soundFilter        = (wire.sound_filter != 0u);
    cfg.microEnable        = (wire.micro_enable != 0u);
    cfg.microGain          = std::clamp(wire.micro_gain, 0, kMaxMicroGain);
    cfg.microNoiseSuppress = (wire.micro_noise_suppress != 0u);
    cfg.talkMode           = (wire.talk_mode == SVP_TALK_MODE_TOGGLE) ? TalkMode::Toggle : TalkMode::PushToTalk;
    outDeafened            = (wire.deafened != 0u);
    return true;
}

class FrameGuard {
public:
    explicit FrameGuard(svplus::mobile::MobileGameState& gs) noexcept : gs_(gs) {
        gs_.setFrameActive(true);
    }
    ~FrameGuard() {
        gs_.setFrameActive(false);
    }
    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;

private:
    svplus::mobile::MobileGameState& gs_;
};

} // namespace

struct svp_session {
    svp_host_t                                  host{};
    svplus::mobile::MobileBassLibrary           bass{};
    svplus::mobile::MobileControlTransport      transport{};
    svplus::mobile::MobileIncomingControlQueue  incomingControl{};
    svplus::mobile::MobileGameState             gameState{};
    svplus::mobile::MobileKeyInput              keyInput{};

    std::vector<svplus::mobile::IncomingControlMessage> incomingDrainScratch{};

    std::once_flag                              frameThreadOnce{};
    std::thread::id                             firstFrameThreadId{};
    std::atomic<bool>                           wrongFrameThreadWarned{false};

    std::mutex                                  configMutex{};
    CoreConfig                                  baseConfig{};
    bool                                        configDeafened{false};
    std::shared_ptr<const CoreConfig>           configSnapshot{};

    std::mutex                                  endpointMutex{};
    std::string                                 serverEndpoint{};

    std::atomic<bool>                           connected{false};
    std::atomic<bool>                           disconnectPending{false};
    std::atomic<bool>                           inputBlocked{false};
    std::atomic<bool>                           suspended{false};
    std::atomic<int64_t>                        lastFrameMs{0};
    std::atomic<bool>                           frameStalled{false};
    std::atomic<uint64_t>                       watchdogTriggerCount{0};
    std::atomic<bool>                           audioRunning{false};
    std::thread                                 audioThread{};

    svplus::log::LogSinkFn                      prevLogSink{};

    void refreshPlaybackDeafen(const char* origin) noexcept {
        bool deaf = false;
        {
            std::lock_guard lock(configMutex);
            deaf = configDeafened;
        }
        deaf = deaf ||
               suspended.load(std::memory_order_acquire) ||
               frameStalled.load(std::memory_order_acquire);
        audio::PlaybackEngine::instance().setDeafened(deaf, origin);
    }

    void audioThreadMain(std::promise<void> initReady) {
        auto& capture  = audio::CaptureEngine::instance();
        auto& voice    = net::VoiceClient::instance();
        auto& playback = audio::PlaybackEngine::instance();

        (void)bass.initialize();
        (void)bass.pollReady();

        std::shared_ptr<const CoreConfig> initCfg;
        {
            std::lock_guard lock(configMutex);
            initCfg = configSnapshot;
        }
        if (initCfg) {
            (void)capture.initialize(*initCfg);
        }
        initReady.set_value();

        bool audioSawConnected = false;
        while (audioRunning.load(std::memory_order_acquire)) {
            (void)bass.pollReady();

            const bool connNow = connected.load(std::memory_order_acquire);
            bool stalledNow = false;
            if (connNow) {
                const int64_t lastMs = lastFrameMs.load(std::memory_order_acquire);
                if (lastMs > 0) {
                    const int64_t gapMs = steadyNowMs() - lastMs;
                    if (gapMs > 1000) {
                        stalledNow = true;
                        if (!frameStalled.exchange(true, std::memory_order_acq_rel)) {
                            watchdogTriggerCount.fetch_add(1, std::memory_order_relaxed);
                            keyInput.reset();
                            capture.clearToggleLatches();
                            refreshPlaybackDeafen("frame_stalled");
                            SVPLUS_LOG_INF_CAT(
                                "net",
                                "svp_frame() not called for %lld ms; releasing keys and pausing voice until frames resume",
                                static_cast<long long>(gapMs));
                        }
                    } else {
                        stalledNow = frameStalled.load(std::memory_order_acquire);
                    }
                }
            } else {
                if (frameStalled.exchange(false, std::memory_order_acq_rel)) {
                    refreshPlaybackDeafen("frame_watchdog_disconnect");
                }
            }

            std::shared_ptr<const CoreConfig> cfg;
            {
                std::lock_guard lock(configMutex);
                cfg = configSnapshot;
            }
            if (cfg) {
                const bool susp = suspended.load(std::memory_order_acquire);
                const bool inBlk = inputBlocked.load(std::memory_order_acquire);
                const bool blocked = susp || inBlk || stalledNow;
                const char* blockReason = susp
                    ? "suspended"
                    : (stalledNow ? "frame_stalled" : (inBlk ? "input_blocked" : nullptr));

                capture.tick(*cfg, blocked, blockReason, voice.isAllowToggleMode());

                const bool needDisconnect = !connNow &&
                    (audioSawConnected || disconnectPending.exchange(false, std::memory_order_acq_rel));
                if (connNow) {
                    audioSawConnected = true;
                    disconnectPending.store(false, std::memory_order_release);
                    voice.tick(*cfg, true);
                    playback.tick(*cfg, voice.isVoiceOcclusionEnabled());
                } else if (needDisconnect) {
                    audioSawConnected = false;
                    voice.tick(*cfg, false);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        voice.shutdown();
        capture.shutdown();
        playback.shutdown();
        bass.shutdown();
    }
};

extern "C" {

SVP_API uint32_t svp_get_api_version(void) {
    return SVP_API_VERSION;
}

SVP_API uint32_t svp_get_version(void) {
    return static_cast<uint32_t>(svplus::version::kMajor) * 10000u +
           static_cast<uint32_t>(svplus::version::kMinor) * 100u +
           static_cast<uint32_t>(svplus::version::kPatch);
}

SVP_API svp_result_t svp_create(const svp_host_t* host,
                                const svp_config_t* initial_config,
                                svp_session_t** out_session) {
    try {
        if (!out_session) {
            return SVP_ERR_INVALID_ARG;
        }
        *out_session = nullptr;

        svp_host_t parsedHost{};
        if (!parseHostCallbacks(host, parsedHost)) {
            return SVP_ERR_INVALID_ARG;
        }

        CoreConfig cfg{};
        bool deafened = false;
        if (!parseConfigStruct(initial_config, cfg, deafened)) {
            return SVP_ERR_INVALID_ARG;
        }

        std::lock_guard guard(g_sessionMutex);
        if (g_activeSession != nullptr) {
            return SVP_ERR_STATE;
        }

        auto session = std::make_unique<svp_session>();
        session->host = parsedHost;
        session->bass.setResolver(parsedHost.get_bass_proc, parsedHost.user);
        session->gameState.configure(parsedHost);
        session->baseConfig     = cfg;
        session->configDeafened = deafened;
        session->configSnapshot = std::make_shared<const CoreConfig>(cfg);

        auto hostLogFn = parsedHost.log;
        auto hostLogUser = parsedHost.user;
        session->prevLogSink = svplus::log::Logger::ExchangeSink(
            [hostLogFn, hostLogUser](svplus::log::Level lvl, const char* msg) {
                if (!msg) {
                    return;
                }
                if (hostLogFn) {
                    hostLogFn(hostLogUser, toMobileLogLevel(lvl), msg);
                } else {
#if defined(__ANDROID__)
                    __android_log_print(toAndroidLogPriority(lvl), "SampVoicePlus", "%s", msg);
#else
                    (void)lvl;
#endif
                }
            });

        auto& capture  = audio::CaptureEngine::instance();
        auto& playback = audio::PlaybackEngine::instance();
        auto& voice    = net::VoiceClient::instance();

        capture.setPlatform(&session->bass, &session->keyInput);
        capture.setCallbacks(
            [](std::span<const std::uint8_t> opusPacket, std::uint32_t seq) {
                net::VoiceClient::instance().sendEncodedVoiceFrame(opusPacket, seq);
            },
            [](std::uint8_t vk, bool down) {
                net::VoiceClient::instance().sendKeyEvent(vk, down);
            });
        playback.setPlatform(&session->bass, &session->gameState);
        playback.setDeafened(deafened, "init");
        voice.setPlatform(&session->transport, &session->gameState);

        std::promise<void> initReady;
        std::future<void> initFuture = initReady.get_future();
        session->audioRunning.store(true, std::memory_order_release);
        try {
            session->audioThread = std::thread(
                &svp_session::audioThreadMain, session.get(), std::move(initReady));
        } catch (...) {
            session->audioRunning.store(false, std::memory_order_release);
            capture.setCallbacks({}, {});
            capture.setPlatform(nullptr, nullptr);
            playback.setPlatform(nullptr, nullptr);
            voice.setPlatform(nullptr, nullptr);
            (void)svplus::log::Logger::ExchangeSink(std::move(session->prevLogSink));
            throw;
        }
        initFuture.wait();

        g_activeSession = session.get();
        *out_session = session.release();
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API void svp_destroy(svp_session_t* session) {
    try {
        if (!session) {
            return;
        }

        std::lock_guard guard(g_sessionMutex);
        session->audioRunning.store(false, std::memory_order_release);
        if (session->audioThread.joinable()) {
            session->audioThread.join();
        }

        auto& capture  = audio::CaptureEngine::instance();
        auto& playback = audio::PlaybackEngine::instance();
        auto& voice    = net::VoiceClient::instance();

        capture.setCallbacks({}, {});
        capture.setPlatform(nullptr, nullptr);
        playback.setPlatform(nullptr, nullptr);
        voice.setPlatform(nullptr, nullptr);

        session->incomingControl.clear();
        session->transport.resetOnMainThread();
        session->keyInput.reset();
        session->gameState.reset();

        (void)svplus::log::Logger::ExchangeSink(std::move(session->prevLogSink));

        if (g_activeSession == session) {
            g_activeSession = nullptr;
        }
        delete session;
    } catch (...) {
    }
}

SVP_API svp_result_t svp_build_join_payload(svp_session_t* session,
                                            const char* host,
                                            uint32_t ipv4_net,
                                            uint16_t port,
                                            uint8_t* out_buf,
                                            uint32_t out_cap,
                                            uint32_t* out_len) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        constexpr uint32_t kJoinSize = sizeof(ConnectPacket) + sizeof(ConnectPlusExtension);
        static_assert(kJoinSize == SVP_JOIN_PAYLOAD_SIZE);
        if (out_len) {
            *out_len = kJoinSize;
        }
        if (!out_buf) {
            return SVP_ERR_INVALID_ARG;
        }
        if (out_cap < kJoinSize) {
            return SVP_ERR_BUFFER;
        }

        std::shared_ptr<const CoreConfig> cfg;
        {
            std::lock_guard lock(session->configMutex);
            cfg = session->configSnapshot;
        }
        const bool microAvailable =
            (cfg ? cfg->microEnable : true) &&
            audio::CaptureEngine::instance().hasMicrophone();

        ConnectPacket conn{};
        conn.signature = kConnectSignature;
        conn.version   = kLegacyVersion;
        conn.micro     = microAvailable ? uint8_t{1} : uint8_t{0};

        ConnectPlusExtension ext{};
        ext.magic        = kPlusExtensionMagic;
        ext.capabilities =
            static_cast<uint32_t>(PlusCapabilityFlags::AdaptiveJitter) |
            static_cast<uint32_t>(PlusCapabilityFlags::Telemetry) |
            static_cast<uint32_t>(PlusCapabilityFlags::Mobile);
        ext.clientBuild = svplus::version::kClientBuild;

        std::memcpy(out_buf, &conn, sizeof(conn));
        std::memcpy(out_buf + sizeof(conn), &ext, sizeof(ext));

        const std::string hostStr = host ? std::string(host) : std::string{};
        const std::string epKey   = formatServerEndpointKey(hostStr.c_str(), ipv4_net, port);
        {
            std::lock_guard lock(session->endpointMutex);
            session->serverEndpoint = epKey;
        }

        net::VoiceClient::instance().setServerEndpointInfo(hostStr, ipv4_net, port, conn.micro != 0);
        audio::PlaybackEngine::instance().setCurrentServerEndpoint(epKey);
        SVPLUS_LOG_INF_CAT(
            "net",
            "Prepared mobile RPC25 join payload (wire=v%u, build=0x%04X, caps=0x%X, micro=%u, server=%s)",
            static_cast<unsigned>(conn.version),
            static_cast<unsigned>(ext.clientBuild),
            static_cast<unsigned>(ext.capabilities),
            static_cast<unsigned>(conn.micro),
            epKey.empty() ? "<unknown>" : epKey.c_str());
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_build_join_rpc(svp_session_t* session,
                                        const char* host,
                                        uint32_t ipv4_net,
                                        uint16_t port,
                                        const uint8_t* rpc_in,
                                        uint32_t rpc_in_bits,
                                        uint8_t* out_buf,
                                        uint32_t out_cap,
                                        uint32_t* out_len,
                                        int32_t* out_replaced_offset) {
    try {
        if (!session || !rpc_in || rpc_in_bits == 0u || !out_buf || !out_len) {
            return SVP_ERR_INVALID_ARG;
        }

        const uint32_t inBytes   = (rpc_in_bits + 7u) / 8u;
        const uint32_t fullBytes = rpc_in_bits / 8u;
        int32_t replacedOffset = -1;
        for (uint32_t i = 0u; i + 4u <= fullBytes; ++i) {
            if (std::memcmp(rpc_in + i, kConnectSignatureBytes, 4u) == 0) {
                replacedOffset = static_cast<int32_t>(i);
                break;
            }
        }

        const uint32_t prefixBytes = (replacedOffset >= 0) ? static_cast<uint32_t>(replacedOffset) : inBytes;
        const uint32_t neededLen   = prefixBytes + SVP_JOIN_PAYLOAD_SIZE;
        *out_len = neededLen;
        if (out_replaced_offset) {
            *out_replaced_offset = replacedOffset;
        }
        if (out_cap < neededLen) {
            return SVP_ERR_BUFFER;
        }

        if (prefixBytes > 0u) {
            std::memmove(out_buf, rpc_in, prefixBytes);
            const uint32_t remBits = rpc_in_bits % 8u;
            if (replacedOffset < 0 && remBits != 0u) {
                // RakNet BitStream packs bits MSB-first within a partial final byte.
                const uint8_t keepMask = static_cast<uint8_t>(0xFFu << (8u - remBits));
                out_buf[prefixBytes - 1u] &= keepMask;
            }
        }

        const svp_result_t rc = svp_build_join_payload(
            session, host, ipv4_net, port, out_buf + prefixBytes, SVP_JOIN_PAYLOAD_SIZE, nullptr);
        if (rc != SVP_OK) {
            return rc;
        }

        SVPLUS_LOG_INF_CAT(
            "net",
            "Prepared mobile RPC25 join RPC (inBytes=%u, outBytes=%u, replacedOffset=%d)",
            static_cast<unsigned>(inBytes),
            static_cast<unsigned>(neededLen),
            static_cast<int>(replacedOffset));
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_connected(svp_session_t* session, uint8_t connected) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        const bool nowConn = (connected != 0u);
        const bool prevConn = session->connected.exchange(nowConn, std::memory_order_acq_rel);
        if (nowConn) {
            session->disconnectPending.store(false, std::memory_order_release);
        } else {
            session->disconnectPending.store(true, std::memory_order_release);
            if (session->frameStalled.exchange(false, std::memory_order_acq_rel)) {
                session->refreshPlaybackDeafen("disconnect");
            }
            session->incomingControl.clear();
            session->transport.clearOutgoing();
            if (prevConn) {
                std::lock_guard lock(session->endpointMutex);
                session->serverEndpoint.clear();
            }
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_on_control_packet(svp_session_t* session,
                                           const uint8_t* data,
                                           uint32_t size,
                                           uint8_t* out_consumed) {
    try {
        if (out_consumed) {
            *out_consumed = 0u;
        }
        if (!session || !data || size == 0u) {
            return SVP_ERR_INVALID_ARG;
        }
        if (data[0] != kRakNetPacketIdControl) {
            return SVP_OK;
        }
        if (out_consumed) {
            *out_consumed = 1u;
        }
        if (size < 1u + sizeof(ControlPacketHeader)) {
            return SVP_OK;
        }
        ControlPacketHeader hdr{};
        std::memcpy(&hdr, data + 1u, sizeof(hdr));
        const std::size_t avail = static_cast<std::size_t>(size) - 1u - sizeof(hdr);
        if (hdr.length > avail) {
            return SVP_OK;
        }
        session->incomingControl.enqueue(hdr.packet, data + 1u + sizeof(hdr), hdr.length);
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_frame(svp_session_t* session) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }

        const auto thisTid = std::this_thread::get_id();
        std::call_once(session->frameThreadOnce, [session, thisTid]() {
            session->firstFrameThreadId = thisTid;
        });
        if (thisTid != session->firstFrameThreadId &&
            !session->wrongFrameThreadWarned.exchange(true, std::memory_order_acq_rel)) {
            SVPLUS_LOG_WRN_CAT(
                "net",
                "svp_frame() called from a different thread than the initial game thread; svp_frame() must run on a single thread");
        }

        const int64_t nowMs = steadyNowMs();
        const int64_t prevMs = session->lastFrameMs.exchange(nowMs, std::memory_order_acq_rel);
        if (session->frameStalled.exchange(false, std::memory_order_acq_rel)) {
            session->refreshPlaybackDeafen("frame_resume");
            const long long elapsedMs = (prevMs > 0 && nowMs >= prevMs) ? static_cast<long long>(nowMs - prevMs) : 0LL;
            SVPLUS_LOG_INF_CAT("net", "svp_frame() resumed after %lld ms", elapsedMs);
        }

        FrameGuard frameScope(session->gameState);

        session->gameState.flushPendingNamesOnFrame();

        session->incomingControl.drain(session->incomingDrainScratch);
        auto& voice = net::VoiceClient::instance();
        for (const auto& msg : session->incomingDrainScratch) {
            voice.handleControlPacket(
                msg.packetId,
                msg.payload.empty() ? nullptr : msg.payload.data(),
                msg.payload.size());
        }

        session->transport.drainToHost(session->host.send_control, session->host.user);
        if (session->connected.load(std::memory_order_acquire)) {
            audio::PlaybackEngine::instance().updateSpatialFromMainThread(
                voice.isVoiceOcclusionEnabled());
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_player_name(svp_session_t* session, uint16_t player_id, const char* name) {
    try {
        if (!session || player_id >= kMaxPlayerSlots) {
            return SVP_ERR_INVALID_ARG;
        }
        (void)session->gameState.setPlayerName(player_id, name ? std::string_view(name) : std::string_view{});
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_clear_player(svp_session_t* session, uint16_t player_id) {
    try {
        if (!session || player_id >= kMaxPlayerSlots) {
            return SVP_ERR_INVALID_ARG;
        }
        (void)session->gameState.clearPlayerName(player_id);
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_key_state(svp_session_t* session, uint8_t vk_code, uint8_t down) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        session->keyInput.setKeyDown(vk_code, down != 0u);
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_get_keys(svp_session_t* session,
                                  uint8_t* out_keys,
                                  uint32_t max_keys,
                                  uint32_t* out_count,
                                  uint64_t* out_generation) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        if (max_keys > 0u && !out_keys) {
            return SVP_ERR_INVALID_ARG;
        }
        std::uint64_t gen = 0u;
        const auto keys = audio::CaptureEngine::instance().registeredKeysSnapshot(&gen);
        const std::uint32_t total = static_cast<std::uint32_t>(keys.size());
        if (out_count) {
            *out_count = total;
        }
        if (out_generation) {
            *out_generation = gen;
        }
        const std::uint32_t copyN = (std::min)(max_keys, total);
        if (copyN > 0u && out_keys) {
            std::memcpy(out_keys, keys.data(), copyN);
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_get_key_states(svp_session_t* session,
                                        uint8_t* out_keys,
                                        uint8_t* out_active,
                                        uint32_t max_keys,
                                        uint32_t* out_count,
                                        uint64_t* out_generation) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        if (max_keys > 0u && !out_keys && !out_active) {
            return SVP_ERR_INVALID_ARG;
        }
        std::uint64_t gen = 0u;
        const auto states = audio::CaptureEngine::instance().registeredKeyStatesSnapshot(&gen);
        const std::uint32_t total = static_cast<std::uint32_t>(states.size());
        if (out_count) {
            *out_count = total;
        }
        if (out_generation) {
            *out_generation = gen;
        }
        const std::uint32_t copyN = (std::min)(max_keys, total);
        for (std::uint32_t i = 0u; i < copyN; ++i) {
            if (out_keys) {
                out_keys[i] = states[i].vk;
            }
            if (out_active) {
                out_active[i] = states[i].active ? 1u : 0u;
            }
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_input_blocked(svp_session_t* session, uint8_t blocked) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        session->inputBlocked.store(blocked != 0u, std::memory_order_release);
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_suspended(svp_session_t* session, uint8_t suspended) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        session->suspended.store(suspended != 0u, std::memory_order_release);
        session->refreshPlaybackDeafen(suspended != 0u ? "mobile_suspend" : "mobile_resume");
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_config(svp_session_t* session, const svp_config_t* config) {
    try {
        if (!session || !config) {
            return SVP_ERR_INVALID_ARG;
        }
        CoreConfig next{};
        bool nextDeafened = false;
        if (!parseConfigStruct(config, next, nextDeafened)) {
            return SVP_ERR_INVALID_ARG;
        }
        {
            std::lock_guard lock(session->configMutex);
            next.blacklistEntries    = session->baseConfig.blacklistEntries;
            next.muteGeneration      = session->baseConfig.muteGeneration;
            next.playerVolumeEntries = session->baseConfig.playerVolumeEntries;
            next.volumeGeneration    = session->baseConfig.volumeGeneration;
            session->baseConfig      = next;
            session->configDeafened  = nextDeafened;
            session->configSnapshot  = std::make_shared<const CoreConfig>(session->baseConfig);
        }
        session->refreshPlaybackDeafen("mobile_config");
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_set_player_entries(svp_session_t* session,
                                            const svp_player_entry_t* entries,
                                            uint32_t count) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        if (count > 0u && !entries) {
            return SVP_ERR_INVALID_ARG;
        }

        std::vector<BlacklistEntry> newBlacklist;
        std::vector<PlayerVolumeEntry> newVolumes;
        if (count > 0u) {
            const auto* baseBytes = reinterpret_cast<const std::uint8_t*>(entries);
            const uint32_t stride = entries[0].struct_size;
            if (stride < kPlayerEntryV1Size) {
                return SVP_ERR_INVALID_ARG;
            }

            for (uint32_t i = 0; i < count; ++i) {
                const auto* elemPtr = baseBytes + static_cast<std::size_t>(i) * stride;
                uint32_t elemStructSize = 0u;
                std::memcpy(&elemStructSize, elemPtr, sizeof(elemStructSize));
                if (elemStructSize < kPlayerEntryV1Size) {
                    return SVP_ERR_INVALID_ARG;
                }
                svp_player_entry_t wire{};
                wire.volume_pct = kDefaultPlayerVolume;
                const std::size_t copyBytes = (std::min)({
                    static_cast<std::size_t>(stride),
                    static_cast<std::size_t>(elemStructSize),
                    sizeof(svp_player_entry_t)
                });
                std::memcpy(&wire, elemPtr, copyBytes);

                const std::string name = readBoundedCString(wire.player_name, sizeof(wire.player_name));
                const std::string ep   = readBoundedCString(wire.server_endpoint, sizeof(wire.server_endpoint));
                if (name.empty()) {
                    continue;
                }
                if (wire.muted != 0u) {
                    BlacklistEntry b{};
                    b.playerName     = name;
                    b.serverEndpoint = ep;
                    b.scope = (wire.scope == SVP_BLACKLIST_SCOPE_ALL)
                        ? BlacklistScope::All
                        : BlacklistScope::Server;
                    if (b.scope == BlacklistScope::All || !b.serverEndpoint.empty()) {
                        newBlacklist.push_back(std::move(b));
                    }
                }
                const int vol = std::clamp(wire.volume_pct, 0, kMaxPlayerVolume);
                if (vol != kDefaultPlayerVolume && !ep.empty()) {
                    PlayerVolumeEntry v{};
                    v.playerName     = name;
                    v.serverEndpoint = ep;
                    v.volume         = vol;
                    newVolumes.push_back(std::move(v));
                }
            }
        }

        {
            std::lock_guard lock(session->configMutex);
            session->baseConfig.blacklistEntries    = std::move(newBlacklist);
            session->baseConfig.playerVolumeEntries = std::move(newVolumes);
            ++session->baseConfig.muteGeneration;
            ++session->baseConfig.volumeGeneration;
            session->configSnapshot = std::make_shared<const CoreConfig>(session->baseConfig);
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_get_status(svp_session_t* session, svp_status_t* out_status) {
    try {
        if (!session || !out_status || out_status->struct_size < kStatusV1Size) {
            return SVP_ERR_INVALID_ARG;
        }
        const uint32_t callerSize = out_status->struct_size;

        const auto& voice    = net::VoiceClient::instance();
        const auto& capture  = audio::CaptureEngine::instance();
        const auto& playback = audio::PlaybackEngine::instance();

        std::shared_ptr<const CoreConfig> cfg;
        {
            std::lock_guard lock(session->configMutex);
            cfg = session->configSnapshot;
        }
        std::string epCopy;
        {
            std::lock_guard lock(session->endpointMutex);
            epCopy = session->serverEndpoint;
        }

        const bool conn       = session->connected.load(std::memory_order_acquire);
        const bool voiceReady = voice.isVoiceConnected();
        const auto diag       = voice.diagnosticState();
        const bool serverHasVoice = conn &&
            (voiceReady || (diag != net::VoiceDiagnosticState::Idle && diag != net::VoiceDiagnosticState::NoVoiceServer));

        const std::size_t copyBytes = (std::min)(static_cast<std::size_t>(callerSize), sizeof(svp_status_t));
        svp_status_t full{};
        full.struct_size         = static_cast<uint32_t>(copyBytes);
        full.connected           = conn ? 1u : 0u;
        full.server_has_voice    = serverHasVoice ? 1u : 0u;
        full.voice_ready         = voiceReady ? 1u : 0u;
        full.mic_available       = ((cfg ? cfg->microEnable : true) && capture.hasMicrophone()) ? 1u : 0u;
        full.transmitting        = capture.isTransmitting() ? 1u : 0u;
        full.input_level         = capture.currentInputLevel();
        full.server_muted        = capture.isServerMuted() ? 1u : 0u;
        full.toggle_allowed      = voice.isAllowToggleMode() ? 1u : 0u;
        full.nearby_list_enabled = voice.isNearbyListEnabled() ? 1u : 0u;
        full.occlusion_enabled   = (voice.isVoiceOcclusionEnabled() && session->gameState.hasOcclusion()) ? 1u : 0u;
        full.latency_ms          = voice.rttMs();
        copyBoundedUtf8(epCopy, full.server_endpoint);
        full.keys_generation     = capture.keysGeneration();
        full.speakers_generation = playback.activeSpeakersGeneration();
        full.audio_ready         = session->bass.isAudioReady() ? 1u : 0u;

        std::memcpy(out_status, &full, copyBytes);
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_get_speakers(svp_session_t* session,
                                      svp_speaker_info_t* out_speakers,
                                      uint32_t max_speakers,
                                      uint32_t* out_count) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        uint32_t stride = 0u;
        if (max_speakers > 0u) {
            if (!out_speakers || out_speakers[0].struct_size < kSpeakerInfoV1Size) {
                return SVP_ERR_INVALID_ARG;
            }
            stride = out_speakers[0].struct_size;
        }

        const auto snap = audio::PlaybackEngine::instance().activeSpeakersSnapshot();
        const uint32_t total = snap ? static_cast<uint32_t>(snap->size()) : 0u;
        if (out_count) {
            *out_count = total;
        }
        if (max_speakers == 0u || !snap || snap->empty()) {
            return SVP_OK;
        }

        const std::size_t copyBytes = (std::min)(static_cast<std::size_t>(stride), sizeof(svp_speaker_info_t));
        auto* dstBytes = reinterpret_cast<std::uint8_t*>(out_speakers);
        const uint32_t copyN = (std::min)(max_speakers, total);
        for (uint32_t i = 0; i < copyN; ++i) {
            const auto& src = (*snap)[i];

            svp_speaker_info_t item{};
            item.struct_size  = static_cast<uint32_t>(copyBytes);
            item.player_id    = src.playerId;
            copyBoundedUtf8(src.playerName, item.player_name);
            copyBoundedUtf8(src.streamName, item.stream_name);
            item.stream_color = src.streamColor;
            item.icon_id      = static_cast<uint8_t>(src.iconId);
            item.blocked      = src.blocked ? 1u : 0u;

            const uint32_t tagCount = static_cast<uint32_t>(
                (std::min<std::size_t>)(src.streams.size(), SVP_MAX_STREAM_TAGS));
            item.tag_count = tagCount;
            for (uint32_t t = 0; t < tagCount; ++t) {
                item.tags[t].color = src.streams[t].color;
                copyBoundedUtf8(src.streams[t].name, item.tags[t].name);
            }

            std::memcpy(dstBytes + static_cast<std::size_t>(i) * stride, &item, copyBytes);
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API int32_t svp_get_player_voice_state(svp_session_t* session, uint16_t player_id) {
    try {
        if (!session || player_id >= kMaxPlayerSlots) {
            return SVP_ERR_INVALID_ARG;
        }
        return static_cast<int32_t>(net::VoiceClient::instance().playerVoiceState(player_id));
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_get_my_streams(svp_session_t* session,
                                        svp_local_stream_t* out_streams,
                                        uint32_t max_streams,
                                        uint32_t* out_count) {
    try {
        if (!session) {
            return SVP_ERR_INVALID_ARG;
        }
        uint32_t stride = 0u;
        if (max_streams > 0u) {
            if (!out_streams || out_streams[0].struct_size < kLocalStreamV1Size) {
                return SVP_ERR_INVALID_ARG;
            }
            stride = out_streams[0].struct_size;
        }

        const auto snap = net::VoiceClient::instance().localSpeakerStreamsSnapshot();
        const uint32_t total = snap ? static_cast<uint32_t>(snap->size()) : 0u;
        if (out_count) {
            *out_count = total;
        }
        if (max_streams == 0u || !snap || snap->empty()) {
            return SVP_OK;
        }

        const std::size_t copyBytes = (std::min)(static_cast<std::size_t>(stride), sizeof(svp_local_stream_t));
        auto* dstBytes = reinterpret_cast<std::uint8_t*>(out_streams);
        const uint32_t copyN = (std::min)(max_streams, total);
        for (uint32_t i = 0; i < copyN; ++i) {
            const auto& src = (*snap)[i];

            svp_local_stream_t item{};
            item.struct_size = static_cast<uint32_t>(copyBytes);
            item.stream_id   = src.streamId;
            item.color       = src.color;
            item.kind        = static_cast<uint8_t>(
                (src.kind != 0u) ? SVP_STREAM_KIND_POSITIONAL : SVP_STREAM_KIND_GLOBAL);
            copyBoundedUtf8(src.name, item.name);

            std::memcpy(dstBytes + static_cast<std::size_t>(i) * stride, &item, copyBytes);
        }
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

SVP_API svp_result_t svp_get_tx_stats(svp_session_t* session, svp_tx_stats_t* out_stats) {
    try {
        if (!session || !out_stats || out_stats->struct_size < kTxStatsV1Size) {
            return SVP_ERR_INVALID_ARG;
        }
        const uint32_t callerSize = out_stats->struct_size;
        const auto snap = audio::CaptureEngine::instance().lastTxStatsSnapshot();

        svp_tx_stats_t wire{};
        const std::size_t copyBytes = (std::min)(static_cast<std::size_t>(callerSize), sizeof(svp_tx_stats_t));
        wire.struct_size            = static_cast<uint32_t>(copyBytes);
        wire.enc_errors             = static_cast<uint32_t>((std::min<std::uint64_t>)(snap.encErrors, UINT32_MAX));
        wire.frames                 = snap.frames;
        wire.pre_peak_db            = snap.prePeakDb;
        wire.pre_rms_db             = snap.preRmsDb;
        wire.post_peak_db           = snap.postPeakDb;
        wire.post_rms_db            = snap.postRmsDb;
        wire.tx_count               = snap.txCount;
        wire.watchdog_trigger_count = session->watchdogTriggerCount.load(std::memory_order_relaxed);

        std::memcpy(out_stats, &wire, copyBytes);
        return SVP_OK;
    } catch (...) {
        return SVP_ERR_INTERNAL;
    }
}

} // extern "C"
