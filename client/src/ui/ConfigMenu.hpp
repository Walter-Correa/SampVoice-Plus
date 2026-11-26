#pragma once

#include "audio/ActiveSpeakerQueue.hpp"
#include "config/ClientConfig.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Protocol.hpp"
#include "ui/D3D9Hook.hpp"
#include "ui/Localization.hpp"
#include "ui/MenuLayout.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <span>
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

    struct BrandAnimationInputs {
        bool measuring{false};
        bool transmitting{false};
        bool deafened{false};
        bool soundEnable{true};
        int  soundVolume{100};
        bool listenerDisabled{false};
        const std::vector<audio::ActiveSpeakerInfo>* speakers{nullptr};
    };

    [[nodiscard]] static bool shouldAnimateBrandHeader(const BrandAnimationInputs& in) noexcept;

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

    /// Renders the config menu with optional extra tab descriptors or extra General right-column controls.
    void renderWithExtraTabs(config::ClientConfig&                   cfg,
                             Localization&                           loc,
                             const UiTextures&                       textures,
                             std::span<const MenuTabSpec>            extraTabs,
                             bool&                                   outConfigDirty,
                             const std::function<void(MenuLayout&)>& extraGeneralRight = {});

    /// Renders the two-part Blacklist tab from an explicit server/player view (used by both live UI and previews).
    void renderBlacklistTabWithState(config::ClientConfig& cfg,
                                     Localization& loc,
                                     const BlacklistTabView& view,
                                     bool& dirty);

    /// Renders the two-part Blacklist tab through an explicit MenuLayout (used by previews/harness).
    void renderBlacklistTabWithLayout(config::ClientConfig& cfg,
                                      Localization& loc,
                                      const BlacklistTabView& view,
                                      MenuLayout& layout,
                                      bool& dirty);

    /// Renders the Microphone tab with an explicit server toggle-mode flag (used by both live UI and layout proofs).
    void renderMicrophoneTabWithState(config::ClientConfig& cfg,
                                      Localization& loc,
                                      bool toggleAllowed,
                                      bool& dirty);

    void renderMicrophoneTabWithLayout(config::ClientConfig& cfg,
                                       Localization& loc,
                                       bool toggleAllowed,
                                       MenuLayout& layout,
                                       bool& dirty);

    /// Renders General/Audio tabs with explicit MenuLayout (for layout/measurement proofs).
    void renderGeneralTabWithLayout(config::ClientConfig& cfg,
                                    Localization& loc,
                                    const UiTextures& textures,
                                    MenuLayout& layout,
                                    bool& dirty);
    void renderAudioTabWithLayout(config::ClientConfig& cfg,
                                  Localization& loc,
                                  MenuLayout& layout,
                                  bool& dirty);

    [[nodiscard]] const MeasuredMenuDimensions& lastMeasuredDimensions() const noexcept { return measured_; }
    [[nodiscard]] int activeTabIndex() const noexcept { return activeTab_; }
    void setActiveTabIndex(int idx) noexcept { activeTab_ = idx; }

private:
    ConfigMenu() = default;
    ~ConfigMenu() = default;
    ConfigMenu(const ConfigMenu&) = delete;
    ConfigMenu& operator=(const ConfigMenu&) = delete;

    void renderChromeHeader(const config::ClientConfig& cfg, Localization& loc, MenuLayout& layout) const;
    void renderChromeFooter(config::ClientConfig& cfg, Localization& loc, MenuLayout& layout, bool& dirty);
    void renderGeneralTab(config::ClientConfig& cfg,
                          Localization& loc,
                          const UiTextures& textures,
                          MenuLayout& layout,
                          bool& dirty,
                          const std::function<void(MenuLayout&)>& extraRight = {});
    void renderAudioTab(config::ClientConfig& cfg, Localization& loc, MenuLayout& layout, bool& dirty);
    void renderMicrophoneTab(config::ClientConfig& cfg, Localization& loc, MenuLayout& layout, bool& dirty);
    void renderBlacklistTab(config::ClientConfig& cfg, Localization& loc, const UiTextures& textures, MenuLayout& layout, bool& dirty);
    void renderBlacklistTabImpl(config::ClientConfig& cfg,
                                Localization& loc,
                                const BlacklistTabView& view,
                                const std::vector<std::size_t>* precomputedNearbyOrder,
                                MenuLayout& layout,
                                bool& dirty);

    bool open_{false};
    bool applyingOpeningLayout_{false};
    bool forceApplyAutoSize_{false};
    bool safetyNetForceSize_{false};
    bool pendingCondRemeasure_{false};
    bool anyPopupOpenLastFrame_{false};
    bool closePopupOnNextFrame_{false};
    bool capturingHotkey_{false};
    bool capturingDeafenHotkey_{false};
    bool deafenHotkeyMenuConflict_{false};
    bool nearbyOnlyFilter_{false};
    int  activeTab_{0};
    char searchBuf_[128]{};
    char allSearchBuf_[128]{};

    MeasuredMenuDimensions                 measured_{};
    bool                                   hasMeasured_{false};
    Language                               lastMeasuredLang_{Language::Count};
    std::size_t                            lastExtraTabCount_{0};
    bool                                   lastHasExtraGeneralRight_{false};
    float                                  generalRightMinW_{0.0f};
    float                                  generalRightPrefW_{0.0f};
    float                                  lastGeneralRightW_{0.0f};
    std::chrono::steady_clock::time_point  lastMeasureTime_{};
    ImVec2                                 lastAppliedCodeSize_{0.0f, 0.0f};
    ImVec2                                 expectedWindowPos_{0.0f, 0.0f};
    float                                  safetyBoostW_{0.0f};
    float                                  safetyBoostH_{0.0f};
    int                                    settledSameSizeFrames_{0};
    ImVec2                                 prevObservedWinSize_{0.0f, 0.0f};
    bool                                   loggedSmallScreenInf_{false};
    std::array<std::array<bool, 16>, Localization::kLanguageCount> loggedOverflowWarn_{};

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
    std::string                                   fittedCellScratch_{};
    bool                                          volDragActive_{false};
    int                                           volDragInitialPct_{100};
};

} // namespace svplus::client::ui
