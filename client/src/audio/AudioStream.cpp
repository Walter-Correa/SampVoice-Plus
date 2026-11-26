#include "audio/AudioStream.hpp"
#include "ShutdownState.hpp"
#include "svplus/common/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace svplus::client::audio {
namespace {

[[nodiscard]] float measureQueuedMs(const BassApi& bass, HSTREAM handle) noexcept {
    if (!handle || !bass.ChannelGetData) {
        return 0.0f;
    }
    const DWORD availBytes = bass.ChannelGetData(handle, nullptr, BASS_DATA_AVAILABLE);
    if (availBytes == DWORD(-1)) {
        return 0.0f;
    }
    constexpr float kBytesPerMs =
        static_cast<float>(protocol::kFrequency) * static_cast<float>(sizeof(std::int16_t)) / 1000.0f;
    return static_cast<float>(availBytes) / kBytesPerMs;
}

bool mapParameterAttribute(std::uint32_t paramId, DWORD& outAttrib, float& outDefault) noexcept {
    using PT = protocol::ParameterType;
    switch (static_cast<PT>(paramId)) {
        case PT::Frequency: outAttrib = BASS_ATTRIB_FREQ;    outDefault = static_cast<float>(protocol::kFrequency); return true;
        case PT::Volume:    outAttrib = BASS_ATTRIB_VOL;     outDefault = 1.0f; return true;
        case PT::Panning:   outAttrib = BASS_ATTRIB_PAN;     outDefault = 0.0f; return true;
        case PT::Src:       outAttrib = BASS_ATTRIB_SRC;     outDefault = 1.0f; return true;
        case PT::EaxMix:
        default:            return false;
    }
}

void applyDx8Parameters(HFX fx, std::uint32_t effectType, const std::vector<std::uint8_t>& raw) noexcept {
    const auto& bass = BassLoader::instance().api();
    if (!bass.FXSetParameters || fx == 0) {
        return;
    }
    using ET = protocol::EffectType;
    switch (static_cast<ET>(effectType)) {
        case ET::Chorus:
            if (raw.size() >= sizeof(BASS_DX8_CHORUS)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::Compressor:
            if (raw.size() >= sizeof(BASS_DX8_COMPRESSOR)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::Distortion:
            if (raw.size() >= sizeof(BASS_DX8_DISTORTION)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::Echo: {
            BASS_DX8_ECHO echo{};
            if (raw.size() >= sizeof(protocol::EchoParams)) {
                protocol::EchoParams p{};
                std::memcpy(&p, raw.data(), sizeof(p));
                echo.fWetDryMix  = p.wetDryMix;
                echo.fFeedback   = p.feedback;
                echo.fLeftDelay  = p.leftDelay;
                echo.fRightDelay = p.rightDelay;
                echo.lPanDelay   = p.panDelay ? TRUE : FALSE;
                bass.FXSetParameters(fx, &echo);
            } else if (raw.size() == 17u) {
                // Legacy CyberMor v3.1 wire packet had 1-byte bool panDelay after 4 floats
                std::memcpy(&echo.fWetDryMix, raw.data(), 16u);
                echo.lPanDelay = raw[16] ? TRUE : FALSE;
                bass.FXSetParameters(fx, &echo);
            }
            break;
        }
        case ET::Flanger:
            if (raw.size() >= sizeof(BASS_DX8_FLANGER)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::Gargle:
            if (raw.size() >= sizeof(BASS_DX8_GARGLE)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::I3dl2Reverb:
            if (raw.size() >= sizeof(BASS_DX8_I3DL2REVERB)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::ParamEq:
            if (raw.size() >= sizeof(BASS_DX8_PARAMEQ)) bass.FXSetParameters(fx, raw.data());
            break;
        case ET::Reverb:
            if (raw.size() >= sizeof(BASS_DX8_REVERB)) bass.FXSetParameters(fx, raw.data());
            break;
        default:
            break;
    }
}

DWORD mapBassFxType(std::uint32_t effectType, bool& valid) noexcept {
    using ET = protocol::EffectType;
    valid = true;
    switch (static_cast<ET>(effectType)) {
        case ET::Chorus:      return BASS_FX_DX8_CHORUS;
        case ET::Compressor:  return BASS_FX_DX8_COMPRESSOR;
        case ET::Distortion:  return BASS_FX_DX8_DISTORTION;
        case ET::Echo:        return BASS_FX_DX8_ECHO;
        case ET::Flanger:     return BASS_FX_DX8_FLANGER;
        case ET::Gargle:      return BASS_FX_DX8_GARGLE;
        case ET::I3dl2Reverb: return BASS_FX_DX8_I3DL2REVERB;
        case ET::ParamEq:     return BASS_FX_DX8_PARAMEQ;
        case ET::Reverb:      return BASS_FX_DX8_REVERB;
        default:              valid = false; return 0;
    }
}

/// Persistent per-speaker speech clarity DSP (1st-order 80 Hz high-pass + smooth RMS compressor).
void processAudioDsp(SpeakerChannel& ch,
                     std::span<std::int16_t> pcm,
                     bool applyFilter,
                     bool applyBalancer,
                     bool targetOccluded) noexcept {
    const bool occlusionActive = targetOccluded || (ch.occFilterProgress > 0.0f);
    if (pcm.empty() || (!applyFilter && !applyBalancer && !occlusionActive)) {
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPiOverFs = (2.0f * kPi) / kOcclusionSampleRateHz;
    constexpr float kTransitionSamples = (kOcclusionSampleRateHz * kOcclusionTransitionMs) * 0.001f;
    constexpr float kProgressStep = (kTransitionSamples > 0.0f) ? (1.0f / kTransitionSamples) : 1.0f;
    const float kLogCutoffRatio = std::log(kOcclusionCutoffHz / kOcclusionClearCutoffHz);

    for (auto& sample : pcm) {
        float x = static_cast<float>(sample);
        if (applyFilter) {
            const float y = x - ch.hpPrevX + 0.9896f * ch.hpPrevY;
            ch.hpPrevX = x;
            ch.hpPrevY = y;
            x = y;
        }
        if (applyBalancer) {
            const float absX = std::fabs(x) / 32768.0f;
            const float coeff = (absX > ch.env) ? 0.02f : 0.0005f;
            ch.env += coeff * (absX - ch.env);
            constexpr float targetRms = 0.25f;
            const float gain = (ch.env > 0.04f)
                ? std::clamp(targetRms / (ch.env + 0.05f), 0.5f, 2.8f)
                : 1.0f;
            x *= gain;
        }
        if (targetOccluded || ch.occFilterProgress > 0.0f) {
            if (targetOccluded) {
                ch.occFilterProgress = (std::min)(1.0f, ch.occFilterProgress + kProgressStep);
            } else {
                ch.occFilterProgress = (std::max)(0.0f, ch.occFilterProgress - kProgressStep);
            }

            if (ch.occFilterProgress > 0.0f) {
                const float fc = kOcclusionClearCutoffHz * std::exp(ch.occFilterProgress * kLogCutoffRatio);
                const float alpha = 1.0f - std::exp(-kTwoPiOverFs * (fc * kOcclusionCascadeScale));
                if (!ch.occLpInitialized) {
                    ch.occLpState1 = x;
                    ch.occLpState2 = x;
                    ch.occLpInitialized = true;
                }
                ch.occLpState1 += alpha * (x - ch.occLpState1);
                ch.occLpState2 += alpha * (ch.occLpState1 - ch.occLpState2);
                x = ch.occLpState2;
            } else {
                ch.occLpInitialized = false;
                ch.occLpState1 = 0.0f;
                ch.occLpState2 = 0.0f;
            }
        }
        sample = static_cast<std::int16_t>(std::clamp(x, -32767.0f, 32767.0f));
    }
}

} // namespace

void processChannelAudioDsp(SpeakerChannel& ch,
                            std::span<std::int16_t> pcm,
                            bool applyFilter,
                            bool applyBalancer,
                            bool targetOccluded) noexcept {
    processAudioDsp(ch, pcm, applyFilter, applyBalancer, targetOccluded);
}

float advanceChannelOcclusionGain(SpeakerChannel& ch,
                                  bool targetOccluded,
                                  std::chrono::steady_clock::time_point now) noexcept {
    const float targetGain = targetOccluded ? kOcclusionVolumeGain : 1.0f;
    if (ch.lastOccTickTime.time_since_epoch().count() == 0) {
        ch.lastOccTickTime = now;
        return std::clamp(ch.occGainFactor, kOcclusionVolumeGain, 1.0f);
    }
    if (now > ch.lastOccTickTime) {
        const float dtMs = std::chrono::duration<float, std::milli>(now - ch.lastOccTickTime).count();
        ch.lastOccTickTime = now;
        const float maxStep = (kOcclusionTransitionMs > 0.0f)
            ? ((1.0f - kOcclusionVolumeGain) * (dtMs / kOcclusionTransitionMs))
            : (1.0f - kOcclusionVolumeGain);
        if (ch.occGainFactor < targetGain) {
            ch.occGainFactor = (std::min)(targetGain, ch.occGainFactor + maxStep);
        } else if (ch.occGainFactor > targetGain) {
            ch.occGainFactor = (std::max)(targetGain, ch.occGainFactor - maxStep);
        }
    }
    ch.occGainFactor = std::clamp(ch.occGainFactor, kOcclusionVolumeGain, 1.0f);
    return ch.occGainFactor;
}

void StreamParameterState::slide(float startVal, float endVal, std::uint32_t timeMs) noexcept {
    if (timeMs == 0) {
        set(endVal);
        return;
    }
    value     = startVal;
    endValue  = endVal;
    ratePerMs = (endVal - startVal) / static_cast<float>(timeMs);
    endTime   = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeMs);
    sliding   = true;
}

float StreamParameterState::current() noexcept {
    if (!sliding) {
        return value;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= endTime) {
        value   = endValue;
        sliding = false;
        return value;
    }
    const float remainingMs = std::chrono::duration<float, std::milli>(endTime - now).count();
    return endValue - remainingMs * ratePerMs;
}

AudioStream::AudioStream(std::uint32_t id,
                         StreamKind kind,
                         std::uint32_t targetOrZero,
                         float distance,
                         protocol::Vec3f position,
                         std::uint32_t color,
                         std::string name)
    : id_(id)
    , kind_(kind)
    , target_(targetOrZero)
    , distance_(distance)
    , position_(position)
    , color_(color)
    , name_(std::move(name))
    , cachedSpatial_(kind == StreamKind::Global ? SpatialAttenuation{1.0f, 0.0f} : SpatialAttenuation{0.0f, 0.0f}) {}

AudioStream::~AudioStream() {
    if (isProcessShuttingDown()) {
        channels_.clear();
        return;
    }
    const auto& bass = BassLoader::instance().api();
    for (auto& [pid, ch] : channels_) {
        (void)pid;
        if (ch && ch->bassStream && bass.StreamFree) {
            bass.StreamFree(ch->bassStream);
        }
    }
    channels_.clear();
}

float AudioStream::distance() const noexcept {
    std::lock_guard lock(streamMutex_);
    return distance_;
}

protocol::Vec3f AudioStream::position() const noexcept {
    std::lock_guard lock(streamMutex_);
    return position_;
}

protocol::StreamIconId AudioStream::iconId() const noexcept {
    std::lock_guard lock(streamMutex_);
    return iconId_;
}

void AudioStream::setDistance(float d) noexcept {
    std::lock_guard lock(streamMutex_);
    distance_ = d;
}

void AudioStream::setIconId(protocol::StreamIconId iconId) noexcept {
    std::lock_guard lock(streamMutex_);
    iconId_ = iconId;
}

AudioStream::SpatialAttenuation AudioStream::computeSpatialAttenuation(
    StreamKind kind,
    float streamDistance,
    const float sourcePos[3],
    bool sourcePosValid,
    const float camPos[3],
    const float camFront[3],
    const float camTop[3],
    bool hasCamera,
    const float localPedPos[3],
    bool hasLocalPed) noexcept {
    if (kind == StreamKind::Global) {
        return {1.0f, 0.0f};
    }
    if (!sourcePosValid || !hasCamera || !sourcePos || !camPos || !camFront || !camTop) {
        return {0.0f, 0.0f};
    }

    // Distance and source direction originate from the local ped when available and <= 20 m
    // from the camera; free-cam / spectator views (> 20 m) fall back to the camera position.
    constexpr float kMaxPedCameraDistSq = 20.0f * 20.0f;
    bool usePedPos = hasLocalPed && localPedPos &&
        std::isfinite(localPedPos[0]) && std::isfinite(localPedPos[1]) && std::isfinite(localPedPos[2]);
    if (usePedPos) {
        const float cdx = camPos[0] - localPedPos[0];
        const float cdy = camPos[1] - localPedPos[1];
        const float cdz = camPos[2] - localPedPos[2];
        usePedPos = (cdx * cdx + cdy * cdy + cdz * cdz) <= kMaxPedCameraDistSq;
    }
    const float* const listenerPos = usePedPos ? localPedPos : camPos;

    const float dx = sourcePos[0] - listenerPos[0];
    const float dy = sourcePos[1] - listenerPos[1];
    const float dz = sourcePos[2] - listenerPos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    const float maxDist = (std::max)(1.0f, streamDistance);
    const float minDist = maxDist * 0.20f;
    if (dist >= maxDist) {
        return {0.0f, 0.0f};
    }

    float gain = 1.0f;
    if (dist > minDist) {
        const float ratio = (maxDist - dist) / (maxDist - minDist);
        gain = ratio * std::sqrt(ratio); // ((max - d) / (max - min))^1.5
    }

    // In GTA SA / RenderWare right-handed camera basis: front=(0,1,0), top=(0,0,1) => front x top = (1,0,0) right.
    const float rx = camFront[1] * camTop[2] - camFront[2] * camTop[1];
    const float ry = camFront[2] * camTop[0] - camFront[0] * camTop[2];
    const float rz = camFront[0] * camTop[1] - camFront[1] * camTop[0];
    const float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);

    constexpr float kMaxSpatialPan = 0.5f;
    constexpr float kPanProximityMeters = 3.0f;
    float pan = 0.0f;
    if (dist > 1e-4f && rLen > 1e-4f) {
        const float invDist = 1.0f / dist;
        const float invR    = 1.0f / rLen;
        const float dotRight =
            (dx * invDist) * (rx * invR) +
            (dy * invDist) * (ry * invR) +
            (dz * invDist) * (rz * invR);
        pan = kMaxSpatialPan * std::clamp(dotRight, -1.0f, 1.0f);
        pan *= std::clamp(dist / kPanProximityMeters, 0.0f, 1.0f);
    }

    return {gain, pan};
}

void AudioStream::setPosition(protocol::Vec3f pos) noexcept {
    std::lock_guard lock(streamMutex_);
    position_ = pos;
}

void AudioStream::clearChannels() noexcept {
    std::lock_guard lock(streamMutex_);
    const auto& bass = BassLoader::instance().api();
    for (auto& [pid, ch] : channels_) {
        (void)pid;
        if (!ch || !ch->bassStream) {
            continue;
        }
        if (bass.ChannelRemoveFX) {
            for (HFX fx : ch->effects) {
                bass.ChannelRemoveFX(ch->bassStream, fx);
            }
        }
        if (bass.StreamFree) {
            bass.StreamFree(ch->bassStream);
        }
        ch->bassStream = 0;
    }
    channels_.clear();
    activeSpeakersLastSeen_.clear();
    hasActiveSpeakers_.store(false, std::memory_order_release);
}

void AudioStream::setParameter(std::uint32_t paramId, float value) noexcept {
    std::lock_guard lock(streamMutex_);
    params_[paramId].set(value);
}

void AudioStream::slideParameter(std::uint32_t paramId,
                                 float startVal,
                                 float endVal,
                                 std::uint32_t timeMs) noexcept {
    std::lock_guard lock(streamMutex_);
    params_[paramId].slide(startVal, endVal, timeMs);
}

void AudioStream::addOrUpdateEffect(const StreamEffectSpec& spec) {
    std::lock_guard lock(streamMutex_);
    effectSpecs_[spec.effectId] = spec;
    for (auto& [pid, ch] : channels_) {
        (void)pid;
        if (ch) {
            rebuildChannelEffects(*ch);
        }
    }
}

void AudioStream::removeEffect(std::uint32_t effectId) {
    std::lock_guard lock(streamMutex_);
    effectSpecs_.erase(effectId);
    for (auto& [pid, ch] : channels_) {
        (void)pid;
        if (ch) {
            rebuildChannelEffects(*ch);
        }
    }
}

SpeakerChannel* AudioStream::ensureChannel(std::uint16_t speakerId) {
    auto it = channels_.find(speakerId);
    if (it != channels_.end()) {
        return it->second.get();
    }

    const auto& bass = BassLoader::instance();
    if (!bass.isLoaded() || !bass.isAudioReady() || !bass.api().StreamCreate) {
        return nullptr;
    }

    auto ch = std::make_unique<SpeakerChannel>();
    ch->playerId = speakerId;
    if (!ch->decoder.initialize()) {
        return nullptr;
    }

    // Scope BASS_CONFIG_BUFFER strictly around voice stream creation so SA-MP's global audio buffer stays intact.
    // Voice channels omit BASS_SAMPLE_3D and use software distance/pan so SA-MP's 3D audio stays untouched.
    DWORD prevBufferMs = DWORD(-1);
    bool restoredBuffer = false;
    if (bass.api().GetConfig && bass.api().SetConfig) {
        prevBufferMs = bass.api().GetConfig(BASS_CONFIG_BUFFER);
        if (prevBufferMs != DWORD(-1) && prevBufferMs != 200u) {
            restoredBuffer = (bass.api().SetConfig(BASS_CONFIG_BUFFER, 200u) != FALSE);
        }
    }

    ch->bassStream = bass.api().StreamCreate(
        static_cast<DWORD>(protocol::kFrequency),
        1,
        0,
        STREAMPROC_PUSH,
        nullptr);

    if (restoredBuffer && prevBufferMs != DWORD(-1) && bass.api().SetConfig) {
        bass.api().SetConfig(BASS_CONFIG_BUFFER, prevBufferMs);
    }

    if (!ch->bassStream) {
        return nullptr;
    }

    rebuildChannelEffects(*ch);
    ch->beginPrebuffer();

    auto* ptr = ch.get();
    channels_.emplace(speakerId, std::move(ch));
    return ptr;
}

void AudioStream::rebuildChannelEffects(SpeakerChannel& ch) {
    const auto& bass = BassLoader::instance().api();
    if (!ch.bassStream || !bass.ChannelSetFX || !bass.ChannelRemoveFX) {
        return;
    }
    for (HFX fx : ch.effects) {
        bass.ChannelRemoveFX(ch.bassStream, fx);
    }
    ch.effects.clear();

    for (const auto& [eid, spec] : effectSpecs_) {
        (void)eid;
        applyEffectToChannel(ch, spec);
    }
}

void AudioStream::applyEffectToChannel(SpeakerChannel& ch, const StreamEffectSpec& spec) {
    const auto& bass = BassLoader::instance().api();
    bool valid = false;
    const DWORD fxType = mapBassFxType(spec.effectType, valid);
    if (!valid || !ch.bassStream || !bass.ChannelSetFX) {
        return;
    }
    const HFX fx = bass.ChannelSetFX(ch.bassStream, fxType, spec.priority);
    if (fx != 0) {
        applyDx8Parameters(fx, spec.effectType, spec.paramBytes);
        ch.effects.push_back(fx);
    }
}

void AudioStream::flushJitterDrops(SpeakerChannel& ch) noexcept {
    const std::uint64_t dropped = ch.jitter.takeDroppedFrames();
    if (dropped == 0) {
        return;
    }
    if (auto* counter = dropCounter_.load(std::memory_order_acquire)) {
        counter->fetch_add(dropped, std::memory_order_relaxed);
    }
}

void AudioStream::manageChannelBacklog(SpeakerChannel& ch, std::chrono::steady_clock::time_point now) noexcept {
    const auto& bass = BassLoader::instance().api();
    if (!ch.bassStream || !bass.ChannelGetData) {
        return;
    }

    const float queuedMs = measureQueuedMs(bass, ch.bassStream);
    const bool inFlushBurstCoalesce =
        (ch.lastFlushTime.time_since_epoch().count() != 0) &&
        ((now - ch.lastFlushTime) <= kQueueFlushCoalesceWindow) &&
        (queuedMs > kQueueFlushCoalesceCapMs);

    if (queuedMs > kQueueFlushThresholdMs || inFlushBurstCoalesce) {
        if (!inFlushBurstCoalesce) {
            ch.lastFlushTime = now;
        }
        ch.flushTempPcm.assign(ch.recentPcm.begin(), ch.recentPcm.end());
        const std::size_t fadeCount = (std::min)(ch.flushTempPcm.size(), kQueueFlushFadeSamples);
        for (std::size_t i = 0; i < fadeCount; ++i) {
            const float fadeGain = static_cast<float>(i) / static_cast<float>(fadeCount);
            ch.flushTempPcm[i] = static_cast<std::int16_t>(
                static_cast<float>(ch.flushTempPcm[i]) * fadeGain);
        }

        if (bass.ChannelPlay) {
            bass.ChannelPlay(ch.bassStream, TRUE);
        }
        if (!ch.flushTempPcm.empty() && bass.StreamPutData) {
            bass.StreamPutData(
                ch.bassStream,
                ch.flushTempPcm.data(),
                static_cast<DWORD>(ch.flushTempPcm.size() * sizeof(std::int16_t)));
        }

        ch.prebuffering      = false;
        ch.prebufferedFrames = ch.targetPrebufferFrames;
        ch.catchUpActive     = false;
        if (ch.targetPrebufferFrames > SpeakerChannel::kMinPrebufferFrames) {
            --ch.targetPrebufferFrames;
            ch.stableFrames = 0;
        }

        const float afterMs = measureQueuedMs(bass, ch.bassStream);
        if (ch.lastCatchUpLogTime.time_since_epoch().count() == 0 ||
            (now - ch.lastCatchUpLogTime) >= kQueueCatchUpLogInterval) {
            ch.lastCatchUpLogTime = now;
            SVPLUS_LOG_INF_CAT(
                "audio",
                "voice catch-up: speaker [%u] backlog %.0f ms -> %.0f ms",
                static_cast<unsigned>(ch.playerId),
                queuedMs,
                afterMs);
        }
    } else if (queuedMs > kQueueCatchUpHighMs) {
        if (!ch.catchUpActive) {
            ch.catchUpActive = true;
            if (ch.lastCatchUpLogTime.time_since_epoch().count() == 0 ||
                (now - ch.lastCatchUpLogTime) >= kQueueCatchUpLogInterval) {
                ch.lastCatchUpLogTime = now;
                SVPLUS_LOG_INF_CAT(
                    "audio",
                    "voice catch-up: speaker [%u] backlog %.0f ms -> %.0f ms",
                    static_cast<unsigned>(ch.playerId),
                    queuedMs,
                    kQueueCatchUpLowMs);
            }
        }
    } else if (ch.catchUpActive && queuedMs <= kQueueCatchUpLowMs) {
        ch.catchUpActive = false;
    }
}

void AudioStream::applyParametersToChannel(SpeakerChannel& ch, float masterVolume, bool soundEnabled) {
    const auto& bass = BassLoader::instance().api();
    if (!ch.bassStream || !bass.ChannelSetAttribute) {
        return;
    }

    float streamVol  = 1.0f;
    float scriptPan  = 0.0f;
    float scriptFreq = static_cast<float>(protocol::kFrequency);
    for (auto& [paramId, state] : params_) {
        if (paramId == static_cast<std::uint32_t>(protocol::ParameterType::Volume)) {
            streamVol = (std::max)(0.0f, state.current());
            continue;
        }
        if (paramId == static_cast<std::uint32_t>(protocol::ParameterType::Panning)) {
            scriptPan = state.current();
            continue;
        }
        if (paramId == static_cast<std::uint32_t>(protocol::ParameterType::Frequency)) {
            scriptFreq = (std::max)(100.0f, state.current());
            continue;
        }
        if (paramId == static_cast<std::uint32_t>(protocol::ParameterType::EaxMix)) {
            // Accepted without effect since voice channels avoid global/3D EAX state.
            continue;
        }
        DWORD attrib = 0;
        float defVal = 0.0f;
        if (mapParameterAttribute(paramId, attrib, defVal)) {
            (void)defVal;
            bass.ChannelSetAttribute(ch.bassStream, attrib, state.current());
        }
    }

    const float finalVol = soundEnabled
        ? std::clamp(streamVol * masterVolume * ch.playerVolume * cachedSpatial_.gain * ch.occGainFactor, 0.0f, 4.0f)
        : 0.0f;
    bass.ChannelSetAttribute(ch.bassStream, BASS_ATTRIB_VOL, finalVol);

    const float targetFreq = std::clamp(
        scriptFreq * (ch.catchUpActive ? kQueueCatchUpRate : 1.0f),
        100.0f,
        200000.0f);
    if (!ch.hasAppliedFreq) {
        bass.ChannelSetAttribute(ch.bassStream, BASS_ATTRIB_FREQ, targetFreq);
        ch.lastAppliedFreq = targetFreq;
        ch.hasAppliedFreq  = true;
    } else if (std::fabs(targetFreq - ch.lastAppliedFreq) > 1.0f) {
        if (bass.ChannelSlideAttribute) {
            bass.ChannelSlideAttribute(ch.bassStream, BASS_ATTRIB_FREQ, targetFreq, kQueueCatchUpSlideMs);
        } else {
            bass.ChannelSetAttribute(ch.bassStream, BASS_ATTRIB_FREQ, targetFreq);
        }
        ch.lastAppliedFreq = targetFreq;
    }

    const float finalPan = std::clamp(cachedSpatial_.pan + scriptPan, -1.0f, 1.0f);
    if (!ch.hasAppliedPan) {
        bass.ChannelSetAttribute(ch.bassStream, BASS_ATTRIB_PAN, finalPan);
        ch.lastAppliedPan = finalPan;
        ch.hasAppliedPan  = true;
    } else if (std::fabs(finalPan - ch.lastAppliedPan) > 0.005f) {
        if (bass.ChannelSlideAttribute) {
            bass.ChannelSlideAttribute(ch.bassStream, BASS_ATTRIB_PAN, finalPan, 50u);
        } else {
            bass.ChannelSetAttribute(ch.bassStream, BASS_ATTRIB_PAN, finalPan);
        }
        ch.lastAppliedPan = finalPan;
    }
}

void AudioStream::decodeAndSubmitStep(SpeakerChannel& ch,
                                      const SpeakerJitterBuffer::OutputStep& step,
                                      bool applyFilter,
                                      bool applyBalancer,
                                      bool targetOccluded,
                                      std::chrono::steady_clock::time_point now) noexcept {
    const auto& bass = BassLoader::instance().api();
    if (!ch.bassStream || !bass.StreamPutData) {
        return;
    }

    int decoded = 0;
    if (step.kind == SpeakerJitterBuffer::ActionKind::DecodeNormal) {
        decoded = ch.decoder.decode(step.payload, decodeBuf_, false);
    } else if (step.kind == SpeakerJitterBuffer::ActionKind::DecodeFecAndThenNormal) {
        std::span<std::int16_t> fecSpan(decodeBuf_.data(), protocol::kLegacyFrameSamples);
        decoded = ch.decoder.decode(step.payload, fecSpan, true);
        if (decoded <= 0) {
            decoded = ch.decoder.decode({}, fecSpan, false);
        }
    } else {
        std::span<std::int16_t> plcSpan(decodeBuf_.data(), protocol::kLegacyFrameSamples);
        decoded = ch.decoder.decode({}, plcSpan, false);
    }

    if (decoded <= 0) {
        if (auto* counter = dropCounter_.load(std::memory_order_acquire)) {
            counter->fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    if (ch.snapOcclusionOnNextDecode) {
        ch.occFilterProgress         = targetOccluded ? 1.0f : 0.0f;
        ch.occGainFactor             = targetOccluded ? kOcclusionVolumeGain : 1.0f;
        ch.occLpInitialized          = false;
        ch.occLpState1               = 0.0f;
        ch.occLpState2               = 0.0f;
        ch.lastOccTickTime           = now;
        ch.snapOcclusionOnNextDecode = false;
    }

    auto pcmSpan = std::span<std::int16_t>(decodeBuf_.data(), static_cast<std::size_t>(decoded));
    processAudioDsp(ch, pcmSpan, applyFilter, applyBalancer, targetOccluded);
    ch.appendRecentPcm(pcmSpan);
    bass.StreamPutData(
        ch.bassStream,
        pcmSpan.data(),
        static_cast<DWORD>(decoded) * sizeof(std::int16_t));

    const bool highQueue = (measureQueuedMs(bass, ch.bassStream) > kQueueCatchUpHighMs);
    if (ch.onDecodedFrameQueued(highQueue) && bass.ChannelPlay) {
        bass.ChannelPlay(ch.bassStream, FALSE);
    }
}

void AudioStream::pushVoiceFrame(std::uint16_t speakerId,
                                 std::uint32_t seq,
                                 std::span<const std::uint8_t> opusPayload,
                                 float masterVolume,
                                 float playerVolume,
                                 bool soundEnabled,
                                 bool playerMuted,
                                 bool applyFilter,
                                 bool applyBalancer) {
    // When sound_enable=0, preserve prior behavior: ignore inbound frames cleanly without HUD presence.
    if (!soundEnabled || opusPayload.empty()) {
        return;
    }

    std::lock_guard lock(streamMutex_);
    const auto now = std::chrono::steady_clock::now();
    // Track speaking activity for all speakers (including blocked) without decoding or creating BASS channels.
    activeSpeakersLastSeen_[speakerId] = now;
    hasActiveSpeakers_.store(true, std::memory_order_release);

    if (playerMuted) {
        return;
    }

    SpeakerChannel* ch = ensureChannel(speakerId);
    if (!ch || !ch->bassStream) {
        return;
    }
    ch->playerVolume = std::clamp(playerVolume, 0.0f, 2.0f);
    if (ch->lastPacketTime.time_since_epoch().count() != 0 &&
        (now - ch->lastPacketTime) > std::chrono::milliseconds(250)) {
        // Resumed speech after silence pause: re-establish prebuffer cushion cleanly without resetting stable counter
        const auto& bass = BassLoader::instance().api();
        if (bass.ChannelPause) {
            bass.ChannelPause(ch->bassStream);
        }
        ch->jitter.reset();
        ch->beginPrebuffer(false);
        ch->snapOcclusionOnNextDecode = true;
        ch->recentPcm.clear();
        ch->catchUpActive = false;
    }

    const bool streamOccluded = (kind_ != StreamKind::Global) && latestOccluded_.load(std::memory_order_acquire);
    if (!ch->snapOcclusionOnNextDecode) {
        (void)advanceChannelOcclusionGain(*ch, streamOccluded, now);
    }
    ch->jitter.pushAndDrain(seq, opusPayload, now, [&](const SpeakerJitterBuffer::OutputStep& step) {
        decodeAndSubmitStep(*ch, step, applyFilter, applyBalancer, streamOccluded, now);
    });
    flushJitterDrops(*ch);
    ch->lastPacketTime = now;
    manageChannelBacklog(*ch, now);
    applyParametersToChannel(*ch, masterVolume, soundEnabled);
}

void AudioStream::resetSpeaker(std::uint16_t speakerId) noexcept {
    std::lock_guard lock(streamMutex_);
    auto it = channels_.find(speakerId);
    if (it != channels_.end() && it->second) {
        const auto& bass = BassLoader::instance().api();
        if (it->second->bassStream && bass.ChannelPause) {
            bass.ChannelPause(it->second->bassStream);
        }
        it->second->jitter.reset();
        it->second->decoder.reset();
        it->second->beginPrebuffer();
        it->second->hpPrevX = 0.0f;
        it->second->hpPrevY = 0.0f;
        it->second->env     = 0.0f;
        it->second->occLpState1              = 0.0f;
        it->second->occLpState2              = 0.0f;
        it->second->occFilterProgress        = 0.0f;
        it->second->occLpInitialized         = false;
        it->second->snapOcclusionOnNextDecode = true;
        it->second->occGainFactor            = 1.0f;
        it->second->lastOccTickTime          = {};
        it->second->catchUpActive            = false;
        it->second->hasAppliedFreq           = false;
        it->second->lastAppliedFreq          = static_cast<float>(protocol::kFrequency);
        it->second->recentPcm.clear();
        it->second->flushTempPcm.clear();
        it->second->lastFlushTime            = {};
    }
}

void AudioStream::tick(const float resolvedPos[3],
                       bool posValid,
                       const float camPos[3],
                       const float camFront[3],
                       const float camTop[3],
                       bool hasCamera,
                       const float localPedPos[3],
                       bool hasLocalPed,
                       float masterVolume,
                       bool soundEnabled,
                       bool applyFilter,
                       bool applyBalancer,
                       SpeakerMutedFn isSpeakerMuted,
                       void* mutedCtx,
                       SpeakerVolumeFn getSpeakerVolume) {
    std::lock_guard lock(streamMutex_);
    tickLocked(resolvedPos, posValid, camPos, camFront, camTop, hasCamera,
               localPedPos, hasLocalPed, masterVolume, soundEnabled,
               applyFilter, applyBalancer, isSpeakerMuted, mutedCtx, getSpeakerVolume);
}

void AudioStream::tickAndCollectSpeakers(const float resolvedPos[3],
                                         bool posValid,
                                         const float camPos[3],
                                         const float camFront[3],
                                         const float camTop[3],
                                         bool hasCamera,
                                         const float localPedPos[3],
                                         bool hasLocalPed,
                                         float masterVolume,
                                         bool soundEnabled,
                                         bool applyFilter,
                                         bool applyBalancer,
                                         SpeakerMutedFn isSpeakerMuted,
                                         void* mutedCtx,
                                         std::vector<std::uint16_t>& outSpeakers,
                                         protocol::StreamIconId& outIconId,
                                         SpeakerVolumeFn getSpeakerVolume) {
    std::lock_guard lock(streamMutex_);
    tickLocked(resolvedPos, posValid, camPos, camFront, camTop, hasCamera,
               localPedPos, hasLocalPed, masterVolume, soundEnabled,
               applyFilter, applyBalancer, isSpeakerMuted, mutedCtx, getSpeakerVolume);
    collectActiveSpeakersLocked(outSpeakers);
    outIconId = iconId_;
}

void AudioStream::tickLocked(const float resolvedPos[3],
                             bool posValid,
                             const float camPos[3],
                             const float camFront[3],
                             const float camTop[3],
                             bool hasCamera,
                             const float localPedPos[3],
                             bool hasLocalPed,
                             float masterVolume,
                             bool soundEnabled,
                             bool applyFilter,
                             bool applyBalancer,
                             SpeakerMutedFn isSpeakerMuted,
                             void* mutedCtx,
                             SpeakerVolumeFn getSpeakerVolume) {
    cachedSpatial_ = computeSpatialAttenuation(
        kind_, distance_, resolvedPos, posValid, camPos, camFront, camTop, hasCamera, localPedPos, hasLocalPed);
    const bool streamOccluded = isLatestOccluded();

    const auto& bass = BassLoader::instance().api();
    const auto now = std::chrono::steady_clock::now();

    for (auto it = channels_.begin(); it != channels_.end();) {
        auto& ch = it->second;
        if (!ch || (now - ch->lastPacketTime) > kSpeakerChannelIdleFree) {
            if (ch && ch->bassStream && bass.StreamFree) {
                bass.StreamFree(ch->bassStream);
            }
            it = channels_.erase(it);
            continue;
        }

        if (getSpeakerVolume) {
            ch->playerVolume = std::clamp(getSpeakerVolume(it->first, mutedCtx), 0.0f, 2.0f);
        }
        if (!ch->snapOcclusionOnNextDecode) {
            (void)advanceChannelOcclusionGain(*ch, streamOccluded, now);
        }
        const bool muted = isSpeakerMuted ? isSpeakerMuted(it->first, mutedCtx) : false;
        if (!muted && soundEnabled) {
            ch->jitter.drainTimedOut(now, [&](const SpeakerJitterBuffer::OutputStep& step) {
                decodeAndSubmitStep(*ch, step, applyFilter, applyBalancer, streamOccluded, now);
            });
            flushJitterDrops(*ch);
            manageChannelBacklog(*ch, now);
            if (bass.ChannelIsActive) {
                const DWORD active = bass.ChannelIsActive(ch->bassStream);
                if (ch->onPlaybackPoll(active == BASS_ACTIVE_STALLED, now)) {
                    if (bass.ChannelPause) {
                        bass.ChannelPause(ch->bassStream);
                    }
                }
            }
        } else if (muted && ch->bassStream) {
            // Instant mute cut on the next audio tick: pause playback & clear queued jitter frames.
            if (bass.ChannelPause) {
                bass.ChannelPause(ch->bassStream);
            }
            ch->jitter.reset();
            ch->beginPrebuffer(false);
            ch->recentPcm.clear();
            ch->catchUpActive = false;
        }

        applyParametersToChannel(*ch, muted ? 0.0f : masterVolume, soundEnabled && !muted);
        ++it;
    }

    for (auto seenIt = activeSpeakersLastSeen_.begin(); seenIt != activeSpeakersLastSeen_.end();) {
        if ((now - seenIt->second) > kSpeakerDisplayHold) {
            seenIt = activeSpeakersLastSeen_.erase(seenIt);
        } else {
            ++seenIt;
        }
    }
    hasActiveSpeakers_.store(!activeSpeakersLastSeen_.empty(), std::memory_order_release);
}

void AudioStream::collectActiveSpeakers(std::vector<std::uint16_t>& outSpeakers) const {
    std::lock_guard lock(streamMutex_);
    collectActiveSpeakersLocked(outSpeakers);
}

void AudioStream::collectActiveSpeakersLocked(std::vector<std::uint16_t>& outSpeakers) const {
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [pid, lastSeen] : activeSpeakersLastSeen_) {
        if ((now - lastSeen) <= kSpeakerDisplayHold) {
            outSpeakers.push_back(pid);
        }
    }
}

std::size_t AudioStream::channelCount() const noexcept {
    std::lock_guard lock(streamMutex_);
    return channels_.size();
}

} // namespace svplus::client::audio
