#pragma once

#include "config/ClientConfig.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Protocol.hpp"
#include "ui/D3D9Hook.hpp"
#include "ui/Localization.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace svplus::client::ui {

struct BlacklistOnlinePlayerView {
    std::uint16_t              id{0};
    std::string                name{};
    protocol::PlayerVoiceState voiceState{protocol::PlayerVoiceState::Ok};
    bool                       streamedIn{false};
    float                      distanceMeters{0.0f};
};

struct BlacklistTabView {
    bool                                   connected{false};
    bool                                   voiceStatusEnabled{false};
    bool                                   nearbyListEnabled{false};
    std::string                            serverName{};
    std::string                            serverEndpoint{};
    const UiTextures*                      textures{nullptr};
    std::vector<BlacklistOnlinePlayerView> onlinePlayers{};
};

class ConfigMenu {
public:
    static ConfigMenu& instance() noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void               setOpen(bool open) noexcept;

    /// Handles WM_KEYDOWN / WM_SYSKEYDOWN / mouse buttons for hotkey toggle & rebinding.
    /// Returns true if the message was consumed by the menu/hotkey capture.
    bool handleWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam,
                             config::ClientConfig& cfg,
                             bool chatOrGameMenuOpen,
                             bool& outConfigDirty);

    void render(config::ClientConfig& cfg,
                Localization& loc,
                const UiTextures& textures,
                bool& outConfigDirty);

    /// Renders the two-part Blacklist tab from an explicit server/player view (used by both live UI and previews).
    void renderBlacklistTabWithState(config::ClientConfig& cfg,
                                     Localization& loc,
                                     const BlacklistTabView& view,
                                     bool& dirty);

private:
    ConfigMenu() = default;
    ~ConfigMenu() = default;
    ConfigMenu(const ConfigMenu&) = delete;
    ConfigMenu& operator=(const ConfigMenu&) = delete;

    void renderGeneralTab(config::ClientConfig& cfg, Localization& loc, const UiTextures& textures, bool& dirty);
    void renderAudioTab(config::ClientConfig& cfg, Localization& loc, bool& dirty);
    void renderMicrophoneTab(config::ClientConfig& cfg, Localization& loc, bool& dirty);
    void renderBlacklistTab(config::ClientConfig& cfg, Localization& loc, const UiTextures& textures, bool& dirty);
    void renderBlacklistTabImpl(config::ClientConfig& cfg,
                                Localization& loc,
                                const BlacklistTabView& view,
                                const std::vector<std::size_t>* precomputedNearbyOrder,
                                bool& dirty);

    bool open_{false};
    bool applyingOpeningLayout_{false};
    bool capturingHotkey_{false};
    bool nearbyOnlyFilter_{false};
    int  activeTab_{0};
    char searchBuf_[128]{};
    char allSearchBuf_[128]{};

    BlacklistTabView                              cachedBlacklistView_{};
    std::chrono::steady_clock::time_point         lastBlacklistRebuild_{};
    bool                                          hasBlacklistCache_{false};
    std::size_t                                   lastConnectedRemoteCount_{0};
    std::vector<samp::RemotePlayerMenuSnapshot>   remotePlayerScratch_{};
    std::vector<std::size_t>                      cachedNearbyOrder_{};
    std::vector<const BlacklistOnlinePlayerView*> listedPlayersScratch_{};
    std::vector<const config::BlacklistEntry*>    activeEntriesScratch_{};
    std::vector<std::size_t>                      sortedBlacklistIndicesScratch_{};
    std::string                                   fittedNameScratch_{};
};

} // namespace svplus::client::ui
