#include "audio/AudioStream.hpp"
#include "ShutdownState.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace svplus::client::audio {
namespace {

constexpr auto kSpeakerTimeout = std::chrono::milliseconds(400);

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
void processAudioDsp(SpeakerChannel& ch, std::span<std::int16_t> pcm, bool applyFilter, bool applyBalancer) noexcept {
    if (pcm.empty() || (!applyFilter && !applyBalancer)) {
        return;
    }

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
        sample = static_cast<std::int16_t>(std::clamp(x, -32767.0f, 32767.0f));
    }
}

} // namespace

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

void AudioStream::setDistance(float d) noexcept {
    distance_ = d;
}

AudioStream::SpatialAttenuation AudioStream::computeSpatialAttenuation(
    StreamKind kind,
    float streamDistance,
    const float sourcePos[3],
    bool sourcePosValid,
    const float camPos[3],
    const float camFront[3],
    const float camTop[3],
    bool hasCamera) noexcept {
    if (kind == StreamKind::Global) {
        return {1.0f, 0.0f};
    }
    if (!sourcePosValid || !hasCamera || !sourcePos || !camPos || !camFront || !camTop) {
        return {0.0f, 0.0f};
    }

    const float dx = sourcePos[0] - camPos[0];
    const float dy = sourcePos[1] - camPos[1];
    const float dz = sourcePos[2] - camPos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    const float maxDist = (std::max)(1.0f, streamDistance);
    const float minDist = maxDist * 0.12f;
    if (dist >= maxDist) {
        return {0.0f, 0.0f};
    }

    float gain = (dist <= minDist) ? 1.0f : (minDist / dist);
    if (dist > maxDist * 0.85f) {
        const float t = std::clamp((maxDist - dist) / (0.15f * maxDist), 0.0f, 1.0f);
        gain *= t * t * (3.0f - 2.0f * t);
    }

    // In GTA SA / RenderWare right-handed camera basis: front=(0,1,0), top=(0,0,1) => front x top = (1,0,0) right.
    const float rx = camFront[1] * camTop[2] - camFront[2] * camTop[1];
    const float ry = camFront[2] * camTop[0] - camFront[0] * camTop[2];
    const float rz = camFront[0] * camTop[1] - camFront[1] * camTop[0];
    const float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);

    float pan = 0.0f;
    if (dist > 1e-4f && rLen > 1e-4f) {
        const float invDist = 1.0f / dist;
        const float invR    = 1.0f / rLen;
        const float dotRight =
            (dx * invDist) * (rx * invR) +
            (dy * invDist) * (ry * invR) +
            (dz * invDist) * (rz * invR);
        pan = 0.8f * std::clamp(dotRight, -1.0f, 1.0f);
        pan *= std::clamp(dist / minDist, 0.0f, 1.0f);
    }

    return {gain, pan};
}

void AudioStream::setPosition(protocol::Vec3f pos) noexcept {
    position_ = pos;
}

void AudioStream::clearChannels() noexcept {
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
}

void AudioStream::setParameter(std::uint32_t paramId, float value) noexcept {
    params_[paramId].set(value);
}

void AudioStream::slideParameter(std::uint32_t paramId,
                                 float startVal,
                                 float endVal,
                                 std::uint32_t timeMs) noexcept {
    params_[paramId].slide(startVal, endVal, timeMs);
}

void AudioStream::addOrUpdateEffect(const StreamEffectSpec& spec) {
    effectSpecs_[spec.effectId] = spec;
    for (auto& [pid, ch] : channels_) {
        (void)pid;
        if (ch) {
            rebuildChannelEffects(*ch);
        }
    }
}

void AudioStream::removeEffect(std::uint32_t effectId) {
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

void AudioStream::applyParametersToChannel(SpeakerChannel& ch, float masterVolume, bool soundEnabled) {
    const auto& bass = BassLoader::instance().api();
    if (!ch.bassStream || !bass.ChannelSetAttribute) {
        return;
    }

    float streamVol = 1.0f;
    float scriptPan = 0.0f;
    for (auto& [paramId, state] : params_) {
        if (paramId == static_cast<std::uint32_t>(protocol::ParameterType::Volume)) {
            streamVol = (std::max)(0.0f, state.current());
            continue;
        }
        if (paramId == static_cast<std::uint32_t>(protocol::ParameterType::Panning)) {
            scriptPan = state.current();
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
        ? std::clamp(streamVol * masterVolume * cachedSpatial_.gain, 0.0f, 4.0f)
        : 0.0f;
    bass.ChannelSetAttribute(ch.bassStream, BASS_ATTRIB_VOL, finalVol);

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
                                      bool applyBalancer) noexcept {
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
        return;
    }

    auto pcmSpan = std::span<std::int16_t>(decodeBuf_.data(), static_cast<std::size_t>(decoded));
    processAudioDsp(ch, pcmSpan, applyFilter, applyBalancer);
    bass.StreamPutData(
        ch.bassStream,
        pcmSpan.data(),
        static_cast<DWORD>(decoded) * sizeof(std::int16_t));

    if (ch.onDecodedFrameQueued() && bass.ChannelPlay) {
        bass.ChannelPlay(ch.bassStream, FALSE);
    }
}

void AudioStream::pushVoiceFrame(std::uint16_t speakerId,
                                 std::uint32_t seq,
                                 std::span<const std::uint8_t> opusPayload,
                                 float masterVolume,
                                 bool soundEnabled,
                                 bool playerMuted,
                                 bool applyFilter,
                                 bool applyBalancer) {
    // When sound_enable=0, preserve prior behavior: ignore inbound frames cleanly without HUD presence.
    if (!soundEnabled || opusPayload.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    // Track speaking activity for all speakers (including blocked) without decoding or creating BASS channels.
    activeSpeakersLastSeen_[speakerId] = now;

    if (playerMuted) {
        return;
    }

    SpeakerChannel* ch = ensureChannel(speakerId);
    if (!ch || !ch->bassStream) {
        return;
    }
    if (ch->lastPacketTime.time_since_epoch().count() != 0 &&
        (now - ch->lastPacketTime) > std::chrono::milliseconds(250)) {
        // Resumed speech after silence pause: re-establish prebuffer cushion cleanly without resetting stable counter
        const auto& bass = BassLoader::instance().api();
        if (bass.ChannelPause) {
            bass.ChannelPause(ch->bassStream);
        }
        ch->jitter.reset();
        ch->beginPrebuffer(false);
    }

    ch->jitter.pushAndDrain(seq, opusPayload, now, [&](const SpeakerJitterBuffer::OutputStep& step) {
        decodeAndSubmitStep(*ch, step, applyFilter, applyBalancer);
    });
    ch->lastPacketTime = now;
    applyParametersToChannel(*ch, masterVolume, soundEnabled);
}

void AudioStream::resetSpeaker(std::uint16_t speakerId) noexcept {
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
    }
}

void AudioStream::tick(const float resolvedPos[3],
                       bool posValid,
                       const float camPos[3],
                       const float camFront[3],
                       const float camTop[3],
                       bool hasCamera,
                       float masterVolume,
                       bool soundEnabled,
                       bool applyFilter,
                       bool applyBalancer,
                       const std::function<bool(std::uint16_t)>& isSpeakerMuted) {
    cachedSpatial_ = computeSpatialAttenuation(
        kind_, distance_, resolvedPos, posValid, camPos, camFront, camTop, hasCamera);

    const auto& bass = BassLoader::instance().api();
    const auto now = std::chrono::steady_clock::now();

    for (auto it = channels_.begin(); it != channels_.end();) {
        auto& ch = it->second;
        if (!ch || (now - ch->lastPacketTime) > kSpeakerTimeout) {
            if (ch && ch->bassStream && bass.StreamFree) {
                bass.StreamFree(ch->bassStream);
            }
            it = channels_.erase(it);
            continue;
        }

        const bool muted = isSpeakerMuted ? isSpeakerMuted(it->first) : false;
        if (!muted && soundEnabled) {
            ch->jitter.drainTimedOut(now, [&](const SpeakerJitterBuffer::OutputStep& step) {
                decodeAndSubmitStep(*ch, step, applyFilter, applyBalancer);
            });
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
        }

        applyParametersToChannel(*ch, muted ? 0.0f : masterVolume, soundEnabled && !muted);
        ++it;
    }

    for (auto seenIt = activeSpeakersLastSeen_.begin(); seenIt != activeSpeakersLastSeen_.end();) {
        if ((now - seenIt->second) > kSpeakerTimeout) {
            seenIt = activeSpeakersLastSeen_.erase(seenIt);
        } else {
            ++seenIt;
        }
    }
}

void AudioStream::collectActiveSpeakers(std::vector<std::uint16_t>& outSpeakers) const {
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [pid, lastSeen] : activeSpeakersLastSeen_) {
        if ((now - lastSeen) <= kSpeakerTimeout) {
            outSpeakers.push_back(pid);
        }
    }
}

} // namespace svplus::client::audio
