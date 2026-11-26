/*
 * SampVoice Plus - Lock-Friendly Per-Player Voice State Store Implementation
 */

#include "svplus/server/PlayerStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#include "svplus/common/Protocol.hpp"

namespace svplus::server {

void PlayerVoiceSlot::Reset() {
    connected.store(false, std::memory_order_release);
    hasPlugin.store(false, std::memory_order_release);
    udpIdentified.store(false, std::memory_order_release);
    microStatus.store(false, std::memory_order_release);
    recordStatus.store(false, std::memory_order_release);
    muteStatus.store(false, std::memory_order_release);
    muteTimed = false;
    muteExpireAt = {};
    listenerEnabled.store(true, std::memory_order_release);
    pluginVersion.store(0, std::memory_order_release);
    clientProtocol.store(0, std::memory_order_release);
    v4Warned.store(false, std::memory_order_release);
    capabilities.store(0, std::memory_order_release);
    keyBits.fill(0);
    activeKeyCount.store(0, std::memory_order_release);

    {
        std::unique_lock lock(dataMutex);
        serverKey = 0;
        fullSessionKey = 0;
        udpEndpoint = {};
        lastUdpSwitchTime = {};
        speakerStreams.clear();
        listenerStreams.clear();
    }

    {
        std::lock_guard rateLock(rateMutex);
        voiceRateTokens = kVoiceRateMaxBurst;
        lastRateRefill = {};
        lastDropLogTime = {};
        lastNatLogTime = {};
        droppedVoicePackets = 0;
    }
    ResetControlLimiter();
}

void PlayerVoiceSlot::ResetControlLimiter() noexcept {
    std::lock_guard lock(controlRateMutex);
    controlBuckets_[0] = {kKeyRateMaxBurst, {}};
    controlBuckets_[1] = {kMicroRateMaxBurst, {}};
    controlBuckets_[2] = {kPingRateMaxBurst, {}};
    lastControlDropLogTime = {};
    droppedControlPackets = 0;
    pressedKeyBits.fill(0);
    droppedPressKeys.fill(0);
    droppedReleaseKeys.fill(0);
    droppedPingCount = 0;
    droppedMicroCount = 0;
}

bool PlayerVoiceSlot::TakePressedKey(uint8_t keyId) noexcept {
    std::lock_guard lock(controlRateMutex);
    const std::size_t word = keyId >> 6;
    const uint64_t mask = 1ull << (keyId & 63);
    const bool wasPressed = (pressedKeyBits[word] & mask) != 0;
    pressedKeyBits[word] &= ~mask;
    return wasPressed;
}

std::vector<uint8_t> PlayerVoiceSlot::TakePressedKeysForRemoval(const std::array<uint64_t, 4>& allowedKeyBits) {
    std::vector<uint8_t> released;
    std::lock_guard lock(controlRateMutex);
    for (std::size_t word = 0; word < 4; ++word) {
        const uint64_t activeAndPressed = pressedKeyBits[word] & allowedKeyBits[word];
        if (activeAndPressed != 0) {
            for (unsigned bit = 0; bit < 64; ++bit) {
                if ((activeAndPressed & (1ull << bit)) != 0) {
                    released.push_back(static_cast<uint8_t>((word << 6) | bit));
                }
            }
        }
    }
    pressedKeyBits.fill(0);
    return released;
}

void PlayerVoiceSlot::ClearPressedKey(uint8_t keyId) noexcept {
    (void)TakePressedKey(keyId);
}

void PlayerVoiceSlot::ClearAllPressedKeys() noexcept {
    std::lock_guard lock(controlRateMutex);
    pressedKeyBits.fill(0);
}

namespace {

std::string FormatAndClearControlDropSummary(
    uint64_t& totalDropped,
    std::array<uint32_t, 256>& pressKeys,
    std::array<uint32_t, 256>& releaseKeys,
    uint64_t& microCount
) {
    std::string detail;
    char buf[96]{};

    bool anyPress = false;
    for (std::size_t k = 0; k < pressKeys.size(); ++k) {
        if (pressKeys[k] > 0) {
            anyPress = true;
            std::snprintf(buf, sizeof(buf), "PressKey 0x%02X x%u",
                          static_cast<unsigned>(k), pressKeys[k]);
            if (!detail.empty()) detail += ", ";
            detail += buf;
            pressKeys[k] = 0;
        }
    }
    if (!anyPress) {
        detail += "PressKey x0";
    }

    for (std::size_t k = 0; k < releaseKeys.size(); ++k) {
        if (releaseKeys[k] > 0) {
            std::snprintf(buf, sizeof(buf), "ReleaseKey 0x%02X x%u",
                          static_cast<unsigned>(k), releaseKeys[k]);
            if (!detail.empty()) detail += ", ";
            detail += buf;
            releaseKeys[k] = 0;
        }
    }

    std::snprintf(buf, sizeof(buf), ", micro x%llu",
                  static_cast<unsigned long long>(microCount));
    detail += buf;

    const uint64_t count = totalDropped;
    totalDropped = 0;
    microCount = 0;

    return "dropped " + std::to_string(count) + " packets in the last interval (" + detail + ")";
}

} // namespace

bool PlayerVoiceSlot::AcceptControlPacket(
    uint16_t packetType,
    uint8_t keyId,
    ControlBucketKind kind,
    std::chrono::steady_clock::time_point now,
    std::string& outFloodSummary
) {
    outFloodSummary.clear();

    struct BucketSpec {
        double rate;
        double burst;
    };
    static constexpr std::array<BucketSpec, 3> kSpecs{{
        {kKeyRateTokensPerSec,   kKeyRateMaxBurst},
        {kMicroRateTokensPerSec, kMicroRateMaxBurst},
        {kPingRateTokensPerSec,  kPingRateMaxBurst}
    }};

    const auto idx = static_cast<std::size_t>(kind);
    if (idx >= kSpecs.size()) {
        return false;
    }

    const bool isPress = (packetType == static_cast<uint16_t>(protocol::ControlPacketType::PressKey));
    const bool isRelease = (packetType == static_cast<uint16_t>(protocol::ControlPacketType::ReleaseKey));
    const uint64_t keyMask = 1ull << (keyId & 63);
    const std::size_t keyWord = keyId >> 6;
    const auto logInterval = std::chrono::seconds(kControlFloodLogSec);

    std::lock_guard lock(controlRateMutex);

    if (isPress) {
        if ((pressedKeyBits[keyWord] & keyMask) != 0) {
            return false; // Duplicate PressKey for an already held key: ignore completely
        }
    } else if (isRelease) {
        if ((pressedKeyBits[keyWord] & keyMask) == 0) {
            return false; // ReleaseKey for an unpressed key: ignore completely
        }
        // Always accept ReleaseKey for an active pressed key without touching token bucket
        pressedKeyBits[keyWord] &= ~keyMask;
        if (droppedControlPackets > 0 &&
            lastControlDropLogTime.time_since_epoch().count() != 0 &&
            (now - lastControlDropLogTime) >= logInterval) {
            outFloodSummary = FormatAndClearControlDropSummary(
                droppedControlPackets, droppedPressKeys, droppedReleaseKeys,
                droppedMicroCount);
            lastControlDropLogTime = now;
        }
        return true;
    }

    const double rate  = kSpecs[idx].rate;
    const double burst = kSpecs[idx].burst;
    ControlBucketState& bucket = controlBuckets_[idx];

    if (bucket.lastRefill.time_since_epoch().count() == 0) {
        bucket.lastRefill = now;
        bucket.tokens = burst;
    } else {
        const double elapsedSec = std::chrono::duration<double>(now - bucket.lastRefill).count();
        if (elapsedSec > 0.0) {
            bucket.tokens = std::min(burst, bucket.tokens + elapsedSec * rate);
            bucket.lastRefill = now;
        }
    }

    if (bucket.tokens >= 1.0) {
        bucket.tokens -= 1.0;
        if (isPress) {
            pressedKeyBits[keyWord] |= keyMask;
        }
        if (droppedControlPackets > 0 &&
            lastControlDropLogTime.time_since_epoch().count() != 0 &&
            (now - lastControlDropLogTime) >= logInterval) {
            outFloodSummary = FormatAndClearControlDropSummary(
                droppedControlPackets, droppedPressKeys, droppedReleaseKeys,
                droppedMicroCount);
            lastControlDropLogTime = now;
        }
        return true;
    }

    if (kind == ControlBucketKind::TelemetryPing) {
        // Excess telemetry pings (e.g., after a client pause) are logged only at Debug level, never WRN.
        ++droppedPingCount;
        return false;
    }

    ++droppedControlPackets;
    if (isPress) {
        ++droppedPressKeys[keyId];
    } else if (kind == ControlBucketKind::MicroState) {
        ++droppedMicroCount;
    }

    if (lastControlDropLogTime.time_since_epoch().count() == 0 ||
        (now - lastControlDropLogTime) >= logInterval) {
        outFloodSummary = FormatAndClearControlDropSummary(
            droppedControlPackets, droppedPressKeys, droppedReleaseKeys,
            droppedMicroCount);
        lastControlDropLogTime = now;
    }
    return false;
}

bool PlayerVoiceSlot::ConsumeControlToken(ControlBucketKind kind,
                                          std::chrono::steady_clock::time_point now,
                                          uint64_t& outDroppedToLog) noexcept {
    outDroppedToLog = 0;

    struct BucketSpec {
        double rate;
        double burst;
    };
    static constexpr std::array<BucketSpec, 3> kSpecs{{
        {kKeyRateTokensPerSec,   kKeyRateMaxBurst},
        {kMicroRateTokensPerSec, kMicroRateMaxBurst},
        {kPingRateTokensPerSec,  kPingRateMaxBurst}
    }};

    const auto idx = static_cast<std::size_t>(kind);
    if (idx >= kSpecs.size()) {
        return false;
    }
    const double rate  = kSpecs[idx].rate;
    const double burst = kSpecs[idx].burst;

    std::lock_guard lock(controlRateMutex);
    ControlBucketState& bucket = controlBuckets_[idx];

    if (bucket.lastRefill.time_since_epoch().count() == 0) {
        bucket.lastRefill = now;
        bucket.tokens = burst;
    } else {
        const double elapsedSec = std::chrono::duration<double>(now - bucket.lastRefill).count();
        if (elapsedSec > 0.0) {
            bucket.tokens = std::min(burst, bucket.tokens + elapsedSec * rate);
            bucket.lastRefill = now;
        }
    }

    const auto logInterval = std::chrono::seconds(kControlFloodLogSec);
    if (bucket.tokens >= 1.0) {
        bucket.tokens -= 1.0;
        if (droppedControlPackets > 0 &&
            lastControlDropLogTime.time_since_epoch().count() != 0 &&
            (now - lastControlDropLogTime) >= logInterval) {
            outDroppedToLog = droppedControlPackets;
            droppedControlPackets = 0;
            lastControlDropLogTime = now;
        }
        return true;
    }

    ++droppedControlPackets;
    if (lastControlDropLogTime.time_since_epoch().count() == 0 ||
        (now - lastControlDropLogTime) >= logInterval) {
        outDroppedToLog = droppedControlPackets;
        droppedControlPackets = 0;
        lastControlDropLogTime = now;
    }
    return false;
}

bool PlayerVoiceSlot::ShouldLogNatPortUpdate(std::chrono::steady_clock::time_point now) noexcept {
    std::lock_guard lock(rateMutex);
    if (lastNatLogTime.time_since_epoch().count() != 0 &&
        (now - lastNatLogTime) < std::chrono::seconds(60)) {
        return false;
    }
    lastNatLogTime = now;
    return true;
}

bool PlayerVoiceSlot::ConsumeVoiceToken(std::chrono::steady_clock::time_point now, uint64_t& outDroppedToLog) noexcept {
    outDroppedToLog = 0;
    std::lock_guard lock(rateMutex);

    if (lastRateRefill.time_since_epoch().count() == 0) {
        lastRateRefill = now;
        lastDropLogTime = now;
        voiceRateTokens = kVoiceRateMaxBurst;
    } else {
        const double elapsedSec = std::chrono::duration<double>(now - lastRateRefill).count();
        if (elapsedSec > 0.0) {
            voiceRateTokens = std::min(kVoiceRateMaxBurst, voiceRateTokens + elapsedSec * kVoiceRateTokensPerSec);
            lastRateRefill = now;
        }
    }

    if (voiceRateTokens >= 1.0) {
        voiceRateTokens -= 1.0;
        if (droppedVoicePackets > 0 && (now - lastDropLogTime) >= std::chrono::seconds(60)) {
            outDroppedToLog = droppedVoicePackets;
            droppedVoicePackets = 0;
            lastDropLogTime = now;
        }
        return true;
    }

    ++droppedVoicePackets;
    if ((now - lastDropLogTime) >= std::chrono::seconds(60)) {
        outDroppedToLog = droppedVoicePackets;
        droppedVoicePackets = 0;
        lastDropLogTime = now;
    }
    return false;
}

PlayerStore::PlayerStore() {
    std::random_device rd;
    const auto nowTicks = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    std::seed_seq seq{rd(), rd(), nowTicks, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu)};
    rng_.seed(seq);
}

uint32_t PlayerStore::RegisterConnect(
    uint16_t playerId,
    uint32_t ipv4Net,
    uint8_t version,
    uint8_t clientProtocolVal,
    bool hasMicro,
    uint32_t capabilities
) {
    if (playerId >= kMaxPlayers || ipv4Net == 0) {
        return 0;
    }

    PlayerVoiceSlot& slot = slots_[playerId];
    if (slot.connected.load(std::memory_order_acquire)) {
        RegisterDisconnect(playerId);
    }

    uint32_t randomKey = 0;
    uint64_t fullKey = 0;

    {
        std::lock_guard rngLock(rngMutex_);
        std::unique_lock mapLock(keyMapMutex_);
        do {
            randomKey = rng_();
            fullKey = MakeSessionKey(ipv4Net, randomKey);
        } while (randomKey == 0 || sessionKeyToPlayer_.find(fullKey) != sessionKeyToPlayer_.end());

        sessionKeyToPlayer_[fullKey] = playerId;
    }

    {
        std::unique_lock lock(slot.dataMutex);
        slot.serverKey = randomKey;
        slot.fullSessionKey = fullKey;
        slot.udpEndpoint = {};
        slot.lastUdpSwitchTime = {};
        slot.speakerStreams.clear();
        slot.listenerStreams.clear();
    }
    {
        std::lock_guard rateLock(slot.rateMutex);
        slot.voiceRateTokens = PlayerVoiceSlot::kVoiceRateMaxBurst;
        slot.lastRateRefill = {};
        slot.lastDropLogTime = {};
        slot.droppedVoicePackets = 0;
    }
    slot.ResetControlLimiter();

    slot.ClearKeys();
    slot.pluginVersion.store(version, std::memory_order_release);
    slot.clientProtocol.store(clientProtocolVal, std::memory_order_release);
    slot.v4Warned.store(false, std::memory_order_release);
    slot.microStatus.store(hasMicro, std::memory_order_release);
    slot.capabilities.store(capabilities, std::memory_order_release);
    slot.recordStatus.store(false, std::memory_order_release);
    slot.muteStatus.store(false, std::memory_order_release);
    slot.listenerEnabled.store(true, std::memory_order_release);
    slot.udpIdentified.store(false, std::memory_order_release);
    slot.hasPlugin.store(version > 0, std::memory_order_release);
    slot.connected.store(true, std::memory_order_release);

    pluginFlags_[playerId].store(version > 0, std::memory_order_release);
    plusFlags_[playerId].store(clientProtocolVal >= protocol::kPlusVersion, std::memory_order_release);
    return randomKey;
}

bool PlayerStore::RegisterUnsupportedV4(uint16_t playerId) {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    PlayerVoiceSlot& slot = slots_[playerId];
    slot.clientProtocol.store(protocol::kCyberMorV4Protocol, std::memory_order_release);
    slot.pluginVersion.store(0, std::memory_order_release);
    slot.hasPlugin.store(false, std::memory_order_release);
    slot.capabilities.store(0, std::memory_order_release);
    pluginFlags_[playerId].store(false, std::memory_order_release);
    plusFlags_[playerId].store(false, std::memory_order_release);
    return !slot.v4Warned.exchange(true, std::memory_order_acq_rel);
}

void PlayerStore::RegisterDisconnect(uint16_t playerId) {
    if (playerId >= kMaxPlayers) {
        return;
    }

    PlayerVoiceSlot& slot = slots_[playerId];
    pluginFlags_[playerId].store(false, std::memory_order_release);
    plusFlags_[playerId].store(false, std::memory_order_release);

    uint64_t keyToErase = 0;
    {
        std::shared_lock lock(slot.dataMutex);
        keyToErase = slot.fullSessionKey;
    }

    if (keyToErase != 0) {
        std::unique_lock lock(keyMapMutex_);
        sessionKeyToPlayer_.erase(keyToErase);
    }

    slot.Reset();
}

uint16_t PlayerStore::FindPlayerByUdpKey(uint32_t ipv4Net, uint32_t serverKey) const {
    if (ipv4Net == 0 || serverKey == 0) {
        return protocol::kInvalidPlayerId;
    }

    const uint64_t fullKey = MakeSessionKey(ipv4Net, serverKey);
    std::shared_lock lock(keyMapMutex_);
    const auto it = sessionKeyToPlayer_.find(fullKey);
    return it != sessionKeyToPlayer_.end() ? it->second : protocol::kInvalidPlayerId;
}

bool PlayerStore::TryIdentifyUdpEndpoint(uint16_t playerId, const net::UdpEndpoint& ep, bool* outPortUpdated) {
    if (outPortUpdated) {
        *outPortUpdated = false;
    }
    if (playerId >= kMaxPlayers) {
        return false;
    }
    PlayerVoiceSlot& slot = slots_[playerId];
    if (!slot.connected.load(std::memory_order_acquire)) {
        return false;
    }

    if (slot.udpIdentified.load(std::memory_order_acquire)) {
        // Fast path: check under shared lock if same endpoint; only take unique lock on NAT port change
        net::UdpEndpoint currentEp{};
        {
            std::shared_lock rlock(slot.dataMutex);
            currentEp = slot.udpEndpoint;
        }
        if (currentEp == ep || currentEp.ipv4Net != ep.ipv4Net) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        std::unique_lock wlock(slot.dataMutex);
        if (slot.udpEndpoint.ipv4Net == ep.ipv4Net && slot.udpEndpoint.portHost != ep.portHost &&
            (now - slot.lastUdpSwitchTime) >= std::chrono::seconds(2)) {
            slot.udpEndpoint.portHost = ep.portHost;
            slot.lastUdpSwitchTime = now;
            if (outPortUpdated) {
                *outPortUpdated = true;
            }
        }
        return false;
    }

    std::unique_lock lock(slot.dataMutex);
    if (slot.udpIdentified.load(std::memory_order_relaxed)) {
        return false;
    }
    slot.udpEndpoint = ep;
    slot.lastUdpSwitchTime = std::chrono::steady_clock::now();
    slot.udpIdentified.store(true, std::memory_order_release);
    return true;
}

bool PlayerStore::GetUdpEndpoint(uint16_t playerId, net::UdpEndpoint& outEp) const {
    if (playerId >= kMaxPlayers) {
        return false;
    }
    const PlayerVoiceSlot& slot = slots_[playerId];
    if (!slot.udpIdentified.load(std::memory_order_acquire)) {
        return false;
    }
    std::shared_lock lock(slot.dataMutex);
    outEp = slot.udpEndpoint;
    return true;
}

PlayerVoiceSlot* PlayerStore::GetSlot(uint16_t playerId) noexcept {
    return playerId < kMaxPlayers ? &slots_[playerId] : nullptr;
}

const PlayerVoiceSlot* PlayerStore::GetSlot(uint16_t playerId) const noexcept {
    return playerId < kMaxPlayers ? &slots_[playerId] : nullptr;
}

void PlayerStore::AttachSpeakerStream(uint16_t playerId, uint32_t streamId) {
    if (playerId >= kMaxPlayers) {
        return;
    }
    PlayerVoiceSlot& slot = slots_[playerId];
    std::unique_lock lock(slot.dataMutex);
    if (std::find(slot.speakerStreams.begin(), slot.speakerStreams.end(), streamId) == slot.speakerStreams.end()) {
        slot.speakerStreams.push_back(streamId);
    }
}

void PlayerStore::DetachSpeakerStream(uint16_t playerId, uint32_t streamId) {
    if (playerId >= kMaxPlayers) {
        return;
    }
    PlayerVoiceSlot& slot = slots_[playerId];
    std::unique_lock lock(slot.dataMutex);
    const auto it = std::find(slot.speakerStreams.begin(), slot.speakerStreams.end(), streamId);
    if (it != slot.speakerStreams.end()) {
        *it = slot.speakerStreams.back();
        slot.speakerStreams.pop_back();
    }
}

void PlayerStore::AttachListenerStream(uint16_t playerId, uint32_t streamId) {
    if (playerId >= kMaxPlayers) {
        return;
    }
    PlayerVoiceSlot& slot = slots_[playerId];
    std::unique_lock lock(slot.dataMutex);
    if (std::find(slot.listenerStreams.begin(), slot.listenerStreams.end(), streamId) == slot.listenerStreams.end()) {
        slot.listenerStreams.push_back(streamId);
    }
}

void PlayerStore::DetachListenerStream(uint16_t playerId, uint32_t streamId) {
    if (playerId >= kMaxPlayers) {
        return;
    }
    PlayerVoiceSlot& slot = slots_[playerId];
    std::unique_lock lock(slot.dataMutex);
    const auto it = std::find(slot.listenerStreams.begin(), slot.listenerStreams.end(), streamId);
    if (it != slot.listenerStreams.end()) {
        *it = slot.listenerStreams.back();
        slot.listenerStreams.pop_back();
    }
}

void PlayerStore::ClearAll() {
    {
        std::unique_lock lock(keyMapMutex_);
        sessionKeyToPlayer_.clear();
    }
    for (uint16_t i = 0; i < kMaxPlayers; ++i) {
        pluginFlags_[i].store(false, std::memory_order_release);
        plusFlags_[i].store(false, std::memory_order_release);
        slots_[i].Reset();
    }
}

} // namespace svplus::server
