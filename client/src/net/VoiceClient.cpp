#include "net/VoiceClient.hpp"
#include "ShutdownState.hpp"

#include "audio/CaptureEngine.hpp"
#include "audio/PlaybackEngine.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Crc32c.hpp"
#include "svplus/common/Logger.hpp"
#include "util/Utf8Util.hpp"

#include <cstring>

namespace svplus::client::net {
namespace {

std::int64_t steadyNowMs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string formatEndpointHostPort(const std::string& host, const svplus::net::UdpEndpoint& ep) {
    if (!host.empty()) {
        return host + ":" + std::to_string(ep.portHost);
    }
    const auto* b = reinterpret_cast<const std::uint8_t*>(&ep.ipv4Net);
    char buf[48]{};
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                  static_cast<unsigned>(b[0]),
                  static_cast<unsigned>(b[1]),
                  static_cast<unsigned>(b[2]),
                  static_cast<unsigned>(b[3]),
                  static_cast<unsigned>(ep.portHost));
    return buf;
}

std::uint64_t steadyNowUs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

std::string extractNullTerminatedString(const std::uint8_t* ptr, std::size_t maxLen) {
    if (!ptr || maxLen == 0) {
        return {};
    }
    const void* nul = std::memchr(ptr, '\0', maxLen);
    const std::size_t len = nul ? static_cast<std::size_t>(static_cast<const std::uint8_t*>(nul) - ptr) : maxLen;
    return std::string(reinterpret_cast<const char*>(ptr), len);
}

constexpr std::uint64_t packEndpoint(const svplus::net::UdpEndpoint& ep) noexcept {
    return (static_cast<std::uint64_t>(ep.ipv4Net) << 16) | static_cast<std::uint64_t>(ep.portHost);
}

} // namespace

VoiceClient& VoiceClient::instance() noexcept {
    static auto* inst = new VoiceClient();
    return *inst;
}

void VoiceClient::setServerEndpointInfo(std::string_view host, std::uint32_t ipv4Net, std::uint16_t rakPort, bool microSentInHandshake) {
    handshakeMicroSent_.store(microSentInHandshake, std::memory_order_release);
    if (rakPort == 0 && host.empty()) {
        return;
    }
    std::lock_guard lock(endpointMutex_);
    if (!host.empty()) {
        serverHost_ = std::string(host);
    }
    if (ipv4Net != 0 && ipv4Net != 0xFFFFFFFFu) {
        serverIpv4Net_ = ipv4Net;
    }
    if (rakPort != 0) {
        rakPort_ = rakPort;
    }
}

void VoiceClient::startUdpLoop() {
    stopUdpLoop();
    if (!udpSocket_.Open({.recvBufferBytes = 512 * 1024, .sendBufferBytes = 512 * 1024, .nonBlocking = true, .reportConnReset = true})) {
        SVPLUS_LOG_ERR_CAT("net", "Failed to open client UDP voice socket");
        return;
    }
    if (!udpSocket_.Bind(0, 0)) {
        SVPLUS_LOG_ERR_CAT("net", "Failed to bind client UDP voice socket");
        udpSocket_.Close();
        return;
    }
    rxRunning_.store(true, std::memory_order_release);
    rxThread_ = std::thread(&VoiceClient::udpRxThreadFunc, this);
    SVPLUS_LOG_INF_CAT("net", "UDP voice socket bound and RX thread started");
    sendKeepAlive();
}

void VoiceClient::stopUdpLoop() noexcept {
    rxRunning_.store(false, std::memory_order_release);
    if (rxThread_.joinable()) {
        if (isProcessShuttingDown()) {
            rxThread_.detach();
        } else {
            rxThread_.join(); // Safe: WaitReadable wakes in <= 80 ms, then audio thread closes socket
        }
    }
    udpSocket_.Close();
}

void VoiceClient::udpRxThreadFunc() {
    std::array<std::uint8_t, protocol::kMaxUdpPacketSize> buf{};
    svplus::net::UdpEndpoint from{};

    while (rxRunning_.load(std::memory_order_acquire) && !isProcessShuttingDown()) {
        const int ready = udpSocket_.WaitReadable(80);
        if (ready < 0) {
            if (rxRunning_.load(std::memory_order_acquire) && !isProcessShuttingDown()) {
                lastUdpSocketErrMs_.store(steadyNowMs(), std::memory_order_release);
                // Drain pending ICMP socket error so WSAPoll does not busy-spin on POLLERR.
                udpSocket_.RecvFrom(buf.data(), buf.size(), from);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }
        if (ready == 0) {
            continue;
        }
        while (rxRunning_.load(std::memory_order_acquire) && !isProcessShuttingDown()) {
            const int recvd = udpSocket_.RecvFrom(buf.data(), buf.size(), from);
            if (recvd < 0) {
                lastUdpSocketErrMs_.store(steadyNowMs(), std::memory_order_release);
                break;
            }
            if (recvd == 0) {
                break;
            }

            // Strictly drop any UDP datagram not originating from the connected voice server endpoint
            const std::uint64_t expected = expectedSourcePacked_.load(std::memory_order_acquire);
            if (expected == 0 || packEndpoint(from) != expected) {
                continue;
            }

            if (static_cast<std::size_t>(recvd) < sizeof(protocol::VoicePacketHeader)) {
                continue;
            }

            protocol::VoicePacketHeader hdr{};
            std::memcpy(&hdr, buf.data(), sizeof(hdr));
            if (!crypto::VerifyVoiceHeaderHash(hdr)) {
                continue;
            }
            lastUdpRxMs_.store(steadyNowMs(), std::memory_order_release);
            if (hdr.packet != static_cast<std::uint32_t>(protocol::VoicePacketType::VoicePacket)) {
                continue;
            }
            if (hdr.length > protocol::kMaxVoicePayloadBytes ||
                static_cast<std::size_t>(recvd) < sizeof(hdr) + hdr.length) {
                continue;
            }

            QueuedVoicePacket item;
            item.streamId  = hdr.stream;
            item.speakerId = hdr.sender;
            item.seq       = hdr.packid;
            item.length    = hdr.length;
            if (hdr.length > 0) {
                std::memcpy(item.payload.data(), buf.data() + sizeof(hdr), hdr.length);
            }
            if (rxAudioQueue_.TryPush(item)) {
                rxVoicePackets_.fetch_add(1, std::memory_order_relaxed);
            } else {
                rxQueueDropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

void VoiceClient::sendKeepAlive() noexcept {
    const std::uint32_t key = serverKey_.load(std::memory_order_acquire);
    if (!key || !udpSocket_.IsOpen() || serverEndpoint_.portHost == 0 || serverEndpoint_.ipv4Net == 0) {
        return;
    }

    protocol::VoicePacketHeader hdr{};
    hdr.svrkey = key;
    hdr.packet = static_cast<std::uint32_t>(protocol::VoicePacketType::KeepAlive);
    hdr.hash   = crypto::ComputeVoiceHeaderHash(hdr);
    if (udpSocket_.SendTo(serverEndpoint_, &hdr, sizeof(hdr)) < 0) {
        lastUdpSocketErrMs_.store(steadyNowMs(), std::memory_order_release);
    }
    lastKeepAlive_ = std::chrono::steady_clock::now();
}

void VoiceClient::sendEncodedVoiceFrame(std::span<const std::uint8_t> opusPacket, std::uint32_t seq) {
    const std::uint32_t key = serverKey_.load(std::memory_order_acquire);
    if (!key || !udpSocket_.IsOpen() || serverEndpoint_.portHost == 0 || serverEndpoint_.ipv4Net == 0 ||
        opusPacket.size() > protocol::kMaxVoicePayloadBytes) {
        return;
    }

    std::array<std::uint8_t, protocol::kMaxUdpPacketSize> outBuf{};
    protocol::VoicePacketHeader hdr{};
    hdr.svrkey = key;
    hdr.packet = static_cast<std::uint32_t>(protocol::VoicePacketType::VoicePacket);
    hdr.stream = 0;
    hdr.sender = 0;
    hdr.length = static_cast<std::uint16_t>(opusPacket.size());
    hdr.packid = seq;
    hdr.hash   = crypto::ComputeVoiceHeaderHash(hdr);

    std::memcpy(outBuf.data(), &hdr, sizeof(hdr));
    if (!opusPacket.empty()) {
        std::memcpy(outBuf.data() + sizeof(hdr), opusPacket.data(), opusPacket.size());
    }
    if (udpSocket_.SendTo(serverEndpoint_, outBuf.data(), sizeof(hdr) + opusPacket.size()) > 0) {
        txVoicePackets_.fetch_add(1, std::memory_order_relaxed);
    }
}

void VoiceClient::sendKeyEvent(std::uint8_t vkCode, bool pressed) {
    auto& bridge = samp::SampBridge::instance();
    if (pressed) {
        const protocol::PressKeyPacket pkt{vkCode};
        bridge.sendControlPacket(static_cast<std::uint16_t>(protocol::ControlPacketType::PressKey),
                                 &pkt, sizeof(pkt));
    } else {
        const protocol::ReleaseKeyPacket pkt{vkCode};
        bridge.sendControlPacket(static_cast<std::uint16_t>(protocol::ControlPacketType::ReleaseKey),
                                 &pkt, sizeof(pkt));
    }
}

void VoiceClient::handleControlPacket(std::uint16_t packetId, const std::uint8_t* data, std::size_t size) {
    using CPT = protocol::ControlPacketType;
    auto& playback = audio::PlaybackEngine::instance();
    auto& capture  = audio::CaptureEngine::instance();

    switch (static_cast<CPT>(packetId)) {
        case CPT::ServerInfo: {
            if (size < sizeof(protocol::ServerInfoPacket)) break;
            protocol::ServerInfoPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));

            svplus::net::UdpEndpoint ep{};
            std::string hostLog;
            {
                std::lock_guard lock(endpointMutex_);
                hostLog = serverHost_;
                if (serverIpv4Net_ != 0 && serverIpv4Net_ != 0xFFFFFFFFu) {
                    ep.ipv4Net  = serverIpv4Net_;
                    ep.portHost = pkt.serverPort;
                } else if (!serverHost_.empty()) {
                    svplus::net::UdpEndpoint::FromString(serverHost_, pkt.serverPort, ep);
                }
                if (ep.ipv4Net != 0 && ep.portHost != 0) {
                    pendingEndpoint_  = ep;
                    pendingServerKey_ = pkt.serverKey;
                    hasPendingConnect_.store(true, std::memory_order_release);
                }
            }

            if (ep.ipv4Net != 0 && ep.portHost != 0) {
                pluginInitialized_.store(false, std::memory_order_release);
                serverIsPlus_.store(false, std::memory_order_release);
                serverMicroSynced_ = false;
                clearLocalSpeakerStreams();
                serverUdpPort_.store(ep.portHost, std::memory_order_release);
                SVPLUS_LOG_INF_CAT("net", "Control[ServerInfo] endpoint=%s:%u key=0x%08X",
                                   hostLog.c_str(), static_cast<unsigned>(pkt.serverPort), pkt.serverKey);
            } else {
                SVPLUS_LOG_WRN_CAT("net", "Control[ServerInfo] received but server IPv4 is unavailable");
            }
            break;
        }
        case CPT::PluginInit: {
            if (size < sizeof(protocol::PluginInitPacket)) break;
            protocol::PluginInitPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            capture.setServerMuted(pkt.mute != 0);
            pluginInitialized_.store(true, std::memory_order_release);
            SVPLUS_LOG_INF_CAT("net", "Control[PluginInit] bitrate=%u muted=%u",
                               pkt.bitrate, static_cast<unsigned>(pkt.mute));
            break;
        }
        case CPT::MuteEnable:
            capture.setServerMuted(true);
            SVPLUS_LOG_INF_CAT("net", "Control[MuteEnable]");
            break;
        case CPT::MuteDisable:
            capture.setServerMuted(false);
            SVPLUS_LOG_INF_CAT("net", "Control[MuteDisable]");
            break;
        case CPT::StartRecord:
            capture.setServerRecordActive(true);
            SVPLUS_LOG_INF_CAT("net", "Control[StartRecord]");
            break;
        case CPT::StopRecord:
            capture.setServerRecordActive(false);
            SVPLUS_LOG_INF_CAT("net", "Control[StopRecord]");
            break;
        case CPT::AddKey: {
            if (size < sizeof(protocol::AddKeyPacket)) break;
            protocol::AddKeyPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            capture.addActivationKey(pkt.keyId);
            SVPLUS_LOG_INF_CAT("net", "Control[AddKey] vk=0x%02X", static_cast<unsigned>(pkt.keyId));
            break;
        }
        case CPT::RemoveKey: {
            if (size < sizeof(protocol::RemoveKeyPacket)) break;
            protocol::RemoveKeyPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            capture.removeActivationKey(pkt.keyId);
            SVPLUS_LOG_INF_CAT("net", "Control[RemoveKey] vk=0x%02X", static_cast<unsigned>(pkt.keyId));
            break;
        }
        case CPT::RemoveAllKeys:
            capture.clearActivationKeys();
            SVPLUS_LOG_INF_CAT("net", "Control[RemoveAllKeys]");
            break;
        case CPT::CreateGStream: {
            if (size < sizeof(protocol::CreateGStreamPacket)) break;
            protocol::CreateGStreamPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            const std::string name = util::ensureUtf8Name(extractNullTerminatedString(
                data + sizeof(pkt), size - sizeof(pkt)));
            playback.createStream(pkt.stream, audio::StreamKind::Global, 0, 0.0f, {}, pkt.color, name);
            break;
        }
        case CPT::CreateLPStream: {
            if (size < sizeof(protocol::CreateLPStreamPacket)) break;
            protocol::CreateLPStreamPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            const std::string name = util::ensureUtf8Name(extractNullTerminatedString(
                data + sizeof(pkt), size - sizeof(pkt)));
            playback.createStream(pkt.stream, audio::StreamKind::LocalAtPoint, 0,
                                  pkt.distance, pkt.position, pkt.color, name);
            break;
        }
        case CPT::CreateLStreamAtVehicle:
        case CPT::CreateLStreamAtPlayer:
        case CPT::CreateLStreamAtObject: {
            if (size < sizeof(protocol::CreateLStreamAtPacket)) break;
            protocol::CreateLStreamAtPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            const std::string name = util::ensureUtf8Name(extractNullTerminatedString(
                data + sizeof(pkt), size - sizeof(pkt)));
            audio::StreamKind kind = audio::StreamKind::LocalAtPlayer;
            if (static_cast<CPT>(packetId) == CPT::CreateLStreamAtVehicle) {
                kind = audio::StreamKind::LocalAtVehicle;
            } else if (static_cast<CPT>(packetId) == CPT::CreateLStreamAtObject) {
                kind = audio::StreamKind::LocalAtObject;
            }
            playback.createStream(pkt.stream, kind, pkt.target, pkt.distance, {}, pkt.color, name);
            break;
        }
        case CPT::UpdateLStreamDistance: {
            if (size < sizeof(protocol::UpdateLStreamDistancePacket)) break;
            protocol::UpdateLStreamDistancePacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.updateStreamDistance(pkt.stream, pkt.distance);
            break;
        }
        case CPT::UpdateLPStreamPosition: {
            if (size < sizeof(protocol::UpdateLPStreamPositionPacket)) break;
            protocol::UpdateLPStreamPositionPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.updateStreamPosition(pkt.stream, pkt.position);
            break;
        }
        case CPT::DeleteStream: {
            if (size < sizeof(protocol::DeleteStreamPacket)) break;
            protocol::DeleteStreamPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.deleteStream(pkt.stream);
            break;
        }
        case CPT::SetStreamParameter: {
            if (size < sizeof(protocol::SetStreamParameterPacket)) break;
            protocol::SetStreamParameterPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.setStreamParameter(pkt.stream, pkt.parameter, pkt.value);
            break;
        }
        case CPT::SlideStreamParameter: {
            if (size < sizeof(protocol::SlideStreamParameterPacket)) break;
            protocol::SlideStreamParameterPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.slideStreamParameter(pkt.stream, pkt.parameter, pkt.startValue, pkt.endValue, pkt.timeMs);
            break;
        }
        case CPT::CreateEffect: {
            if (size < sizeof(protocol::CreateEffectPacket)) break;
            protocol::CreateEffectPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            audio::StreamEffectSpec spec;
            spec.effectId   = pkt.effect;
            spec.effectType = pkt.number;
            spec.priority   = pkt.priority;
            if (size > sizeof(pkt)) {
                spec.paramBytes.assign(data + sizeof(pkt), data + size);
            }
            playback.createEffect(pkt.stream, spec);
            break;
        }
        case CPT::DeleteEffect: {
            if (size < sizeof(protocol::DeleteEffectPacket)) break;
            protocol::DeleteEffectPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.deleteEffect(pkt.stream, pkt.effect);
            break;
        }
        case CPT::PlusCapabilities: {
            if (size < sizeof(protocol::PlusCapabilitiesPacket)) break;
            protocol::PlusCapabilitiesPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            serverIsPlus_.store(true, std::memory_order_release);
            SVPLUS_LOG_INF_CAT("net", "Control[PlusCapabilities] flags=0x%X frameMs=%u",
                               pkt.serverCapabilities, pkt.recommendedFrameMs);
            break;
        }
        case CPT::PlusTelemetryPong: {
            if (size < sizeof(protocol::PlusTelemetryPongPacket)) break;
            protocol::PlusTelemetryPongPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            const std::uint64_t nowUs = steadyNowUs();
            if (nowUs >= pkt.clientTimestampUs) {
                const float ms = static_cast<float>(nowUs - pkt.clientTimestampUs) / 1000.0f;
                rttMs_.store(ms, std::memory_order_release);
            }
            break;
        }
        case CPT::PlusListenerState: {
            if (size < sizeof(protocol::PlusListenerStatePacket)) break;
            protocol::PlusListenerStatePacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            const bool disabled = (pkt.enabled == 0);
            listenerDisabled_.store(disabled, std::memory_order_release);
            playback.setListenerDisabled(disabled);
            SVPLUS_LOG_INF_CAT("net", "Control[PlusListenerState] enabled=%d", disabled ? 0 : 1);
            break;
        }
        case CPT::PlusStreamIcon: {
            if (size < sizeof(protocol::PlusStreamIconPacket)) break;
            protocol::PlusStreamIconPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            playback.setStreamIcon(pkt.stream, static_cast<protocol::StreamIconId>(pkt.iconId));
            SVPLUS_LOG_INF_CAT("net", "Control[PlusStreamIcon] stream=0x%08X icon=%u",
                               pkt.stream, static_cast<unsigned>(pkt.iconId));
            break;
        }
        case CPT::PlusSpeakerStreams: {
            if (size < sizeof(protocol::PlusSpeakerStreamsPacket)) break;
            protocol::PlusSpeakerStreamsPacket pkt{};
            std::memcpy(&pkt, data, sizeof(pkt));
            std::vector<LocalSpeakerStreamTag> parsed;
            parsed.reserve(pkt.count);
            std::size_t off = sizeof(protocol::PlusSpeakerStreamsPacket);
            for (std::uint16_t i = 0; i < pkt.count; ++i) {
                if (off + sizeof(protocol::PlusSpeakerStreamEntryHeader) > size) {
                    break;
                }
                protocol::PlusSpeakerStreamEntryHeader eh{};
                std::memcpy(&eh, data + off, sizeof(eh));
                off += sizeof(eh);
                if (off + eh.nameLen > size) {
                    break;
                }
                std::string name = util::ensureUtf8Name(extractNullTerminatedString(data + off, eh.nameLen));
                off += eh.nameLen;
                parsed.push_back(LocalSpeakerStreamTag{eh.streamId, eh.color, std::move(name), eh.kind});
            }

            bool changed = false;
            {
                std::lock_guard lock(localSpeakerStreamsMutex_);
                if (!localSpeakerStreams_ || *localSpeakerStreams_ != parsed) {
                    localSpeakerStreams_ = std::make_shared<const std::vector<LocalSpeakerStreamTag>>(parsed);
                    changed = true;
                }
            }
            if (changed) {
                std::string desc;
                for (const auto& tag : parsed) {
                    if (tag.name.empty()) {
                        continue;
                    }
                    if (!desc.empty()) {
                        desc += ", ";
                    }
                    desc += "[" + tag.name + (tag.kind != 0 ? " (pos)]" : " (global)]");
                }
                if (desc.empty()) {
                    desc = parsed.empty() ? "(none)" : "(unnamed)";
                }
                SVPLUS_LOG_INF_CAT("net", "Local speaker streams: %s", desc.c_str());
            }
            break;
        }
        default:
            break;
    }
}

void VoiceClient::tick(const config::ClientConfig& cfg, bool sampConnected) {
    if (!sampConnected) {
        wasSampConnected_ = false;
        sampConnectTime_  = {};
        diagnosticState_.store(VoiceDiagnosticState::Idle, std::memory_order_release);
        clearLocalSpeakerStreams();
        if (serverKey_.load(std::memory_order_acquire) != 0 || udpSocket_.IsOpen()) {
            resetSessionOnAudioThread();
        }
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!wasSampConnected_) {
        wasSampConnected_    = true;
        sampConnectTime_     = now;
        serverInfoTime_      = {};
        keepAliveBackoffMs_  = 250;
    }

    if (hasPendingConnect_.exchange(false, std::memory_order_acq_rel)) {
        svplus::net::UdpEndpoint ep{};
        std::uint32_t key = 0;
        {
            std::lock_guard lock(endpointMutex_);
            ep  = pendingEndpoint_;
            key = pendingServerKey_;
        }
        if (ep.ipv4Net != 0 && ep.portHost != 0 && key != 0) {
            serverEndpoint_     = ep;
            expectedSourcePacked_.store(packEndpoint(ep), std::memory_order_release);
            serverKey_.store(key, std::memory_order_release);
            serverUdpPort_.store(ep.portHost, std::memory_order_release);
            serverInfoTime_     = now;
            keepAliveBackoffMs_ = 250;
            startUdpLoop();
        }
    }

    auto& playback = audio::PlaybackEngine::instance();
    QueuedVoicePacket pkt;
    while (rxAudioQueue_.TryPop(pkt)) {
        if (pkt.length == 0) {
            playback.onSpeakerEnd(pkt.streamId, pkt.speakerId);
        } else {
            playback.onVoicePacket(
                pkt.streamId,
                pkt.speakerId,
                pkt.seq,
                std::span<const std::uint8_t>(pkt.payload.data(), pkt.length),
                cfg);
        }
    }

    const bool initDone = pluginInitialized_.load(std::memory_order_acquire);
    const std::uint32_t key = serverKey_.load(std::memory_order_acquire);
    if (key != 0) {
        if (!initDone) {
            // Exponential keep-alive retry during UDP hole-punching (250ms -> 500 -> 1000 -> 2000 -> 4000ms)
            if ((now - lastKeepAlive_) >= std::chrono::milliseconds(keepAliveBackoffMs_)) {
                sendKeepAlive();
                keepAliveBackoffMs_ = (std::min)(keepAliveBackoffMs_ * 2u, 4000u);
            }
        } else if ((now - lastKeepAlive_) >= std::chrono::milliseconds(1500)) {
            sendKeepAlive();
        }
    }

    // R29-5: UDP reachability & mid-session outage monitoring
    if (key != 0 && serverEndpoint_.portHost != 0) {
        std::string hostCopy;
        {
            std::lock_guard lock(endpointMutex_);
            hostCopy = serverHost_;
        }
        const std::string epLabel = formatEndpointHostPort(hostCopy, serverEndpoint_);
        const std::int64_t nowMs = steadyNowMs();
        const std::int64_t lastRxMs = lastUdpRxMs_.load(std::memory_order_acquire);
        const std::int64_t lastErrMs = lastUdpSocketErrMs_.load(std::memory_order_acquire);
        const bool anyUdpAlive = initDone || (lastRxMs > 0);

        if (!udpHealthyOnce_) {
            if (anyUdpAlive) {
                udpHealthyOnce_ = true;
                if (initialUdpUnreachableErrLogged_) {
                    initialUdpUnreachableErrLogged_ = false;
                    SVPLUS_LOG_INF_CAT("net", "Voice server %s UDP connection established after earlier timeout", epLabel.c_str());
                }
            } else if (serverInfoTime_.time_since_epoch().count() != 0 &&
                       (now - serverInfoTime_) >= std::chrono::seconds(15) &&
                       !initialUdpUnreachableErrLogged_) {
                initialUdpUnreachableErrLogged_ = true;
                SVPLUS_LOG_ERR_CAT(
                    "net",
                    "Voice server %s unreachable over UDP after 15s (UDP port blocked on server or firewall)",
                    epLabel.c_str());
            }
        } else {
            const bool activeSocketError =
                (lastErrMs > 0) && (lastErrMs > lastRxMs) && ((nowMs - lastErrMs) <= 3500);
            const bool droppedNow = !initDone || activeSocketError;
            if (droppedNow) {
                if (udpDropStartTime_.time_since_epoch().count() == 0) {
                    udpDropStartTime_ = now;
                } else if ((now - udpDropStartTime_) >= std::chrono::seconds(30) &&
                           !midSessionUdpDropErrLogged_) {
                    midSessionUdpDropErrLogged_ = true;
                    SVPLUS_LOG_ERR_CAT(
                        "net",
                        "Voice UDP connection to %s lost and did not recover within 30s (UDP port blocked on server or firewall)",
                        epLabel.c_str());
                }
            } else {
                udpDropStartTime_ = {};
                if (midSessionUdpDropErrLogged_) {
                    midSessionUdpDropErrLogged_ = false;
                    SVPLUS_LOG_INF_CAT("net", "Voice UDP connection to %s restored", epLabel.c_str());
                }
            }
        }
    }

    if (serverIsPlus_.load(std::memory_order_acquire) &&
        (now - lastPingSent_) >= std::chrono::milliseconds(2000)) {
        lastPingSent_ = now;
        // OutgoingControlQueue keeps at most 1 ping queued and stamps clientTimestampUs on the game thread at TX.
        const protocol::PlusTelemetryPingPacket ping{0};
        samp::SampBridge::instance().sendControlPacket(
            static_cast<std::uint16_t>(protocol::ControlPacketType::PlusTelemetryPing),
            &ping,
            sizeof(ping));
    }

    // Notify SampVoice Plus (v12+) servers whenever microphone availability changes after handshake.
    const auto& capture = audio::CaptureEngine::instance();
    const bool effectiveMicro = cfg.microEnable && capture.hasMicrophone();
    if (initDone && serverIsPlus_.load(std::memory_order_acquire)) {
        if (!serverMicroSynced_) {
            serverKnownMicro_ = handshakeMicroSent_.load(std::memory_order_acquire);
            serverMicroSynced_ = true;
        }
        if (effectiveMicro != serverKnownMicro_) {
            const protocol::PlusMicroStatePacket micPkt{effectiveMicro ? std::uint8_t{1} : std::uint8_t{0}};
            if (samp::SampBridge::instance().sendControlPacket(
                    static_cast<std::uint16_t>(protocol::ControlPacketType::PlusMicroState),
                    &micPkt,
                    sizeof(micPkt))) {
                serverKnownMicro_ = effectiveMicro;
                SVPLUS_LOG_INF_CAT("net", "Sent Control[PlusMicroState] hasMicro=%d", effectiveMicro ? 1 : 0);
            }
        }
    }

    // Update 9-state F11 diagnostic telemetry
    VoiceDiagnosticState diag = VoiceDiagnosticState::Connecting;
    if (initDone) {
        if (capture.isServerMuted()) {
            diag = VoiceDiagnosticState::ConnectedMuted;
        } else if (listenerDisabled_.load(std::memory_order_acquire)) {
            diag = VoiceDiagnosticState::ListenerDisabled;
        } else if (!cfg.microEnable) {
            diag = VoiceDiagnosticState::ConnectedMicDisabled;
        } else if (!capture.hasMicrophone()) {
            diag = VoiceDiagnosticState::ConnectedNoMic;
        } else {
            diag = VoiceDiagnosticState::ConnectedOk;
        }
    } else if (key != 0) {
        if (serverInfoTime_.time_since_epoch().count() != 0 &&
            (now - serverInfoTime_) >= std::chrono::milliseconds(3500)) {
            diag = VoiceDiagnosticState::UdpBlocked;
        } else {
            diag = VoiceDiagnosticState::Connecting;
        }
    } else {
        if (sampConnectTime_.time_since_epoch().count() != 0 &&
            (now - sampConnectTime_) >= std::chrono::seconds(3)) {
            diag = VoiceDiagnosticState::NoVoiceServer;
        } else {
            diag = VoiceDiagnosticState::Connecting;
        }
    }
    diagnosticState_.store(diag, std::memory_order_release);
}

void VoiceClient::resetSessionOnAudioThread() {
    stopUdpLoop();
    samp::SampBridge::instance().clearOutgoingControlQueue();
    lastPingSent_ = {};
    expectedSourcePacked_.store(0, std::memory_order_release);
    serverEndpoint_ = {};
    {
        std::lock_guard lock(endpointMutex_);
        pendingEndpoint_  = {};
        pendingServerKey_ = 0;
        serverIpv4Net_    = 0;
    }
    hasPendingConnect_.store(false, std::memory_order_release);
    serverKey_.store(0, std::memory_order_release);
    serverUdpPort_.store(0, std::memory_order_release);
    pluginInitialized_.store(false, std::memory_order_release);
    serverIsPlus_.store(false, std::memory_order_release);
    listenerDisabled_.store(false, std::memory_order_release);
    serverMicroSynced_ = false;
    serverKnownMicro_  = false;
    rttMs_.store(-1.0f, std::memory_order_release);
    diagnosticState_.store(VoiceDiagnosticState::Idle, std::memory_order_release);
    serverInfoTime_ = {};
    keepAliveBackoffMs_ = 250;
    lastUdpRxMs_.store(0, std::memory_order_release);
    lastUdpSocketErrMs_.store(0, std::memory_order_release);
    udpHealthyOnce_ = false;
    initialUdpUnreachableErrLogged_ = false;
    udpDropStartTime_ = {};
    midSessionUdpDropErrLogged_ = false;

    QueuedVoicePacket dummy;
    while (rxAudioQueue_.TryPop(dummy)) {}

    audio::CaptureEngine::instance().clearActivationKeys();
    audio::CaptureEngine::instance().setServerRecordActive(false);
    audio::CaptureEngine::instance().setServerMuted(false);
    audio::PlaybackEngine::instance().resetAll();
    clearLocalSpeakerStreams();
}

void VoiceClient::clearLocalSpeakerStreams() {
    std::lock_guard lock(localSpeakerStreamsMutex_);
    if (!localSpeakerStreams_ || !localSpeakerStreams_->empty()) {
        localSpeakerStreams_ = std::make_shared<const std::vector<LocalSpeakerStreamTag>>();
    }
}

std::shared_ptr<const std::vector<LocalSpeakerStreamTag>> VoiceClient::localSpeakerStreamsSnapshot() const {
    std::lock_guard lock(localSpeakerStreamsMutex_);
    return localSpeakerStreams_;
}

void VoiceClient::shutdown() noexcept {
    if (isProcessShuttingDown()) {
        return;
    }
    resetSessionOnAudioThread();
}

} // namespace svplus::client::net
