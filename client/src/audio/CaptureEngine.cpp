#include "audio/CaptureEngine.hpp"
#include "ShutdownState.hpp"

#include "svplus/common/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace svplus::client::audio {
namespace {

const char* bassErrorName(int err) noexcept {
    switch (err) {
        case BASS_OK:               return "BASS_OK (ok)";
        case BASS_ERROR_MEM:        return "BASS_ERROR_MEM (memory error)";
        case BASS_ERROR_FILEOPEN:   return "BASS_ERROR_FILEOPEN (cannot open)";
        case BASS_ERROR_DRIVER:     return "BASS_ERROR_DRIVER (driver unavailable)";
        case BASS_ERROR_BUFLOST:    return "BASS_ERROR_BUFLOST (sample buffer lost)";
        case BASS_ERROR_HANDLE:     return "BASS_ERROR_HANDLE (invalid handle)";
        case BASS_ERROR_FORMAT:     return "BASS_ERROR_FORMAT (unsupported format)";
        case BASS_ERROR_POSITION:   return "BASS_ERROR_POSITION (invalid position)";
        case BASS_ERROR_INIT:       return "BASS_ERROR_INIT (device not initialized)";
        case BASS_ERROR_START:      return "BASS_ERROR_START (output not started)";
        case BASS_ERROR_ALREADY:    return "BASS_ERROR_ALREADY (already initialized)";
        case BASS_ERROR_NOTAUDIO:   return "BASS_ERROR_NOTAUDIO (not audio)";
        case BASS_ERROR_NOCHAN:     return "BASS_ERROR_NOCHAN (no free channels)";
        case BASS_ERROR_ILLTYPE:    return "BASS_ERROR_ILLTYPE (illegal type)";
        case BASS_ERROR_ILLPARAM:   return "BASS_ERROR_ILLPARAM (illegal parameter)";
        case BASS_ERROR_NO3D:       return "BASS_ERROR_NO3D (no 3D support)";
        case BASS_ERROR_NOEAX:      return "BASS_ERROR_NOEAX (no EAX support)";
        case BASS_ERROR_DEVICE:     return "BASS_ERROR_DEVICE (invalid/unplugged device)";
        case BASS_ERROR_NOPLAY:     return "BASS_ERROR_NOPLAY (not playing)";
        case BASS_ERROR_FREQ:       return "BASS_ERROR_FREQ (illegal sample rate)";
        case BASS_ERROR_NOTFILE:    return "BASS_ERROR_NOTFILE (not a file stream)";
        case BASS_ERROR_NOHW:       return "BASS_ERROR_NOHW (no hardware voices)";
        case BASS_ERROR_EMPTY:      return "BASS_ERROR_EMPTY (empty sound)";
        case BASS_ERROR_NONET:      return "BASS_ERROR_NONET (no network connection)";
        case BASS_ERROR_CREATE:     return "BASS_ERROR_CREATE (cannot create)";
        case BASS_ERROR_NOFX:       return "BASS_ERROR_NOFX (effects unavailable)";
        case BASS_ERROR_NOTAVAIL:   return "BASS_ERROR_NOTAVAIL (requested data/action unavailable)";
        case BASS_ERROR_DECODE:     return "BASS_ERROR_DECODE (decoding channel)";
        case BASS_ERROR_DX:         return "BASS_ERROR_DX (insufficient DirectX version)";
        case BASS_ERROR_TIMEOUT:    return "BASS_ERROR_TIMEOUT (timed out)";
        case BASS_ERROR_FILEFORM:   return "BASS_ERROR_FILEFORM (unsupported file format)";
        case BASS_ERROR_SPEAKER:    return "BASS_ERROR_SPEAKER (unavailable speaker)";
        case BASS_ERROR_VERSION:    return "BASS_ERROR_VERSION (invalid BASS version)";
        case BASS_ERROR_CODEC:      return "BASS_ERROR_CODEC (codec unsupported)";
        case BASS_ERROR_ENDED:      return "BASS_ERROR_ENDED (stream ended)";
        case BASS_ERROR_BUSY:       return "BASS_ERROR_BUSY (device busy/exclusive)";
        case BASS_ERROR_UNSTREAMABLE: return "BASS_ERROR_UNSTREAMABLE (unstreamable)";
        case BASS_ERROR_PROTOCOL:   return "BASS_ERROR_PROTOCOL (unsupported protocol)";
        case BASS_ERROR_UNKNOWN:    return "BASS_ERROR_UNKNOWN (unknown driver error)";
        default:                    return "BASS_ERROR (unspecified)";
    }
}

} // namespace

CaptureEngine& CaptureEngine::instance() noexcept {
    static auto* inst = new CaptureEngine();
    return *inst;
}

void CaptureEngine::setCallbacks(EncodedFrameCallback onFrame, KeyStateCallback onKeyState) {
    std::lock_guard lock(mutex_);
    onFrame_    = std::move(onFrame);
    onKeyState_ = std::move(onKeyState);
}

void CaptureEngine::addActivationKey(std::uint8_t vkCode) {
    std::lock_guard lock(mutex_);
    for (const auto& k : activationKeys_) {
        if (k.vk == vkCode) {
            return;
        }
    }
    activationKeys_.push_back({vkCode, false});
}

void CaptureEngine::removeActivationKey(std::uint8_t vkCode) {
    KeyStateCallback cb;
    bool notifyRelease = false;
    {
        std::lock_guard lock(mutex_);
        auto it = std::remove_if(activationKeys_.begin(), activationKeys_.end(), [&](const KeyBinding& b) {
            if (b.vk == vkCode) {
                if (b.pressed) {
                    notifyRelease = true;
                }
                return true;
            }
            return false;
        });
        activationKeys_.erase(it, activationKeys_.end());
        cb = onKeyState_;
    }
    if (notifyRelease && cb) {
        const auto now = std::chrono::steady_clock::now();
        recordKeyTransitionLog(vkCode, false, "key_removed", now);
        cb(vkCode, false);
        flushKeyTransitionBurst(now, true);
    }
}

void CaptureEngine::clearActivationKeys() {
    std::vector<std::uint8_t> toRelease;
    KeyStateCallback cb;
    {
        std::lock_guard lock(mutex_);
        for (const auto& b : activationKeys_) {
            if (b.pressed) {
                toRelease.push_back(b.vk);
            }
        }
        activationKeys_.clear();
        cb = onKeyState_;
    }
    const auto now = std::chrono::steady_clock::now();
    if (cb) {
        for (std::uint8_t vk : toRelease) {
            recordKeyTransitionLog(vk, false, "keys_cleared", now);
            cb(vk, false);
        }
    }
    flushKeyTransitionBurst(now, true);
}

void CaptureEngine::recordKeyTransitionLog(std::uint8_t vk,
                                           bool pressed,
                                           const char* reason,
                                           std::chrono::steady_clock::time_point now) {
    while (!keyTransitionTimes_.empty() &&
           (now - keyTransitionTimes_.front()) >= std::chrono::seconds(1)) {
        keyTransitionTimes_.pop_front();
    }
    keyTransitionTimes_.push_back(now);

    if (keyTransitionTimes_.size() > 20u) {
        if (!keyBurstCoalescing_) {
            keyBurstCoalescing_ = true;
            lastKeyBurstLog_    = now;
        }
    }

    char item[96]{};
    if (reason && reason[0] != '\0') {
        std::snprintf(item, sizeof(item), "0x%02X %s (%s)",
                      static_cast<unsigned>(vk), pressed ? "down" : "up", reason);
    } else {
        std::snprintf(item, sizeof(item), "0x%02X %s",
                      static_cast<unsigned>(vk), pressed ? "down" : "up");
    }

    if (keyBurstCoalescing_) {
        ++coalescedKeyTransitions_;
        if (coalescedKeySummary_.size() < 320u) {
            if (!coalescedKeySummary_.empty()) {
                coalescedKeySummary_ += ", ";
            }
            coalescedKeySummary_ += item;
        }
        if ((now - lastKeyBurstLog_) >= std::chrono::seconds(1)) {
            flushKeyTransitionBurst(now, false);
        }
        return;
    }

    if (reason && reason[0] != '\0') {
        SVPLUS_LOG_INF_CAT("audio", "Activation key 0x%02X %s -> sent (reason=%s)",
                           static_cast<unsigned>(vk), pressed ? "down" : "up", reason);
    } else {
        SVPLUS_LOG_INF_CAT("audio", "Activation key 0x%02X %s -> sent",
                           static_cast<unsigned>(vk), pressed ? "down" : "up");
    }
}

void CaptureEngine::flushKeyTransitionBurst(std::chrono::steady_clock::time_point now, bool force) {
    if (!keyBurstCoalescing_) {
        return;
    }
    if (!force && (now - lastKeyBurstLog_) < std::chrono::seconds(1)) {
        return;
    }
    if (coalescedKeyTransitions_ > 0u) {
        SVPLUS_LOG_WRN_CAT(
            "audio",
            "High activation key transition rate (>20/s): %zu transitions coalesced [%s]",
            coalescedKeyTransitions_,
            coalescedKeySummary_.c_str());
        coalescedKeyTransitions_ = 0;
        coalescedKeySummary_.clear();
    }
    lastKeyBurstLog_ = now;
    while (!keyTransitionTimes_.empty() &&
           (now - keyTransitionTimes_.front()) >= std::chrono::seconds(1)) {
        keyTransitionTimes_.pop_front();
    }
    if (keyTransitionTimes_.size() <= 20u) {
        keyBurstCoalescing_ = false;
    }
}

void CaptureEngine::setServerRecordActive(bool active) noexcept {
    serverRecordActive_.store(active, std::memory_order_release);
}

void CaptureEngine::setServerMuted(bool muted) noexcept {
    serverMuted_.store(muted, std::memory_order_release);
}

void CaptureEngine::latchPressedKeysForMute() noexcept {
    std::vector<std::uint8_t> latchedKeys;
    {
        std::lock_guard lock(mutex_);
        for (auto& binding : activationKeys_) {
            if (binding.pressed) {
                binding.pressed = false;
                binding.toggleLatched = false;
                binding.rawDownLast = false;
                binding.muteLatched = true;
                latchedKeys.push_back(binding.vk);
            }
        }
        if (!serverRecordActive_.load(std::memory_order_acquire)) {
            bool anyStillPressed = false;
            for (const auto& b : activationKeys_) {
                if (b.pressed) {
                    anyStillPressed = true;
                    break;
                }
            }
            if (!anyStillPressed) {
                transmitting_.store(false, std::memory_order_release);
            }
        }
    }
    for (std::uint8_t vk : latchedKeys) {
        SVPLUS_LOG_INF_CAT(
            "audio",
            "Activation key 0x%02X latched by server mute (stopped transmitting; ReleaseKey will follow on physical release)",
            static_cast<unsigned>(vk));
    }
}

void CaptureEngine::requestBitrate(std::uint32_t rawBitrate, bool fromServer) noexcept {
    bool clamped = false;
    const std::uint32_t normalized = protocol::clampOpusBitrate(rawBitrate, &clamped);
    if (clamped) {
        SVPLUS_LOG_WRN_CAT(
            "audio",
            "Server requested voice bitrate %u bps outside valid range [%u..%u]; clamping to %u bps",
            rawBitrate,
            protocol::kMinBitrate,
            protocol::kMaxBitrate,
            normalized);
    }

    std::uint64_t cur = pendingBitratePacked_.load(std::memory_order_relaxed);
    std::uint64_t next = 0;
    do {
        const std::uint32_t nextGen = static_cast<std::uint32_t>(cur >> 33u) + 1u;
        next = (static_cast<std::uint64_t>(nextGen) << 33u) |
               (fromServer ? (std::uint64_t{1} << 32u) : std::uint64_t{0}) |
               static_cast<std::uint64_t>(normalized);
    } while (!pendingBitratePacked_.compare_exchange_weak(
        cur, next, std::memory_order_release, std::memory_order_relaxed));
}

void CaptureEngine::requestRefreshDevices() noexcept {
    pendingRefresh_.store(true, std::memory_order_release);
}

void CaptureEngine::requestSelectDevice(std::string_view name) {
    std::lock_guard lock(pendingDeviceMutex_);
    pendingDeviceName_      = std::string(name);
    hasPendingDeviceSelect_ = true;
}

void CaptureEngine::refreshDevicesOnAudioThread() {
    const auto& bass = BassLoader::instance();
    if (!bass.isLoaded() || !bass.api().RecordGetDeviceInfo) {
        hasDevice_.store(false, std::memory_order_release);
        return;
    }

    std::vector<CaptureDeviceInfo> found;
    BASS_DEVICEINFO info{};
    for (DWORD idx = 0; bass.api().RecordGetDeviceInfo(idx, &info); ++idx) {
        if (!(info.flags & BASS_DEVICE_ENABLED)) {
            continue;
        }
        CaptureDeviceInfo item;
        item.deviceIndex = static_cast<int>(idx);
        item.name        = info.name ? info.name : ("Device " + std::to_string(idx));
        item.isDefault   = (info.flags & BASS_DEVICE_DEFAULT) != 0;
        found.push_back(std::move(item));
    }

    std::lock_guard lock(mutex_);
    devices_ = std::move(found);
    hasDevice_.store(!devices_.empty(), std::memory_order_release);

    activeDevicePresent_ = false;
    if (selectedDeviceIndex_ >= 0) {
        for (const auto& d : devices_) {
            if (d.deviceIndex == selectedDeviceIndex_ &&
                (selectedDeviceName_.empty() || d.name == selectedDeviceName_)) {
                activeDevicePresent_ = true;
                break;
            }
        }
    }
}

std::vector<CaptureDeviceInfo> CaptureEngine::devicesSnapshot() const {
    std::lock_guard lock(mutex_);
    return devices_;
}

bool CaptureEngine::openCaptureStream(int deviceIndex, std::string_view deviceName) {
    closeCaptureStream();
    lastFailedDeviceName_ = std::string(deviceName);

    const auto& bass = BassLoader::instance();
    if (!bass.isLoaded() || !bass.api().RecordInit || !bass.api().RecordStart) {
        lastBassError_ = BASS_ERROR_UNKNOWN;
        return false;
    }

    if (!bass.api().RecordInit(deviceIndex)) {
        const int err = bass.api().ErrorGetCode ? bass.api().ErrorGetCode() : BASS_ERROR_UNKNOWN;
        if (err != BASS_ERROR_ALREADY) {
            lastBassError_ = err;
            SVPLUS_LOG_WRN_CAT("audio", "BASS_RecordInit(%d) failed (err=%d: %s)", deviceIndex, err, bassErrorName(err));
            return false;
        }
    }
    if (bass.api().RecordSetDevice && deviceIndex >= 0) {
        bass.api().RecordSetDevice(static_cast<DWORD>(deviceIndex));
    }

    // Pull-mode capture (RECORDPROC == nullptr -> drained via BASS_ChannelGetData)
    recordHandle_ = bass.api().RecordStart(
        static_cast<DWORD>(protocol::kFrequency),
        1,
        0,
        nullptr,
        nullptr);
    if (!recordHandle_) {
        const int err = bass.api().ErrorGetCode ? bass.api().ErrorGetCode() : BASS_ERROR_UNKNOWN;
        lastBassError_ = err;
        SVPLUS_LOG_WRN_CAT("audio", "BASS_RecordStart(%d) failed (err=%d: %s)", deviceIndex, err, bassErrorName(err));
        return false;
    }

    const bool hadPersistentErr = openFailureErrLogged_;
    selectedDeviceIndex_     = deviceIndex;
    selectedDeviceName_      = std::string(deviceName);
    activeDevicePresent_     = true;
    reconnectLogged_         = false;
    consecutiveOpenFailures_ = 0;
    lastBassError_           = 0;
    openFailureErrLogged_    = false;
    ringClear();
    SVPLUS_LOG_INF_CAT("audio", "Opened microphone capture device '%.*s' (index=%d)",
                       static_cast<int>(deviceName.size()), deviceName.data(), deviceIndex);
    if (hadPersistentErr) {
        SVPLUS_LOG_INF_CAT("audio", "Microphone capture device '%s' recovered and opened cleanly",
                           selectedDeviceName_.c_str());
    }
    return true;
}

void CaptureEngine::closeCaptureStream() noexcept {
    const auto& bass = BassLoader::instance();
    if (recordHandle_ && bass.isLoaded() && bass.api().ChannelStop) {
        bass.api().ChannelStop(recordHandle_);
    }
    recordHandle_ = 0;
    activeDevicePresent_ = false;
    if (bass.isLoaded() && bass.api().RecordFree) {
        bass.api().RecordFree();
    }
}

bool CaptureEngine::selectDeviceOnAudioThread(std::string_view name) {
    refreshDevicesOnAudioThread();
    int targetIdx = -1;
    std::string resolvedName;
    {
        std::lock_guard lock(mutex_);
        if (devices_.empty()) {
            selectedDeviceIndex_ = -1;
            selectedDeviceName_.clear();
            return false;
        }
        if (!name.empty()) {
            for (const auto& d : devices_) {
                if (d.name == name) {
                    targetIdx = d.deviceIndex;
                    resolvedName = d.name;
                    break;
                }
            }
        }
        if (targetIdx < 0) {
            for (const auto& d : devices_) {
                if (d.isDefault) {
                    targetIdx = d.deviceIndex;
                    resolvedName = d.name;
                    break;
                }
            }
        }
        if (targetIdx < 0) {
            targetIdx = devices_.front().deviceIndex;
            resolvedName = devices_.front().name;
        }
    }
    if (openCaptureStream(targetIdx, resolvedName)) {
        return true;
    }
    ++consecutiveOpenFailures_;
    constexpr int kMaxRecoveryAttemptsBeforeErr = 5;
    if (consecutiveOpenFailures_ >= kMaxRecoveryAttemptsBeforeErr && !openFailureErrLogged_) {
        openFailureErrLogged_ = true;
        SVPLUS_LOG_ERR_CAT(
            "audio",
            "Failed to open microphone '%s' after %d attempt(s) (bassErr=%d: %s)",
            resolvedName.empty() ? "<default>" : resolvedName.c_str(),
            consecutiveOpenFailures_,
            lastBassError_,
            bassErrorName(lastBassError_));
    }
    return false;
}

bool CaptureEngine::initialize(const config::ClientConfig& cfg) {
    const std::uint64_t packed = pendingBitratePacked_.load(std::memory_order_acquire);
    const std::uint32_t initBitrate = static_cast<std::uint32_t>(packed & 0xFFFFFFFFu);
    const bool fromServer = ((packed >> 32u) & 1u) != 0u;
    if (!encoder_.initialize(static_cast<int>(initBitrate))) {
        SVPLUS_LOG_ERR_CAT("audio", "Failed to create Opus voice encoder");
        return false;
    }
    appliedBitratePacked_ = packed;
    activeBitrate_.store(initBitrate, std::memory_order_release);
    if (fromServer) {
        SVPLUS_LOG_INF_CAT("audio", "Opus voice bitrate applied: %u bps", initBitrate);
    }
    captureScratch_.reserve(kRingCapacity);
    opusOutBuf_.resize(protocol::kMaxVoicePayloadBytes);
    refreshDevicesOnAudioThread();
    if (hasDevice_.load(std::memory_order_acquire)) {
        selectDeviceOnAudioThread(cfg.microDevice);
    }
    return true;
}

void CaptureEngine::shutdown() noexcept {
    if (isProcessShuttingDown()) {
        hasDevice_.store(false, std::memory_order_release);
        transmitting_.store(false, std::memory_order_release);
        wasTransmitting_ = false;
        return;
    }
    flushKeyTransitionBurst(std::chrono::steady_clock::now(), true);
    closeCaptureStream();
    encoder_.shutdown();
    ringClear();
    hasDevice_.store(false, std::memory_order_release);
    transmitting_.store(false, std::memory_order_release);
    wasTransmitting_ = false;
    pendingBitratePacked_.store(static_cast<std::uint64_t>(protocol::kDefaultBitrate), std::memory_order_release);
    appliedBitratePacked_ = static_cast<std::uint64_t>(protocol::kDefaultBitrate);
    activeBitrate_.store(protocol::kDefaultBitrate, std::memory_order_release);
}

void CaptureEngine::ringClear() noexcept {
    ringHead_  = 0;
    ringTail_  = 0;
    ringCount_ = 0;
}

void CaptureEngine::ringPush(const std::int16_t* samples, std::size_t count) noexcept {
    if (!samples || count == 0) {
        return;
    }
    if (count >= kRingCapacity) {
        samples += (count - kRingCapacity);
        count    = kRingCapacity;
        ringClear();
    } else if (ringCount_ + count > kRingCapacity) {
        const std::size_t drop = (ringCount_ + count) - kRingCapacity;
        ringHead_   = (ringHead_ + drop) % kRingCapacity;
        ringCount_ -= drop;
    }

    const std::size_t firstChunk = (std::min)(count, kRingCapacity - ringTail_);
    std::memcpy(ringBuf_.data() + ringTail_, samples, firstChunk * sizeof(std::int16_t));
    if (count > firstChunk) {
        std::memcpy(ringBuf_.data(), samples + firstChunk, (count - firstChunk) * sizeof(std::int16_t));
    }
    ringTail_   = (ringTail_ + count) % kRingCapacity;
    ringCount_ += count;
}

bool CaptureEngine::ringPopFrame(std::int16_t* outFrame) noexcept {
    if (ringCount_ < kFrameSamples) {
        return false;
    }
    const std::size_t firstChunk = (std::min)(kFrameSamples, kRingCapacity - ringHead_);
    std::memcpy(outFrame, ringBuf_.data() + ringHead_, firstChunk * sizeof(std::int16_t));
    if (kFrameSamples > firstChunk) {
        std::memcpy(outFrame + firstChunk, ringBuf_.data(), (kFrameSamples - firstChunk) * sizeof(std::int16_t));
    }
    ringHead_   = (ringHead_ + kFrameSamples) % kRingCapacity;
    ringCount_ -= kFrameSamples;
    return true;
}

void CaptureEngine::flushPaddedTail(const EncodedFrameCallback& frameCb) {
    if (!frameCb || ringCount_ < kMinTailSamples) {
        ringClear();
        return;
    }
    const std::size_t n = (std::min)(ringCount_, kFrameSamples);
    const std::size_t firstChunk = (std::min)(n, kRingCapacity - ringHead_);
    std::memcpy(encodeScratch_.data(), ringBuf_.data() + ringHead_, firstChunk * sizeof(std::int16_t));
    if (n > firstChunk) {
        std::memcpy(encodeScratch_.data() + firstChunk, ringBuf_.data(), (n - firstChunk) * sizeof(std::int16_t));
    }
    if (n < kFrameSamples) {
        std::fill(encodeScratch_.begin() + static_cast<std::ptrdiff_t>(n), encodeScratch_.end(), std::int16_t{0});
    }
    ringClear();

    const int encodedLen = encoder_.encode(encodeScratch_, opusOutBuf_);
    if (encodedLen > 0) {
        frameCb(std::span<const std::uint8_t>(opusOutBuf_.data(), static_cast<std::size_t>(encodedLen)),
                sequenceCounter_++);
    }
}

void CaptureEngine::processMicDsp(std::span<std::int16_t> pcm, float gain, bool suppressNoise) noexcept {
    double sumSq = 0.0;
    for (auto& sample : pcm) {
        float x = static_cast<float>(sample);

        if (suppressNoise) {
            // 80 Hz high-pass rumble/plosive filter
            const float hp = x - hpPrevX_ + 0.9896f * hpPrevY_;
            hpPrevX_ = x;
            hpPrevY_ = hp;
            x = hp;

            // Smooth RMS noise gate
            const float absNorm = std::fabs(x) / 32768.0f;
            const float coeff   = (absNorm > gateEnv_) ? 0.05f : 0.0004f;
            gateEnv_ += coeff * (absNorm - gateEnv_);
            if (gateEnv_ < 0.0045f) {
                const float atten = gateEnv_ / 0.0045f;
                x *= atten * atten;
            }
        }

        x *= gain;
        x = std::clamp(x, -32767.0f, 32767.0f);
        sample = static_cast<std::int16_t>(x);
        sumSq += static_cast<double>(x) * static_cast<double>(x);
    }

    if (!pcm.empty()) {
        const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(pcm.size())) / 32768.0);
        inputLevel_.store(std::clamp(rms * 3.2f, 0.0f, 1.0f), std::memory_order_release);
    }
}

void CaptureEngine::pollActivationKeys(
    const config::ClientConfig& cfg,
    bool inputBlocked,
    const char* inputBlockReason,
    std::chrono::steady_clock::time_point now,
    bool& outAnyKeyHeld
) {
    struct PendingTransitionLog {
        std::uint8_t vk{0};
        bool         pressed{false};
        const char*  reason{nullptr};
    };
    std::vector<PendingTransitionLog> transitions;
    std::vector<std::uint8_t> latchClearedKeys;
    bool anyKeyHeld = false;
    bool anyConflictNow = false;
    std::uint8_t conflictVkNow = 0;
    KeyStateCallback keyCb;

    {
        std::lock_guard lock(mutex_);
        keyCb = onKeyState_;
        const bool hasDeviceNow = hasDevice_.load(std::memory_order_acquire);
        const bool micAvailable = cfg.microEnable && hasDeviceNow;
        const bool isToggleMode = (cfg.talkMode == config::TalkMode::Toggle);

        for (auto& binding : activationKeys_) {
            const bool physDown = ((GetAsyncKeyState(binding.vk) & 0x8000) != 0);
            if (static_cast<std::uint32_t>(binding.vk) == cfg.menuHotkey) {
                anyConflictNow = true;
                conflictVkNow  = binding.vk;
                if (!binding.conflictWarned) {
                    binding.conflictWarned = true;
                    SVPLUS_LOG_WRN_CAT(
                        "audio",
                        "Ignoring SvAddKey 0x%02X because it conflicts with the menu hotkey; rebind the menu key in Settings",
                        static_cast<unsigned>(binding.vk));
                }
                binding.toggleLatched = false;
                binding.rawDownLast   = false;
                binding.physDownLast  = physDown;
                if (!physDown && binding.muteLatched) {
                    binding.muteLatched = false;
                    latchClearedKeys.push_back(binding.vk);
                    transitions.push_back({binding.vk, false, "mute_latch_release"});
                } else if (binding.pressed) {
                    binding.pressed = false;
                    transitions.push_back({binding.vk, false, "menu_hotkey_conflict"});
                }
                continue;
            }
            binding.conflictWarned = false;

            if (binding.muteLatched) {
                binding.toggleLatched = false;
                binding.rawDownLast   = false;
                binding.physDownLast  = physDown;
                if (!physDown) {
                    binding.muteLatched = false;
                    latchClearedKeys.push_back(binding.vk);
                    transitions.push_back({binding.vk, false, "mute_latch_release"});
                }
                continue;
            }

            const bool rawDown = !inputBlocked && micAvailable && physDown;
            bool down = false;
            if (isToggleMode) {
                if (rawDown && !binding.rawDownLast) {
                    binding.toggleLatched = !binding.toggleLatched;
                }
                binding.rawDownLast = rawDown;
                if (!micAvailable) {
                    binding.toggleLatched = false;
                }
                down = binding.toggleLatched;
            } else {
                binding.toggleLatched = false;
                binding.rawDownLast   = rawDown;
                down = rawDown;
            }

            if (down != binding.pressed) {
                const char* reason = nullptr;
                if (!cfg.microEnable) {
                    reason = "micro_disabled";
                } else if (!hasDeviceNow) {
                    reason = "no_mic_device";
                } else if (inputBlocked) {
                    reason = (inputBlockReason && inputBlockReason[0] != '\0')
                        ? inputBlockReason
                        : "input_blocked";
                } else if (physDown == binding.physDownLast) {
                    reason = "unblocked_or_mic_restored";
                }
                binding.pressed = down;
                transitions.push_back({binding.vk, down, reason});
            }
            binding.physDownLast = physDown;
            if (binding.pressed) {
                anyKeyHeld = true;
            }
        }
    }

    hasKeyConflict_.store(anyConflictNow, std::memory_order_release);
    conflictingKey_.store(conflictVkNow, std::memory_order_release);
    outAnyKeyHeld = anyKeyHeld;

    for (std::uint8_t vk : latchClearedKeys) {
        SVPLUS_LOG_INF_CAT(
            "audio",
            "Activation key 0x%02X mute latch cleared after physical release (ready for new press)",
            static_cast<unsigned>(vk));
    }

    if (keyCb) {
        for (const auto& t : transitions) {
            recordKeyTransitionLog(t.vk, t.pressed, t.reason, now);
            keyCb(t.vk, t.pressed);
        }
    }
}

void CaptureEngine::tick(const config::ClientConfig& cfg, bool inputBlocked, const char* inputBlockReason) {
    const auto now = std::chrono::steady_clock::now();
    const auto& bass = BassLoader::instance();
    flushKeyTransitionBurst(now, false);

    // Apply server-configured Opus bitrate safely on the audio thread without recreating the encoder.
    const std::uint64_t pendingPacked = pendingBitratePacked_.load(std::memory_order_acquire);
    if (pendingPacked != appliedBitratePacked_) {
        const std::uint32_t targetBitrate = static_cast<std::uint32_t>(pendingPacked & 0xFFFFFFFFu);
        const bool fromServer = ((pendingPacked >> 32u) & 1u) != 0u;
        if (encoder_.setBitrate(static_cast<int>(targetBitrate))) {
            appliedBitratePacked_ = pendingPacked;
            const std::uint32_t prevBitrate = activeBitrate_.exchange(targetBitrate, std::memory_order_acq_rel);
            if (fromServer) {
                SVPLUS_LOG_INF_CAT("audio", "Opus voice bitrate applied: %u bps", targetBitrate);
            } else if (prevBitrate != targetBitrate) {
                SVPLUS_LOG_INF_CAT("audio", "Opus voice bitrate reset to %u bps", targetBitrate);
            }
        }
    }

    // Detect unplugged / stopped capture device or periodic hot-plug changes every 2s
    bool streamDead = false;
    if (recordHandle_ && bass.isLoaded() && bass.api().ChannelIsActive) {
        if (bass.api().ChannelIsActive(recordHandle_) == BASS_ACTIVE_STOPPED) {
            streamDead = true;
        }
    }

    const bool periodicScanDue =
        lastDeviceScan_.time_since_epoch().count() == 0 ||
        (now - lastDeviceScan_) >= std::chrono::seconds(2);

    if (pendingRefresh_.exchange(false, std::memory_order_acq_rel) || periodicScanDue || streamDead) {
        lastDeviceScan_ = now;
        refreshDevicesOnAudioThread();

        // Check if currently open device vanished OR preferred device reappeared
        if (recordHandle_ && !activeDevicePresent_) {
            streamDead = true;
        } else if (hasDevice_.load(std::memory_order_acquire) && !cfg.microDevice.empty() &&
                   selectedDeviceName_ != cfg.microDevice) {
            std::lock_guard lock(mutex_);
            for (const auto& d : devices_) {
                if (d.name == cfg.microDevice) {
                    streamDead = true;
                    break;
                }
            }
        }
    }

    if (streamDead && recordHandle_) {
        if (!reconnectLogged_) {
            SVPLUS_LOG_WRN_CAT("audio", "Capture device interrupted or unplugged; attempting automatic recovery");
            reconnectLogged_ = true;
        }
        closeCaptureStream();
    }

    {
        std::string devName;
        bool doSelect = false;
        {
            std::lock_guard lock(pendingDeviceMutex_);
            if (hasPendingDeviceSelect_) {
                devName = std::move(pendingDeviceName_);
                hasPendingDeviceSelect_ = false;
                doSelect = true;
            }
        }
        if (doSelect) {
            selectDeviceOnAudioThread(devName);
        } else if (!recordHandle_ && hasDevice_.load(std::memory_order_acquire) &&
                   (lastReconnectAttempt_.time_since_epoch().count() == 0 ||
                    (now - lastReconnectAttempt_) >= std::chrono::seconds(1))) {
            lastReconnectAttempt_ = now;
            if (selectDeviceOnAudioThread(cfg.microDevice)) {
                SVPLUS_LOG_INF_CAT("audio", "Capture device restored: '%s'", selectedDeviceName_.c_str());
            }
        }
    }

    bool anyKeyHeld = false;
    pollActivationKeys(cfg, inputBlocked, inputBlockReason, now, anyKeyHeld);
    EncodedFrameCallback frameCb;
    {
        std::lock_guard lock(mutex_);
        frameCb = onFrame_;
    }

    const bool shouldTransmit =
        cfg.microEnable &&
        hasDevice_.load(std::memory_order_acquire) &&
        !serverMuted_.load(std::memory_order_acquire) &&
        (anyKeyHeld || serverRecordActive_.load(std::memory_order_acquire));
    transmitting_.store(shouldTransmit, std::memory_order_release);

    if (!recordHandle_ || !bass.isLoaded() || !bass.api().ChannelGetData) {
        inputLevel_.store(0.0f, std::memory_order_release);
        ringClear();
        wasTransmitting_ = false;
        return;
    }

    const DWORD availBytes = bass.api().ChannelGetData(recordHandle_, nullptr, BASS_DATA_AVAILABLE);
    if (availBytes == DWORD(-1)) {
        if (!reconnectLogged_) {
            const int err = bass.api().ErrorGetCode ? bass.api().ErrorGetCode() : BASS_ERROR_UNKNOWN;
            SVPLUS_LOG_WRN_CAT("audio", "BASS_ChannelGetData failed (err=%d: %s); resetting capture stream",
                               err, bassErrorName(err));
            reconnectLogged_ = true;
        }
        closeCaptureStream();
        inputLevel_.store(0.0f, std::memory_order_release);
        ringClear();
        wasTransmitting_ = false;
        return;
    }
    if (availBytes > 0) {
        const std::size_t availSamples = availBytes / sizeof(std::int16_t);
        captureScratch_.resize(availSamples);
        const DWORD readBytes = bass.api().ChannelGetData(
            recordHandle_,
            captureScratch_.data(),
            static_cast<DWORD>(availSamples * sizeof(std::int16_t)));
        if (readBytes != DWORD(-1) && readBytes > 0) {
            const std::size_t readSamples = readBytes / sizeof(std::int16_t);
            const float gain = static_cast<float>(std::clamp(cfg.microVolume, 0, 100)) / 75.0f;

            processMicDsp(std::span<std::int16_t>(captureScratch_.data(), readSamples), gain, cfg.microNoiseSuppress);
            if (shouldTransmit) {
                ringPush(captureScratch_.data(), readSamples);
            }
        }
    }

    if (!shouldTransmit) {
        if (wasTransmitting_) {
            SVPLUS_LOG_INF_CAT("audio", "Microphone transmission stopped");
            flushPaddedTail(frameCb);
        } else {
            ringClear();
        }
        wasTransmitting_ = false;
        return;
    }

    if (!wasTransmitting_) {
        SVPLUS_LOG_INF_CAT("audio", "Microphone transmission started (mode=%s, trigger=%s)",
                           cfg.talkMode == config::TalkMode::Toggle ? "toggle" : "push",
                           anyKeyHeld ? "key" : "server_record");
    }
    wasTransmitting_ = true;
    if (!frameCb) {
        ringClear();
        return;
    }

    while (ringPopFrame(encodeScratch_.data())) {
        const int encodedLen = encoder_.encode(encodeScratch_, opusOutBuf_);
        if (encodedLen > 0) {
            frameCb(std::span<const std::uint8_t>(opusOutBuf_.data(), static_cast<std::size_t>(encodedLen)),
                    sequenceCounter_++);
        }
    }
}

} // namespace svplus::client::audio
