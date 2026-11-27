#include "MobilePlatform.hpp"

#include "svplus/client/Utf8Util.hpp"
#include "svplus/common/Logger.hpp"

#include <cstring>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace svplus::mobile {

std::atomic<MobileBassLibrary*> MobileBassLibrary::activeInstance_{nullptr};

namespace {

int mapDx8FxBit(DWORD bassFxType) noexcept {
    switch (bassFxType) {
        case BASS_FX_DX8_CHORUS:      return 0;
        case BASS_FX_DX8_COMPRESSOR:  return 1;
        case BASS_FX_DX8_DISTORTION:  return 2;
        case BASS_FX_DX8_ECHO:        return 3;
        case BASS_FX_DX8_FLANGER:     return 4;
        case BASS_FX_DX8_GARGLE:      return 5;
        case BASS_FX_DX8_I3DL2REVERB: return 6;
        case BASS_FX_DX8_PARAMEQ:     return 7;
        case BASS_FX_DX8_REVERB:      return 8;
        default:                      return 9;
    }
}

} // namespace

void* MobileBassLibrary::lookupRawSymbol(const char* name) const noexcept {
    if (!name || name[0] == '\0') {
        return nullptr;
    }
    if (procLookup_) {
        return procLookup_(user_, name);
    }
#if !defined(_WIN32)
    return dlHandle_ ? dlsym(dlHandle_, name) : nullptr;
#else
    return nullptr;
#endif
}

template <typename Fn>
bool MobileBassLibrary::resolveSymbol(const char* name, Fn& out) noexcept {
    void* sym = lookupRawSymbol(name);
    if (!sym) {
        out = nullptr;
        return false;
    }
    static_assert(sizeof(out) == sizeof(void*), "Function pointer size mismatch");
    std::memcpy(&out, &sym, sizeof(out));
    return out != nullptr;
}

HFX WINAPI MobileBassLibrary::channelSetFxThunk(DWORD handle, DWORD type, int priority) noexcept {
    auto* self = activeInstance_.load(std::memory_order_acquire);
    if (!self) {
        return 0;
    }
    HFX fx = 0;
    if (self->rawChannelSetFX_) {
        fx = self->rawChannelSetFX_(handle, type, priority);
    }
    if (fx == 0) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<unsigned>(mapDx8FxBit(type));
        if ((self->refusedFxLogMask_.fetch_or(bit, std::memory_order_acq_rel) & bit) == 0u) {
            SVPLUS_LOG_WRN_CAT(
                "audio",
                "BASS_ChannelSetFX refused DX8 effect type 0x%X; stream will play without this effect",
                static_cast<unsigned>(type));
        }
    }
    return fx;
}

bool MobileBassLibrary::initialize() noexcept {
    if (loaded_.load(std::memory_order_acquire)) {
        return true;
    }

    if (!procLookup_) {
#if !defined(_WIN32)
        (void)dlerror();
        dlHandle_ = dlopen("libbass.so", RTLD_LAZY | RTLD_NOLOAD);
        if (!dlHandle_) {
            (void)dlerror();
            dlHandle_ = dlopen("libbass.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (!dlHandle_) {
            const char* dlErr = dlerror();
            SVPLUS_LOG_ERR_CAT("audio", "Failed to load libbass.so (%s)",
                               (dlErr && dlErr[0] != '\0') ? dlErr : "unknown error");
            return false;
        }
#else
        SVPLUS_LOG_ERR_CAT("audio", "MobileBassLibrary requires host->get_bass_proc on Windows");
        return false;
#endif
    }

    api_ = {};
    rawChannelSetFX_ = nullptr;
    refusedFxLogMask_.store(0u, std::memory_order_release);

    bool ok = true;
    ok &= resolveSymbol("BASS_GetVersion", api_.GetVersion);
    ok &= resolveSymbol("BASS_ErrorGetCode", api_.ErrorGetCode);
    ok &= resolveSymbol("BASS_GetDevice", api_.GetDevice);
    ok &= resolveSymbol("BASS_SetConfig", api_.SetConfig);
    ok &= resolveSymbol("BASS_GetConfig", api_.GetConfig);
    ok &= resolveSymbol("BASS_StreamCreate", api_.StreamCreate);
    ok &= resolveSymbol("BASS_StreamFree", api_.StreamFree);
    ok &= resolveSymbol("BASS_StreamPutData", api_.StreamPutData);
    ok &= resolveSymbol("BASS_RecordGetDeviceInfo", api_.RecordGetDeviceInfo);
    ok &= resolveSymbol("BASS_RecordInit", api_.RecordInit);
    ok &= resolveSymbol("BASS_RecordFree", api_.RecordFree);
    ok &= resolveSymbol("BASS_RecordSetDevice", api_.RecordSetDevice);
    ok &= resolveSymbol("BASS_RecordGetDevice", api_.RecordGetDevice);
    ok &= resolveSymbol("BASS_RecordStart", api_.RecordStart);
    ok &= resolveSymbol("BASS_ChannelIsActive", api_.ChannelIsActive);
    ok &= resolveSymbol("BASS_ChannelFlags", api_.ChannelFlags);
    ok &= resolveSymbol("BASS_ChannelPlay", api_.ChannelPlay);
    ok &= resolveSymbol("BASS_ChannelStop", api_.ChannelStop);
    ok &= resolveSymbol("BASS_ChannelPause", api_.ChannelPause);
    ok &= resolveSymbol("BASS_ChannelSetAttribute", api_.ChannelSetAttribute);
    ok &= resolveSymbol("BASS_ChannelGetAttribute", api_.ChannelGetAttribute);
    ok &= resolveSymbol("BASS_ChannelSlideAttribute", api_.ChannelSlideAttribute);
    ok &= resolveSymbol("BASS_ChannelGetData", api_.ChannelGetData);

    // Optional DX8 effect exports on mobile BASS builds
    (void)resolveSymbol("BASS_ChannelSetFX", rawChannelSetFX_);
    (void)resolveSymbol("BASS_ChannelRemoveFX", api_.ChannelRemoveFX);
    (void)resolveSymbol("BASS_FXSetParameters", api_.FXSetParameters);
    api_.ChannelSetFX = &MobileBassLibrary::channelSetFxThunk;

    if (!ok) {
        SVPLUS_LOG_ERR_CAT("audio", "Failed to resolve required BASS 2.4 symbols");
        shutdown();
        return false;
    }

    activeInstance_.store(this, std::memory_order_release);
    loaded_.store(true, std::memory_order_release);
    (void)pollReady();
    SVPLUS_LOG_INF_CAT("audio", "Bound to mobile BASS library (v0x%lX)",
                       static_cast<unsigned long>(api_.GetVersion ? api_.GetVersion() : 0u));
    return true;
}

bool MobileBassLibrary::pollReady() noexcept {
    if (ready_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!loaded_.load(std::memory_order_acquire) || !api_.GetDevice || !api_.ErrorGetCode) {
        return false;
    }
    const DWORD dev = api_.GetDevice();
    if (dev != DWORD(-1) && api_.ErrorGetCode() == BASS_OK) {
        ready_.store(true, std::memory_order_release);
        SVPLUS_LOG_INF_CAT("audio", "Detected initialized BASS output device (%lu)", static_cast<unsigned long>(dev));
        return true;
    }
    return false;
}

void MobileBassLibrary::shutdown() noexcept {
    MobileBassLibrary* expected = this;
    (void)activeInstance_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    loaded_.store(false, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    rawChannelSetFX_ = nullptr;
    api_ = {};
    refusedFxLogMask_.store(0u, std::memory_order_release);
#if !defined(_WIN32)
    if (dlHandle_) {
        dlclose(dlHandle_);
        dlHandle_ = nullptr;
    }
#endif
}

void MobileIncomingControlQueue::enqueue(std::uint16_t packetId, const std::uint8_t* data, std::size_t size) {
    const std::size_t msgBytes = sizeof(svplus::protocol::ControlPacketHeader) + size;
    bool logDrop = false;
    std::size_t snapCount = 0u;
    std::size_t snapBytes = 0u;
    std::uint64_t snapSuppressed = 0u;

    {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= kMaxPackets || (queuedBytes_ + msgBytes) > kMaxBytes) {
            const auto now = std::chrono::steady_clock::now();
            if (lastDropWarnTime_.time_since_epoch().count() == 0 ||
                (now - lastDropWarnTime_) >= std::chrono::seconds(2)) {
                lastDropWarnTime_ = now;
                logDrop           = true;
                snapCount         = queue_.size();
                snapBytes         = queuedBytes_;
                snapSuppressed    = suppressedDrops_;
                suppressedDrops_  = 0u;
            } else {
                ++suppressedDrops_;
            }
        } else {
            IncomingControlMessage msg{};
            msg.packetId       = packetId;
            msg.accountedBytes = msgBytes;
            if (data && size > 0u) {
                msg.payload.assign(data, data + size);
            }
            queuedBytes_ += msgBytes;
            queue_.push_back(std::move(msg));
            return;
        }
    }

    if (logDrop) {
        SVPLUS_LOG_WRN_CAT(
            "net",
            "Incoming control queue limit reached (%zu packets, %zu bytes); dropping packet id=%u (suppressed=%llu)",
            snapCount,
            snapBytes,
            static_cast<unsigned>(packetId),
            static_cast<unsigned long long>(snapSuppressed));
    }
}

void MobileIncomingControlQueue::drain(std::vector<IncomingControlMessage>& out) {
    out.clear();
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return;
    }
    out.reserve(queue_.size());
    while (!queue_.empty()) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    queuedBytes_ = 0u;
}

void MobileIncomingControlQueue::clear() noexcept {
    std::lock_guard lock(mutex_);
    queue_.clear();
    queuedBytes_ = 0u;
}

bool MobileControlTransport::send(std::uint16_t packetId, const void* data, std::size_t size) {
    if (size > 0xFFFFu) {
        return false;
    }
    return queue_.enqueue(packetId, data, static_cast<std::uint16_t>(size));
}

void MobileControlTransport::clearOutgoing() noexcept {
    queue_.clear();
}

void MobileControlTransport::drainToHost(SendControlFn sendFn, void* user) {
    queue_.drain(drainScratch_);
    if (!sendFn) {
        return;
    }
    for (const auto& msg : drainScratch_) {
        if (msg.size > 0) {
            sendFn(user, msg.bytes.data(), static_cast<uint32_t>(msg.size));
        }
    }
}

void MobileControlTransport::resetOnMainThread() noexcept {
    queue_.resetOnMainThread();
}

void MobileGameState::reset() noexcept {
    inFrame_.store(false, std::memory_order_release);
    host_ = {};
    {
        std::lock_guard lock(pendingMutex_);
        for (const std::uint16_t id : dirtyPlayerIds_) {
            if (id < pendingNames_.size()) {
                pendingNames_[id].op = PendingNameOp::None;
                pendingNames_[id].raw.clear();
            }
        }
        dirtyPlayerIds_.clear();
        hasPendingNames_.store(false, std::memory_order_release);
    }
    framePendingScratch_.clear();
    {
        std::lock_guard lock(nameMutex_);
        for (auto& name : cachedNames_) {
            name.clear();
        }
        nameGen_.fetch_add(1u, std::memory_order_release);
    }
}

bool MobileGameState::setPlayerName(std::uint16_t playerId, std::string_view rawName) {
    if (playerId >= svplus::client::kMaxPlayerSlots) {
        return false;
    }
    std::lock_guard lock(pendingMutex_);
    auto& slot = pendingNames_[playerId];
    if (slot.op == PendingNameOp::None) {
        dirtyPlayerIds_.push_back(playerId);
    }
    slot.op = PendingNameOp::Set;
    slot.raw.assign(rawName);
    hasPendingNames_.store(true, std::memory_order_release);
    return true;
}

bool MobileGameState::clearPlayerName(std::uint16_t playerId) {
    if (playerId >= svplus::client::kMaxPlayerSlots) {
        return false;
    }
    std::lock_guard lock(pendingMutex_);
    auto& slot = pendingNames_[playerId];
    if (slot.op == PendingNameOp::None) {
        dirtyPlayerIds_.push_back(playerId);
    }
    slot.op = PendingNameOp::Clear;
    slot.raw.clear();
    hasPendingNames_.store(true, std::memory_order_release);
    return true;
}

void MobileGameState::flushPendingNamesOnFrame() {
    if (!hasPendingNames_.load(std::memory_order_acquire)) {
        return;
    }

    framePendingScratch_.clear();
    {
        std::lock_guard lock(pendingMutex_);
        for (const std::uint16_t id : dirtyPlayerIds_) {
            if (id >= pendingNames_.size()) {
                continue;
            }
            auto& slot = pendingNames_[id];
            if (slot.op != PendingNameOp::None) {
                FramePendingNameItem item{};
                item.playerId = id;
                item.op       = slot.op;
                item.raw      = std::move(slot.raw);
                slot.op       = PendingNameOp::None;
                slot.raw.clear();
                framePendingScratch_.push_back(std::move(item));
            }
        }
        dirtyPlayerIds_.clear();
        hasPendingNames_.store(false, std::memory_order_release);
    }

    if (framePendingScratch_.empty()) {
        return;
    }

    for (auto& item : framePendingScratch_) {
        if (item.op == PendingNameOp::Set && !item.raw.empty()) {
            decodeLegacyText(item.raw, item.decoded);
        } else {
            item.decoded.clear();
        }
    }

    {
        std::lock_guard lock(nameMutex_);
        bool anyChanged = false;
        for (auto& item : framePendingScratch_) {
            if (item.playerId >= cachedNames_.size()) {
                continue;
            }
            if (cachedNames_[item.playerId] != item.decoded) {
                cachedNames_[item.playerId] = std::move(item.decoded);
                anyChanged = true;
            }
        }
        if (anyChanged) {
            nameGen_.fetch_add(1u, std::memory_order_release);
        }
    }
}

bool MobileGameState::getCameraBasis(float pos[3], float front[3], float top[3]) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.get_camera || !pos || !front || !top) {
        return false;
    }
    return host_.get_camera(host_.user, pos, front, top) != 0;
}

bool MobileGameState::getLocalPedPosition(float pos[3]) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.get_local_ped_position || !pos) {
        return false;
    }
    return host_.get_local_ped_position(host_.user, pos) != 0;
}

bool MobileGameState::getPlayerPosition(std::uint16_t playerId, float pos[3]) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.get_player_position || !pos) {
        return false;
    }
    return host_.get_player_position(host_.user, playerId, pos) != 0;
}

bool MobileGameState::getVehiclePosition(std::uint16_t vehicleId, float pos[3]) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.get_vehicle_position || !pos) {
        return false;
    }
    return host_.get_vehicle_position(host_.user, vehicleId, pos) != 0;
}

bool MobileGameState::getObjectPosition(std::uint16_t objectId, float pos[3]) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.get_object_position || !pos) {
        return false;
    }
    return host_.get_object_position(host_.user, objectId, pos) != 0;
}

bool MobileGameState::isLocalPlayerInVehicle(std::uint16_t vehicleId) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.is_local_in_vehicle) {
        return false;
    }
    return host_.is_local_in_vehicle(host_.user, vehicleId) != 0;
}

void MobileGameState::copyCachedPlayerName(std::uint16_t playerId, std::string& out) const {
    out.clear();
    if (playerId >= svplus::client::kMaxPlayerSlots) {
        return;
    }
    std::lock_guard lock(nameMutex_);
    out.assign(cachedNames_[playerId]);
}

const void* MobileGameState::getObjectEntity(std::uint16_t objectId) const noexcept {
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(objectId) + 1u);
}

bool MobileGameState::isLineOfSightClear(
    const float origin[3],
    const float target[3],
    const void* ignoreEntity) const noexcept {
    if (!inFrame_.load(std::memory_order_acquire) || !host_.is_line_of_sight_clear || !origin || !target) {
        return true;
    }
    const uint16_t ignoreObjectId = ignoreEntity
        ? static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(ignoreEntity) - 1u)
        : uint16_t{SVP_INVALID_OBJECT_ID};
    return host_.is_line_of_sight_clear(host_.user, origin, target, ignoreObjectId) != 0;
}

void MobileGameState::decodeLegacyText(std::string_view raw, std::string& out) const {
    if (raw.empty()) {
        out.clear();
        return;
    }
    if (svplus::client::util::isValidUtf8(raw)) {
        out.assign(raw);
        return;
    }
    if (host_.decode_text) {
        std::array<char, 512> fixedBuf{};
        const uint32_t rawLen = static_cast<uint32_t>(raw.size());
        if (rawLen < fixedBuf.size() &&
            host_.decode_text(host_.user,
                              reinterpret_cast<const uint8_t*>(raw.data()),
                              rawLen,
                              fixedBuf.data(),
                              static_cast<uint32_t>(fixedBuf.size())) != 0) {
            fixedBuf.back() = '\0';
            const std::size_t outLen = std::strlen(fixedBuf.data());
            if (svplus::client::util::isValidUtf8(std::string_view(fixedBuf.data(), outLen))) {
                out.assign(fixedBuf.data(), outLen);
                return;
            }
        }
    }
    svplus::client::util::ensureUtf8FallbackInto(raw, out);
}

} // namespace svplus::mobile
