#include "svplus/client/PlaybackEngine.hpp"

#include "svplus/common/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace svplus::client::audio {

namespace {

struct MuteCallbackCtx {
    PlaybackEngine*   engine;
    const CoreConfig* cfg;
    const IGameState* gameState;
    bool              deafened;
    std::uint32_t     nameGen;
    std::uint32_t     serverGen;
};

} // namespace

PlaybackEngine& PlaybackEngine::instance() noexcept {
    static auto* inst = new PlaybackEngine();
    return *inst;
}

void PlaybackEngine::setPlatform(const IBassLibrary* bass, const IGameState* gameState) noexcept {
    gameState_ = gameState;
    std::lock_guard lock(mutex_);
    bass_ = bass;
    for (const auto& entry : orderedStreams_) {
        if (entry.stream) {
            entry.stream->setBassLibrary(bass);
        }
    }
}

void PlaybackEngine::setDeafened(bool deafened, const char* origin) noexcept {
    const bool prev = deafened_.exchange(deafened, std::memory_order_acq_rel);
    if (prev != deafened) {
        SVPLUS_LOG_INF_CAT("audio", "Deafen %s (via %s)",
                           deafened ? "enabled" : "disabled",
                           (origin && origin[0] != '\0') ? origin : "unknown");
    }
}

void PlaybackEngine::createStream(std::uint32_t streamId,
                                  StreamKind kind,
                                  std::uint32_t target,
                                  float distance,
                                  protocol::Vec3f position,
                                  std::uint32_t color,
                                  std::string name) {
    const IBassLibrary* bassPtr = nullptr;
    {
        std::lock_guard lock(mutex_);
        bassPtr = bass_;
    }
    auto stream = std::make_shared<AudioStream>(
        streamId, kind, target, distance, position, color, name, bassPtr);
    const float loggedDistance = stream->distance();
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
                       streamId, static_cast<unsigned>(kind), target, loggedDistance, color, name.c_str());
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

void PlaybackEngine::updateStreamInfo(std::uint32_t streamId, std::uint32_t color, std::string_view name) {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    if (stream) {
        stream->setInfo(color, name);
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

const PlaybackEngine::MuteCacheEntry& PlaybackEngine::speakerStateCached(
    std::uint16_t speakerId,
    const CoreConfig& cfg,
    std::uint32_t nameGen,
    std::uint32_t serverGen,
    const IGameState* gameState) noexcept {
    static const MuteCacheEntry kDefaultEntry{};
    if (speakerId >= kMaxPlayerSlots) {
        return kDefaultEntry;
    }
    const std::uint32_t muteGen = cfg.muteGeneration;
    const std::uint32_t volGen  = cfg.volumeGeneration;
    auto& entry = muteCache_[speakerId];
    const bool needMuteRefresh   = (entry.nameGen != nameGen || entry.muteGen != muteGen || entry.serverGen != serverGen);
    const bool needVolumeRefresh = (entry.nameGen != nameGen || entry.volumeGen != volGen || entry.serverGen != serverGen);
    if (needMuteRefresh || needVolumeRefresh) {
        std::string epCopy;
        {
            std::lock_guard lk(serverMutex_);
            epCopy = currentServerEndpoint_;
        }
        std::string playerName;
        if (gameState) {
            gameState->copyCachedPlayerName(speakerId, playerName);
        }
        if (needMuteRefresh) {
            entry.muted   = cfg.isPlayerMuted(playerName, epCopy);
            entry.muteGen = muteGen;
        }
        if (needVolumeRefresh) {
            entry.volumeFactor = static_cast<float>(cfg.getPlayerVolumePct(playerName, epCopy)) / 100.0f;
            entry.volumeGen    = volGen;
        }
        entry.nameGen   = nameGen;
        entry.serverGen = serverGen;
    }
    return entry;
}

const PlaybackEngine::MuteCacheEntry& PlaybackEngine::speakerStateCached(
    std::uint16_t speakerId,
    const CoreConfig& cfg,
    std::uint32_t nameGen,
    const IGameState* gameState) noexcept {
    return speakerStateCached(speakerId, cfg, nameGen, serverGen_.load(std::memory_order_acquire), gameState);
}

const PlaybackEngine::MuteCacheEntry& PlaybackEngine::speakerStateCached(
    std::uint16_t speakerId,
    const CoreConfig& cfg) noexcept {
    const IGameState* gameState = gameState_;
    const std::uint32_t nameGen = gameState ? gameState->nameCacheGeneration() : 0u;
    return speakerStateCached(speakerId, cfg, nameGen, serverGen_.load(std::memory_order_acquire), gameState);
}

bool PlaybackEngine::isSpeakerMutedCached(std::uint16_t speakerId, const CoreConfig& cfg, std::uint32_t nameGen, std::uint32_t serverGen, const IGameState* gameState) noexcept {
    return speakerStateCached(speakerId, cfg, nameGen, serverGen, gameState).muted;
}

bool PlaybackEngine::isSpeakerMutedCached(std::uint16_t speakerId, const CoreConfig& cfg, std::uint32_t nameGen, const IGameState* gameState) noexcept {
    return speakerStateCached(speakerId, cfg, nameGen, gameState).muted;
}

bool PlaybackEngine::isSpeakerMutedCached(std::uint16_t speakerId, const CoreConfig& cfg) noexcept {
    return speakerStateCached(speakerId, cfg).muted;
}

float PlaybackEngine::speakerVolumeCached(std::uint16_t speakerId, const CoreConfig& cfg, std::uint32_t nameGen, std::uint32_t serverGen, const IGameState* gameState) noexcept {
    return speakerStateCached(speakerId, cfg, nameGen, serverGen, gameState).volumeFactor;
}

float PlaybackEngine::speakerVolumeCached(std::uint16_t speakerId, const CoreConfig& cfg, std::uint32_t nameGen, const IGameState* gameState) noexcept {
    return speakerStateCached(speakerId, cfg, nameGen, gameState).volumeFactor;
}

float PlaybackEngine::speakerVolumeCached(std::uint16_t speakerId, const CoreConfig& cfg) noexcept {
    return speakerStateCached(speakerId, cfg).volumeFactor;
}

void PlaybackEngine::onVoicePacket(std::uint32_t streamId,
                                   std::uint16_t speakerId,
                                   std::uint32_t seq,
                                   std::span<const std::uint8_t> opusPayload,
                                   const CoreConfig& cfg) {
    if (listenerDisabled_.load(std::memory_order_acquire)) {
        packetsIgnored_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    packetsReceived_.fetch_add(1, std::memory_order_relaxed);

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

    const float masterVol = static_cast<float>(std::clamp(cfg.soundVolume, 0, 100)) / 100.0f;
    const int channelPct = (stream->kind() == StreamKind::Global)
        ? std::clamp(cfg.globalVolume, 0, kMaxChannelVolume)
        : std::clamp(cfg.localVolume, 0, kMaxChannelVolume);
    const float effectiveMasterVol = masterVol * (static_cast<float>(channelPct) / 100.0f);

    const auto& spState = speakerStateCached(speakerId, cfg);
    const bool muted = spState.muted || deafened_.load(std::memory_order_acquire);
    if (muted) {
        packetsIgnored_.fetch_add(1, std::memory_order_relaxed);
    }

    stream->setDropCounter(&packetsDropped_);
    stream->pushVoiceFrame(
        speakerId,
        seq,
        opusPayload,
        effectiveMasterVol,
        spState.volumeFactor,
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

void PlaybackEngine::shutdown() noexcept {
    if (isProcessShuttingDown()) {
        return;
    }
    resetAll();
    deafened_.store(false, std::memory_order_release);
    occlusionStates_.clear();
    occlusionCursor_ = 0;
    packetsReceived_.store(0, std::memory_order_release);
    packetsDropped_.store(0, std::memory_order_release);
    packetsIgnored_.store(0, std::memory_order_release);
    setCurrentServerEndpoint({});
    setPlatform(nullptr, nullptr);
}

void PlaybackEngine::updateSpatialFromMainThread(
    bool voiceOcclusionEnabled,
    const SpatialLosProbe* envProbe,
    std::chrono::steady_clock::time_point nowOverride) {
    const IGameState* gameState = gameState_;
    const auto now = (nowOverride.time_since_epoch().count() != 0)
        ? nowOverride
        : std::chrono::steady_clock::now();

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
    for (auto& stream : spatialStreamsScratch_) {
        spatialQueriesScratch_.push_back(TargetQuery{
            stream->id(),
            stream->kind(),
            stream->target(),
            stream->distance(),
            stream->hasActiveSpeakersNow(),
            stream->position(),
            std::move(stream)
        });
    }
    spatialStreamsScratch_.clear();
    std::sort(spatialQueriesScratch_.begin(), spatialQueriesScratch_.end(),
              [](const TargetQuery& a, const TargetQuery& b) { return a.id < b.id; });

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
    if (envProbe && envProbe->getCameraBasis) {
        nextSnap->hasCamera = envProbe->getCameraBasis(nextSnap->camPos, nextSnap->camFront, nextSnap->camTop, envProbe->ctx);
    } else if (gameState) {
        nextSnap->hasCamera = gameState->getCameraBasis(nextSnap->camPos, nextSnap->camFront, nextSnap->camTop);
    } else {
        nextSnap->hasCamera = false;
    }
    if (nextSnap->hasCamera) {
        if (envProbe && envProbe->getLocalPedPosition) {
            nextSnap->hasLocalPed = envProbe->getLocalPedPosition(nextSnap->localPedPos, envProbe->ctx);
        } else if (gameState) {
            nextSnap->hasLocalPed = gameState->getLocalPedPosition(nextSnap->localPedPos);
        } else {
            nextSnap->hasLocalPed = false;
        }
    } else {
        nextSnap->hasLocalPed = false;
    }

    for (const auto& q : spatialQueriesScratch_) {
        CachedPos cp{};
        if (envProbe && envProbe->resolveStreamPos) {
            cp.valid = envProbe->resolveStreamPos(q.id, q.kind, q.target, q.staticPos, cp.pos, envProbe->ctx);
        } else {
            switch (q.kind) {
                case StreamKind::LocalAtPoint:
                    cp.pos[0] = q.staticPos.x;
                    cp.pos[1] = q.staticPos.y;
                    cp.pos[2] = q.staticPos.z;
                    cp.valid  = true;
                    break;
                case StreamKind::LocalAtPlayer:
                    cp.valid = gameState && gameState->getPlayerPosition(static_cast<std::uint16_t>(q.target), cp.pos);
                    break;
                case StreamKind::LocalAtVehicle:
                    cp.valid = gameState && gameState->getVehiclePosition(static_cast<std::uint16_t>(q.target), cp.pos);
                    break;
                case StreamKind::LocalAtObject:
                    cp.valid = gameState && gameState->getObjectPosition(static_cast<std::uint16_t>(q.target), cp.pos);
                    break;
                case StreamKind::Global:
                default:
                    break;
            }
        }
        nextSnap->streamPositions.emplace_back(q.id, cp);
    }

    const bool occlusionSupported = (envProbe != nullptr) || (gameState && gameState->hasOcclusion());
    if (!voiceOcclusionEnabled || !occlusionSupported) {
        occlusionStates_.clear();
        occlusionCursor_ = 0;
        for (const auto& q : spatialQueriesScratch_) {
            if (q.stream) {
                q.stream->setLatestOccluded(false);
            }
        }
    } else {
        // Prune occlusion tracking entries for streams that no longer exist
        for (auto it = occlusionStates_.begin(); it != occlusionStates_.end();) {
            const std::uint32_t trackedId = it->first;
            const auto foundIt = std::lower_bound(
                spatialQueriesScratch_.begin(), spatialQueriesScratch_.end(), trackedId,
                [](const TargetQuery& q, std::uint32_t key) { return q.id < key; });
            const bool exists = (foundIt != spatialQueriesScratch_.end() && foundIt->id == trackedId);
            if (!exists) {
                it = occlusionStates_.erase(it);
            } else {
                ++it;
            }
        }

        // Determine listener head (ped + ~0.7m if <= 20m from camera, else camera) and world range ref
        constexpr float kMaxPedCameraDistSq = 20.0f * 20.0f;
        const bool pedFinite = nextSnap->hasLocalPed &&
            std::isfinite(nextSnap->localPedPos[0]) &&
            std::isfinite(nextSnap->localPedPos[1]) &&
            std::isfinite(nextSnap->localPedPos[2]);
        bool usePedPos = false;
        if (nextSnap->hasCamera && pedFinite) {
            const float cdx = nextSnap->camPos[0] - nextSnap->localPedPos[0];
            const float cdy = nextSnap->camPos[1] - nextSnap->localPedPos[1];
            const float cdz = nextSnap->camPos[2] - nextSnap->localPedPos[2];
            usePedPos = (cdx * cdx + cdy * cdy + cdz * cdz) <= kMaxPedCameraDistSq;
        }

        const bool hasListenerPoint = usePedPos || nextSnap->hasCamera;
        float distRefPos[3]{};
        float listenerHeadPos[3]{};
        if (usePedPos) {
            distRefPos[0]      = nextSnap->localPedPos[0];
            distRefPos[1]      = nextSnap->localPedPos[1];
            distRefPos[2]      = nextSnap->localPedPos[2];
            listenerHeadPos[0] = nextSnap->localPedPos[0];
            listenerHeadPos[1] = nextSnap->localPedPos[1];
            listenerHeadPos[2] = nextSnap->localPedPos[2] + kOcclusionPedHeadOffsetZ;
        } else if (nextSnap->hasCamera) {
            distRefPos[0]      = nextSnap->camPos[0];
            distRefPos[1]      = nextSnap->camPos[1];
            distRefPos[2]      = nextSnap->camPos[2];
            listenerHeadPos[0] = nextSnap->camPos[0];
            listenerHeadPos[1] = nextSnap->camPos[1];
            listenerHeadPos[2] = nextSnap->camPos[2];
        }

        const std::size_t n = spatialQueriesScratch_.size();
        if (n > 0 && hasListenerPoint) {
            const std::size_t startIdx = occlusionCursor_ % n;
            std::size_t checksThisFrame = 0;

            for (std::size_t step = 0; step < n && checksThisFrame < kMaxOcclusionChecksPerFrame; ++step) {
                const std::size_t idx = (startIdx + step) % n;
                const auto& q = spatialQueriesScratch_[idx];
                auto& cp = nextSnap->streamPositions[idx].second;
                if (!cp.valid || !q.hasActiveSpeakers) {
                    continue;
                }

                const float maxDist = (std::max)(1.0f, q.distance);
                const float dx = cp.pos[0] - distRefPos[0];
                const float dy = cp.pos[1] - distRefPos[1];
                const float dz = cp.pos[2] - distRefPos[2];
                if ((dx * dx + dy * dy + dz * dz) >= (maxDist * maxDist)) {
                    continue;
                }

                OcclusionTrackState& st = occlusionStates_[q.id];
                if (q.kind == StreamKind::LocalAtVehicle) {
                    const bool inSameVehicle = (envProbe && envProbe->isLocalInVehicle)
                        ? envProbe->isLocalInVehicle(static_cast<std::uint16_t>(q.target), envProbe->ctx)
                        : (envProbe ? false : (gameState && gameState->isLocalPlayerInVehicle(static_cast<std::uint16_t>(q.target))));
                    if (inSameVehicle) {
                        st.occluded  = false;
                        st.sameCount = 0;
                        occlusionCursor_ = (idx + 1u) % n;
                        continue;
                    }
                }

                const bool hasPrevCheck = (st.lastCheckTime.time_since_epoch().count() != 0);
                const bool intervalDue  = !hasPrevCheck || (now - st.lastCheckTime) >= kOcclusionCheckInterval;
                if (!intervalDue) {
                    continue;
                }

                const bool isStale = !hasPrevCheck || (now - st.lastCheckTime) > kOcclusionStaleAfter;
                occlusionCursor_ = (idx + 1u) % n;
                ++checksThisFrame;
                st.lastCheckTime = now;

                float sourcePos[3]{cp.pos[0], cp.pos[1], cp.pos[2]};
                if (q.kind == StreamKind::LocalAtPlayer) {
                    sourcePos[2] += kOcclusionPedHeadOffsetZ;
                } else if (q.kind == StreamKind::LocalAtVehicle) {
                    sourcePos[2] += kOcclusionVehicleOffsetZ;
                }

                const void* ignoreEntity = nullptr;
                bool skipLosClear = false;
                if (q.kind == StreamKind::LocalAtObject || q.kind == StreamKind::LocalAtPoint) {
                    if (q.kind == StreamKind::LocalAtObject) {
                        ignoreEntity = (envProbe && envProbe->resolveObjectEntity)
                            ? envProbe->resolveObjectEntity(static_cast<std::uint16_t>(q.target), envProbe->ctx)
                            : (envProbe ? nullptr : (gameState ? gameState->getObjectEntity(static_cast<std::uint16_t>(q.target)) : nullptr));
                    }
                    const float vx = sourcePos[0] - listenerHeadPos[0];
                    const float vy = sourcePos[1] - listenerHeadPos[1];
                    const float vz = sourcePos[2] - listenerHeadPos[2];
                    const float losDistSq = vx * vx + vy * vy + vz * vz;
                    if (!std::isfinite(losDistSq) || losDistSq <= (kOcclusionSourceMarginM * kOcclusionSourceMarginM)) {
                        skipLosClear = true;
                    } else {
                        const float losDist = std::sqrt(losDistSq);
                        const float ratio = kOcclusionSourceMarginM / losDist;
                        sourcePos[0] -= vx * ratio;
                        sourcePos[1] -= vy * ratio;
                        sourcePos[2] -= vz * ratio;
                    }
                }

                const bool losClear = skipLosClear
                    ? true
                    : ((envProbe && envProbe->checkLineOfSightClear)
                        ? envProbe->checkLineOfSightClear(listenerHeadPos, sourcePos, ignoreEntity, envProbe->ctx)
                        : (envProbe ? true : (!gameState || gameState->isLineOfSightClear(listenerHeadPos, sourcePos, ignoreEntity))));
                const bool obsOccluded = !losClear;

                if (isStale) {
                    st.occluded  = obsOccluded;
                    st.sameCount = 0;
                } else if (obsOccluded == st.occluded) {
                    st.sameCount = 0;
                } else {
                    if (st.sameCount == 0 || st.pendingCandidate != obsOccluded) {
                        st.pendingCandidate = obsOccluded;
                        st.sameCount = 1u;
                    } else {
                        ++st.sameCount;
                        if (st.sameCount >= kOcclusionHysteresisCount) {
                            st.occluded  = obsOccluded;
                            st.sameCount = 0;
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < n; ++i) {
                const auto it = occlusionStates_.find(spatialQueriesScratch_[i].id);
                const bool occ = (it != occlusionStates_.end()) ? it->second.occluded : false;
                if (spatialQueriesScratch_[i].stream) {
                    spatialQueriesScratch_[i].stream->setLatestOccluded(occ);
                }
            }
        } else {
            for (const auto& q : spatialQueriesScratch_) {
                if (q.stream) {
                    q.stream->setLatestOccluded(false);
                }
            }
        }
    }
    spatialQueriesScratch_.clear();

    std::lock_guard spLock(spatialMutex_);
    publishedSpatial_ = std::move(nextSnap);
}

bool PlaybackEngine::isStreamOccludedSnapshot(std::uint32_t streamId) const noexcept {
    std::shared_ptr<AudioStream> stream;
    {
        std::lock_guard lock(mutex_);
        if (const auto it = streams_.find(streamId); it != streams_.end()) {
            stream = it->second;
        }
    }
    return stream ? stream->isLatestOccluded() : false;
}

void PlaybackEngine::tick(const CoreConfig& cfg, bool /*voiceOcclusionEnabled*/) {
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

    std::shared_ptr<const SpatialSnapshot> snap;
    {
        std::lock_guard spLock(spatialMutex_);
        snap = publishedSpatial_;
    }

    const float masterVol = static_cast<float>(std::clamp(cfg.soundVolume, 0, 100)) / 100.0f;
    const float localChanVol  = masterVol * (static_cast<float>(std::clamp(cfg.localVolume, 0, kMaxChannelVolume)) / 100.0f);
    const float globalChanVol = masterVol * (static_cast<float>(std::clamp(cfg.globalVolume, 0, kMaxChannelVolume)) / 100.0f);
    const bool deafened = deafened_.load(std::memory_order_acquire);

    tickStreamsScratch_.clear();
    {
        std::lock_guard lock(mutex_);
        tickStreamsScratch_.assign(orderedStreams_.begin(), orderedStreams_.end());
        prevPublishedScratch_.assign(speakerQueue_.all().begin(), speakerQueue_.all().end());
    }

    speakerIdxByIdScratch_.fill(-1);
    speakerIdxOverflowScratch_.clear();
    std::size_t activeCount = 0;

    const IGameState* tickGameState = gameState_;
    const std::uint32_t tickNameGen   = tickGameState ? tickGameState->nameCacheGeneration() : 0u;
    const std::uint32_t tickServerGen = serverGen_.load(std::memory_order_acquire);
    MuteCallbackCtx muteCtx{this, &cfg, tickGameState, deafened, tickNameGen, tickServerGen};
    auto muteCheck = [](std::uint16_t spId, void* ctx) noexcept -> bool {
        auto* c = static_cast<MuteCallbackCtx*>(ctx);
        return c->deafened || c->engine->isSpeakerMutedCached(spId, *c->cfg, c->nameGen, c->serverGen, c->gameState);
    };
    auto volLookup = [](std::uint16_t spId, void* ctx) noexcept -> float {
        auto* c = static_cast<MuteCallbackCtx*>(ctx);
        return c->engine->speakerVolumeCached(spId, *c->cfg, c->nameGen, c->serverGen, c->gameState);
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
        streamNameScratch_.clear();
        protocol::StreamIconId streamIcon = protocol::StreamIconId::Speaker;
        std::uint32_t streamColor = 0u;
        const float effectiveMasterVol = (stream->kind() == StreamKind::Global) ? globalChanVol : localChanVol;
        stream->tickAndCollectSpeakers(
            resolvedPos,
            posValid,
            camPos,
            camFront,
            camTop,
            hasCamera,
            localPedPos,
            hasLocalPed,
            effectiveMasterVol,
            cfg.soundEnable,
            cfg.soundFilter,
            cfg.soundBalancer,
            muteCheck,
            &muteCtx,
            streamSpeakersScratch_,
            streamIcon,
            streamColor,
            streamNameScratch_,
            volLookup);

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
            const bool blocked = isSpeakerMutedCached(spId, cfg, tickNameGen, tickServerGen, tickGameState);
            std::size_t existingIdx = static_cast<std::size_t>(-1);
            if (spId < kMaxPlayerSlots) {
                if (speakerIdxByIdScratch_[spId] >= 0) {
                    existingIdx = static_cast<std::size_t>(speakerIdxByIdScratch_[spId]);
                }
            } else if (auto exIt = speakerIdxOverflowScratch_.find(spId); exIt != speakerIdxOverflowScratch_.end()) {
                existingIdx = exIt->second;
            }

            if (existingIdx != static_cast<std::size_t>(-1)) {
                auto& dst = activeNowScratch_[existingIdx];
                appendStreamTag(dst, activeStreamCountsScratch_[existingIdx], streamColor, streamNameScratch_);
                dst.blocked = dst.blocked || blocked;
            } else {
                const std::size_t newIdx = activeCount++;
                if (newIdx >= activeNowScratch_.size()) {
                    activeNowScratch_.emplace_back();
                    activeStreamCountsScratch_.push_back(0u);
                }
                auto& info = activeNowScratch_[newIdx];
                info.playerId    = spId;
                info.streamColor = streamColor;
                info.iconId      = streamIcon;
                info.blocked     = blocked;
                info.streamName.assign(streamNameScratch_);
                if (tickGameState) {
                    tickGameState->copyCachedPlayerName(spId, info.playerName);
                } else {
                    info.playerName.clear();
                }
                if (info.playerName.empty()) {
                    char fallbackBuf[32]{};
                    std::snprintf(fallbackBuf, sizeof(fallbackBuf), "Player %u", static_cast<unsigned>(spId));
                    info.playerName.assign(fallbackBuf);
                }
                activeStreamCountsScratch_[newIdx] = 0u;
                appendStreamTag(info, activeStreamCountsScratch_[newIdx], streamColor, streamNameScratch_);

                if (spId < kMaxPlayerSlots) {
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
        .packetsIgnored     = packetsIgnored_.load(std::memory_order_relaxed),
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
    speakersGeneration_.fetch_add(1, std::memory_order_release);
}

std::shared_ptr<const std::vector<ActiveSpeakerInfo>> PlaybackEngine::activeSpeakersSnapshot() const {
    std::lock_guard snapLock(speakerSnapshotMutex_);
    return publishedSpeakers_;
}

} // namespace svplus::client::audio
