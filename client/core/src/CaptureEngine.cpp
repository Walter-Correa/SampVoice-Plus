#include "svplus/client/CaptureEngine.hpp"

#include "svplus/common/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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
    keysGeneration_.fetch_add(1, std::memory_order_release);
}

void CaptureEngine::removeActivationKey(std::uint8_t vkCode) {
    KeyStateCallback cb;
    bool notifyRelease = false;
    bool removed = false;
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
        if (it != activationKeys_.end()) {
            activationKeys_.erase(it, activationKeys_.end());
            keysGeneration_.fetch_add(1, std::memory_order_release);
            removed = true;
        }
        cb = onKeyState_;
    }
    (void)removed;
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
        if (!activationKeys_.empty()) {
            for (const auto& b : activationKeys_) {
                if (b.pressed) {
                    toRelease.push_back(b.vk);
                }
            }
            activationKeys_.clear();
            keysGeneration_.fetch_add(1, std::memory_order_release);
        }
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

void CaptureEngine::clearToggleLatches() noexcept {
    std::lock_guard lock(mutex_);
    for (auto& binding : activationKeys_) {
        binding.toggleLatched = false;
        binding.rawDownLast   = false;
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
    const IBassLibrary* bass = bass_;
    const BassApi* api = cachedBassApi_;
    if (!bass || !api || !bass->isLoaded() || !api->RecordGetDeviceInfo) {
        hasDevice_.store(false, std::memory_order_release);
        return;
    }

    std::vector<CaptureDeviceInfo> found;
    BASS_DEVICEINFO info{};
    for (DWORD idx = 0; api->RecordGetDeviceInfo(idx, &info); ++idx) {
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

    const IBassLibrary* bass = bass_;
    const BassApi* api = cachedBassApi_;
    if (!bass || !api || !bass->isLoaded() || !api->RecordInit || !api->RecordStart) {
        lastBassError_ = BASS_ERROR_UNKNOWN;
        return false;
    }

    if (!api->RecordInit(deviceIndex)) {
        const int err = api->ErrorGetCode ? api->ErrorGetCode() : BASS_ERROR_UNKNOWN;
        if (err != BASS_ERROR_ALREADY) {
            lastBassError_ = err;
            SVPLUS_LOG_WRN_CAT("audio", "BASS_RecordInit(%d) failed (err=%d: %s)", deviceIndex, err, bassErrorName(err));
            return false;
        }
    }
    if (api->RecordSetDevice && deviceIndex >= 0) {
        api->RecordSetDevice(static_cast<DWORD>(deviceIndex));
    }

    // Pull-mode capture (RECORDPROC == nullptr -> drained via BASS_ChannelGetData)
    recordHandle_ = api->RecordStart(
        static_cast<DWORD>(protocol::kFrequency),
        1,
        0,
        nullptr,
        nullptr);
    if (!recordHandle_) {
        const int err = api->ErrorGetCode ? api->ErrorGetCode() : BASS_ERROR_UNKNOWN;
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
    txRing_.clear();
    limiterEnv_              = 0.0f;
    SVPLUS_LOG_INF_CAT("audio", "Opened microphone capture device '%.*s' (index=%d)",
                       static_cast<int>(deviceName.size()), deviceName.data(), deviceIndex);
    if (hadPersistentErr) {
        SVPLUS_LOG_INF_CAT("audio", "Microphone capture device '%s' recovered and opened cleanly",
                           selectedDeviceName_.c_str());
    }
    return true;
}

void CaptureEngine::closeCaptureStream() noexcept {
    const IBassLibrary* bass = bass_;
    const BassApi* api = cachedBassApi_;
    if (recordHandle_ && bass && api && bass->isLoaded() && api->ChannelStop) {
        api->ChannelStop(recordHandle_);
    }
    recordHandle_ = 0;
    activeDevicePresent_ = false;
    if (bass && api && bass->isLoaded() && api->RecordFree) {
        api->RecordFree();
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

bool CaptureEngine::initialize(const CoreConfig& cfg) {
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
        curTxAccum_.reset();
        return;
    }
    flushKeyTransitionBurst(std::chrono::steady_clock::now(), true);
    stopMicMonitorOnAudioThread("shutdown");
    closeCaptureStream();
    encoder_.shutdown();
    txRing_.clear();
    monitorRing_.clear();
    {
        std::lock_guard lock(mutex_);
        devices_.clear();
        selectedDeviceIndex_ = -1;
        selectedDeviceName_.clear();
        activeDevicePresent_ = false;
        if (!activationKeys_.empty()) {
            activationKeys_.clear();
            keysGeneration_.fetch_add(1, std::memory_order_release);
        }
    }
    {
        std::lock_guard lock(pendingDeviceMutex_);
        hasPendingDeviceSelect_ = false;
        pendingDeviceName_.clear();
    }
    {
        std::lock_guard lock(monitorReasonMutex_);
        monitorStopReason_.clear();
    }
    lastDeviceScan_ = {};
    lastReconnectAttempt_ = {};
    reconnectLogged_ = false;
    lastBassError_ = 0;
    lastFailedDeviceName_.clear();
    consecutiveOpenFailures_ = 0;
    openFailureErrLogged_ = false;
    pendingRefresh_.store(false, std::memory_order_release);
    sequenceCounter_ = 0;
    keyTransitionTimes_.clear();
    keyBurstCoalescing_ = false;
    coalescedKeyTransitions_ = 0;
    coalescedKeySummary_.clear();
    lastKeyBurstLog_ = {};
    appliedMonitorBitrate_ = protocol::kDefaultBitrate;
    micMonitorDesired_.store(false, std::memory_order_release);
    micMonitorActive_ = false;
    hpPrevX_ = 0.0f;
    hpPrevY_ = 0.0f;
    gateEnv_ = 0.0f;
    limiterEnv_ = 0.0f;
    hasDevice_.store(false, std::memory_order_release);
    transmitting_.store(false, std::memory_order_release);
    serverRecordActive_.store(false, std::memory_order_release);
    serverMuted_.store(false, std::memory_order_release);
    hasKeyConflict_.store(false, std::memory_order_release);
    conflictingKey_.store(0, std::memory_order_release);
    inputLevel_.store(0.0f, std::memory_order_release);
    wasTransmitting_ = false;
    curTxAccum_.reset();
    {
        std::lock_guard txLk(txStatsMutex_);
        lastTxStats_ = TxUtteranceStats{};
    }
    pendingBitratePacked_.store(static_cast<std::uint64_t>(protocol::kDefaultBitrate), std::memory_order_release);
    appliedBitratePacked_ = static_cast<std::uint64_t>(protocol::kDefaultBitrate);
    activeBitrate_.store(protocol::kDefaultBitrate, std::memory_order_release);
    bass_ = nullptr;
    cachedBassApi_ = nullptr;
    keyInput_ = nullptr;
}

void CaptureEngine::PcmRingBuffer::clear() noexcept {
    head  = 0;
    tail  = 0;
    count = 0;
}

void CaptureEngine::PcmRingBuffer::push(const std::int16_t* samples, std::size_t n) noexcept {
    if (!samples || n == 0) {
        return;
    }
    if (n >= kRingCapacity) {
        samples += (n - kRingCapacity);
        n        = kRingCapacity;
        clear();
    } else if (count + n > kRingCapacity) {
        const std::size_t drop = (count + n) - kRingCapacity;
        head   = (head + drop) % kRingCapacity;
        count -= drop;
    }

    const std::size_t firstChunk = (std::min)(n, kRingCapacity - tail);
    std::memcpy(buf.data() + tail, samples, firstChunk * sizeof(std::int16_t));
    if (n > firstChunk) {
        std::memcpy(buf.data(), samples + firstChunk, (n - firstChunk) * sizeof(std::int16_t));
    }
    tail   = (tail + n) % kRingCapacity;
    count += n;
}

bool CaptureEngine::PcmRingBuffer::popFrame(std::int16_t* outFrame) noexcept {
    if (count < kFrameSamples) {
        return false;
    }
    const std::size_t firstChunk = (std::min)(kFrameSamples, kRingCapacity - head);
    std::memcpy(outFrame, buf.data() + head, firstChunk * sizeof(std::int16_t));
    if (kFrameSamples > firstChunk) {
        std::memcpy(outFrame + firstChunk, buf.data(), (kFrameSamples - firstChunk) * sizeof(std::int16_t));
    }
    head   = (head + kFrameSamples) % kRingCapacity;
    count -= kFrameSamples;
    return true;
}

void CaptureEngine::setMicMonitorEnabled(bool enabled, const char* reason) {
    if (!enabled) {
        std::lock_guard lk(monitorReasonMutex_);
        monitorStopReason_ = (reason && reason[0] != '\0') ? reason : "user";
    }
    micMonitorDesired_.store(enabled, std::memory_order_release);
}

bool CaptureEngine::hasActivationKey(std::uint8_t vkCode) const noexcept {
    if (vkCode == 0) {
        return false;
    }
    std::lock_guard lock(mutex_);
    for (const auto& b : activationKeys_) {
        if (b.vk == vkCode) {
            return true;
        }
    }
    return false;
}

std::vector<std::uint8_t> CaptureEngine::registeredKeysSnapshot(std::uint64_t* outGeneration) const {
    std::lock_guard lock(mutex_);
    std::vector<std::uint8_t> keys;
    keys.reserve(activationKeys_.size());
    for (const auto& b : activationKeys_) {
        keys.push_back(b.vk);
    }
    if (outGeneration) {
        *outGeneration = keysGeneration_.load(std::memory_order_acquire);
    }
    return keys;
}

std::vector<CaptureEngine::KeyStateSnapshotEntry> CaptureEngine::registeredKeyStatesSnapshot(
    std::uint64_t* outGeneration) const {
    std::lock_guard lock(mutex_);
    std::vector<KeyStateSnapshotEntry> states;
    states.reserve(activationKeys_.size());
    for (const auto& b : activationKeys_) {
        states.push_back(KeyStateSnapshotEntry{b.vk, b.pressed});
    }
    if (outGeneration) {
        *outGeneration = keysGeneration_.load(std::memory_order_acquire);
    }
    return states;
}

CaptureEngine::TxUtteranceStats CaptureEngine::lastTxStatsSnapshot() const noexcept {
    std::lock_guard lock(txStatsMutex_);
    return lastTxStats_;
}

void CaptureEngine::startMicMonitorOnAudioThread() {
    if (micMonitorActive_) {
        return;
    }
    const IBassLibrary* bass = bass_;
    const BassApi* api = cachedBassApi_;
    if (!bass || !api || !bass->isLoaded() || !api->StreamCreate || !api->StreamPutData) {
        micMonitorDesired_.store(false, std::memory_order_release);
        SVPLUS_LOG_WRN_CAT("audio", "Cannot start microphone loopback monitor: BASS unavailable");
        return;
    }

    const std::uint32_t curBitrate = activeBitrate_.load(std::memory_order_acquire);
    if (!monitorEncoder_.initialize(static_cast<int>(curBitrate)) || !monitorDecoder_.initialize()) {
        monitorEncoder_.shutdown();
        monitorDecoder_.shutdown();
        micMonitorDesired_.store(false, std::memory_order_release);
        SVPLUS_LOG_WRN_CAT("audio", "Failed to initialize Opus loopback codec for microphone monitor");
        return;
    }
    appliedMonitorBitrate_ = curBitrate;

    DWORD prevBufferMs = DWORD(-1);
    bool restoredBuffer = false;
    if (api->GetConfig && api->SetConfig) {
        prevBufferMs = api->GetConfig(BASS_CONFIG_BUFFER);
        if (prevBufferMs != DWORD(-1) && prevBufferMs != 200u) {
            restoredBuffer = (api->SetConfig(BASS_CONFIG_BUFFER, 200u) != FALSE);
        }
    }

    monitorBassStream_ = api->StreamCreate(
        static_cast<DWORD>(protocol::kFrequency),
        1,
        0,
        STREAMPROC_PUSH,
        nullptr);

    if (restoredBuffer && prevBufferMs != DWORD(-1) && api->SetConfig) {
        api->SetConfig(BASS_CONFIG_BUFFER, prevBufferMs);
    }

    if (!monitorBassStream_) {
        const int err = api->ErrorGetCode ? api->ErrorGetCode() : BASS_ERROR_UNKNOWN;
        monitorEncoder_.shutdown();
        monitorDecoder_.shutdown();
        micMonitorDesired_.store(false, std::memory_order_release);
        SVPLUS_LOG_WRN_CAT("audio", "Failed to create BASS loopback stream (err=%d: %s)", err, bassErrorName(err));
        return;
    }

    if (api->ChannelSetAttribute) {
        api->ChannelSetAttribute(monitorBassStream_, BASS_ATTRIB_VOL, 1.0f);
    }

    monitorRing_.clear();
    micMonitorActive_ = true;
    SVPLUS_LOG_INF_CAT("audio", "Microphone loopback monitor started (bitrate=%u bps)", curBitrate);
}

void CaptureEngine::stopMicMonitorOnAudioThread(const char* reason) noexcept {
    micMonitorDesired_.store(false, std::memory_order_release);
    const IBassLibrary* bass = bass_;
    const BassApi* api = cachedBassApi_;
    if (monitorBassStream_ && bass && api && bass->isLoaded()) {
        if (api->ChannelStop) {
            api->ChannelStop(monitorBassStream_);
        }
        if (api->StreamFree) {
            api->StreamFree(monitorBassStream_);
        }
    }
    monitorBassStream_ = 0;
    monitorEncoder_.shutdown();
    monitorDecoder_.shutdown();
    monitorRing_.clear();
    if (micMonitorActive_) {
        micMonitorActive_ = false;
        SVPLUS_LOG_INF_CAT("audio", "Microphone loopback monitor stopped (reason=%s)",
                           (reason && reason[0] != '\0') ? reason : "unknown");
    }
}

void CaptureEngine::flushPaddedTail(const EncodedFrameCallback& frameCb) {
    if (!frameCb || txRing_.count < kMinTailSamples) {
        txRing_.clear();
        return;
    }
    const std::size_t n = (std::min)(txRing_.count, kFrameSamples);
    const std::size_t firstChunk = (std::min)(n, kRingCapacity - txRing_.head);
    std::memcpy(encodeScratch_.data(), txRing_.buf.data() + txRing_.head, firstChunk * sizeof(std::int16_t));
    if (n > firstChunk) {
        std::memcpy(encodeScratch_.data() + firstChunk, txRing_.buf.data(), (n - firstChunk) * sizeof(std::int16_t));
    }
    if (n < kFrameSamples) {
        std::fill(encodeScratch_.begin() + static_cast<std::ptrdiff_t>(n), encodeScratch_.end(), std::int16_t{0});
    }
    txRing_.clear();

    const int encodedLen = encoder_.encode(encodeScratch_, opusOutBuf_);
    if (encodedLen > 0) {
        ++curTxAccum_.frames;
        frameCb(std::span<const std::uint8_t>(opusOutBuf_.data(), static_cast<std::size_t>(encodedLen)),
                sequenceCounter_++);
    } else {
        ++curTxAccum_.encErrors;
    }
}

namespace {

float absToDbfs(std::uint16_t peakAbs) noexcept {
    if (peakAbs == 0u) {
        return -std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(20.0 * std::log10(static_cast<double>(peakAbs) / 32768.0));
}

float rmsSqToDbfs(double sumSq, std::uint64_t samples) noexcept {
    if (samples == 0u || sumSq <= 0.0) {
        return -std::numeric_limits<float>::infinity();
    }
    const double rmsNorm = std::sqrt(sumSq / static_cast<double>(samples)) / 32768.0;
    if (rmsNorm <= 0.0) {
        return -std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(20.0 * std::log10(rmsNorm));
}

void formatDbfs(float db, char* out, std::size_t cap) noexcept {
    if (!std::isfinite(db)) {
        std::snprintf(out, cap, "-inf");
    } else {
        std::snprintf(out, cap, "%.1fdB", static_cast<double>(db));
    }
}

constexpr float kMicLimiterKneeNorm         = 0.70794578f; // -3 dBFS in normalized [-1, 1] scale
constexpr float kMicLimiterCeilingNorm      = 0.985f;      // Leave slight safety headroom below +/-32767
constexpr float kMicLimiterHeadroomNorm     = kMicLimiterCeilingNorm - kMicLimiterKneeNorm;
constexpr float kMicLimiterHardMaxSample    = 32767.0f;
constexpr float kMicLimiterAttackCoeff      = 0.35f;
constexpr float kMicLimiterReleaseCoeff     = 0.0005f;     // ~83 ms release at 24 kHz

} // namespace

void CaptureEngine::processMicDsp(std::span<std::int16_t> pcm,
                                  float gain,
                                  bool suppressNoise,
                                  BlockLevelStats* outStats) noexcept {
    const bool useLimiter = (gain > 1.0f);
    if (!useLimiter) {
        limiterEnv_ = 0.0f;
    }

    std::uint32_t prePeak = 0;
    std::uint32_t postPeak = 0;
    double preSumSq = 0.0;
    double sumSq = 0.0;
    for (auto& sample : pcm) {
        const std::int32_t rawSample = sample;
        const std::uint32_t rawAbs = static_cast<std::uint32_t>(rawSample < 0 ? -rawSample : rawSample);
        if (rawAbs > prePeak) {
            prePeak = rawAbs;
        }
        preSumSq += static_cast<double>(rawSample) * static_cast<double>(rawSample);

        float x = static_cast<float>(rawSample);

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
        if (useLimiter) {
            const float absNorm = std::fabs(x) / kMicLimiterHardMaxSample;
            if (absNorm > limiterEnv_) {
                limiterEnv_ += kMicLimiterAttackCoeff * (absNorm - limiterEnv_);
            } else {
                limiterEnv_ += kMicLimiterReleaseCoeff * (absNorm - limiterEnv_);
            }
            const float envPeak = (std::max)(absNorm, limiterEnv_);
            if (envPeak > kMicLimiterKneeNorm) {
                const float targetEnv =
                    kMicLimiterKneeNorm +
                    kMicLimiterHeadroomNorm * std::tanh((envPeak - kMicLimiterKneeNorm) / kMicLimiterHeadroomNorm);
                x *= (targetEnv / envPeak);
            }
            // Smooth waveshaper guard for any fast sub-attack transient above ceiling
            const float postAbs = std::fabs(x) / kMicLimiterHardMaxSample;
            if (postAbs > kMicLimiterCeilingNorm) {
                const float excess = (postAbs - kMicLimiterCeilingNorm) / (1.0f - kMicLimiterCeilingNorm);
                const float limitedNorm =
                    kMicLimiterCeilingNorm + (1.0f - kMicLimiterCeilingNorm) * 0.95f * std::tanh(excess);
                const float sign = (x < 0.0f) ? -1.0f : 1.0f;
                x = sign * limitedNorm * kMicLimiterHardMaxSample;
            }
        }
        x = std::clamp(x, -kMicLimiterHardMaxSample, kMicLimiterHardMaxSample);
        const std::int16_t outSample = static_cast<std::int16_t>(x);
        sample = outSample;
        const std::int32_t outAbs = outSample < 0 ? -static_cast<std::int32_t>(outSample) : static_cast<std::int32_t>(outSample);
        if (static_cast<std::uint32_t>(outAbs) > postPeak) {
            postPeak = static_cast<std::uint32_t>(outAbs);
        }
        sumSq += static_cast<double>(outSample) * static_cast<double>(outSample);
    }

    if (outStats) {
        outStats->prePeakAbs  = static_cast<std::uint16_t>((std::min<std::uint32_t>)(prePeak, 32768u));
        outStats->postPeakAbs = static_cast<std::uint16_t>((std::min<std::uint32_t>)(postPeak, 32768u));
        outStats->preSumSq    = preSumSq;
        outStats->postSumSq   = sumSq;
        outStats->samples     = static_cast<std::uint64_t>(pcm.size());
    }

    if (!pcm.empty()) {
        const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(pcm.size())) / 32768.0);
        inputLevel_.store(std::clamp(rms * 3.2f, 0.0f, 1.0f), std::memory_order_release);
    }
}

void CaptureEngine::publishAndLogTxStop() noexcept {
    const float prePeakDb  = absToDbfs(curTxAccum_.prePeakAbs);
    const float preRmsDb   = rmsSqToDbfs(curTxAccum_.preSumSq, curTxAccum_.samples);
    const float postPeakDb = absToDbfs(curTxAccum_.postPeakAbs);
    const float postRmsDb  = rmsSqToDbfs(curTxAccum_.postSumSq, curTxAccum_.samples);

    char prePeakBuf[20]{};
    char preRmsBuf[20]{};
    char postPeakBuf[20]{};
    char postRmsBuf[20]{};
    formatDbfs(prePeakDb, prePeakBuf, sizeof(prePeakBuf));
    formatDbfs(preRmsDb, preRmsBuf, sizeof(preRmsBuf));
    formatDbfs(postPeakDb, postPeakBuf, sizeof(postPeakBuf));
    formatDbfs(postRmsDb, postRmsBuf, sizeof(postRmsBuf));

    SVPLUS_LOG_INF_CAT(
        "audio",
        "Microphone transmission stopped (frames=%llu, pre=[peak=%s, rms=%s], post=[peak=%s, rms=%s], encErr=%llu)",
        static_cast<unsigned long long>(curTxAccum_.frames),
        prePeakBuf,
        preRmsBuf,
        postPeakBuf,
        postRmsBuf,
        static_cast<unsigned long long>(curTxAccum_.encErrors));

    {
        std::lock_guard lock(txStatsMutex_);
        lastTxStats_.frames     = curTxAccum_.frames;
        lastTxStats_.encErrors  = curTxAccum_.encErrors;
        lastTxStats_.prePeakDb  = prePeakDb;
        lastTxStats_.preRmsDb   = preRmsDb;
        lastTxStats_.postPeakDb = postPeakDb;
        lastTxStats_.postRmsDb  = postRmsDb;
        ++lastTxStats_.txCount;
    }
    curTxAccum_.reset();
}

namespace {

bool keyInputIsDown(std::uint8_t vk, void* ctx) noexcept {
    const auto* keyInput = static_cast<const IKeyInput*>(ctx);
    return keyInput && keyInput->isKeyDown(vk);
}

} // namespace

void CaptureEngine::pollActivationKeys(
    const CoreConfig& cfg,
    bool inputBlocked,
    const char* inputBlockReason,
    bool allowToggleMode,
    std::chrono::steady_clock::time_point now,
    bool& outAnyKeyHeld
) {
    evaluateActivationKeys(
        cfg,
        inputBlocked,
        inputBlockReason,
        allowToggleMode,
        hasDevice_.load(std::memory_order_acquire),
        &keyInputIsDown,
        const_cast<void*>(static_cast<const void*>(keyInput_)),
        now,
        outAnyKeyHeld);
}

void CaptureEngine::evaluateActivationKeys(
    const CoreConfig& cfg,
    bool inputBlocked,
    const char* inputBlockReason,
    bool allowToggleMode,
    bool hasDeviceNow,
    bool (*isKeyPhysicallyDown)(std::uint8_t vk, void* ctx) noexcept,
    void* keyProbeCtx,
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
        const bool micAvailable = cfg.microEnable && hasDeviceNow;
        const bool isToggleMode = allowToggleMode && (cfg.talkMode == TalkMode::Toggle);

        for (auto& binding : activationKeys_) {
            const bool physDown = isKeyPhysicallyDown ? isKeyPhysicallyDown(binding.vk, keyProbeCtx) : false;
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
            const bool wasToggleLatched = binding.toggleLatched;
            bool down = false;
            if (isToggleMode) {
                if (rawDown && !binding.rawDownLast) {
                    binding.toggleLatched = !binding.toggleLatched;
                }
                binding.rawDownLast = rawDown;
                if (!micAvailable || inputBlocked) {
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
                } else if (wasToggleLatched && !allowToggleMode && !down) {
                    reason = "toggle_disallowed_by_server";
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

void CaptureEngine::tick(const CoreConfig& cfg,
                         bool inputBlocked,
                         const char* inputBlockReason,
                         bool allowToggleMode) {
    const auto now = std::chrono::steady_clock::now();
    const IBassLibrary* bass = bass_;
    const BassApi* api = cachedBassApi_;
    const bool bassLoaded = (bass != nullptr && api != nullptr && bass->isLoaded());
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
    if (recordHandle_ && bassLoaded && api->ChannelIsActive) {
        if (api->ChannelIsActive(recordHandle_) == BASS_ACTIVE_STOPPED) {
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
            stopMicMonitorOnAudioThread("device_changed");
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
    pollActivationKeys(cfg, inputBlocked, inputBlockReason, allowToggleMode, now, anyKeyHeld);
    EncodedFrameCallback frameCb;
    {
        std::lock_guard lock(mutex_);
        frameCb = onFrame_;
    }

    const bool hasDeviceNow = hasDevice_.load(std::memory_order_acquire);
    const bool monitorDesiredNow = micMonitorDesired_.load(std::memory_order_acquire);
    if (!cfg.microEnable || !hasDeviceNow || !recordHandle_) {
        if (micMonitorActive_ || monitorDesiredNow) {
            stopMicMonitorOnAudioThread(!cfg.microEnable ? "micro_disabled" : "no_mic_device");
        }
    } else if (monitorDesiredNow != micMonitorActive_) {
        if (monitorDesiredNow) {
            startMicMonitorOnAudioThread();
        } else {
            std::string stopReason;
            {
                std::lock_guard lk(monitorReasonMutex_);
                stopReason = monitorStopReason_;
            }
            stopMicMonitorOnAudioThread(stopReason.empty() ? "user" : stopReason.c_str());
        }
    }

    const bool shouldTransmit =
        cfg.microEnable &&
        hasDeviceNow &&
        !serverMuted_.load(std::memory_order_acquire) &&
        (anyKeyHeld || serverRecordActive_.load(std::memory_order_acquire));
    transmitting_.store(shouldTransmit, std::memory_order_release);

    if (!recordHandle_ || !bassLoaded || !api->ChannelGetData) {
        inputLevel_.store(0.0f, std::memory_order_release);
        txRing_.clear();
        if (wasTransmitting_) {
            publishAndLogTxStop();
        }
        wasTransmitting_ = false;
        return;
    }

    const DWORD availBytes = api->ChannelGetData(recordHandle_, nullptr, BASS_DATA_AVAILABLE);
    if (availBytes == DWORD(-1)) {
        if (!reconnectLogged_) {
            const int err = api->ErrorGetCode ? api->ErrorGetCode() : BASS_ERROR_UNKNOWN;
            SVPLUS_LOG_WRN_CAT("audio", "BASS_ChannelGetData failed (err=%d: %s); resetting capture stream",
                               err, bassErrorName(err));
            reconnectLogged_ = true;
        }
        stopMicMonitorOnAudioThread("capture_stream_error");
        closeCaptureStream();
        inputLevel_.store(0.0f, std::memory_order_release);
        txRing_.clear();
        if (wasTransmitting_) {
            publishAndLogTxStop();
        }
        wasTransmitting_ = false;
        return;
    }
    if (availBytes > 0) {
        const std::size_t availSamples = availBytes / sizeof(std::int16_t);
        captureScratch_.resize(availSamples);
        const DWORD readBytes = api->ChannelGetData(
            recordHandle_,
            captureScratch_.data(),
            static_cast<DWORD>(availSamples * sizeof(std::int16_t)));
        if (readBytes != DWORD(-1) && readBytes > 0) {
            const std::size_t readSamples = readBytes / sizeof(std::int16_t);
            const float gain = static_cast<float>(std::clamp(cfg.microGain, 0, kMaxMicroGain)) / 100.0f;

            BlockLevelStats blockStats{};
            processMicDsp(std::span<std::int16_t>(captureScratch_.data(), readSamples),
                          gain, cfg.microNoiseSuppress, &blockStats);
            if (shouldTransmit) {
                if (blockStats.prePeakAbs > curTxAccum_.prePeakAbs) {
                    curTxAccum_.prePeakAbs = blockStats.prePeakAbs;
                }
                if (blockStats.postPeakAbs > curTxAccum_.postPeakAbs) {
                    curTxAccum_.postPeakAbs = blockStats.postPeakAbs;
                }
                curTxAccum_.preSumSq  += blockStats.preSumSq;
                curTxAccum_.postSumSq += blockStats.postSumSq;
                curTxAccum_.samples   += blockStats.samples;
                txRing_.push(captureScratch_.data(), readSamples);
            }
            if (micMonitorActive_) {
                monitorRing_.push(captureScratch_.data(), readSamples);
            }
        }
    }

    if (micMonitorActive_ && monitorBassStream_ && api->StreamPutData) {
        const std::uint32_t curBitrate = activeBitrate_.load(std::memory_order_acquire);
        if (curBitrate != appliedMonitorBitrate_) {
            if (monitorEncoder_.setBitrate(static_cast<int>(curBitrate))) {
                appliedMonitorBitrate_ = curBitrate;
            }
        }
        while (monitorRing_.popFrame(encodeScratch_.data())) {
            const int encodedLen = monitorEncoder_.encode(encodeScratch_, opusOutBuf_);
            if (encodedLen > 0) {
                const int decodedSamples = monitorDecoder_.decode(
                    std::span<const std::uint8_t>(opusOutBuf_.data(), static_cast<std::size_t>(encodedLen)),
                    monitorDecodeBuf_,
                    false);
                if (decodedSamples > 0) {
                    api->StreamPutData(
                        monitorBassStream_,
                        monitorDecodeBuf_.data(),
                        static_cast<DWORD>(decodedSamples) * sizeof(std::int16_t));
                    if (api->ChannelPlay &&
                        (!api->ChannelIsActive ||
                         api->ChannelIsActive(monitorBassStream_) != BASS_ACTIVE_PLAYING)) {
                        api->ChannelPlay(monitorBassStream_, FALSE);
                    }
                }
            }
        }
    }

    if (!shouldTransmit) {
        if (wasTransmitting_) {
            flushPaddedTail(frameCb);
            publishAndLogTxStop();
        } else {
            txRing_.clear();
        }
        wasTransmitting_ = false;
        return;
    }

    if (!wasTransmitting_) {
        const bool effectiveToggleMode = allowToggleMode && (cfg.talkMode == TalkMode::Toggle);
        SVPLUS_LOG_INF_CAT("audio", "Microphone transmission started (mode=%s, trigger=%s)",
                           effectiveToggleMode ? "toggle" : "push",
                           anyKeyHeld ? "key" : "server_record");
    }
    wasTransmitting_ = true;
    if (!frameCb) {
        txRing_.clear();
        return;
    }

    while (txRing_.popFrame(encodeScratch_.data())) {
        const int encodedLen = encoder_.encode(encodeScratch_, opusOutBuf_);
        if (encodedLen > 0) {
            ++curTxAccum_.frames;
            frameCb(std::span<const std::uint8_t>(opusOutBuf_.data(), static_cast<std::size_t>(encodedLen)),
                    sequenceCounter_++);
        } else {
            ++curTxAccum_.encErrors;
        }
    }
}

} // namespace svplus::client::audio
