/*
 * SampVoice Plus - Shared High-Performance Server Engine Implementation
 */

#include "svplus/server/ServerCore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "svplus/common/Crc32c.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Version.hpp"

namespace svplus::server {

void ServerCore::DeferredDeleteSink::ScheduleOrSendDeleteStream(
    uint16_t playerId,
    uint32_t streamId,
    int64_t lastVoiceForwardNs,
    const ControlPacketBuffer& packetDelete,
    IServerAdapter& adapter
) {
    if (playerId >= kMaxPlayers) {
        return;
    }

    const bool isPlus = store_.PlusFlags()[playerId].load(std::memory_order_acquire);
    if (isPlus) {
        (void)CancelPendingDeleteStream(playerId, streamId);
        adapter.SendControlPacket(playerId, packetDelete.Data(), packetDelete.Size());
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    const int64_t delayNs = std::chrono::duration_cast<std::chrono::nanoseconds>(kLegacyDeleteDelay).count();

    if (lastVoiceForwardNs <= 0 || nowNs < lastVoiceForwardNs || (nowNs - lastVoiceForwardNs) >= delayNs) {
        (void)CancelPendingDeleteStream(playerId, streamId);
        adapter.SendControlPacket(playerId, packetDelete.Data(), packetDelete.Size());
        return;
    }

    const int64_t remainingNs = delayNs - (nowNs - lastVoiceForwardNs);
    const auto sendAt = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::nanoseconds(remainingNs));

    ControlPacketBuffer evictedPacket;
    uint16_t evictedPlayer = protocol::kInvalidPlayerId;

    {
        std::lock_guard lock(mutex_);
        for (auto& entry : pending_) {
            if (entry.playerId == playerId && entry.streamId == streamId) {
                entry.sendAt = sendAt;
                entry.packetDelete = packetDelete;
                return;
            }
        }

        if (perPlayerCount_[playerId] >= kMaxPendingDeletesPerPlayer) {
            auto earliest = pending_.end();
            for (auto it = pending_.begin(); it != pending_.end(); ++it) {
                if (it->playerId == playerId &&
                    (earliest == pending_.end() || it->sendAt < earliest->sendAt)) {
                    earliest = it;
                }
            }
            if (earliest != pending_.end()) {
                evictedPlayer = earliest->playerId;
                evictedPacket = std::move(earliest->packetDelete);
                *earliest = std::move(pending_.back());
                pending_.pop_back();
                --perPlayerCount_[playerId];
            }
        }

        pending_.push_back(PendingDeleteEntry{playerId, streamId, sendAt, packetDelete});
        ++perPlayerCount_[playerId];
    }

    if (evictedPlayer != protocol::kInvalidPlayerId && adapter.IsPlayerConnected(evictedPlayer)) {
        adapter.SendControlPacket(evictedPlayer, evictedPacket.Data(), evictedPacket.Size());
    }
}

bool ServerCore::DeferredDeleteSink::CancelPendingDeleteStream(uint16_t playerId, uint32_t streamId) {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (perPlayerCount_[playerId] == 0) {
        return false;
    }
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (pending_[i].playerId == playerId && pending_[i].streamId == streamId) {
            pending_[i] = std::move(pending_.back());
            pending_.pop_back();
            --perPlayerCount_[playerId];
            return true;
        }
    }
    return false;
}

void ServerCore::DeferredDeleteSink::FlushDue(
    std::chrono::steady_clock::time_point now,
    IServerAdapter& adapter
) {
    dueScratch_.clear();
    {
        std::lock_guard lock(mutex_);
        if (pending_.empty()) {
            return;
        }
        std::size_t i = 0;
        while (i < pending_.size()) {
            if (pending_[i].sendAt <= now) {
                const uint16_t pid = pending_[i].playerId;
                if (pid < kMaxPlayers && perPlayerCount_[pid] > 0) {
                    --perPlayerCount_[pid];
                }
                dueScratch_.push_back(std::move(pending_[i]));
                pending_[i] = std::move(pending_.back());
                pending_.pop_back();
            } else {
                ++i;
            }
        }
    }

    for (const auto& entry : dueScratch_) {
        const auto* slot = store_.GetSlot(entry.playerId);
        if (slot &&
            slot->connected.load(std::memory_order_acquire) &&
            adapter.IsPlayerConnected(entry.playerId)) {
            adapter.SendControlPacket(entry.playerId, entry.packetDelete.Data(), entry.packetDelete.Size());
        }
    }
}

void ServerCore::DeferredDeleteSink::ClearPlayer(uint16_t playerId) {
    if (playerId >= kMaxPlayers) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (perPlayerCount_[playerId] == 0) {
        return;
    }
    std::size_t i = 0;
    while (i < pending_.size()) {
        if (pending_[i].playerId == playerId) {
            pending_[i] = std::move(pending_.back());
            pending_.pop_back();
        } else {
            ++i;
        }
    }
    perPlayerCount_[playerId] = 0;
}

void ServerCore::DeferredDeleteSink::ClearAll() {
    std::lock_guard lock(mutex_);
    pending_.clear();
    dueScratch_.clear();
    perPlayerCount_.fill(0);
}

ServerCore::ServerCore(IServerAdapter& adapter)
    : adapter_(adapter) {}

ServerCore::~ServerCore() {
    Stop();
}

bool ServerCore::Start(uint16_t udpPort, uint32_t workerThreadCount, uint32_t updateRateMs) {
    ServerStartOptions opts{};
    opts.udpPort = udpPort;
    opts.explicitPort = (udpPort != 0);
    opts.workerThreadCount = workerThreadCount;
    opts.updateRateMs = updateRateMs;
    opts.portOrigin = opts.explicitPort ? "explicit" : "default";
    return Start(opts);
}

bool ServerCore::Start(const ServerStartOptions& options) {
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }

    uint32_t bindIpv4Net = 0;
    const std::string effectiveBindStr = options.bindIp.empty() ? "0.0.0.0" : options.bindIp;
    if (!options.bindIp.empty() && options.bindIp != "0.0.0.0") {
        net::UdpEndpoint bindEp{};
        if (!net::UdpEndpoint::FromString(options.bindIp, 0, bindEp)) {
            log::Logger::Log(
                log::Level::Error,
                "Invalid voice bind IPv4 address '%s'; refusing to start",
                options.bindIp.c_str()
            );
            return false;
        }
        bindIpv4Net = bindEp.ipv4Net;
    }

    net::UdpSocketOptions opts{};
    opts.nonBlocking = true;
    opts.reuseAddress = false;
    opts.exclusiveAddressUse = (options.udpPort != 0);
    opts.recvBufferBytes = 4 * 1024 * 1024;
    opts.sendBufferBytes = 4 * 1024 * 1024;

    if (!udpSocket_.Open(opts)) {
        log::Logger::Log(log::Level::Error, "Failed to open server UDP socket (err=%d)", udpSocket_.LastError());
        return false;
    }

    if (!udpSocket_.Bind(bindIpv4Net, options.udpPort)) {
        if (options.explicitPort && options.udpPort != 0) {
            log::Logger::Log(
                log::Level::Error,
                "Failed to bind explicit voice UDP port %hu on %s (err=%d); refusing random port fallback",
                options.udpPort,
                effectiveBindStr.c_str(),
                udpSocket_.LastError()
            );
        } else {
            log::Logger::Log(
                log::Level::Error,
                "Failed to bind voice UDP socket on %s:%hu (err=%d)",
                effectiveBindStr.c_str(),
                options.udpPort,
                udpSocket_.LastError()
            );
        }
        udpSocket_.Close();
        return false;
    }

    updateRate_ = std::chrono::milliseconds(options.updateRateMs > 0 ? options.updateRateMs : 30);
    legacyColorFormat_ = options.legacyColorFormat;
    voiceStatus_ = options.voiceStatus;
    nearbyList_ = options.nearbyList;
    voiceStateConnected_.fill(false);
    lastSentVoiceState_.fill(static_cast<uint8_t>(protocol::PlayerVoiceState::Ok));
    lastSpatialTick_ = std::chrono::steady_clock::now();

    const uint32_t threads = std::clamp(options.workerThreadCount, 1u, 16u);
    running_.store(true, std::memory_order_release);

    ioReaderThread_ = std::thread(&ServerCore::IoReaderLoop, this);
    workerThreads_.reserve(threads);
    for (uint32_t i = 0; i < threads; ++i) {
        workerThreads_.emplace_back(&ServerCore::WorkerLoop, this);
    }

    const uint16_t boundPort = udpSocket_.LocalPort();
    const bool hasCustomBind = (!effectiveBindStr.empty() && effectiveBindStr != "0.0.0.0");

    char cfgBuf[768]{};
    std::snprintf(
        cfgBuf,
        sizeof(cfgBuf),
        "Effective config: port=%hu [%s] bind=%s [%s] threads=%u [%s] updaterate=%u ms [%s] legacy_color_format=%s [%s] voice_status=%s [%s] nearby_list=%s [%s]",
        boundPort,
        options.portOrigin.c_str(),
        effectiveBindStr.c_str(),
        options.bindOrigin.c_str(),
        threads,
        options.threadsOrigin.c_str(),
        static_cast<unsigned>(updateRate_.count()),
        options.rateOrigin.c_str(),
        legacyColorFormat_ ? "true" : "false",
        options.legacyColorOrigin.c_str(),
        voiceStatus_ ? "true" : "false",
        options.voiceStatusOrigin.c_str(),
        nearbyList_ ? "true" : "false",
        options.nearbyListOrigin.c_str()
    );
    effectiveConfigLine_ = cfgBuf;
    effectiveConfigLogged_ = false;

    const std::string endpointText = hasCustomBind
        ? ("UDP " + effectiveBindStr + ":" + std::to_string(boundPort))
        : ("UDP port " + std::to_string(boundPort));
    if (options.explicitPort) {
        log::Logger::Log(
            log::Level::Info,
            "SampVoice Plus %s voice server running on %s",
            svplus::version::kString,
            endpointText.c_str()
        );
    } else {
        log::Logger::Log(
            log::Level::Warn,
            "SampVoice Plus %s voice server running on automatic %s (changes on every restart); set sampvoice.port to a fixed value and open it as UDP in your firewall",
            svplus::version::kString,
            endpointText.c_str()
        );
    }
    return true;
}

void ServerCore::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    workerWakeCv_.notify_all();

    if (ioReaderThread_.joinable()) {
        ioReaderThread_.join();
    }

    for (auto& t : workerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workerThreads_.clear();

    udpSocket_.Close();

    {
        std::lock_guard lock(dynamicStreamsMutex_);
        dynamicStreams_.clear();
    }
    {
        std::lock_guard lock(ownedMutex_);
        ownedStreams_.clear();
        ownedEffects_.clear();
    }
    {
        std::lock_guard lock(warnIconMutex_);
        warnedUnknownIcons_.clear();
    }
    hasBuiltGrid_ = false;
    deferredDeleteSink_.ClearAll();
    {
        std::lock_guard lock(timedMutesMutex_);
        timedMutedPlayers_.clear();
        expiredMutesScratch_.clear();
    }
    streamPool_.Clear();
    effectPool_.Clear();
    playerStore_.ClearAll();

    // Drain queues cleanly
    memory::PooledPacket<protocol::kMaxUdpPacketSize> pkt;
    while (voiceQueue_.TryPop(pkt)) {}
    uint16_t pid = 0;
    while (pendingInitQueue_.TryPop(pid)) {}
    IncomingControlEvent ev;
    while (incomingControlQueue_.TryPop(ev)) {}
    controlDrainScratch_.clear();
    syntheticControlQueue_.clear();
    syntheticDrainScratch_.clear();
    nextControlEventSeq_.store(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(controlQueueDropMutex_);
        lastControlQueueDropLogTime_ = {};
        droppedControlQueueEvents_ = 0;
        hasDroppedControlQueueEvents_.store(false, std::memory_order_release);
    }

    effectiveConfigLine_.clear();
    effectiveConfigLogged_ = false;
    voiceStateConnected_.fill(false);
    lastSentVoiceState_.fill(static_cast<uint8_t>(protocol::PlayerVoiceState::Ok));
    log::Logger::Log(log::Level::Debug, "SampVoice Plus Core stopped cleanly");
}

void ServerCore::IoReaderLoop() {
    constexpr std::size_t kBatchSize = 32;
    std::array<memory::PooledPacket<protocol::kMaxUdpPacketSize>, kBatchSize> batchPackets{};
    std::array<net::UdpSocket::BatchItem, kBatchSize> batchItems{};

    while (running_.load(std::memory_order_acquire)) {
        const int ready = udpSocket_.WaitReadable(50);
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }
        if (ready <= 0) {
            continue;
        }

        // Top up only missing pooled buffers; retain unconsumed buffers across wakeups
        std::size_t acquired = 0;
        for (std::size_t i = 0; i < kBatchSize; ++i) {
            if (!batchPackets[i]) {
                batchPackets[i] = packetPool_.Acquire();
                if (!batchPackets[i]) {
                    break;
                }
            }
            batchItems[i].data = batchPackets[i]->data.data();
            batchItems[i].capacity = batchPackets[i]->data.size();
            batchItems[i].receivedSize = 0;
            ++acquired;
        }

        if (acquired == 0) {
            std::this_thread::yield();
            continue;
        }

        const int receivedCount = udpSocket_.RecvBatch(batchItems.data(), acquired);
        if (receivedCount <= 0) {
            continue;
        }

        bool pushedAny = false;
        for (int i = 0; i < receivedCount; ++i) {
            auto pkt = std::move(batchPackets[i]);
            const std::size_t len = batchItems[i].receivedSize;
            const net::UdpEndpoint senderEp = batchItems[i].sender;

            if (len < sizeof(protocol::VoicePacketHeader)) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            auto* hdr = reinterpret_cast<protocol::VoicePacketHeader*>(pkt->data.data());
            if (!crypto::VerifyVoiceHeaderHash(*hdr)) {
                batchPackets[i] = std::move(pkt);
                continue;
            }
            if (len != sizeof(protocol::VoicePacketHeader) + hdr->length) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            const uint16_t playerId = playerStore_.FindPlayerByUdpKey(senderEp.ipv4Net, hdr->svrkey);
            if (playerId == protocol::kInvalidPlayerId) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            bool portUpdated = false;
            if (playerStore_.TryIdentifyUdpEndpoint(playerId, senderEp, &portUpdated)) {
                pendingInitQueue_.TryPush(playerId);
            } else if (portUpdated) {
                PlayerVoiceSlot* natSlot = playerStore_.GetSlot(playerId);
                if (natSlot && natSlot->ShouldLogNatPortUpdate(std::chrono::steady_clock::now())) {
                    log::Logger::Log(
                        log::Level::Debug,
                        "Updated NAT UDP port for player %hu to %hu",
                        playerId, senderEp.portHost
                    );
                }
            }

            if (hdr->packet == static_cast<uint32_t>(protocol::VoicePacketType::KeepAlive)) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            if (hdr->packet != static_cast<uint32_t>(protocol::VoicePacketType::VoicePacket) ||
                hdr->length == 0 ||
                hdr->length > protocol::kMaxVoicePayloadBytes) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            PlayerVoiceSlot* slot = playerStore_.GetSlot(playerId);
            if (!slot ||
                slot->muteStatus.load(std::memory_order_acquire) ||
                (!slot->recordStatus.load(std::memory_order_acquire) &&
                 slot->activeKeyCount.load(std::memory_order_acquire) == 0)) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            uint64_t droppedToLog = 0;
            const bool rateAllowed = slot->ConsumeVoiceToken(std::chrono::steady_clock::now(), droppedToLog);
            if (droppedToLog > 0) {
                log::Logger::Log(
                    log::Level::Warn,
                    "Rate-limited UDP voice flood from player %hu: dropped %llu packets in the last interval",
                    playerId,
                    static_cast<unsigned long long>(droppedToLog)
                );
            }
            if (!rateAllowed) {
                batchPackets[i] = std::move(pkt);
                continue;
            }

            pkt->size = len;
            pkt->sourceIpv4 = senderEp.ipv4Net;
            pkt->sourcePort = senderEp.portHost;

            hdr->sender = playerId;
            hdr->svrkey = 0;

            if (voiceQueue_.TryPush(std::move(pkt))) {
                pushedAny = true;
            }
        }

        if (pushedAny) {
            workerWakeCv_.notify_one();
        }
    }
}

void ServerCore::WorkerLoop() {
    memory::PooledPacket<protocol::kMaxUdpPacketSize> pkt;

    while (running_.load(std::memory_order_acquire)) {
        if (voiceQueue_.TryPop(pkt)) {
            ProcessVoicePacket(std::move(pkt));
            continue;
        }

        std::unique_lock lock(workerWakeMutex_);
        workerWakeCv_.wait_for(lock, std::chrono::milliseconds(50), [&]() {
            return !running_.load(std::memory_order_acquire) || !voiceQueue_.IsEmpty();
        });
    }

    while (voiceQueue_.TryPop(pkt)) {
        ProcessVoicePacket(std::move(pkt));
    }
}

void ServerCore::ProcessVoicePacket(memory::PooledPacket<protocol::kMaxUdpPacketSize> packet) {
    if (!packet || packet->size < sizeof(protocol::VoicePacketHeader)) {
        return;
    }

    auto* hdr = reinterpret_cast<protocol::VoicePacketHeader*>(packet->data.data());
    const uint16_t senderId = hdr->sender;

    const PlayerVoiceSlot* slot = playerStore_.GetSlot(senderId);
    if (!slot || !slot->connected.load(std::memory_order_acquire)) {
        return;
    }
    if (slot->muteStatus.load(std::memory_order_acquire)) {
        return;
    }
    if (!slot->recordStatus.load(std::memory_order_acquire) &&
        slot->activeKeyCount.load(std::memory_order_acquire) == 0) {
        return;
    }

    thread_local std::vector<uint32_t> speakerStreams;
    speakerStreams.clear();
    {
        std::shared_lock lock(slot->dataMutex);
        speakerStreams.assign(slot->speakerStreams.begin(), slot->speakerStreams.end());
    }

    if (speakerStreams.empty()) {
        return;
    }

    net::UdpEndpoint targetEp{};

    for (uint32_t streamId : speakerStreams) {
        const auto stream = streamPool_.Get(streamId);
        if (!stream || !stream->HasSpeaker(senderId)) {
            continue;
        }

        const auto listeners = stream->GetListenersSnapshot();
        if (!listeners || listeners->empty()) {
            continue;
        }

        hdr->stream = streamId;
        hdr->hash = crypto::ComputeVoiceHeaderHash(*hdr);

        bool forwardedAny = false;
        for (uint16_t listenerId : *listeners) {
            if (listenerId == senderId) {
                continue;
            }
            const auto* listenerSlot = playerStore_.GetSlot(listenerId);
            if (!listenerSlot || !listenerSlot->listenerEnabled.load(std::memory_order_acquire)) {
                continue;
            }
            if (playerStore_.GetUdpEndpoint(listenerId, targetEp)) {
                udpSocket_.SendTo(targetEp, packet->data.data(), packet->size);
                forwardedAny = true;
            }
        }
        if (forwardedAny) {
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            stream->RecordVoiceForwarded(nowNs);
        }
    }
}

void ServerCore::Tick() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // 1. Send deferred pluginInit packets for newly UDP-identified players
    uint16_t initPlayerId = 0;
    while (pendingInitQueue_.TryPop(initPlayerId)) {
        const auto* slot = playerStore_.GetSlot(initPlayerId);
        if (!slot || !slot->connected.load(std::memory_order_acquire)) {
            continue;
        }

        protocol::PluginInitPacket initPkt{};
        initPkt.bitrate = bitrate_.load(std::memory_order_acquire);
        initPkt.mute = slot->muteStatus.load(std::memory_order_acquire) ? 1 : 0;

        const auto buf = ControlPacketBuffer::Create(protocol::ControlPacketType::PluginInit, initPkt);
        adapter_.SendControlPacket(initPlayerId, buf.Data(), buf.Size());

        // Notify SV+ clients of extended server options, capabilities, initial listener state, and voice states
        if (slot->clientProtocol.load(std::memory_order_acquire) >= protocol::kPlusVersion) {
            SendServerOptions(initPlayerId);

            protocol::PlusCapabilitiesPacket capPkt{};
            capPkt.serverCapabilities = static_cast<uint32_t>(
                protocol::PlusCapabilityFlags::AdaptiveJitter | protocol::PlusCapabilityFlags::Telemetry
            );
            capPkt.recommendedFrameMs = protocol::kLegacyFrameMs;
            const auto capBuf = ControlPacketBuffer::Create(
                protocol::ControlPacketType::PlusCapabilities, capPkt
            );
            adapter_.SendControlPacket(initPlayerId, capBuf.Data(), capBuf.Size());

            const protocol::PlusListenerStatePacket lsPkt{
                slot->listenerEnabled.load(std::memory_order_acquire) ? uint8_t{1} : uint8_t{0}
            };
            const auto lsBuf = ControlPacketBuffer::Create(
                protocol::ControlPacketType::PlusListenerState, lsPkt
            );
            adapter_.SendControlPacket(initPlayerId, lsBuf.Data(), lsBuf.Size());

            // Synchronize any speaker streams already attached prior to handshake completion
            bool hasSpeakerStreams = false;
            {
                std::shared_lock lock(slot->dataMutex);
                hasSpeakerStreams = !slot->speakerStreams.empty();
            }
            if (hasSpeakerStreams) {
                SyncSpeakerStreams(initPlayerId);
            }

            if (voiceStatus_) {
                SyncPlayerVoiceStateIfChanged(initPlayerId, initPlayerId);
                SendVoiceStateSnapshot(initPlayerId);
            }
        } else if (voiceStatus_) {
            SyncPlayerVoiceStateIfChanged(initPlayerId);
        }
    }

    // 2. Drain incoming network control packets and main-thread synthetic control events
    // before dispatching, then merge them strictly by global arrival/enqueue sequence number.
    // Any synthetic events generated inside callbacks go into syntheticControlQueue_ for the next tick.
    controlDrainScratch_.clear();
    IncomingControlEvent ev;
    while (incomingControlQueue_.TryPop(ev)) {
        controlDrainScratch_.push_back(ev);
    }

    syntheticDrainScratch_.clear();
    syntheticDrainScratch_.swap(syntheticControlQueue_);

    std::size_t netIdx = 0;
    std::size_t synIdx = 0;
    while (netIdx < controlDrainScratch_.size() && synIdx < syntheticDrainScratch_.size()) {
        if (controlDrainScratch_[netIdx].seq <= syntheticDrainScratch_[synIdx].seq) {
            DispatchControlEvent(controlDrainScratch_[netIdx++]);
        } else {
            DispatchControlEvent(syntheticDrainScratch_[synIdx++]);
        }
    }
    while (netIdx < controlDrainScratch_.size()) {
        DispatchControlEvent(controlDrainScratch_[netIdx++]);
    }
    while (synIdx < syntheticDrainScratch_.size()) {
        DispatchControlEvent(syntheticDrainScratch_[synIdx++]);
    }

    // 3. Flush deferred legacy v11 DeleteStream packets, timed mute expirations, voice-state polling, and spatial grid
    const auto now = std::chrono::steady_clock::now();
    if (hasDroppedControlQueueEvents_.load(std::memory_order_relaxed)) {
        ReportControlQueueDropIfDue(now, false);
    }
    deferredDeleteSink_.FlushDue(now, adapter_);
    ProcessExpiredMutes(now);
    if (now - lastSpatialTick_ >= updateRate_) {
        lastSpatialTick_ = now;
        // Reconcile player voice-status deltas on updateRate_ cadence (~30 ms) to detect non-plugin joins/exits cleanly.
        if (voiceStatus_) {
            for (uint16_t pid = 0; pid < static_cast<uint16_t>(kMaxPlayers); ++pid) {
                SyncPlayerVoiceStateIfChanged(pid);
            }
        }
        RebuildSpatialGridNow();
        hasBuiltGrid_ = true;

        std::vector<std::shared_ptr<Stream>> dynSnapshot;
        {
            std::lock_guard lock(dynamicStreamsMutex_);
            dynSnapshot = dynamicStreams_;
        }

        for (const auto& stream : dynSnapshot) {
            stream->TickDynamic(
                spatialGrid_,
                adapter_,
                playerStore_.PluginFlags(),
                playerStore_.PlusFlags(),
                spatialScratch_,
                desiredMarks_,
                NextDesiredGen(),
                addScratch_,
                &deferredDeleteSink_
            );
        }
    }
}

void ServerCore::HandlePlayerConnectRpc(uint16_t playerId, const uint8_t* rpcBytes, std::size_t rpcSize) {
    if (playerId >= kMaxPlayers) {
        return;
    }
    if (!rpcBytes || rpcSize < 4u) {
        if (const auto* existing = playerStore_.GetSlot(playerId);
            existing && existing->connected.load(std::memory_order_acquire)) {
            HandlePlayerDisconnect(playerId);
        }
        return;
    }

    // Scan for 0xDeadBeef signature inside RPC 25 payload
    const uint8_t* match = nullptr;
    for (std::size_t i = 0; i + sizeof(protocol::ConnectPacket) <= rpcSize; ++i) {
        if (std::memcmp(rpcBytes + i, protocol::kConnectSignatureBytes, 4) == 0) {
            match = rpcBytes + i;
            break;
        }
    }

    if (!match) {
        // Detect CyberMor v4.x signature (0xDEADC0DE) and warn once without routing invalid packets
        for (std::size_t i = 0; i + 4u <= rpcSize; ++i) {
            if (std::memcmp(rpcBytes + i, protocol::kCyberMorV4SignatureBytes, 4) == 0) {
                if (const auto* existing = playerStore_.GetSlot(playerId);
                    existing && existing->connected.load(std::memory_order_acquire)) {
                    HandlePlayerDisconnect(playerId);
                }
                if (playerStore_.RegisterUnsupportedV4(playerId)) {
                    log::Logger::Log(
                        log::Level::Warn,
                        "Player %hu connected with unsupported CyberMor v4.x client (protocol 4); please use SampVoice v3.x or SampVoice Plus",
                        playerId
                    );
                }
                return;
            }
        }
        // Vanilla / non-plugin reconnect on the same ID must still clear any stale session (R3-8)
        if (const auto* existing = playerStore_.GetSlot(playerId);
            existing && existing->connected.load(std::memory_order_acquire)) {
            HandlePlayerDisconnect(playerId);
        }
        return;
    }

    protocol::ConnectPacket conn{};
    std::memcpy(&conn, match, sizeof(conn));

    uint8_t clientProto = conn.version;
    uint32_t caps = 0;
    const std::size_t offsetAfterConn = static_cast<std::size_t>(match - rpcBytes) + sizeof(conn);
    if (offsetAfterConn + sizeof(protocol::ConnectPlusExtension) <= rpcSize) {
        protocol::ConnectPlusExtension ext{};
        std::memcpy(&ext, rpcBytes + offsetAfterConn, sizeof(ext));
        if (ext.magic == protocol::kPlusExtensionMagic) {
            caps = ext.capabilities;
            clientProto = protocol::kPlusVersion;
        }
    }

    const uint32_t ipv4Net = adapter_.GetPlayerIpv4Net(playerId);
    if (ipv4Net == 0) {
        return;
    }

    // Ensure full stream detachment if this player ID reconnected before disconnect cleanup ran
    if (const auto* existing = playerStore_.GetSlot(playerId);
        existing && existing->connected.load(std::memory_order_acquire)) {
        HandlePlayerDisconnect(playerId);
    }

    const uint32_t serverKey = playerStore_.RegisterConnect(
        playerId, ipv4Net, conn.version, clientProto, conn.micro != 0, caps
    );
    if (serverKey == 0) {
        return;
    }

    protocol::ServerInfoPacket info{};
    info.serverKey = serverKey;
    info.serverPort = udpSocket_.LocalPort();

    const auto buf = ControlPacketBuffer::Create(protocol::ControlPacketType::ServerInfo, info);
    adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());

    log::Logger::Log(
        log::Level::Debug,
        "Player %hu voice connect (wireVer=%hhu, proto=%hhu, micro=%hhu, key=0x%X)",
        playerId, conn.version, clientProto, conn.micro, serverKey
    );
}

void ServerCore::HandleControlPacket(uint16_t playerId, const uint8_t* bytes, std::size_t size) {
    if (playerId >= kMaxPlayers || !bytes || size < sizeof(protocol::ControlPacketHeader)) {
        return;
    }

    protocol::ControlPacketHeader hdr{};
    std::memcpy(&hdr, bytes, sizeof(hdr));
    if (size != sizeof(protocol::ControlPacketHeader) + hdr.length) {
        return;
    }

    const uint8_t* payload = bytes + sizeof(protocol::ControlPacketHeader);
    IncomingControlEvent ev{};
    ev.playerId = playerId;
    ev.type = hdr.packet;

    PlayerVoiceSlot* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->connected.load(std::memory_order_acquire)) {
        return;
    }

    const auto type = static_cast<protocol::ControlPacketType>(hdr.packet);
    auto bucket = PlayerVoiceSlot::ControlBucketKind::KeyEvent;
    if (type == protocol::ControlPacketType::PressKey && hdr.length == sizeof(protocol::PressKeyPacket)) {
        protocol::PressKeyPacket p{};
        std::memcpy(&p, payload, sizeof(p));
        ev.keyId = p.keyId;
        bucket = PlayerVoiceSlot::ControlBucketKind::KeyEvent;
    } else if (type == protocol::ControlPacketType::ReleaseKey && hdr.length == sizeof(protocol::ReleaseKeyPacket)) {
        protocol::ReleaseKeyPacket p{};
        std::memcpy(&p, payload, sizeof(p));
        ev.keyId = p.keyId;
        bucket = PlayerVoiceSlot::ControlBucketKind::KeyEvent;
    } else if (type == protocol::ControlPacketType::PlusTelemetryPing &&
               hdr.length == sizeof(protocol::PlusTelemetryPingPacket)) {
        protocol::PlusTelemetryPingPacket p{};
        std::memcpy(&p, payload, sizeof(p));
        ev.clientTimestampUs = p.clientTimestampUs;
        bucket = PlayerVoiceSlot::ControlBucketKind::TelemetryPing;
    } else if (type == protocol::ControlPacketType::PlusMicroState &&
               hdr.length == sizeof(protocol::PlusMicroStatePacket)) {
        if (slot->clientProtocol.load(std::memory_order_acquire) < protocol::kPlusVersion) {
            return;
        }
        protocol::PlusMicroStatePacket p{};
        std::memcpy(&p, payload, sizeof(p));
        ev.keyId = p.hasMicro ? 1u : 0u;
        bucket = PlayerVoiceSlot::ControlBucketKind::MicroState;
    } else {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::string floodSummary;
    const bool rateAllowed = slot->AcceptControlPacket(
        hdr.packet, ev.keyId, bucket, now, floodSummary);
    if (!floodSummary.empty()) {
        log::Logger::Log(
            log::Level::Warn,
            "Rate-limited control packet flood from player %hu: %s",
            playerId,
            floodSummary.c_str()
        );
    }
    if (!rateAllowed) {
        if (bucket == PlayerVoiceSlot::ControlBucketKind::TelemetryPing) {
            log::Logger::Log(
                log::Level::Debug,
                "Dropped rate-limited PlusTelemetryPing from player %hu",
                playerId
            );
        }
        return;
    }

    ev.seq = nextControlEventSeq_.fetch_add(1, std::memory_order_relaxed);
    if (!incomingControlQueue_.TryPush(ev)) {
        ReportControlQueueDropIfDue(now, true);
    } else if (hasDroppedControlQueueEvents_.load(std::memory_order_relaxed)) {
        ReportControlQueueDropIfDue(now, false);
    }
}

void ServerCore::DispatchControlEvent(const IncomingControlEvent& queuedEv) {
    const auto* slot = playerStore_.GetSlot(queuedEv.playerId);
    if (!slot || !slot->connected.load(std::memory_order_acquire)) {
        return;
    }

    if (queuedEv.kind == IncomingControlEvent::Kind::VoiceMute) {
        adapter_.OnVoiceMute(queuedEv.playerId, queuedEv.flag, queuedEv.timeMs, queuedEv.speaking);
        return;
    }
    if (queuedEv.kind == IncomingControlEvent::Kind::VoiceUnmute) {
        adapter_.OnVoiceUnmute(queuedEv.playerId, queuedEv.flag);
        return;
    }

    const auto type = static_cast<protocol::ControlPacketType>(queuedEv.type);
    if (type == protocol::ControlPacketType::PressKey) {
        if (slot->HasKey(queuedEv.keyId)) {
            adapter_.OnActivationKeyPress(queuedEv.playerId, queuedEv.keyId);
        }
    } else if (type == protocol::ControlPacketType::ReleaseKey) {
        if (queuedEv.bypassKeyCheck || slot->HasKey(queuedEv.keyId)) {
            adapter_.OnActivationKeyRelease(queuedEv.playerId, queuedEv.keyId);
        }
    } else if (type == protocol::ControlPacketType::PlusTelemetryPing) {
        protocol::PlusTelemetryPingPacket p{};
        p.clientTimestampUs = queuedEv.clientTimestampUs;
        protocol::PlusTelemetryPongPacket pong{};
        pong.clientTimestampUs = p.clientTimestampUs;
        pong.serverQueueDepth = 0;
        const auto pongBuf = ControlPacketBuffer::Create(
            protocol::ControlPacketType::PlusTelemetryPong, pong
        );
        adapter_.SendControlPacket(queuedEv.playerId, pongBuf.Data(), pongBuf.Size());
    } else if (type == protocol::ControlPacketType::PlusMicroState) {
        if (slot->clientProtocol.load(std::memory_order_acquire) >= protocol::kPlusVersion) {
            const bool hasMicro = (queuedEv.keyId != 0);
            if (auto* mutableSlot = playerStore_.GetSlot(queuedEv.playerId)) {
                const bool prev = mutableSlot->microStatus.exchange(hasMicro, std::memory_order_acq_rel);
                if (prev != hasMicro) {
                    log::Logger::Log(
                        log::Level::Debug,
                        "Player %hu microphone state changed: %s",
                        queuedEv.playerId,
                        hasMicro ? "available" : "unavailable"
                    );
                    adapter_.OnMicroStateChange(queuedEv.playerId, hasMicro);
                    if (voiceStatus_) {
                        SyncPlayerVoiceStateIfChanged(queuedEv.playerId);
                    }
                }
            }
        }
    }
}

void ServerCore::EnqueueSyntheticControlEvent(IncomingControlEvent& ev) {
    ev.seq = nextControlEventSeq_.fetch_add(1, std::memory_order_relaxed);
    syntheticControlQueue_.push_back(ev);
}

void ServerCore::ReportControlQueueDropIfDue(std::chrono::steady_clock::time_point now, bool droppedNow) {
    uint64_t droppedToLog = 0;
    {
        std::lock_guard lock(controlQueueDropMutex_);
        if (droppedNow) {
            ++droppedControlQueueEvents_;
            hasDroppedControlQueueEvents_.store(true, std::memory_order_release);
        }
        if (droppedControlQueueEvents_ == 0) {
            hasDroppedControlQueueEvents_.store(false, std::memory_order_release);
            return;
        }
        constexpr auto kLogInterval = std::chrono::seconds(PlayerVoiceSlot::kControlFloodLogSec);
        if (lastControlQueueDropLogTime_.time_since_epoch().count() == 0 ||
            (now - lastControlQueueDropLogTime_) >= kLogInterval) {
            droppedToLog = droppedControlQueueEvents_;
            droppedControlQueueEvents_ = 0;
            hasDroppedControlQueueEvents_.store(false, std::memory_order_release);
            lastControlQueueDropLogTime_ = now;
        }
    }
    if (droppedToLog > 0) {
        log::Logger::Log(
            log::Level::Warn,
            "Incoming control queue full: dropped %llu network control packet(s) in the last interval",
            static_cast<unsigned long long>(droppedToLog)
        );
    }
}

void ServerCore::HandlePlayerDisconnect(uint16_t playerId) {
    if (playerId >= kMaxPlayers) {
        return;
    }

    std::vector<uint32_t> speakerStreams;
    std::vector<uint32_t> listenerStreams;
    if (const auto* slot = playerStore_.GetSlot(playerId)) {
        std::shared_lock lock(slot->dataMutex);
        speakerStreams = slot->speakerStreams;
        listenerStreams = slot->listenerStreams;
    }

    for (uint32_t sid : speakerStreams) {
        if (const auto stream = streamPool_.Get(sid)) {
            stream->DetachSpeaker(playerId);
        }
    }
    for (uint32_t sid : listenerStreams) {
        if (const auto stream = streamPool_.Get(sid)) {
            stream->DetachListener(playerId, adapter_, false, false);
        }
    }

    {
        std::lock_guard lock(dynamicStreamsMutex_);
        for (const auto& stream : dynamicStreams_) {
            if (stream) {
                stream->DetachListener(playerId, adapter_, false, false);
                stream->UnblockListener(playerId);
            }
        }
    }

    deferredDeleteSink_.ClearPlayer(playerId);
    UntrackTimedMute(playerId);
    playerStore_.RegisterDisconnect(playerId);

    if (voiceStatus_) {
        const uint8_t clearState = static_cast<uint8_t>(protocol::PlayerVoiceState::Ok);
        const bool wasTracked = voiceStateConnected_[playerId] || lastSentVoiceState_[playerId] != clearState;
        voiceStateConnected_[playerId] = false;
        lastSentVoiceState_[playerId] = clearState;
        if (wasTracked) {
            BroadcastVoiceStateEntry(playerId, clearState, playerId);
        }
    }
}

// -----------------------------------------------------------------------------
// Pawn Native Operations
// -----------------------------------------------------------------------------

void ServerCore::SvDebug(bool enabled) {
    log::Logger::SetLevel(enabled ? log::Level::Debug : log::Level::Info);
    if (enabled && !effectiveConfigLogged_ && !effectiveConfigLine_.empty()) {
        effectiveConfigLogged_ = true;
        log::Logger::Log(log::Level::Debug, "%s", effectiveConfigLine_.c_str());
    }
}

void ServerCore::SvInit(uint32_t bitrate) {
    bool clamped = false;
    const uint32_t normalized = protocol::clampOpusBitrate(bitrate, &clamped);
    if (clamped) {
        log::Logger::Log(
            log::Level::Warn,
            "SvInit: bitrate %u bps outside valid range [%u..%u]; clamping to %u bps",
            bitrate,
            protocol::kMinBitrate,
            protocol::kMaxBitrate,
            normalized
        );
    }
    bitrate_.store(normalized, std::memory_order_release);
}

uint8_t ServerCore::SvGetVersion(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot ? slot->pluginVersion.load(std::memory_order_acquire) : 0;
}

uint8_t ServerCore::SvGetClientProtocol(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot ? slot->clientProtocol.load(std::memory_order_acquire) : 0;
}

uint32_t ServerCore::SvGetPlusCapabilities(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot ? slot->capabilities.load(std::memory_order_acquire) : 0;
}

bool ServerCore::SvHasMicro(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot && slot->microStatus.load(std::memory_order_acquire);
}

bool ServerCore::SvStartRecord(uint16_t playerId) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    const bool prev = slot->recordStatus.exchange(true, std::memory_order_acq_rel);
    if (!prev) {
        const auto buf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::StartRecord);
        adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    }
    return !prev;
}

bool ServerCore::SvStopRecord(uint16_t playerId) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    const bool prev = slot->recordStatus.exchange(false, std::memory_order_acq_rel);
    if (prev) {
        const auto buf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::StopRecord);
        adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    }
    return prev;
}

bool ServerCore::SvAddKey(uint16_t playerId, uint8_t keyId) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    if (!slot->AddKey(keyId)) {
        return false;
    }
    const protocol::AddKeyPacket pkt{keyId};
    const auto buf = ControlPacketBuffer::Create(protocol::ControlPacketType::AddKey, pkt);
    adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    return true;
}

bool ServerCore::SvHasKey(uint16_t playerId, uint8_t keyId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot && slot->HasKey(keyId);
}

bool ServerCore::SvRemoveKey(uint16_t playerId, uint8_t keyId) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    bool wasPressed = false;
    if (!slot->RemoveKey(keyId, &wasPressed)) {
        return false;
    }
    const protocol::RemoveKeyPacket pkt{keyId};
    const auto buf = ControlPacketBuffer::Create(protocol::ControlPacketType::RemoveKey, pkt);
    adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    if (wasPressed) {
        IncomingControlEvent relEv{};
        relEv.kind = IncomingControlEvent::Kind::ControlPacket;
        relEv.playerId = playerId;
        relEv.type = static_cast<uint16_t>(protocol::ControlPacketType::ReleaseKey);
        relEv.keyId = keyId;
        relEv.bypassKeyCheck = true;
        EnqueueSyntheticControlEvent(relEv);
    }
    return true;
}

void ServerCore::SvRemoveAllKeys(uint16_t playerId) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return;
    }
    const std::vector<uint8_t> pressedKeys = slot->ClearKeys();
    const auto buf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::RemoveAllKeys);
    adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    for (uint8_t keyId : pressedKeys) {
        IncomingControlEvent relEv{};
        relEv.kind = IncomingControlEvent::Kind::ControlPacket;
        relEv.playerId = playerId;
        relEv.type = static_cast<uint16_t>(protocol::ControlPacketType::ReleaseKey);
        relEv.keyId = keyId;
        relEv.bypassKeyCheck = true;
        EnqueueSyntheticControlEvent(relEv);
    }
}

void ServerCore::TrackTimedMute(uint16_t playerId) {
    std::lock_guard lock(timedMutesMutex_);
    if (std::find(timedMutedPlayers_.begin(), timedMutedPlayers_.end(), playerId) == timedMutedPlayers_.end()) {
        timedMutedPlayers_.push_back(playerId);
    }
}

void ServerCore::UntrackTimedMute(uint16_t playerId) {
    std::lock_guard lock(timedMutesMutex_);
    const auto it = std::find(timedMutedPlayers_.begin(), timedMutedPlayers_.end(), playerId);
    if (it != timedMutedPlayers_.end()) {
        *it = timedMutedPlayers_.back();
        timedMutedPlayers_.pop_back();
    }
}

void ServerCore::ProcessExpiredMutes(std::chrono::steady_clock::time_point now) {
    expiredMutesScratch_.clear();
    {
        std::lock_guard lock(timedMutesMutex_);
        if (timedMutedPlayers_.empty()) {
            return;
        }
        std::size_t i = 0;
        while (i < timedMutedPlayers_.size()) {
            const uint16_t pid = timedMutedPlayers_[i];
            PlayerVoiceSlot* slot = playerStore_.GetSlot(pid);
            if (!slot ||
                !slot->connected.load(std::memory_order_acquire) ||
                !slot->muteTimed ||
                !slot->muteStatus.load(std::memory_order_acquire)) {
                if (slot) {
                    slot->muteTimed = false;
                    slot->muteExpireAt = {};
                }
                timedMutedPlayers_[i] = timedMutedPlayers_.back();
                timedMutedPlayers_.pop_back();
                continue;
            }
            if (now >= slot->muteExpireAt) {
                slot->muteTimed = false;
                slot->muteExpireAt = {};
                timedMutedPlayers_[i] = timedMutedPlayers_.back();
                timedMutedPlayers_.pop_back();
                if (slot->muteStatus.exchange(false, std::memory_order_acq_rel)) {
                    expiredMutesScratch_.push_back(pid);
                }
                continue;
            }
            ++i;
        }
    }

    if (expiredMutesScratch_.empty()) {
        return;
    }

    const auto unmuteBuf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::MuteDisable);
    for (uint16_t pid : expiredMutesScratch_) {
        adapter_.SendControlPacket(pid, unmuteBuf.Data(), unmuteBuf.Size());
    }
    for (uint16_t pid : expiredMutesScratch_) {
        adapter_.OnVoiceUnmute(pid, true);
    }
}

bool ServerCore::SvMutePlayerStatus(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot && slot->muteStatus.load(std::memory_order_acquire);
}

void ServerCore::SvMutePlayerEnable(uint16_t playerId, bool releaseKeys, uint32_t timeMs) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return;
    }

    // Capture speaker attachment right when SvMutePlayerEnable is called (before releasing keys
    // or muting), because scripts may detach speaker streams in OnPlayerActivationKeyRelease before
    // OnPlayerVoiceMute is dispatched on the next tick.
    bool speaking = false;
    if (!slot->muteStatus.load(std::memory_order_acquire)) {
        std::shared_lock lock(slot->dataMutex);
        speaking = !slot->speakerStreams.empty();
    }

    if (timeMs > kMaxMuteDurationMs) {
        log::Logger::Log(
            log::Level::Warn,
            "SvMutePlayerEnable: player %hu duration %u ms exceeds 24h limit; clamping to %u ms",
            playerId,
            timeMs,
            kMaxMuteDurationMs
        );
        timeMs = kMaxMuteDurationMs;
    }

    const bool wasMuted = slot->muteStatus.exchange(true, std::memory_order_acq_rel);
    if (timeMs > 0) {
        slot->muteTimed = true;
        slot->muteExpireAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeMs);
        TrackTimedMute(playerId);
    } else {
        slot->muteTimed = false;
        slot->muteExpireAt = {};
        UntrackTimedMute(playerId);
    }

    if (!wasMuted) {
        const auto buf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::MuteEnable);
        adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    }

    if (releaseKeys) {
        if (slot->clientProtocol.load(std::memory_order_acquire) >= protocol::kPlusVersion) {
            const auto latchBuf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::PlusMuteKeyLatch);
            adapter_.SendControlPacket(playerId, latchBuf.Data(), latchBuf.Size());
        }

        const auto releasedKeys = slot->ReleaseAllPressedKeys();
        for (uint8_t keyId : releasedKeys) {
            IncomingControlEvent relEv{};
            relEv.kind = IncomingControlEvent::Kind::ControlPacket;
            relEv.playerId = playerId;
            relEv.type = static_cast<uint16_t>(protocol::ControlPacketType::ReleaseKey);
            relEv.keyId = keyId;
            relEv.bypassKeyCheck = true;
            EnqueueSyntheticControlEvent(relEv);
        }
    }

    if (!wasMuted) {
        IncomingControlEvent muteEv{};
        muteEv.kind = IncomingControlEvent::Kind::VoiceMute;
        muteEv.playerId = playerId;
        muteEv.flag = releaseKeys;
        muteEv.speaking = speaking;
        muteEv.timeMs = timeMs;
        EnqueueSyntheticControlEvent(muteEv);
    }
}

void ServerCore::SvMutePlayerDisable(uint16_t playerId) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return;
    }
    slot->muteTimed = false;
    slot->muteExpireAt = {};
    UntrackTimedMute(playerId);

    if (slot->muteStatus.exchange(false, std::memory_order_acq_rel)) {
        const auto buf = ControlPacketBuffer::CreateEmpty(protocol::ControlPacketType::MuteDisable);
        adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());

        IncomingControlEvent unmuteEv{};
        unmuteEv.kind = IncomingControlEvent::Kind::VoiceUnmute;
        unmuteEv.playerId = playerId;
        unmuteEv.flag = false;
        EnqueueSyntheticControlEvent(unmuteEv);
    }
}

int32_t ServerCore::SvMutePlayerRemaining(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire) || !slot->muteStatus.load(std::memory_order_acquire)) {
        return -1;
    }
    if (!slot->muteTimed) {
        return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    if (slot->muteExpireAt <= now) {
        return 1;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(slot->muteExpireAt - now).count();
    return ms > 0 ? static_cast<int32_t>(ms) : 1;
}

bool ServerCore::SetListenerEnabled(uint16_t playerId, bool enabled) {
    auto* slot = playerStore_.GetSlot(playerId);
    if (!slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    const bool prev = slot->listenerEnabled.exchange(enabled, std::memory_order_acq_rel);
    if (prev == enabled) {
        return false;
    }
    if (slot->clientProtocol.load(std::memory_order_acquire) >= protocol::kPlusVersion &&
        adapter_.IsPlayerConnected(playerId)) {
        const protocol::PlusListenerStatePacket pkt{enabled ? uint8_t{1} : uint8_t{0}};
        const auto buf = ControlPacketBuffer::Create(protocol::ControlPacketType::PlusListenerState, pkt);
        adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    }
    if (voiceStatus_) {
        SyncPlayerVoiceStateIfChanged(playerId);
    }
    return true;
}

void ServerCore::SyncSpeakerStreams(uint16_t playerId) {
    const auto* slot = playerStore_.GetSlot(playerId);
    if (!slot ||
        !slot->connected.load(std::memory_order_acquire) ||
        !slot->hasPlugin.load(std::memory_order_acquire) ||
        slot->clientProtocol.load(std::memory_order_acquire) < protocol::kPlusVersion ||
        !adapter_.IsPlayerConnected(playerId)) {
        return;
    }

    std::vector<uint32_t> attachedIds;
    {
        std::shared_lock lock(slot->dataMutex);
        attachedIds = slot->speakerStreams;
    }

    struct StreamMeta {
        uint64_t    seq{0};
        uint32_t    id{0};
        uint32_t    color{0};
        uint8_t     kind{0};
        std::string name{};
    };
    std::vector<StreamMeta> metas;
    metas.reserve(attachedIds.size());
    for (uint32_t sid : attachedIds) {
        if (const auto s = streamPool_.Get(sid)) {
            const uint8_t kind = s->IsLocal() ? 1u : 0u;
            metas.push_back({s->GetCreationSeq(), sid, s->GetColor(), kind, s->GetName()});
        }
    }
    std::sort(metas.begin(), metas.end(), [](const StreamMeta& a, const StreamMeta& b) {
        return a.seq < b.seq;
    });

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(protocol::PlusSpeakerStreamsPacket) + metas.size() * 24u);
    payload.resize(sizeof(protocol::PlusSpeakerStreamsPacket), 0);

    uint16_t written = 0;
    bool truncated = false;
    for (const auto& m : metas) {
        const std::size_t nameLen = (std::min<std::size_t>)(m.name.size(), 128u);
        const std::size_t entryBytes = sizeof(protocol::PlusSpeakerStreamEntryHeader) + nameLen;
        if (payload.size() + entryBytes > protocol::kMaxControlPayloadSize) {
            truncated = true;
            break;
        }
        protocol::PlusSpeakerStreamEntryHeader eh{};
        eh.streamId = m.id;
        eh.color    = m.color;
        eh.nameLen  = static_cast<uint16_t>(nameLen);
        eh.kind     = m.kind;
        const std::size_t off = payload.size();
        payload.resize(off + entryBytes);
        std::memcpy(payload.data() + off, &eh, sizeof(eh));
        if (nameLen > 0) {
            std::memcpy(payload.data() + off + sizeof(eh), m.name.data(), nameLen);
        }
        ++written;
    }

    protocol::PlusSpeakerStreamsPacket hdr{};
    hdr.count = written;
    std::memcpy(payload.data(), &hdr, sizeof(hdr));

    if (truncated) {
        log::Logger::Log(
            log::Level::Warn,
            "PlusSpeakerStreams truncated for player %hu: wrote %hu of %zu stream(s) (limit %zu bytes)",
            playerId,
            written,
            metas.size(),
            protocol::kMaxControlPayloadSize
        );
    }

    const auto buf = ControlPacketBuffer::CreateRaw(
        protocol::ControlPacketType::PlusSpeakerStreams,
        payload.data(),
        payload.size()
    );
    adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
    log::Logger::Log(
        log::Level::Debug,
        "Sent PlusSpeakerStreams to player %hu (count=%hu, bytes=%zu)",
        playerId,
        written,
        payload.size()
    );
}

uint8_t ServerCore::ComputePlayerVoiceState(uint16_t playerId) const noexcept {
    if (playerId >= kMaxPlayers || !adapter_.IsPlayerConnected(playerId)) {
        return static_cast<uint8_t>(protocol::PlayerVoiceState::Ok);
    }
    const auto* slot = playerStore_.GetSlot(playerId);
    if (!slot ||
        !slot->connected.load(std::memory_order_acquire) ||
        !slot->hasPlugin.load(std::memory_order_acquire) ||
        !slot->listenerEnabled.load(std::memory_order_acquire)) {
        return static_cast<uint8_t>(protocol::PlayerVoiceState::CannotHear);
    }
    return slot->microStatus.load(std::memory_order_acquire)
        ? static_cast<uint8_t>(protocol::PlayerVoiceState::Ok)
        : static_cast<uint8_t>(protocol::PlayerVoiceState::NoMicro);
}

void ServerCore::SendServerOptions(uint16_t playerId) {
    const auto* slot = playerStore_.GetSlot(playerId);
    if (!slot ||
        !slot->connected.load(std::memory_order_acquire) ||
        !slot->hasPlugin.load(std::memory_order_acquire) ||
        slot->clientProtocol.load(std::memory_order_acquire) < protocol::kPlusVersion ||
        !adapter_.IsPlayerConnected(playerId)) {
        return;
    }

    const protocol::PlusServerOptionsPacket pkt{
        voiceStatus_ ? uint8_t{1} : uint8_t{0},
        nearbyList_ ? uint8_t{1} : uint8_t{0}
    };
    const auto buf = ControlPacketBuffer::Create(protocol::ControlPacketType::PlusServerOptions, pkt);
    adapter_.SendControlPacket(playerId, buf.Data(), buf.Size());
}

void ServerCore::SendVoiceStateChunk(
    uint16_t targetPlayerId,
    const std::vector<protocol::PlusPlayerVoiceStateEntry>& entries
) {
    if (!voiceStatus_ || entries.empty()) {
        return;
    }
    const auto* slot = playerStore_.GetSlot(targetPlayerId);
    if (!slot ||
        !slot->connected.load(std::memory_order_acquire) ||
        !slot->hasPlugin.load(std::memory_order_acquire) ||
        slot->clientProtocol.load(std::memory_order_acquire) < protocol::kPlusVersion ||
        !adapter_.IsPlayerConnected(targetPlayerId)) {
        return;
    }

    std::size_t offset = 0;
    std::vector<uint8_t> payload;
    while (offset < entries.size()) {
        const std::size_t chunkCount = (std::min)(
            entries.size() - offset,
            protocol::kMaxVoiceStateEntriesPerPacket
        );
        payload.resize(
            sizeof(protocol::PlusPlayerVoiceStatePacket) +
            chunkCount * sizeof(protocol::PlusPlayerVoiceStateEntry)
        );
        const protocol::PlusPlayerVoiceStatePacket hdr{static_cast<uint16_t>(chunkCount)};
        std::memcpy(payload.data(), &hdr, sizeof(hdr));
        std::memcpy(
            payload.data() + sizeof(hdr),
            entries.data() + offset,
            chunkCount * sizeof(protocol::PlusPlayerVoiceStateEntry)
        );
        const auto buf = ControlPacketBuffer::CreateRaw(
            protocol::ControlPacketType::PlusPlayerVoiceState,
            payload.data(),
            payload.size()
        );
        adapter_.SendControlPacket(targetPlayerId, buf.Data(), buf.Size());
        offset += chunkCount;
    }
}

void ServerCore::SendVoiceStateSnapshot(uint16_t targetPlayerId) {
    if (!voiceStatus_) {
        return;
    }
    std::vector<protocol::PlusPlayerVoiceStateEntry> snapshot;
    for (uint16_t pid = 0; pid < static_cast<uint16_t>(kMaxPlayers); ++pid) {
        if (!adapter_.IsPlayerConnected(pid)) {
            continue;
        }
        snapshot.push_back(protocol::PlusPlayerVoiceStateEntry{pid, ComputePlayerVoiceState(pid)});
    }
    SendVoiceStateChunk(targetPlayerId, snapshot);
}

void ServerCore::BroadcastVoiceStateEntry(uint16_t subjectId, uint8_t state, uint16_t skipPlayerId) {
    if (!voiceStatus_) {
        return;
    }
    std::array<uint8_t, sizeof(protocol::PlusPlayerVoiceStatePacket) + sizeof(protocol::PlusPlayerVoiceStateEntry)> payload{};
    const protocol::PlusPlayerVoiceStatePacket hdr{1u};
    const protocol::PlusPlayerVoiceStateEntry entry{subjectId, state};
    std::memcpy(payload.data(), &hdr, sizeof(hdr));
    std::memcpy(payload.data() + sizeof(hdr), &entry, sizeof(entry));

    const auto buf = ControlPacketBuffer::CreateRaw(
        protocol::ControlPacketType::PlusPlayerVoiceState,
        payload.data(),
        payload.size()
    );
    for (uint16_t pid = 0; pid < static_cast<uint16_t>(kMaxPlayers); ++pid) {
        if (pid == skipPlayerId) {
            continue;
        }
        const auto* slot = playerStore_.GetSlot(pid);
        if (!slot ||
            !slot->connected.load(std::memory_order_acquire) ||
            !slot->hasPlugin.load(std::memory_order_acquire) ||
            slot->clientProtocol.load(std::memory_order_acquire) < protocol::kPlusVersion ||
            !adapter_.IsPlayerConnected(pid)) {
            continue;
        }
        adapter_.SendControlPacket(pid, buf.Data(), buf.Size());
    }
}

void ServerCore::SyncPlayerVoiceStateIfChanged(uint16_t subjectId, uint16_t skipPlayerId) {
    if (!voiceStatus_ || subjectId >= kMaxPlayers) {
        return;
    }
    const bool connected = adapter_.IsPlayerConnected(subjectId);
    const uint8_t desired = connected
        ? ComputePlayerVoiceState(subjectId)
        : static_cast<uint8_t>(protocol::PlayerVoiceState::Ok);
    if (voiceStateConnected_[subjectId] == connected &&
        lastSentVoiceState_[subjectId] == desired) {
        return;
    }
    voiceStateConnected_[subjectId] = connected;
    lastSentVoiceState_[subjectId] = desired;
    BroadcastVoiceStateEntry(subjectId, desired, skipPlayerId);
}

bool ServerCore::SvEnableListener(uint16_t playerId) {
    return SetListenerEnabled(playerId, true);
}

bool ServerCore::SvDisableListener(uint16_t playerId) {
    return SetListenerEnabled(playerId, false);
}

bool ServerCore::SvCheckListener(uint16_t playerId) const {
    const auto* slot = playerStore_.GetSlot(playerId);
    return slot &&
           slot->hasPlugin.load(std::memory_order_acquire) &&
           slot->listenerEnabled.load(std::memory_order_acquire);
}

uint32_t ServerCore::NextDesiredGen() noexcept {
    ++desiredGen_;
    if (desiredGen_ == 0u) {
        std::fill(desiredMarks_.begin(), desiredMarks_.end(), 0u);
        desiredGen_ = 1u;
    }
    return desiredGen_;
}

void ServerCore::RebuildSpatialGridNow() {
    spatialGrid_.Clear();
    adapter_.CollectPlayerSnapshots([this](const PlayerSpatialSnapshot& snap) {
        if (snap.playerId < kMaxPlayers &&
            playerStore_.PluginFlags()[snap.playerId].load(std::memory_order_acquire)) {
            spatialGrid_.Insert(snap);
        }
    });
}

uint32_t ServerCore::RegisterStream(std::shared_ptr<Stream> stream, void* ownerAmx) {
    if (!stream) {
        return memory::kInvalidHandle;
    }
    const uint32_t id = streamPool_.Insert(stream);
    if (id == memory::kInvalidHandle) {
        return memory::kInvalidHandle;
    }
    stream->SetHandle(id);
    stream->SetCreationSeq(nextStreamCreationSeq_++);
    stream->SetOwnerAmx(ownerAmx);

    if (ownerAmx != nullptr) {
        std::lock_guard lock(ownedMutex_);
        ownedStreams_[id] = ownerAmx;
    }

    if (stream->IsDynamic()) {
        {
            std::lock_guard lock(dynamicStreamsMutex_);
            dynamicStreams_.push_back(stream);
        }
        InitialDynamicPopulate(stream);
    }
    return id;
}

void ServerCore::InitialDynamicPopulate(const std::shared_ptr<Stream>& stream) {
    if (!hasBuiltGrid_) {
        RebuildSpatialGridNow();
        hasBuiltGrid_ = true;
    }
    stream->TickDynamic(
        spatialGrid_,
        adapter_,
        playerStore_.PluginFlags(),
        playerStore_.PlusFlags(),
        spatialScratch_,
        desiredMarks_,
        NextDesiredGen(),
        addScratch_,
        &deferredDeleteSink_
    );
}

void ServerCore::CleanupAmxResources(void* ownerAmx) {
    if (ownerAmx == nullptr) {
        return;
    }

    std::vector<uint32_t> streamsToDelete;
    std::vector<uint32_t> effectsToDelete;
    {
        std::lock_guard lock(ownedMutex_);
        for (auto it = ownedStreams_.begin(); it != ownedStreams_.end();) {
            if (it->second == ownerAmx) {
                streamsToDelete.push_back(it->first);
                it = ownedStreams_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = ownedEffects_.begin(); it != ownedEffects_.end();) {
            if (it->second == ownerAmx) {
                effectsToDelete.push_back(it->first);
                it = ownedEffects_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (uint32_t effId : effectsToDelete) {
        SvEffectDelete(effId);
    }
    for (uint32_t streamId : streamsToDelete) {
        SvDeleteStream(streamId);
    }

    if (!streamsToDelete.empty() || !effectsToDelete.empty()) {
        log::Logger::Log(
            log::Level::Debug,
            "AMX script unload cleanup: deleted %zu stream(s) and %zu effect(s)",
            streamsToDelete.size(), effectsToDelete.size()
        );
    }
}

uint32_t ServerCore::SvCreateGStream(uint32_t color, const std::string& name, void* ownerAmx) {
    return RegisterStream(Stream::CreateGlobal(ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateSLStreamAtPoint(
    float distance, float x, float y, float z, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateStaticLocal(EntityKind::Point, distance, 0, {x, y, z}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateSLStreamAtVehicle(
    float distance, uint16_t vehicleId, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateStaticLocal(EntityKind::Vehicle, distance, vehicleId, {}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateSLStreamAtPlayer(
    float distance, uint16_t playerId, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateStaticLocal(EntityKind::Player, distance, playerId, {}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateSLStreamAtObject(
    float distance, uint16_t objectId, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateStaticLocal(EntityKind::Object, distance, objectId, {}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateDLStreamAtPoint(
    float distance, uint32_t maxPlayers, float x, float y, float z, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateDynamicLocal(EntityKind::Point, distance, maxPlayers, 0, {x, y, z}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateDLStreamAtVehicle(
    float distance, uint32_t maxPlayers, uint16_t vehicleId, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateDynamicLocal(EntityKind::Vehicle, distance, maxPlayers, vehicleId, {}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateDLStreamAtPlayer(
    float distance, uint32_t maxPlayers, uint16_t playerId, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateDynamicLocal(EntityKind::Player, distance, maxPlayers, playerId, {}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

uint32_t ServerCore::SvCreateDLStreamAtObject(
    float distance, uint32_t maxPlayers, uint16_t objectId, uint32_t color, const std::string& name, void* ownerAmx
) {
    return RegisterStream(Stream::CreateDynamicLocal(EntityKind::Object, distance, maxPlayers, objectId, {}, ConvertStreamColor(color, legacyColorFormat_), name), ownerAmx);
}

void ServerCore::SvUpdatePositionForLPStream(uint32_t streamId, float x, float y, float z) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->UpdatePosition(x, y, z, adapter_);
    }
}

void ServerCore::SvUpdateDistanceForLStream(uint32_t streamId, float distance) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->UpdateDistance(distance, adapter_, &playerStore_.PlusFlags());
    }
}

bool ServerCore::SvAttachListenerToStream(uint32_t streamId, uint16_t playerId) {
    const auto stream = streamPool_.Get(streamId);
    const auto* slot = playerStore_.GetSlot(playerId);
    if (!stream || !slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    if (!stream->AttachListener(playerId, adapter_, true, &playerStore_.PlusFlags(), &deferredDeleteSink_)) {
        return false;
    }
    if (!stream->IsDynamic()) {
        playerStore_.AttachListenerStream(playerId, streamId);
    }
    return true;
}

bool ServerCore::SvHasListenerInStream(uint32_t streamId, uint16_t playerId) const {
    const auto stream = streamPool_.Get(streamId);
    return stream && stream->HasListener(playerId);
}

bool ServerCore::SvDetachListenerFromStream(uint32_t streamId, uint16_t playerId) {
    const auto stream = streamPool_.Get(streamId);
    if (!stream) {
        return false;
    }
    if (!stream->DetachListener(playerId, adapter_, true, true, &deferredDeleteSink_)) {
        return false;
    }
    if (!stream->IsDynamic()) {
        playerStore_.DetachListenerStream(playerId, streamId);
    }
    return true;
}

void ServerCore::SvDetachAllListenersFromStream(uint32_t streamId) {
    const auto stream = streamPool_.Get(streamId);
    if (!stream) {
        return;
    }
    const auto detached = stream->DetachAllListeners(adapter_, true, &deferredDeleteSink_);
    if (!stream->IsDynamic()) {
        for (uint16_t pid : detached) {
            playerStore_.DetachListenerStream(pid, streamId);
        }
    }
}

bool ServerCore::SvAttachSpeakerToStream(uint32_t streamId, uint16_t playerId) {
    const auto stream = streamPool_.Get(streamId);
    const auto* slot = playerStore_.GetSlot(playerId);
    if (!stream || !slot || !slot->hasPlugin.load(std::memory_order_acquire)) {
        return false;
    }
    if (!stream->AttachSpeaker(playerId)) {
        return false;
    }
    playerStore_.AttachSpeakerStream(playerId, streamId);
    SyncSpeakerStreams(playerId);
    return true;
}

bool ServerCore::SvHasSpeakerInStream(uint32_t streamId, uint16_t playerId) const {
    const auto stream = streamPool_.Get(streamId);
    return stream && stream->HasSpeaker(playerId);
}

bool ServerCore::SvDetachSpeakerFromStream(uint32_t streamId, uint16_t playerId) {
    const auto stream = streamPool_.Get(streamId);
    if (!stream) {
        return false;
    }
    if (!stream->DetachSpeaker(playerId)) {
        return false;
    }
    playerStore_.DetachSpeakerStream(playerId, streamId);
    SyncSpeakerStreams(playerId);
    return true;
}

void ServerCore::SvDetachAllSpeakersFromStream(uint32_t streamId) {
    const auto stream = streamPool_.Get(streamId);
    if (!stream) {
        return;
    }
    const auto detached = stream->DetachAllSpeakers();
    for (uint16_t pid : detached) {
        playerStore_.DetachSpeakerStream(pid, streamId);
        SyncSpeakerStreams(pid);
    }
}

void ServerCore::SvStreamParameterSet(uint32_t streamId, uint32_t parameterId, float value) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->SetParameter(parameterId, value, adapter_);
    }
}

void ServerCore::SvStreamParameterReset(uint32_t streamId, uint32_t parameterId) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->ResetParameter(parameterId, adapter_);
    }
}

bool ServerCore::SvStreamParameterHas(uint32_t streamId, uint32_t parameterId) const {
    const auto stream = streamPool_.Get(streamId);
    return stream && stream->HasParameter(parameterId);
}

float ServerCore::SvStreamParameterGet(uint32_t streamId, uint32_t parameterId) const {
    const auto stream = streamPool_.Get(streamId);
    return stream ? stream->GetParameter(parameterId) : -1.0f;
}

void ServerCore::SvStreamParameterSlideFromTo(
    uint32_t streamId, uint32_t parameterId, float startVal, float endVal, uint32_t timeMs
) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->SlideParameterFromTo(parameterId, startVal, endVal, timeMs, adapter_);
    }
}

void ServerCore::SvStreamParameterSlideTo(
    uint32_t streamId, uint32_t parameterId, float endVal, uint32_t timeMs
) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->SlideParameterTo(parameterId, endVal, timeMs, adapter_);
    }
}

void ServerCore::SvStreamParameterSlide(
    uint32_t streamId, uint32_t parameterId, float deltaVal, uint32_t timeMs
) {
    if (const auto stream = streamPool_.Get(streamId)) {
        stream->SlideParameter(parameterId, deltaVal, timeMs, adapter_);
    }
}

bool ServerCore::SvStreamSetIcon(uint32_t streamId, const std::string& iconName) {
    const auto stream = streamPool_.Get(streamId);
    if (!stream) {
        return false;
    }

    struct IconEntry {
        const char* name;
        protocol::StreamIconId id;
    };
    static constexpr std::array<IconEntry, 7> kIconTable = {{
        {"speaker",   protocol::StreamIconId::Speaker},
        {"radio",     protocol::StreamIconId::Radio},
        {"phone",     protocol::StreamIconId::Phone},
        {"megaphone", protocol::StreamIconId::Megaphone},
        {"police",    protocol::StreamIconId::Police},
        {"admin",     protocol::StreamIconId::Admin},
        {"global",    protocol::StreamIconId::Global},
    }};

    protocol::StreamIconId resolved = protocol::StreamIconId::Speaker;
    bool matched = false;
    for (const auto& entry : kIconTable) {
        if (iconName == entry.name) {
            resolved = entry.id;
            matched = true;
            break;
        }
    }
    if (!matched) {
        bool shouldWarn = false;
        {
            std::lock_guard lock(warnIconMutex_);
            constexpr std::size_t kMaxWarnedUnknownIcons = 32u;
            if (warnedUnknownIcons_.size() < kMaxWarnedUnknownIcons &&
                warnedUnknownIcons_.insert(iconName).second) {
                shouldWarn = true;
            }
        }
        if (shouldWarn) {
            log::Logger::Log(
                log::Level::Warn,
                "SvStreamSetIcon: unknown icon '%s', falling back to 'speaker'",
                iconName.c_str()
            );
        }
    }

    stream->SetIcon(resolved, adapter_, playerStore_.PlusFlags());
    return true;
}

void ServerCore::SvDeleteStream(uint32_t streamId) {
    {
        std::lock_guard lock(ownedMutex_);
        ownedStreams_.erase(streamId);
    }
    const auto stream = streamPool_.Remove(streamId);
    if (!stream) {
        return;
    }

    if (stream->IsDynamic()) {
        std::lock_guard lock(dynamicStreamsMutex_);
        const auto it = std::find(dynamicStreams_.begin(), dynamicStreams_.end(), stream);
        if (it != dynamicStreams_.end()) {
            *it = std::move(dynamicStreams_.back());
            dynamicStreams_.pop_back();
        }
    }

    const auto detachedSpeakers = stream->DetachAllSpeakers();
    for (uint16_t pid : detachedSpeakers) {
        playerStore_.DetachSpeakerStream(pid, streamId);
        SyncSpeakerStreams(pid);
    }

    const auto detachedListeners = stream->DetachAllListeners(adapter_, true, &deferredDeleteSink_);
    if (!stream->IsDynamic()) {
        for (uint16_t pid : detachedListeners) {
            playerStore_.DetachListenerStream(pid, streamId);
        }
    }

    const auto effectIds = stream->ClearEffects();
    for (uint32_t eid : effectIds) {
        if (const auto eff = effectPool_.Get(eid)) {
            eff->DetachFromStream(streamId);
        }
    }
}

void ServerCore::SvEffectAttachStream(uint32_t effectId, uint32_t streamId) {
    const auto effect = effectPool_.Get(effectId);
    const auto stream = streamPool_.Get(streamId);
    if (!effect || !stream) {
        return;
    }
    if (effect->AttachToStream(streamId)) {
        stream->RegisterEffectPacket(effectId, effect->BuildCreatePacket(streamId), adapter_);
    }
}

void ServerCore::SvEffectDetachStream(uint32_t effectId, uint32_t streamId) {
    const auto effect = effectPool_.Get(effectId);
    const auto stream = streamPool_.Get(streamId);
    if (!effect || !stream) {
        return;
    }
    if (effect->DetachFromStream(streamId)) {
        stream->UnregisterEffectPacket(effectId, effect->BuildDeletePacket(streamId), adapter_);
    }
}

void ServerCore::SvEffectDelete(uint32_t effectId) {
    {
        std::lock_guard lock(ownedMutex_);
        ownedEffects_.erase(effectId);
    }
    const auto effect = effectPool_.Remove(effectId);
    if (!effect) {
        return;
    }
    for (uint32_t streamId : effect->AttachedStreams()) {
        if (const auto stream = streamPool_.Get(streamId)) {
            stream->UnregisterEffectPacket(effectId, effect->BuildDeletePacket(streamId), adapter_);
        }
    }
    effect->ClearAttachedStreams();
}

} // namespace svplus::server
