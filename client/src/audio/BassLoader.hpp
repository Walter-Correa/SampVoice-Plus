#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "svplus/client/Platform.hpp"

#include <atomic>

namespace svplus::client::audio {

class BassLoader final : public IBassLibrary {
public:
    static BassLoader& instance() noexcept;

    [[nodiscard]] bool initialize() noexcept override;
    void shutdown() noexcept override;

    /// Non-invasive check whether SA-MP has initialized its BASS output device yet.
    [[nodiscard]] bool pollReady() noexcept override;

    [[nodiscard]] bool isLoaded() const noexcept override { return loaded_.load(std::memory_order_acquire); }
    [[nodiscard]] bool isAudioReady() const noexcept override { return bassInitialized_.load(std::memory_order_acquire); }
    [[nodiscard]] const BassApi& api() const noexcept override { return api_; }

private:
    BassLoader() = default;
    ~BassLoader() = default;
    BassLoader(const BassLoader&) = delete;
    BassLoader& operator=(const BassLoader&) = delete;

    std::atomic<bool> loaded_{false};
    std::atomic<bool> bassInitialized_{false};
    HMODULE     bassSysModule_{nullptr};
    bool        ownsModuleRef_{false};
    BassApi     api_{};
};

} // namespace svplus::client::audio
