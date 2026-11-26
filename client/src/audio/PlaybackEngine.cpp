#include "audio/PlaybackEngine.hpp"

#include "samp/SampBridge.hpp"
#include "svplus/common/Logger.hpp"

#include <algorithm>

namespace svplus::client::audio {

namespace {

struct MuteCallbackCtx {
    PlaybackEngine*             engine;
    const config::ClientConfig* cfg;
};

} // namespace

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
    auto stream = std::make_shared<AudioStream>(
        streamId, kind, target, distance, position, color, name);
    {
        std::lock_guard lock(mutex_);
        const std::uint64_t seq = nextCreateSeq_++;
        if (streams_.find(streamId) != streams_.end()) {
            orderedStreams_.erase(
                std::remove_if(orderedStreams_.begin(), orderedStreams_.end(),
                               [streamId](const OrderedStreamEntry& e) { return e.id == streamId; }),
                orderedStreams_.end());
        }
        streams_[streamId] = stream;
        orderedStreams_.push_back(OrderedStreamEntry{seq, streamId, std::move(stream)});
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
        orderedStreams_.erase(
            std::remove_if(orderedStreams_.begin(), orderedStreams_.end(),
                           [streamId](const OrderedStreamEntry& e) { return e.id == streamId; }),
            orderedStreams_.end());
        streamCountAtomic_.store(streams_.size(), std::memory_order_release);
    }
    SVPLUS_LOG_INF_CAT("audio", "Stream deleted id=0x%08X", streamId);
}

void PlaybackEngine::updateStreamDistance(std::uint32_t streamId, float distance) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->setDistance(distance);
    }
}

void PlaybackEngine::updateStreamPosition(std::uint32_t streamId, protocol::Vec3f position) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->setPosition(position);
    }
}

void PlaybackEngine::setStreamIcon(std::uint32_t streamId, protocol::StreamIconId iconId) {
    const std::size_t idx = static_cast<std::size_t>(iconId);
    const protocol::StreamIconId sanitized = (idx < protocol::kStreamIconCount)
        ? iconId
        : protocol::StreamIconId::Speaker;
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->setIconId(sanitized);
    }
}

void PlaybackEngine::setListenerDisabled(bool disabled) {
    const bool prev = listenerDisabled_.exchange(disabled, std::memory_order_acq_rel);
    if (disabled && !prev) {
        std::vector<std::shared_ptr<AudioStream>> streamsCopy;
        {
            std::lock_guard lock(mutex_);
            speakerQueue_.clear();
            streamsCopy.reserve(orderedStreams_.size());
            for (const auto& entry : orderedStreams_) {
                if (entry.stream) {
                    streamsCopy.push_back(entry.stream);
                }
            }
        }
        for (const auto& stream : streamsCopy) {
            stream->clearChannels();
        }
    }
}

void PlaybackEngine::setStreamParameter(std::uint32_t streamId, std::uint32_t paramId, float value) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->setParameter(paramId, value);
    }
}

void PlaybackEngine::slideStreamParameter(std::uint32_t streamId,
                                          std::uint32_t paramId,
                                          float startVal,
                                          float endVal,
                                          std::uint32_t timeMs) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->slideParameter(paramId, startVal, endVal, timeMs);
    }
}

void PlaybackEngine::createEffect(std::uint32_t streamId, const StreamEffectSpec& spec) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->addOrUpdateEffect(spec);
    }
}

void PlaybackEngine::deleteEffect(std::uint32_t streamId, std::uint32_t effectId) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->removeEffect(effectId);
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
    const bool muted = isSpeakerMutedCached(speakerId, cfg);
    if (muted) {
        packetsDropped_.fetch_add(1, std::memory_order_relaxed);
    }

    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        auto it = streams_.find(streamId);
        if (it != streams_.end()) {
            stream = it->second;
        }
    }
    if (!stream) {
        packetsDropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    stream->pushVoiceFrame(
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
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->resetSpeaker(speakerId);
    }
}

void PlaybackEngine::resetAll() {
    listenerDisabled_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(mutex_);
        streams_.clear();
        orderedStreams_.clear();
        nextCreateSeq_ = 1;
        speakerQueue_.clear();
        muteCache_.fill(MuteCacheEntry{});
        streamCountAtomic_.store(0, std::memory_order_release);
        activeSpeakerCountAtomic_.store(0, std::memory_order_release);
    }
    {
        std::lock_guard spLock(spatialMutex_);
        publishedSpatial_.reset();
    }
    nextPublishedScratch_.clear();
    publishSpeakerSnapshotIfChanged(nextPublishedScratch_);
}

void PlaybackEngine::updateSpatialFromMainThread() {
    const auto& samp = samp::SampBridge::instance();

    spatialStreamsScratch_.clear();
    {
        std::lock_guard lock(mutex_);
        for (const auto& entry : orderedStreams_) {
            if (entry.stream && entry.stream->kind() != StreamKind::Global) {
                spatialStreamsScratch_.push_back(entry.stream);
            }
        }
    }

    spatialQueriesScratch_.clear();
    for (const auto& stream : spatialStreamsScratch_) {
        spatialQueriesScratch_.push_back(TargetQuery{
            stream->id(),
            stream->kind(),
            stream->target(),
            stream->position()
        });
    }
    spatialStreamsScratch_.clear();

    std::shared_ptr<SpatialSnapshot> nextSnap;
    for (auto& buf : spatialBuffers_) {
        if (!buf) {
            buf = std::make_shared<SpatialSnapshot>();
        }
        if (buf.get() != publishedSpatial_.get() && buf.use_count() == 1) {
            nextSnap = buf;
            break;
        }
    }
    if (!nextSnap) {
        nextSnap = std::make_shared<SpatialSnapshot>();
    }

    nextSnap->streamPositions.clear();
    nextSnap->hasCamera = samp.getCameraBasis(nextSnap->camPos, nextSnap->camFront, nextSnap->camTop);
    nextSnap->hasLocalPed = nextSnap->hasCamera && samp.getLocalPlayerPedPosition(nextSnap->localPedPos);
    for (const auto& q : spatialQueriesScratch_) {
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
        nextSnap->streamPositions.emplace_back(q.id, cp);
    }
    std::sort(nextSnap->streamPositions.begin(), nextSnap->streamPositions.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::lock_guard spLock(spatialMutex_);
    publishedSpatial_ = std::move(nextSnap);
}

void PlaybackEngine::tick(const config::ClientConfig& cfg) {
    if (listenerDisabled_.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(mutex_);
            speakerQueue_.clear();
            activeSpeakerCountAtomic_.store(0, std::memory_order_release);
        }
        nextPublishedScratch_.clear();
        publishSpeakerSnapshotIfChanged(nextPublishedScratch_);
        return;
    }
    const auto& samp = samp::SampBridge::instance();

    std::shared_ptr<const SpatialSnapshot> snap;
    {
        std::lock_guard spLock(spatialMutex_);
        snap = publishedSpatial_;
    }

    const float masterVol = static_cast<float>(std::clamp(cfg.soundVolume, 0, 100)) / 100.0f;

    tickStreamsScratch_.clear();
    {
        std::lock_guard lock(mutex_);
        tickStreamsScratch_.assign(orderedStreams_.begin(), orderedStreams_.end());
        prevPublishedScratch_.assign(speakerQueue_.all().begin(), speakerQueue_.all().end());
    }

    speakerIdxByIdScratch_.fill(-1);
    speakerIdxOverflowScratch_.clear();
    std::size_t activeCount = 0;

    MuteCallbackCtx muteCtx{this, &cfg};
    auto muteCheck = [](std::uint16_t spId, void* ctx) noexcept -> bool {
        auto* c = static_cast<MuteCallbackCtx*>(ctx);
        return c->engine->isSpeakerMutedCached(spId, *c->cfg);
    };

    static constexpr float kZeroVec[3]{};
    static constexpr float kDefFront[3]{0.0f, 1.0f, 0.0f};
    static constexpr float kDefTop[3]{0.0f, 0.0f, 1.0f};
    const float* camPos      = snap ? snap->camPos      : kZeroVec;
    const float* camFront    = snap ? snap->camFront    : kDefFront;
    const float* camTop      = snap ? snap->camTop      : kDefTop;
    const bool   hasCamera   = snap ? snap->hasCamera   : false;
    const float* localPedPos = snap ? snap->localPedPos : kZeroVec;
    const bool   hasLocalPed = snap ? snap->hasLocalPed : false;

    for (const auto& entry : tickStreamsScratch_) {
        const std::uint32_t id = entry.id;
        AudioStream* stream = entry.stream.get();
        if (!stream) {
            continue;
        }

        float resolvedPos[3]{};
        bool posValid = false;
        bool foundInSnap = false;
        if (snap && !snap->streamPositions.empty()) {
            const auto& vec = snap->streamPositions;
            const auto it = std::lower_bound(
                vec.begin(), vec.end(), id,
                [](const auto& pair, std::uint32_t key) { return pair.first < key; });
            if (it != vec.end() && it->first == id) {
                foundInSnap    = true;
                posValid       = it->second.valid;
                resolvedPos[0] = it->second.pos[0];
                resolvedPos[1] = it->second.pos[1];
                resolvedPos[2] = it->second.pos[2];
            }
        }
        if (!foundInSnap && stream->kind() == StreamKind::LocalAtPoint) {
            const auto p   = stream->position();
            resolvedPos[0] = p.x;
            resolvedPos[1] = p.y;
            resolvedPos[2] = p.z;
            posValid       = true;
        }

        streamSpeakersScratch_.clear();
        protocol::StreamIconId streamIcon = protocol::StreamIconId::Speaker;
        stream->tickAndCollectSpeakers(
            resolvedPos,
            posValid,
            camPos,
            camFront,
            camTop,
            hasCamera,
            localPedPos,
            hasLocalPed,
            masterVol,
            cfg.soundEnable,
            cfg.soundFilter,
            cfg.soundBalancer,
            muteCheck,
            &muteCtx,
            streamSpeakersScratch_,
            streamIcon);

        auto appendStreamTag = [](ActiveSpeakerInfo& target, std::size_t& tagCount,
                                  std::uint32_t color, std::string_view name) {
            if (tagCount < target.streams.size()) {
                target.streams[tagCount].color = color;
                target.streams[tagCount].name.assign(name);
            } else {
                target.streams.push_back(ActiveStreamTag{color, std::string(name)});
            }
            ++tagCount;
        };

        for (std::uint16_t spId : streamSpeakersScratch_) {
            const bool blocked = isSpeakerMutedCached(spId, cfg);
            std::size_t existingIdx = static_cast<std::size_t>(-1);
            if (spId < samp::kMaxPlayerSlots) {
                if (speakerIdxByIdScratch_[spId] >= 0) {
                    existingIdx = static_cast<std::size_t>(speakerIdxByIdScratch_[spId]);
                }
            } else if (auto exIt = speakerIdxOverflowScratch_.find(spId); exIt != speakerIdxOverflowScratch_.end()) {
                existingIdx = exIt->second;
            }

            if (existingIdx != static_cast<std::size_t>(-1)) {
                auto& dst = activeNowScratch_[existingIdx];
                appendStreamTag(dst, activeStreamCountsScratch_[existingIdx], stream->color(), stream->name());
                dst.blocked = dst.blocked || blocked;
            } else {
                const std::size_t newIdx = activeCount++;
                if (newIdx >= activeNowScratch_.size()) {
                    activeNowScratch_.emplace_back();
                    activeStreamCountsScratch_.push_back(0u);
                }
                auto& info = activeNowScratch_[newIdx];
                info.playerId    = spId;
                info.streamColor = stream->color();
                info.iconId      = streamIcon;
                info.blocked     = blocked;
                info.streamName.assign(stream->name());
                samp.copyCachedPlayerName(spId, info.playerName);
                if (info.playerName.empty()) {
                    char fallbackBuf[32]{};
                    std::snprintf(fallbackBuf, sizeof(fallbackBuf), "Player %u", static_cast<unsigned>(spId));
                    info.playerName.assign(fallbackBuf);
                }
                activeStreamCountsScratch_[newIdx] = 0u;
                appendStreamTag(info, activeStreamCountsScratch_[newIdx], stream->color(), stream->name());

                if (spId < samp::kMaxPlayerSlots) {
                    speakerIdxByIdScratch_[spId] = static_cast<std::int32_t>(newIdx);
                } else {
                    speakerIdxOverflowScratch_.emplace(spId, newIdx);
                }
            }
        }
    }

    for (std::size_t i = 0; i < activeCount; ++i) {
        activeNowScratch_[i].streams.resize(activeStreamCountsScratch_[i]);
    }

    tickStreamsScratch_.clear();

    if (listenerDisabled_.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(mutex_);
            speakerQueue_.clear();
            activeSpeakerCountAtomic_.store(0, std::memory_order_release);
        }
        nextPublishedScratch_.clear();
        publishSpeakerSnapshotIfChanged(nextPublishedScratch_);
        return;
    }

    {
        std::lock_guard lock(mutex_);
        speakerQueue_.reconcile(std::span<const ActiveSpeakerInfo>(activeNowScratch_.data(), activeCount));
        const auto& reconciled = speakerQueue_.all();
        nextPublishedScratch_.assign(reconciled.begin(), reconciled.end());
        activeSpeakerCountAtomic_.store(nextPublishedScratch_.size(), std::memory_order_release);
    }

    if (nextPublishedScratch_ != prevPublishedScratch_) {
        for (const auto& cur : nextPublishedScratch_) {
            const auto prevIt = std::find_if(prevPublishedScratch_.begin(), prevPublishedScratch_.end(),
                [&](const ActiveSpeakerInfo& p) { return p.playerId == cur.playerId; });
            if (prevIt == prevPublishedScratch_.end()) {
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
        for (const auto& prev : prevPublishedScratch_) {
            const bool stillActive = std::any_of(nextPublishedScratch_.begin(), nextPublishedScratch_.end(),
                [&](const ActiveSpeakerInfo& c) { return c.playerId == prev.playerId; });
            if (!stillActive) {
                SVPLUS_LOG_INF_CAT("audio", "Speaker ended: [%u] %s",
                                   static_cast<unsigned>(prev.playerId),
                                   prev.playerName.c_str());
            }
        }
    }

    publishSpeakerSnapshotIfChanged(nextPublishedScratch_);
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
    std::vector<std::shared_ptr<AudioStream>> streamsCopy;
    {
        std::lock_guard lock(mutex_);
        streamsCopy.reserve(orderedStreams_.size());
        for (const auto& entry : orderedStreams_) {
            if (entry.stream) {
                streamsCopy.push_back(entry.stream);
            }
        }
    }
    std::size_t total = 0;
    for (const auto& stream : streamsCopy) {
        total += stream->channelCount();
    }
    return total;
}

bool PlaybackEngine::hasStream(std::uint32_t streamId) const noexcept {
    std::lock_guard lock(mutex_);
    return streams_.find(streamId) != streams_.end();
}

void PlaybackEngine::publishSpeakerSnapshotIfChanged(const std::vector<ActiveSpeakerInfo>& speakers) {
    std::lock_guard snapLock(speakerSnapshotMutex_);
    if (publishedSpeakers_ && *publishedSpeakers_ == speakers) {
        return;
    }
    publishedSpeakers_ = std::make_shared<const std::vector<ActiveSpeakerInfo>>(speakers);
}

std::shared_ptr<const std::vector<ActiveSpeakerInfo>> PlaybackEngine::activeSpeakersSnapshot() const {
    std::lock_guard snapLock(speakerSnapshotMutex_);
    return publishedSpeakers_;
}

} // namespace svplus::client::audio
