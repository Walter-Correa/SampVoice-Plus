#include "audio/PlaybackEngine.hpp"

#include "samp/SampBridge.hpp"
#include "svplus/common/Logger.hpp"

#include <algorithm>
#include <unordered_set>

namespace svplus::client::audio {

PlaybackEngine& PlaybackEngine::instance() noexcept {
    static auto* inst = new PlaybackEngine();
    return *inst;
}

void PlaybackEngine::createStream(std::uint32_t streamId,
                                  StreamKind kind,
                                  std::uint32_t target,
                                  float distance,
                                  protocol::Vec3f position,
                                  std::uint32_t color,
                                  std::string name) {
    {
        std::lock_guard lock(mutex_);
        streamCreateOrder_[streamId] = nextCreateSeq_++;
        streams_[streamId] = std::make_unique<AudioStream>(
            streamId, kind, target, distance, position, color, name);
        streamCountAtomic_.store(streams_.size(), std::memory_order_release);
    }
    SVPLUS_LOG_INF_CAT("audio",
                       "Stream created id=0x%08X kind=%u target=%u dist=%.1f color=0x%08X name='%s'",
                       streamId, static_cast<unsigned>(kind), target, distance, color, name.c_str());
}

void PlaybackEngine::deleteStream(std::uint32_t streamId) {
    {
        std::lock_guard lock(mutex_);
        streams_.erase(streamId);
        streamCreateOrder_.erase(streamId);
        streamCountAtomic_.store(streams_.size(), std::memory_order_release);
    }
    {
        std::lock_guard spLock(spatialMutex_);
        spatialSnapshot_.streamPositions.erase(streamId);
    }
    SVPLUS_LOG_INF_CAT("audio", "Stream deleted id=0x%08X", streamId);
}

void PlaybackEngine::updateStreamDistance(std::uint32_t streamId, float distance) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->setDistance(distance);
    }
}

void PlaybackEngine::updateStreamPosition(std::uint32_t streamId, protocol::Vec3f position) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->setPosition(position);
    }
}

void PlaybackEngine::setStreamIcon(std::uint32_t streamId, protocol::StreamIconId iconId) {
    const std::size_t idx = static_cast<std::size_t>(iconId);
    const protocol::StreamIconId sanitized = (idx < protocol::kStreamIconCount)
        ? iconId
        : protocol::StreamIconId::Speaker;
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->setIconId(sanitized);
    }
}

void PlaybackEngine::setListenerDisabled(bool disabled) {
    const bool prev = listenerDisabled_.exchange(disabled, std::memory_order_acq_rel);
    if (disabled && !prev) {
        std::lock_guard lock(mutex_);
        speakerQueue_.clear();
        for (auto& [id, stream] : streams_) {
            (void)id;
            if (stream) {
                stream->clearChannels();
            }
        }
    }
}

void PlaybackEngine::setStreamParameter(std::uint32_t streamId, std::uint32_t paramId, float value) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->setParameter(paramId, value);
    }
}

void PlaybackEngine::slideStreamParameter(std::uint32_t streamId,
                                          std::uint32_t paramId,
                                          float startVal,
                                          float endVal,
                                          std::uint32_t timeMs) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->slideParameter(paramId, startVal, endVal, timeMs);
    }
}

void PlaybackEngine::createEffect(std::uint32_t streamId, const StreamEffectSpec& spec) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->addOrUpdateEffect(spec);
    }
}

void PlaybackEngine::deleteEffect(std::uint32_t streamId, std::uint32_t effectId) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->removeEffect(effectId);
    }
}

void PlaybackEngine::setCurrentServerEndpoint(std::string_view endpoint) {
    {
        std::lock_guard lk(serverMutex_);
        if (currentServerEndpoint_ == endpoint) {
            return;
        }
        currentServerEndpoint_.assign(endpoint);
    }
    serverGen_.fetch_add(1, std::memory_order_acq_rel);
}

bool PlaybackEngine::isSpeakerMutedCached(std::uint16_t speakerId, const config::ClientConfig& cfg) noexcept {
    if (speakerId >= samp::kMaxPlayerSlots) {
        return false;
    }
    const auto& samp = samp::SampBridge::instance();
    const std::uint32_t nameGen   = samp.nameCacheGeneration();
    const std::uint32_t muteGen   = cfg.muteGeneration;
    const std::uint32_t serverGen = serverGen_.load(std::memory_order_acquire);
    auto& entry = muteCache_[speakerId];
    if (entry.nameGen != nameGen || entry.muteGen != muteGen || entry.serverGen != serverGen) {
        std::string epCopy;
        {
            std::lock_guard lk(serverMutex_);
            epCopy = currentServerEndpoint_;
        }
        entry.muted     = cfg.isPlayerMuted(samp.getCachedPlayerName(speakerId), epCopy);
        entry.nameGen   = nameGen;
        entry.muteGen   = muteGen;
        entry.serverGen = serverGen;
    }
    return entry.muted;
}

void PlaybackEngine::onVoicePacket(std::uint32_t streamId,
                                   std::uint16_t speakerId,
                                   std::uint32_t seq,
                                   std::span<const std::uint8_t> opusPayload,
                                   const config::ClientConfig& cfg) {
    if (listenerDisabled_.load(std::memory_order_acquire)) {
        packetsDropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    packetsReceived_.fetch_add(1, std::memory_order_relaxed);
    const float masterVol = static_cast<float>(std::clamp(cfg.soundVolume, 0, 100)) / 100.0f;

    std::lock_guard lock(mutex_);
    const bool muted = isSpeakerMutedCached(speakerId, cfg);
    if (muted) {
        packetsDropped_.fetch_add(1, std::memory_order_relaxed);
    }
    auto it = streams_.find(streamId);
    if (it == streams_.end() || !it->second) {
        packetsDropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    it->second->pushVoiceFrame(
        speakerId,
        seq,
        opusPayload,
        masterVol,
        cfg.soundEnable,
        muted,
        cfg.soundFilter,
        cfg.soundBalancer);
}

void PlaybackEngine::onSpeakerEnd(std::uint32_t streamId, std::uint16_t speakerId) {
    std::lock_guard lock(mutex_);
    if (auto it = streams_.find(streamId); it != streams_.end() && it->second) {
        it->second->resetSpeaker(speakerId);
    }
}

void PlaybackEngine::resetAll() {
    listenerDisabled_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(mutex_);
        streams_.clear();
        streamCreateOrder_.clear();
        nextCreateSeq_ = 1;
        speakerQueue_.clear();
        muteCache_.fill(MuteCacheEntry{});
        streamCountAtomic_.store(0, std::memory_order_release);
        activeSpeakerCountAtomic_.store(0, std::memory_order_release);
    }
    {
        std::lock_guard spLock(spatialMutex_);
        spatialSnapshot_.hasCamera = false;
        spatialSnapshot_.streamPositions.clear();
    }
    publishSpeakerSnapshot({});
}

void PlaybackEngine::updateSpatialFromMainThread() {
    const auto& samp = samp::SampBridge::instance();

    struct TargetQuery {
        std::uint32_t id;
        StreamKind    kind;
        std::uint32_t target;
        protocol::Vec3f staticPos;
    };

    std::vector<TargetQuery> queries;
    {
        std::lock_guard lock(mutex_);
        queries.reserve(streams_.size());
        for (const auto& [id, stream] : streams_) {
            if (!stream || stream->kind() == StreamKind::Global) {
                continue;
            }
            queries.push_back({id, stream->kind(), stream->target(), stream->position()});
        }
    }

    SpatialSnapshot nextSnap;
    nextSnap.hasCamera = samp.getCameraBasis(nextSnap.camPos, nextSnap.camFront, nextSnap.camTop);
    for (const auto& q : queries) {
        CachedPos cp{};
        switch (q.kind) {
            case StreamKind::LocalAtPoint:
                cp.pos[0] = q.staticPos.x;
                cp.pos[1] = q.staticPos.y;
                cp.pos[2] = q.staticPos.z;
                cp.valid  = true;
                break;
            case StreamKind::LocalAtPlayer:
                cp.valid = samp.getRemotePlayerPosition(static_cast<std::uint16_t>(q.target), cp.pos);
                break;
            case StreamKind::LocalAtVehicle:
                cp.valid = samp.getVehiclePosition(static_cast<std::uint16_t>(q.target), cp.pos);
                break;
            case StreamKind::LocalAtObject:
                cp.valid = samp.getObjectPosition(static_cast<std::uint16_t>(q.target), cp.pos);
                break;
            case StreamKind::Global:
            default:
                break;
        }
        nextSnap.streamPositions.emplace(q.id, cp);
    }

    std::lock_guard spLock(spatialMutex_);
    spatialSnapshot_ = std::move(nextSnap);
}

void PlaybackEngine::tick(const config::ClientConfig& cfg) {
    if (listenerDisabled_.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(mutex_);
            speakerQueue_.clear();
        }
        publishSpeakerSnapshot({});
        return;
    }
    const auto& samp = samp::SampBridge::instance();

    SpatialSnapshot snapCopy;
    {
        std::lock_guard spLock(spatialMutex_);
        snapCopy = spatialSnapshot_;
    }

    const float masterVol = static_cast<float>(std::clamp(cfg.soundVolume, 0, 100)) / 100.0f;
    std::vector<ActiveSpeakerInfo> activeNow;
    std::unordered_map<std::uint16_t, std::size_t> speakerIdxById;
    std::vector<std::uint16_t> streamSpeakers;
    std::vector<ActiveSpeakerInfo> prevPublished;
    std::vector<ActiveSpeakerInfo> nextPublished;

    {
        std::lock_guard lock(mutex_);
        prevPublished = speakerQueue_.all();

        struct OrderedStreamEntry {
            std::uint64_t order;
            std::uint32_t id;
            AudioStream*  stream;
        };
        std::vector<OrderedStreamEntry> orderedStreams;
        orderedStreams.reserve(streams_.size());
        for (auto& [id, stream] : streams_) {
            if (!stream) {
                continue;
            }
            const auto oIt = streamCreateOrder_.find(id);
            const std::uint64_t ord = (oIt != streamCreateOrder_.end()) ? oIt->second : id;
            orderedStreams.push_back({ord, id, stream.get()});
        }
        std::sort(orderedStreams.begin(), orderedStreams.end(),
                  [](const OrderedStreamEntry& a, const OrderedStreamEntry& b) {
                      return a.order < b.order;
                  });

        for (const auto& entry : orderedStreams) {
            const std::uint32_t id = entry.id;
            AudioStream* stream = entry.stream;

            float resolvedPos[3]{};
            bool posValid = false;
            if (auto spIt = snapCopy.streamPositions.find(id); spIt != snapCopy.streamPositions.end()) {
                posValid = spIt->second.valid;
                resolvedPos[0] = spIt->second.pos[0];
                resolvedPos[1] = spIt->second.pos[1];
                resolvedPos[2] = spIt->second.pos[2];
            } else if (stream->kind() == StreamKind::LocalAtPoint) {
                const auto p = stream->position();
                resolvedPos[0] = p.x;
                resolvedPos[1] = p.y;
                resolvedPos[2] = p.z;
                posValid = true;
            }

            stream->tick(
                resolvedPos,
                posValid,
                snapCopy.camPos,
                snapCopy.camFront,
                snapCopy.camTop,
                snapCopy.hasCamera,
                masterVol,
                cfg.soundEnable,
                cfg.soundFilter,
                cfg.soundBalancer,
                [&](std::uint16_t spId) {
                    return isSpeakerMutedCached(spId, cfg);
                });

            streamSpeakers.clear();
            stream->collectActiveSpeakers(streamSpeakers);
            for (std::uint16_t spId : streamSpeakers) {
                const bool blocked = isSpeakerMutedCached(spId, cfg);
                const ActiveStreamTag tag{stream->color(), stream->name()};
                if (auto exIt = speakerIdxById.find(spId); exIt != speakerIdxById.end()) {
                    activeNow[exIt->second].streams.push_back(tag);
                    activeNow[exIt->second].blocked = activeNow[exIt->second].blocked || blocked;
                } else {
                    const std::string name = samp.getCachedPlayerName(spId);
                    speakerIdxById.emplace(spId, activeNow.size());
                    ActiveSpeakerInfo info;
                    info.playerId    = spId;
                    info.streamColor = stream->color();
                    info.iconId      = stream->iconId();
                    info.blocked     = blocked;
                    info.streamName  = stream->name();
                    info.playerName  = name.empty() ? ("Player " + std::to_string(spId)) : name;
                    info.streams.push_back(tag);
                    activeNow.push_back(std::move(info));
                }
            }
        }

        speakerQueue_.reconcile(activeNow);
        nextPublished = speakerQueue_.all();
        activeSpeakerCountAtomic_.store(nextPublished.size(), std::memory_order_release);
    }

    for (const auto& cur : nextPublished) {
        const auto prevIt = std::find_if(prevPublished.begin(), prevPublished.end(),
            [&](const ActiveSpeakerInfo& p) { return p.playerId == cur.playerId; });
        if (prevIt == prevPublished.end()) {
            SVPLUS_LOG_INF_CAT("audio", "Speaker started: [%u] %s (stream='%s')%s",
                               static_cast<unsigned>(cur.playerId),
                               cur.playerName.c_str(),
                               cur.streamName.c_str(),
                               cur.blocked ? " [blocked]" : "");
        } else if (prevIt->blocked != cur.blocked) {
            SVPLUS_LOG_INF_CAT("audio", "Speaker %s mid-speech: [%u] %s",
                               cur.blocked ? "blocked" : "unblocked",
                               static_cast<unsigned>(cur.playerId),
                               cur.playerName.c_str());
        }
    }
    for (const auto& prev : prevPublished) {
        const bool stillActive = std::any_of(nextPublished.begin(), nextPublished.end(),
            [&](const ActiveSpeakerInfo& c) { return c.playerId == prev.playerId; });
        if (!stillActive) {
            SVPLUS_LOG_INF_CAT("audio", "Speaker ended: [%u] %s",
                               static_cast<unsigned>(prev.playerId),
                               prev.playerName.c_str());
        }
    }

    publishSpeakerSnapshot(std::move(nextPublished));
}

PlaybackEngine::MetricsSnapshot PlaybackEngine::metricsSnapshot() const noexcept {
    return MetricsSnapshot{
        .streamCount        = streamCountAtomic_.load(std::memory_order_acquire),
        .activeSpeakerCount = activeSpeakerCountAtomic_.load(std::memory_order_acquire),
        .packetsReceived    = packetsReceived_.load(std::memory_order_relaxed),
        .packetsDropped     = packetsDropped_.load(std::memory_order_relaxed),
    };
}

std::size_t PlaybackEngine::totalChannelCount() const noexcept {
    std::lock_guard lock(mutex_);
    std::size_t total = 0;
    for (const auto& [id, stream] : streams_) {
        (void)id;
        if (stream) {
            total += stream->channelCount();
        }
    }
    return total;
}

bool PlaybackEngine::hasStream(std::uint32_t streamId) const noexcept {
    std::lock_guard lock(mutex_);
    return streams_.find(streamId) != streams_.end();
}

void PlaybackEngine::publishSpeakerSnapshot(std::vector<ActiveSpeakerInfo> speakers) {
    auto nextSnap = std::make_shared<const std::vector<ActiveSpeakerInfo>>(std::move(speakers));
    std::lock_guard snapLock(speakerSnapshotMutex_);
    publishedSpeakers_ = std::move(nextSnap);
}

std::shared_ptr<const std::vector<ActiveSpeakerInfo>> PlaybackEngine::activeSpeakersSnapshot() const {
    std::lock_guard snapLock(speakerSnapshotMutex_);
    return publishedSpeakers_;
}

} // namespace svplus::client::audio
